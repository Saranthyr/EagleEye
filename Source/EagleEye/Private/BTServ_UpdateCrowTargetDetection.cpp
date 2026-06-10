#include "AI/BTServ_UpdateCrowTargetDetection.h"

#include "AIController.h"
#include "AI/BotCharacter.h"
#include "AI/CrowDetectionShareSubsystem.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Camera/CameraComponent.h"
#include "DetectionResult.h"
#include "DrawDebugHelpers.h"
#include "EagleEyeDetectionSettings.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "HAL/CriticalSection.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "MyActorComponent.h"

namespace
{
    struct FDetectionBox
    {
        FVector2D Center = FVector2D::ZeroVector;
        FVector2D Size = FVector2D::ZeroVector;
        float Confidence = 0.f;
        float Area = 0.f;
        int32 ClassId = -1;
        FString ClassLabel;
    };

    struct FCrowTargetDetectionMemory
    {
        FVector LastTargetLocation = FVector::ZeroVector;
        FVector PendingTargetLocation = FVector::ZeroVector;
        FVector LastRawTargetLocation = FVector::ZeroVector;
        FVector TargetVelocity = FVector::ZeroVector;
        FDetectionBox LastTrackedBox;
        float PendingTargetFirstDetectedTime = -FLT_MAX;
        float LastRawTargetTime = -FLT_MAX;
        float LastDetectedTime = -FLT_MAX;
        float PendingConfidence = 0.f;
        float LastConfidence = 0.f;
        int32 PendingClassId = -1;
        int32 LastClassId = -1;
        FString PendingClassLabel;
        FString LastClassLabel;
        int32 LastProcessedFrameSequence = 0;
        int32 ConsecutiveDetections = 0;
        int32 ConsecutiveMisses = 0;
        bool bHasPendingTarget = false;
        bool bHasLastTarget = false;
        bool bHasRawTarget = false;
        bool bHasTrackedBox = false;
    };

    FCriticalSection DepthDetectionMetricsMutex;
    bool bDepthDetectionMetricsInitialized = false;
    FString DepthDetectionMetricsCsvPath;
    FString DepthDetectionMetricsRunId;

    constexpr int32 DepthDetectionMetricsSchemaVersion = 2;
    constexpr float ResolvedActorMatchRadius = 250.f;

    bool IsDetectionMetricLoggingEnabled()
    {
        UEagleEyeDetectionSettings::LoadRuntimeConfig();
        const UEagleEyeDetectionSettings* Settings = GetDefault<UEagleEyeDetectionSettings>();
        return !Settings || Settings->bEnableDetectionMetricLogs;
    }

    const TCHAR* BoolToCsv(bool bValue)
    {
        return bValue ? TEXT("true") : TEXT("false");
    }

    FString EscapeCsvField(const FString& Value)
    {
        if (!Value.Contains(TEXT(",")) && !Value.Contains(TEXT("\"")) && !Value.Contains(TEXT("\n")) && !Value.Contains(TEXT("\r")))
        {
            return Value;
        }

        FString Escaped = Value;
        Escaped.ReplaceInline(TEXT("\""), TEXT("\"\""));
        return FString::Printf(TEXT("\"%s\""), *Escaped);
    }

    FString GetDepthDetectionMetricsHeader()
    {
        return
            TEXT("schema_version,run_id,timestamp,world_seconds,bot,frame_sequence,frame_age_ms,outcome,depth_success,shared_fallback,")
            TEXT("has_player_target,expected_in_fov,player_x,player_y,player_z,")
            TEXT("has_expected_target,target_error_cm,target_planar_error_cm,predicted_error_cm,predicted_planar_error_cm,")
            TEXT("expected_x,expected_y,expected_z,target_x,target_y,target_z,predicted_x,predicted_y,predicted_z,")
            TEXT("resolved_actor,resolved_actor_class,resolved_actor_dist_cm,resolved_actor_is_player,resolved_actor_match,")
            TEXT("class_id,class_label,confidence,box_center_x,box_center_y,box_w,box_h,target_pixel_x,target_pixel_y,")
            TEXT("depth_pixel_x,depth_pixel_y,target_ray_bias,scene_depth,depth_samples,cluster_samples,cluster_ratio,")
            TEXT("required_cluster_samples,required_cluster_ratio,cluster_score,cluster_pixel_dist,cluster_ref_dist,")
            TEXT("tested,valid,too_near,too_far,non_finite,depth_min,depth_max,depth_avg,valid_depth_min,valid_depth_max,valid_depth_avg,")
            TEXT("track_matched,track_iou,track_center_dist,raw_jump_cm,clamped,consecutive_detections,consecutive_misses,detections\n");
    }

    bool DoesCsvHeaderMatch(const FString& Path, const FString& ExpectedHeader)
    {
        FString Contents;
        if (!FFileHelper::LoadFileToString(Contents, *Path))
        {
            return false;
        }

        int32 NewlineIndex = INDEX_NONE;
        FString ExistingHeader = Contents;
        if (Contents.FindChar(TEXT('\n'), NewlineIndex))
        {
            ExistingHeader = Contents.Left(NewlineIndex + 1);
        }

        return ExistingHeader == ExpectedHeader;
    }

    void InitDepthDetectionMetricsTable()
    {
        if (bDepthDetectionMetricsInitialized)
        {
            return;
        }

        if (DepthDetectionMetricsRunId.IsEmpty())
        {
            DepthDetectionMetricsRunId = FGuid::NewGuid().ToString(EGuidFormats::Digits);
        }

        const FString Header = GetDepthDetectionMetricsHeader();
        const FString DefaultCsvPath = FPaths::Combine(
            FPaths::ProjectSavedDir(),
            TEXT("Profiling"),
            TEXT("DepthDetectionMetrics.csv"));
        DepthDetectionMetricsCsvPath = DefaultCsvPath;
        if (IFileManager::Get().FileExists(*DepthDetectionMetricsCsvPath) &&
            !DoesCsvHeaderMatch(DepthDetectionMetricsCsvPath, Header))
        {
            DepthDetectionMetricsCsvPath = FPaths::Combine(
                FPaths::ProjectSavedDir(),
                TEXT("Profiling"),
                TEXT("DepthDetectionMetrics_v2.csv"));
        }

        const FString Directory = FPaths::GetPath(DepthDetectionMetricsCsvPath);
        if (!Directory.IsEmpty())
        {
            IFileManager::Get().MakeDirectory(*Directory, true);
        }

        if (!IFileManager::Get().FileExists(*DepthDetectionMetricsCsvPath))
        {
            FFileHelper::SaveStringToFile(
                Header,
                *DepthDetectionMetricsCsvPath,
                FFileHelper::EEncodingOptions::AutoDetect,
                &IFileManager::Get());
        }

        bDepthDetectionMetricsInitialized = true;
        UE_LOG(LogTemp, Log, TEXT("Depth detection metrics table enabled: %s"), *DepthDetectionMetricsCsvPath);
    }

    struct FMetricPlayerContext
    {
        FVector PlayerLocation = FVector::ZeroVector;
        FVector ExpectedTargetLocation = FVector::ZeroVector;
        bool bHasPlayerTarget = false;
        bool bExpectedInFov = false;
    };

    FMetricPlayerContext BuildMetricPlayerContext(
        const APawn& ControlledPawn,
        const UMyActorComponent& Detector,
        const FVector& TargetOffset)
    {
        FMetricPlayerContext Context;
        const UWorld* World = ControlledPawn.GetWorld();
        const APlayerController* PlayerController = World ? World->GetFirstPlayerController() : nullptr;
        const APawn* PlayerPawn = PlayerController ? PlayerController->GetPawn() : nullptr;
        if (!PlayerPawn || PlayerPawn == &ControlledPawn)
        {
            return Context;
        }

        Context.bHasPlayerTarget = true;
        Context.PlayerLocation = PlayerPawn->GetActorLocation();
        Context.ExpectedTargetLocation = Context.PlayerLocation + TargetOffset;

        const UCameraComponent* Camera = ControlledPawn.FindComponentByClass<UCameraComponent>();
        if (!Camera || Detector.LastFrameSourceWidth <= 0 || Detector.LastFrameSourceHeight <= 0)
        {
            return Context;
        }

        const FVector ToTarget = PlayerPawn->GetPawnViewLocation() - Camera->GetComponentLocation();
        const float Distance = ToTarget.Size();
        if (Distance <= KINDA_SMALL_NUMBER)
        {
            Context.bExpectedInFov = true;
            return Context;
        }

        const FVector Direction = ToTarget / Distance;
        const FTransform CameraTransform = Camera->GetComponentTransform();
        const float ForwardAmount = FVector::DotProduct(Direction, CameraTransform.GetUnitAxis(EAxis::X));
        if (ForwardAmount <= KINDA_SMALL_NUMBER)
        {
            return Context;
        }

        const float RightAmount = FVector::DotProduct(Direction, CameraTransform.GetUnitAxis(EAxis::Y));
        const float UpAmount = FVector::DotProduct(Direction, CameraTransform.GetUnitAxis(EAxis::Z));
        const float Aspect = static_cast<float>(FMath::Max(Detector.LastFrameSourceWidth, 1)) /
            static_cast<float>(FMath::Max(Detector.LastFrameSourceHeight, 1));
        const float HalfHorizontalFovRad = FMath::DegreesToRadians(Camera->FieldOfView) * 0.5f;
        const float HalfVerticalFovRad = FMath::Atan(FMath::Tan(HalfHorizontalFovRad) / FMath::Max(Aspect, KINDA_SMALL_NUMBER));
        const float HorizontalAngleRad = FMath::Atan2(RightAmount, ForwardAmount);
        const float VerticalAngleRad = FMath::Atan2(UpAmount, ForwardAmount);
        Context.bExpectedInFov =
            FMath::Abs(HorizontalAngleRad) <= HalfHorizontalFovRad &&
            FMath::Abs(VerticalAngleRad) <= HalfVerticalFovRad;
        return Context;
    }

    struct FResolvedActorMetric
    {
        FString ActorName;
        FString ActorClassName;
        float Distance = -1.f;
        bool bIsPlayer = false;
        bool bMatchesResolvedTarget = false;
    };

    FResolvedActorMetric FindResolvedActorMetric(
        const APawn& ControlledPawn,
        const FVector* TargetLocation,
        float MatchRadius = ResolvedActorMatchRadius)
    {
        FResolvedActorMetric Metric;
        const UWorld* World = ControlledPawn.GetWorld();
        if (!World || !TargetLocation)
        {
            return Metric;
        }

        const APlayerController* PlayerController = World->GetFirstPlayerController();
        const APawn* PlayerPawn = PlayerController ? PlayerController->GetPawn() : nullptr;
        const APawn* BestPawn = nullptr;
        float BestDistSq = FLT_MAX;
        for (TActorIterator<APawn> It(World); It; ++It)
        {
            const APawn* Candidate = *It;
            if (!IsValid(Candidate) || Candidate == &ControlledPawn)
            {
                continue;
            }

            const float DistSq = FVector::DistSquared(Candidate->GetActorLocation(), *TargetLocation);
            if (DistSq < BestDistSq)
            {
                BestDistSq = DistSq;
                BestPawn = Candidate;
            }
        }

        if (!BestPawn)
        {
            return Metric;
        }

        Metric.ActorName = BestPawn->GetName();
        Metric.ActorClassName = BestPawn->GetClass() ? BestPawn->GetClass()->GetName() : FString();
        Metric.Distance = FMath::Sqrt(BestDistSq);
        Metric.bIsPlayer = BestPawn == PlayerPawn;
        Metric.bMatchesResolvedTarget = Metric.Distance <= MatchRadius;
        return Metric;
    }

    FString ExtractDetectionClassLabel(const FDetectionResult& Detection)
    {
        FString Label = Detection.Label;
        int32 SeparatorIndex = INDEX_NONE;
        if (Label.FindChar(TEXT(':'), SeparatorIndex))
        {
            Label = Label.Left(SeparatorIndex);
        }

        return Label.TrimStartAndEnd();
    }

    bool IsActionableDetection(
        const FDetectionResult& Detection,
        const TArray<int32>& ActionableClassIds,
        const TArray<FName>& ActionableClassLabels)
    {
        if (ActionableClassIds.Num() == 0 && ActionableClassLabels.Num() == 0)
        {
            return true;
        }

        if (ActionableClassIds.Contains(Detection.ClassId))
        {
            return true;
        }

        const FString ClassLabel = ExtractDetectionClassLabel(Detection);
        for (const FName& ActionableLabel : ActionableClassLabels)
        {
            if (!ActionableLabel.IsNone() &&
                ClassLabel.Equals(ActionableLabel.ToString(), ESearchCase::IgnoreCase))
            {
                return true;
            }
        }

        return false;
    }

    float ParseConfidence(const FString& Label)
    {
        int32 SeparatorIndex = INDEX_NONE;
        if (!Label.FindChar(TEXT(':'), SeparatorIndex))
        {
            return 0.f;
        }

        return FCString::Atof(*Label.Mid(SeparatorIndex + 1).TrimStartAndEnd());
    }

    bool DetectionToBox(const FDetectionResult& Detection, FDetectionBox& OutBox)
    {
        if (Detection.Corners.Num() == 0)
        {
            return false;
        }

        float MinX = Detection.Corners[0].X;
        float MinY = Detection.Corners[0].Y;
        float MaxX = Detection.Corners[0].X;
        float MaxY = Detection.Corners[0].Y;

        for (const FVector2D& Corner : Detection.Corners)
        {
            MinX = FMath::Min(MinX, Corner.X);
            MinY = FMath::Min(MinY, Corner.Y);
            MaxX = FMath::Max(MaxX, Corner.X);
            MaxY = FMath::Max(MaxY, Corner.Y);
        }

        const FVector2D Size(FMath::Max(1.f, MaxX - MinX), FMath::Max(1.f, MaxY - MinY));
        if (Size.X <= 1.f || Size.Y <= 1.f)
        {
            return false;
        }

        OutBox.Center = FVector2D((MinX + MaxX) * 0.5f, (MinY + MaxY) * 0.5f);
        OutBox.Size = Size;
        OutBox.Area = Size.X * Size.Y;
        OutBox.Confidence = Detection.Confidence > 0.f ? Detection.Confidence : ParseConfidence(Detection.Label);
        OutBox.ClassId = Detection.ClassId;
        OutBox.ClassLabel = ExtractDetectionClassLabel(Detection);
        return true;
    }

    FVector2D GetBoxMin(const FDetectionBox& Box)
    {
        return Box.Center - (Box.Size * 0.5f);
    }

    FVector2D GetBoxMax(const FDetectionBox& Box)
    {
        return Box.Center + (Box.Size * 0.5f);
    }

    float CalculateBoxIoU(const FDetectionBox& A, const FDetectionBox& B)
    {
        const FVector2D AMin = GetBoxMin(A);
        const FVector2D AMax = GetBoxMax(A);
        const FVector2D BMin = GetBoxMin(B);
        const FVector2D BMax = GetBoxMax(B);

        const float InterMinX = FMath::Max(AMin.X, BMin.X);
        const float InterMinY = FMath::Max(AMin.Y, BMin.Y);
        const float InterMaxX = FMath::Min(AMax.X, BMax.X);
        const float InterMaxY = FMath::Min(AMax.Y, BMax.Y);
        const float InterWidth = FMath::Max(0.f, InterMaxX - InterMinX);
        const float InterHeight = FMath::Max(0.f, InterMaxY - InterMinY);
        const float InterArea = InterWidth * InterHeight;
        const float UnionArea = FMath::Max(A.Area + B.Area - InterArea, KINDA_SMALL_NUMBER);
        return InterArea / UnionArea;
    }

    float CalculateNormalizedCenterDistance(const FDetectionBox& A, const FDetectionBox& B, int32 SourceWidth, int32 SourceHeight)
    {
        const float Diagonal = FMath::Sqrt(
            FMath::Square(FMath::Max(static_cast<float>(SourceWidth), 1.f)) +
            FMath::Square(FMath::Max(static_cast<float>(SourceHeight), 1.f)));
        return FVector2D::Distance(A.Center, B.Center) / FMath::Max(Diagonal, 1.f);
    }

    FDetectionBox SmoothBox(const FDetectionBox& PreviousBox, const FDetectionBox& CurrentBox, float DeltaSeconds, float SmoothingSpeed)
    {
        FDetectionBox SmoothedBox = CurrentBox;
        if (SmoothingSpeed > 0.f)
        {
            SmoothedBox.Center = FMath::Vector2DInterpTo(PreviousBox.Center, CurrentBox.Center, DeltaSeconds, SmoothingSpeed);
            SmoothedBox.Size = FMath::Vector2DInterpTo(PreviousBox.Size, CurrentBox.Size, DeltaSeconds, SmoothingSpeed);
        }

        SmoothedBox.Size.X = FMath::Max(SmoothedBox.Size.X, 1.f);
        SmoothedBox.Size.Y = FMath::Max(SmoothedBox.Size.Y, 1.f);
        SmoothedBox.Area = SmoothedBox.Size.X * SmoothedBox.Size.Y;
        SmoothedBox.Confidence = CurrentBox.Confidence;
        return SmoothedBox;
    }

    bool FindTrackedTargetDetection(
        const UMyActorComponent& Detector,
        const FCrowTargetDetectionMemory& Memory,
        const TArray<int32>& ActionableClassIds,
        const TArray<FName>& ActionableClassLabels,
        float MinAcceptedConfidence,
        bool bEnableTracking,
        float TrackMatchMinIoU,
        float TrackMatchMaxCenterDistance,
        float TrackSwitchConfidenceMargin,
        FDetectionBox& OutBox,
        bool& bOutMatchedTrack,
        float& OutMatchIoU,
        float& OutMatchDistance)
    {
        bool bFoundBestBox = false;
        bool bFoundTrackedBox = false;
        FDetectionBox BestBox;
        FDetectionBox BestTrackedBox;
        float BestScore = -FLT_MAX;
        float BestTrackedScore = -FLT_MAX;
        float BestTrackedIoU = 0.f;
        float BestTrackedDistance = TNumericLimits<float>::Max();

        for (const FDetectionResult& Detection : Detector.LastFrameDetections)
        {
            if (!IsActionableDetection(Detection, ActionableClassIds, ActionableClassLabels))
            {
                continue;
            }

            FDetectionBox Box;
            if (!DetectionToBox(Detection, Box))
            {
                continue;
            }
            if (Box.Confidence < MinAcceptedConfidence)
            {
                continue;
            }

            const float ConfidenceScore = Box.Confidence > 0.f ? Box.Confidence : 0.f;
            const float AreaScore = Box.Area / FMath::Max(
                static_cast<float>(Detector.LastFrameSourceWidth * Detector.LastFrameSourceHeight),
                1.f);
            const float Score = ConfidenceScore + (AreaScore * 0.1f);
            if (!bFoundBestBox || Score > BestScore)
            {
                BestBox = Box;
                BestScore = Score;
                bFoundBestBox = true;
            }

            if (bEnableTracking && Memory.bHasTrackedBox)
            {
                const float IoU = CalculateBoxIoU(Box, Memory.LastTrackedBox);
                const float CenterDistance = CalculateNormalizedCenterDistance(
                    Box,
                    Memory.LastTrackedBox,
                    Detector.LastFrameSourceWidth,
                    Detector.LastFrameSourceHeight);
                const float EffectiveCenterDistance = FMath::Min(TrackMatchMaxCenterDistance, 0.12f);
                const bool bMatchesTrack = IoU >= TrackMatchMinIoU || CenterDistance <= EffectiveCenterDistance;
                if (bMatchesTrack)
                {
                    const float TrackScore = Score + (IoU * 2.f) + FMath::Max(0.f, 1.f - CenterDistance);
                    if (!bFoundTrackedBox || TrackScore > BestTrackedScore)
                    {
                        BestTrackedBox = Box;
                        BestTrackedScore = TrackScore;
                        BestTrackedIoU = IoU;
                        BestTrackedDistance = CenterDistance;
                        bFoundTrackedBox = true;
                    }
                }
            }
        }

        if (!bFoundBestBox)
        {
            return false;
        }

        bOutMatchedTrack = false;
        OutMatchIoU = 0.f;
        OutMatchDistance = TNumericLimits<float>::Max();

        if (bEnableTracking && Memory.bHasTrackedBox && bFoundTrackedBox)
        {
            const bool bBestBoxClearlyBetter = BestBox.Confidence > BestTrackedBox.Confidence + TrackSwitchConfidenceMargin;
            if (!bBestBoxClearlyBetter)
            {
                OutBox = BestTrackedBox;
                bOutMatchedTrack = true;
                OutMatchIoU = BestTrackedIoU;
                OutMatchDistance = BestTrackedDistance;
                return true;
            }
        }

        OutBox = BestBox;
        return true;
    }

    FVector BuildCameraRayDirection(
        const UCameraComponent& Camera,
        const FVector2D& Pixel,
        int32 SourceWidth,
        int32 SourceHeight)
    {
        const float SafeWidth = FMath::Max(static_cast<float>(SourceWidth), 1.f);
        const float SafeHeight = FMath::Max(static_cast<float>(SourceHeight), 1.f);
        const float NdcX = ((Pixel.X / SafeWidth) * 2.f) - 1.f;
        const float NdcY = 1.f - ((Pixel.Y / SafeHeight) * 2.f);

        const float Aspect = SafeWidth / SafeHeight;
        const float HalfHorizontalFovRad = FMath::DegreesToRadians(Camera.FieldOfView) * 0.5f;
        const float HalfVerticalFovRad = FMath::Atan(FMath::Tan(HalfHorizontalFovRad) / FMath::Max(Aspect, KINDA_SMALL_NUMBER));

        const FTransform CameraTransform = Camera.GetComponentTransform();
        const FVector Direction =
            CameraTransform.GetUnitAxis(EAxis::X) +
            (CameraTransform.GetUnitAxis(EAxis::Y) * FMath::Tan(HalfHorizontalFovRad) * NdcX) +
            (CameraTransform.GetUnitAxis(EAxis::Z) * FMath::Tan(HalfVerticalFovRad) * NdcY);

        return Direction.GetSafeNormal();
    }

    struct FSceneDepthResolveStats
    {
        int32 Tested = 0;
        int32 Valid = 0;
        int32 TooNear = 0;
        int32 TooFar = 0;
        int32 NonFinite = 0;
        float MinDepth = FLT_MAX;
        float MaxDepth = -FLT_MAX;
        float ValidMinDepth = FLT_MAX;
        float ValidMaxDepth = -FLT_MAX;
        double DepthSum = 0.0;
        double ValidDepthSum = 0.0;
        FVector2D MinDepthPixel = FVector2D::ZeroVector;
        FVector2D MaxDepthPixel = FVector2D::ZeroVector;
        float ClusterScore = 0.f;
        float ClusterPixelDistance = 0.f;
        float ClusterReferenceDistance = -1.f;

        float GetAverageDepth() const
        {
            return Tested > NonFinite ? static_cast<float>(DepthSum / static_cast<double>(Tested - NonFinite)) : 0.f;
        }

        float GetValidAverageDepth() const
        {
            return Valid > 0 ? static_cast<float>(ValidDepthSum / static_cast<double>(Valid)) : 0.f;
        }
    };

    void AppendDepthDetectionMetricRow(
        const APawn& ControlledPawn,
        const UMyActorComponent& Detector,
        const FVector& ExpectedTargetOffset,
        const TCHAR* Outcome,
        bool bDepthSuccess,
        bool bSharedFallback,
        const FDetectionBox* Box,
        const FVector2D* TargetPixel,
        const FVector2D* ResolvedDepthPixel,
        float TargetRayBias,
        float SceneDepth,
        int32 DepthSampleCount,
        int32 DepthClusterSampleCount,
        int32 RequiredDepthClusterSamples,
        float RequiredDepthClusterRatio,
        const FSceneDepthResolveStats& DepthStats,
        const FVector* TargetLocation,
        const FVector* PredictedTargetLocation,
        bool bMatchedTrack,
        float TrackMatchIoU,
        float TrackMatchDistance,
        float RawTargetJumpDistance,
        bool bClampedTargetJump,
        int32 ConsecutiveDetections,
        int32 ConsecutiveMisses)
    {
        if (!IsDetectionMetricLoggingEnabled())
        {
            return;
        }

        FScopeLock Lock(&DepthDetectionMetricsMutex);
        InitDepthDetectionMetricsTable();
        if (!bDepthDetectionMetricsInitialized)
        {
            return;
        }

        const UWorld* World = ControlledPawn.GetWorld();
        const float WorldSeconds = World ? World->GetTimeSeconds() : 0.f;
        const float FrameAgeMs = Detector.LastFrameTimeSeconds > -FLT_MAX * 0.5f
            ? (WorldSeconds - Detector.LastFrameTimeSeconds) * 1000.f
            : -1.f;

        const FMetricPlayerContext PlayerContext = BuildMetricPlayerContext(ControlledPawn, Detector, ExpectedTargetOffset);
        const FResolvedActorMetric ResolvedActorMetric = FindResolvedActorMetric(ControlledPawn, TargetLocation);
        const FVector ExpectedTarget = PlayerContext.ExpectedTargetLocation;

        const FVector EmptyTarget(-1.f, -1.f, -1.f);
        const FVector& EffectiveTarget = TargetLocation ? *TargetLocation : EmptyTarget;
        const FVector& EffectivePredictedTarget = PredictedTargetLocation ? *PredictedTargetLocation : EmptyTarget;
        const float TargetError = PlayerContext.bHasPlayerTarget && TargetLocation
            ? FVector::Dist(*TargetLocation, ExpectedTarget)
            : -1.f;
        const float TargetPlanarError = PlayerContext.bHasPlayerTarget && TargetLocation
            ? FVector::Dist2D(*TargetLocation, ExpectedTarget)
            : -1.f;
        const float PredictedError = PlayerContext.bHasPlayerTarget && PredictedTargetLocation
            ? FVector::Dist(*PredictedTargetLocation, ExpectedTarget)
            : -1.f;
        const float PredictedPlanarError = PlayerContext.bHasPlayerTarget && PredictedTargetLocation
            ? FVector::Dist2D(*PredictedTargetLocation, ExpectedTarget)
            : -1.f;

        const float ClusterRatio = DepthSampleCount > 0
            ? static_cast<float>(DepthClusterSampleCount) / static_cast<float>(DepthSampleCount)
            : 0.f;
        const float MinDepth = DepthStats.MinDepth == FLT_MAX ? 0.f : DepthStats.MinDepth;
        const float MaxDepth = DepthStats.MaxDepth == -FLT_MAX ? 0.f : DepthStats.MaxDepth;
        const float ValidMinDepth = DepthStats.ValidMinDepth == FLT_MAX ? 0.f : DepthStats.ValidMinDepth;
        const float ValidMaxDepth = DepthStats.ValidMaxDepth == -FLT_MAX ? 0.f : DepthStats.ValidMaxDepth;

        const FVector2D EmptyPixel(-1.f, -1.f);
        const FVector2D& EffectiveTargetPixel = TargetPixel ? *TargetPixel : EmptyPixel;
        const FVector2D& EffectiveDepthPixel = ResolvedDepthPixel ? *ResolvedDepthPixel : EmptyPixel;

        const int32 ClassId = Box ? Box->ClassId : -1;
        const FString ClassLabel = Box ? Box->ClassLabel : FString();
        const float Confidence = Box ? Box->Confidence : -1.f;
        const FVector2D BoxCenter = Box ? Box->Center : EmptyPixel;
        const FVector2D BoxSize = Box ? Box->Size : EmptyPixel;

        const FString Line = FString::Printf(
            TEXT("%d,%s,%s,%.4f,%s,%d,%.3f,%s,%s,%s,")
            TEXT("%s,%s,%.3f,%.3f,%.3f,")
            TEXT("%s,%.3f,%.3f,%.3f,%.3f,")
            TEXT("%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,")
            TEXT("%s,%s,%.3f,%s,%s,")
            TEXT("%d,%s,%.4f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,")
            TEXT("%.3f,%.3f,%.3f,%.3f,%d,%d,%.4f,%d,%.4f,%.4f,%.3f,%.3f,")
            TEXT("%d,%d,%d,%d,%d,%.3f,%.3f,%.3f,")
            TEXT("%.3f,%.3f,%.3f,")
            TEXT("%s,%.4f,%.4f,%.3f,%s,%d,%d,%d\n"),
            DepthDetectionMetricsSchemaVersion,
            *EscapeCsvField(DepthDetectionMetricsRunId),
            *FDateTime::Now().ToIso8601(),
            WorldSeconds,
            *EscapeCsvField(GetNameSafe(&ControlledPawn)),
            Detector.LastFrameSequence,
            FrameAgeMs,
            *EscapeCsvField(FString(Outcome)),
            BoolToCsv(bDepthSuccess),
            BoolToCsv(bSharedFallback),
            BoolToCsv(PlayerContext.bHasPlayerTarget),
            BoolToCsv(PlayerContext.bExpectedInFov),
            PlayerContext.PlayerLocation.X,
            PlayerContext.PlayerLocation.Y,
            PlayerContext.PlayerLocation.Z,
            BoolToCsv(PlayerContext.bHasPlayerTarget),
            TargetError,
            TargetPlanarError,
            PredictedError,
            PredictedPlanarError,
            ExpectedTarget.X,
            ExpectedTarget.Y,
            ExpectedTarget.Z,
            EffectiveTarget.X,
            EffectiveTarget.Y,
            EffectiveTarget.Z,
            EffectivePredictedTarget.X,
            EffectivePredictedTarget.Y,
            EffectivePredictedTarget.Z,
            *EscapeCsvField(ResolvedActorMetric.ActorName),
            *EscapeCsvField(ResolvedActorMetric.ActorClassName),
            ResolvedActorMetric.Distance,
            BoolToCsv(ResolvedActorMetric.bIsPlayer),
            BoolToCsv(ResolvedActorMetric.bMatchesResolvedTarget),
            ClassId,
            *EscapeCsvField(ClassLabel),
            Confidence,
            BoxCenter.X,
            BoxCenter.Y,
            BoxSize.X,
            BoxSize.Y,
            EffectiveTargetPixel.X,
            EffectiveTargetPixel.Y,
            EffectiveDepthPixel.X,
            EffectiveDepthPixel.Y,
            TargetRayBias,
            SceneDepth,
            DepthSampleCount,
            DepthClusterSampleCount,
            ClusterRatio,
            RequiredDepthClusterSamples,
            RequiredDepthClusterRatio,
            DepthStats.ClusterScore,
            DepthStats.ClusterPixelDistance,
            DepthStats.ClusterReferenceDistance,
            DepthStats.Tested,
            DepthStats.Valid,
            DepthStats.TooNear,
            DepthStats.TooFar,
            DepthStats.NonFinite,
            MinDepth,
            MaxDepth,
            DepthStats.GetAverageDepth(),
            ValidMinDepth,
            ValidMaxDepth,
            DepthStats.GetValidAverageDepth(),
            BoolToCsv(bMatchedTrack),
            TrackMatchIoU,
            TrackMatchDistance,
            RawTargetJumpDistance,
            BoolToCsv(bClampedTargetJump),
            ConsecutiveDetections,
            ConsecutiveMisses,
            Detector.LastFrameDetections.Num());

        FFileHelper::SaveStringToFile(
            Line,
            *DepthDetectionMetricsCsvPath,
            FFileHelper::EEncodingOptions::AutoDetect,
            &IFileManager::Get(),
            FILEWRITE_Append);
    }

    bool TryResolveTargetFromSceneDepth(
        const UMyActorComponent& Detector,
        const UCameraComponent& Camera,
        const FVector& RayOrigin,
        const FVector2D& BoxMin,
        const FVector2D& BoxMax,
        const FVector2D& TargetPixel,
        float MaxSceneDepth,
        bool bHasReferenceTarget,
        const FVector& ReferenceTargetLocation,
        float MaxReferenceTargetDistance,
        FVector& OutLocation,
        float& OutSceneDepth,
        FVector2D& OutDepthPixel,
        int32& OutSampleCount,
        int32& OutClusterSampleCount,
        FSceneDepthResolveStats& OutStats)
    {
        OutLocation = FVector::ZeroVector;
        OutSceneDepth = 0.f;
        OutDepthPixel = FVector2D::ZeroVector;
        OutSampleCount = 0;
        OutClusterSampleCount = 0;
        OutStats = FSceneDepthResolveStats();

        const int32 DepthWidth = Detector.LastFrameSceneDepthWidth;
        const int32 DepthHeight = Detector.LastFrameSceneDepthHeight;
        if (Detector.LastFrameSceneDepth.Num() != DepthWidth * DepthHeight ||
            DepthWidth <= 0 ||
            DepthHeight <= 0 ||
            Detector.LastFrameSourceWidth <= 0 ||
            Detector.LastFrameSourceHeight <= 0)
        {
            return false;
        }

        const float ScaleX = static_cast<float>(DepthWidth) / FMath::Max(static_cast<float>(Detector.LastFrameSourceWidth), 1.f);
        const float ScaleY = static_cast<float>(DepthHeight) / FMath::Max(static_cast<float>(Detector.LastFrameSourceHeight), 1.f);
        const FVector2D TargetDepthPixel(
            FMath::Clamp(TargetPixel.X * ScaleX, 0.f, static_cast<float>(DepthWidth - 1)),
            FMath::Clamp(TargetPixel.Y * ScaleY, 0.f, static_cast<float>(DepthHeight - 1)));

        const float SourceMaxX = FMath::Max(static_cast<float>(Detector.LastFrameSourceWidth - 1), 0.f);
        const float SourceMaxY = FMath::Max(static_cast<float>(Detector.LastFrameSourceHeight - 1), 0.f);
        const float InnerMinX = FMath::Clamp(FMath::Lerp(BoxMin.X, BoxMax.X, 0.25f), 0.f, SourceMaxX);
        const float InnerMaxX = FMath::Clamp(FMath::Lerp(BoxMin.X, BoxMax.X, 0.75f), 0.f, SourceMaxX);
        const float InnerMinY = FMath::Clamp(FMath::Lerp(BoxMin.Y, BoxMax.Y, 0.12f), 0.f, SourceMaxY);
        const float InnerMaxY = FMath::Clamp(FMath::Lerp(BoxMin.Y, BoxMax.Y, 0.58f), 0.f, SourceMaxY);

        const int32 DepthMinX = FMath::Clamp(FMath::RoundToInt(InnerMinX * ScaleX), 0, DepthWidth - 1);
        const int32 DepthMaxX = FMath::Clamp(FMath::RoundToInt(InnerMaxX * ScaleX), 0, DepthWidth - 1);
        const int32 DepthMinY = FMath::Clamp(FMath::RoundToInt(InnerMinY * ScaleY), 0, DepthHeight - 1);
        const int32 DepthMaxY = FMath::Clamp(FMath::RoundToInt(InnerMaxY * ScaleY), 0, DepthHeight - 1);
        if (DepthMaxX < DepthMinX || DepthMaxY < DepthMinY)
        {
            return false;
        }

        struct FDepthSample
        {
            float Depth = 0.f;
            FVector2D Pixel = FVector2D::ZeroVector;
        };

        TArray<FDepthSample> Samples;
        Samples.Reserve(128);
        constexpr float MinValidDepth = 100.f;
        const float EffectiveMaxSceneDepth = MaxSceneDepth > MinValidDepth ? MaxSceneDepth : 100000.f;
        const int32 SampleStep = FMath::Max(1, FMath::Min(DepthMaxX - DepthMinX + 1, DepthMaxY - DepthMinY + 1) / 12);

        for (int32 Y = DepthMinY; Y <= DepthMaxY; Y += SampleStep)
        {
            for (int32 X = DepthMinX; X <= DepthMaxX; X += SampleStep)
            {
                const float Depth = Detector.LastFrameSceneDepth[(Y * DepthWidth) + X];
                ++OutStats.Tested;
                if (!FMath::IsFinite(Depth))
                {
                    ++OutStats.NonFinite;
                    continue;
                }

                OutStats.DepthSum += Depth;
                if (Depth < OutStats.MinDepth)
                {
                    OutStats.MinDepth = Depth;
                    OutStats.MinDepthPixel = FVector2D(static_cast<float>(X), static_cast<float>(Y));
                }
                if (Depth > OutStats.MaxDepth)
                {
                    OutStats.MaxDepth = Depth;
                    OutStats.MaxDepthPixel = FVector2D(static_cast<float>(X), static_cast<float>(Y));
                }

                if (Depth < MinValidDepth)
                {
                    ++OutStats.TooNear;
                    continue;
                }
                if (Depth > EffectiveMaxSceneDepth)
                {
                    ++OutStats.TooFar;
                    continue;
                }

                ++OutStats.Valid;
                if (Depth >= MinValidDepth && Depth <= EffectiveMaxSceneDepth)
                {
                    OutStats.ValidDepthSum += Depth;
                    OutStats.ValidMinDepth = FMath::Min(OutStats.ValidMinDepth, Depth);
                    OutStats.ValidMaxDepth = FMath::Max(OutStats.ValidMaxDepth, Depth);
                    Samples.Add({ Depth, FVector2D(static_cast<float>(X), static_cast<float>(Y)) });
                }
            }
        }

        OutSampleCount = Samples.Num();
        if (Samples.Num() == 0)
        {
            return false;
        }

        Samples.Sort([](const FDepthSample& A, const FDepthSample& B)
        {
            return A.Depth < B.Depth;
        });

        const FVector CameraForward = Camera.GetComponentTransform().GetUnitAxis(EAxis::X);
        const float EffectiveMaxReferenceTargetDistance = MaxReferenceTargetDistance > 0.f
            ? MaxReferenceTargetDistance
            : 0.f;
        const float InnerDepthDiagonal = FVector2D::Distance(
            FVector2D(static_cast<float>(DepthMinX), static_cast<float>(DepthMinY)),
            FVector2D(static_cast<float>(DepthMaxX), static_cast<float>(DepthMaxY)));
        const float MaxTargetPixelDistance = FMath::Max(16.f, InnerDepthDiagonal * 0.6f);

        struct FDepthClusterCandidate
        {
            double DepthSum = 0.0;
            FVector2D PixelSum = FVector2D::ZeroVector;
            int32 Count = 0;
            float Score = -FLT_MAX;
            float PixelDistance = 0.f;
            float ReferenceDistance = -1.f;
        };

        FDepthClusterCandidate BestCluster;
        for (int32 SeedIndex = 0; SeedIndex < Samples.Num(); ++SeedIndex)
        {
            const float ReferenceDepth = Samples[SeedIndex].Depth;
            const float ClusterTolerance = FMath::Clamp(ReferenceDepth * 0.08f, 25.f, 250.f);

            FDepthClusterCandidate Candidate;
            for (const FDepthSample& Sample : Samples)
            {
                if (FMath::Abs(Sample.Depth - ReferenceDepth) > ClusterTolerance)
                {
                    continue;
                }

                Candidate.DepthSum += Sample.Depth;
                Candidate.PixelSum += Sample.Pixel;
                ++Candidate.Count;
            }

            if (Candidate.Count <= 0)
            {
                continue;
            }

            const float CandidateDepth = static_cast<float>(Candidate.DepthSum / Candidate.Count);
            const FVector2D CandidatePixel = Candidate.PixelSum / static_cast<float>(Candidate.Count);
            Candidate.PixelDistance = FVector2D::Distance(CandidatePixel, TargetDepthPixel);

            float ReferenceScore = 0.f;
            if (bHasReferenceTarget && EffectiveMaxReferenceTargetDistance > 0.f)
            {
                const FVector CandidateRayDirection = BuildCameraRayDirection(
                    Camera,
                    CandidatePixel,
                    DepthWidth,
                    DepthHeight);
                const float ForwardDot = FVector::DotProduct(CandidateRayDirection, CameraForward);
                if (ForwardDot > KINDA_SMALL_NUMBER)
                {
                    const float RayDistance = CandidateDepth / ForwardDot;
                    if (FMath::IsFinite(RayDistance) && RayDistance > 0.f && RayDistance <= EffectiveMaxSceneDepth)
                    {
                        const FVector CandidateLocation = RayOrigin + (CandidateRayDirection * RayDistance);
                        Candidate.ReferenceDistance = FVector::Dist2D(CandidateLocation, ReferenceTargetLocation);
                        ReferenceScore = 1.f - FMath::Clamp(Candidate.ReferenceDistance / EffectiveMaxReferenceTargetDistance, 0.f, 1.f);
                    }
                }
            }

            const float CountScore = static_cast<float>(Candidate.Count) / static_cast<float>(Samples.Num());
            const float PixelScore = 1.f - FMath::Clamp(Candidate.PixelDistance / MaxTargetPixelDistance, 0.f, 1.f);
            const float DepthOrderScore = Samples.Num() > 1
                ? 1.f - (static_cast<float>(SeedIndex) / static_cast<float>(Samples.Num() - 1))
                : 1.f;
            Candidate.Score = (CountScore * 2.0f) + (PixelScore * 1.25f) + (ReferenceScore * 1.5f) + (DepthOrderScore * 0.15f);

            if (Candidate.Score > BestCluster.Score)
            {
                BestCluster = Candidate;
            }
        }

        if (BestCluster.Count <= 0)
        {
            return false;
        }

        OutClusterSampleCount = BestCluster.Count;
        OutSceneDepth = static_cast<float>(BestCluster.DepthSum / BestCluster.Count);
        OutDepthPixel = BestCluster.PixelSum / static_cast<float>(BestCluster.Count);
        OutStats.ClusterScore = BestCluster.Score;
        OutStats.ClusterPixelDistance = BestCluster.PixelDistance;
        OutStats.ClusterReferenceDistance = BestCluster.ReferenceDistance;
        const FVector RayDirection = BuildCameraRayDirection(
            Camera,
            OutDepthPixel,
            DepthWidth,
            DepthHeight);

        const float ForwardDot = FVector::DotProduct(RayDirection, CameraForward);
        if (ForwardDot <= KINDA_SMALL_NUMBER)
        {
            return false;
        }

        const float RayDistance = OutSceneDepth / ForwardDot;
        if (!FMath::IsFinite(RayDistance) || RayDistance <= 0.f || RayDistance > EffectiveMaxSceneDepth)
        {
            return false;
        }

        OutLocation = RayOrigin + (RayDirection * RayDistance);
        return true;
    }

    uint64 MakeCrowDebugKey(const AActor& ControlledActor, uint32 Slot)
    {
        return (static_cast<uint64>(ControlledActor.GetUniqueID()) << 8) | Slot;
    }

    bool IsDetectionDebugEnabled()
    {
        UEagleEyeDetectionSettings::LoadRuntimeConfig();
        const UEagleEyeDetectionSettings* Settings = GetDefault<UEagleEyeDetectionSettings>();
        return !Settings || Settings->bEnableDetectionDebugLogs;
    }

    void PrintCrowDetectionDebug(
        const AActor& ControlledActor,
        const FString& Message,
        const FColor& Color,
        uint32 Slot,
        bool bDrawDebug,
        bool bLogDebug,
        float Duration = 0.25f)
    {
        const FString FullMessage = FString::Printf(TEXT("%s: %s"), *ControlledActor.GetName(), *Message);
        const bool bGlobalDebugEnabled = IsDetectionDebugEnabled();

        if (bGlobalDebugEnabled && bDrawDebug && GEngine)
        {
            GEngine->AddOnScreenDebugMessage(
                MakeCrowDebugKey(ControlledActor, Slot),
                Duration,
                Color,
                FullMessage);
        }

        if (bGlobalDebugEnabled && bLogDebug)
        {
            UE_LOG(LogTemp, Log, TEXT("CrowDetectionDebug: %s"), *FullMessage);
        }
    }

    void WriteDetectedClassBlackboard(
        UBlackboardComponent& Blackboard,
        const FBlackboardKeySelector& DetectedClassIdKey,
        const FBlackboardKeySelector& DetectedClassLabelKey,
        int32 ClassId,
        const FString& ClassLabel)
    {
        if (!DetectedClassIdKey.SelectedKeyName.IsNone())
        {
            Blackboard.SetValueAsInt(DetectedClassIdKey.SelectedKeyName, ClassId);
        }

        if (!DetectedClassLabelKey.SelectedKeyName.IsNone())
        {
            Blackboard.SetValueAsString(DetectedClassLabelKey.SelectedKeyName, ClassLabel);
        }
    }

    void WriteTargetBlackboard(
        UBlackboardComponent& Blackboard,
        const FBlackboardKeySelector& HasTargetKey,
        const FBlackboardKeySelector& DetectedTargetLocationKey,
        const FBlackboardKeySelector& DetectionConfidenceKey,
        const FBlackboardKeySelector& DetectedClassIdKey,
        const FBlackboardKeySelector& DetectedClassLabelKey,
        const FVector& TargetLocation,
        float Confidence,
        int32 ClassId,
        const FString& ClassLabel)
    {
        Blackboard.SetValueAsBool(HasTargetKey.SelectedKeyName, true);
        Blackboard.SetValueAsVector(DetectedTargetLocationKey.SelectedKeyName, TargetLocation);
        Blackboard.SetValueAsFloat(DetectionConfidenceKey.SelectedKeyName, Confidence);
        WriteDetectedClassBlackboard(Blackboard, DetectedClassIdKey, DetectedClassLabelKey, ClassId, ClassLabel);
    }

    float CalculatePlanarDistance(const FVector& A, const FVector& B)
    {
        FVector Offset = A - B;
        Offset.Z = 0.f;
        return Offset.Size();
    }

    void ClearTargetDetectionMemory(
        UBlackboardComponent& Blackboard,
        const FBlackboardKeySelector& HasTargetKey,
        const FBlackboardKeySelector& DetectedTargetLocationKey,
        const FBlackboardKeySelector& DetectionConfidenceKey,
        const FBlackboardKeySelector& DetectedClassIdKey,
        const FBlackboardKeySelector& DetectedClassLabelKey,
        FCrowTargetDetectionMemory& Memory)
    {
        Memory = FCrowTargetDetectionMemory();

        Blackboard.SetValueAsBool(HasTargetKey.SelectedKeyName, false);
        if (!DetectedTargetLocationKey.SelectedKeyName.IsNone())
        {
            Blackboard.ClearValue(DetectedTargetLocationKey.SelectedKeyName);
        }
        if (!DetectionConfidenceKey.SelectedKeyName.IsNone())
        {
            Blackboard.ClearValue(DetectionConfidenceKey.SelectedKeyName);
        }
        WriteDetectedClassBlackboard(Blackboard, DetectedClassIdKey, DetectedClassLabelKey, -1, FString());
    }

    bool KeepMemoryActive(
        UBlackboardComponent& Blackboard,
        const FBlackboardKeySelector& HasTargetKey,
        const FBlackboardKeySelector& DetectedTargetLocationKey,
        const FBlackboardKeySelector& DetectionConfidenceKey,
        const FBlackboardKeySelector& DetectedClassIdKey,
        const FBlackboardKeySelector& DetectedClassLabelKey,
        FCrowTargetDetectionMemory& Memory,
        float CurrentTime,
        float LoseTargetAfterSeconds)
    {
        const bool bActionCompleted = Memory.bHasLastTarget &&
            !HasTargetKey.SelectedKeyName.IsNone() &&
            !Blackboard.GetValueAsBool(HasTargetKey.SelectedKeyName);
        if (bActionCompleted)
        {
            ClearTargetDetectionMemory(
                Blackboard,
                HasTargetKey,
                DetectedTargetLocationKey,
                DetectionConfidenceKey,
                DetectedClassIdKey,
                DetectedClassLabelKey,
                Memory);
            return false;
        }

        if (!Memory.bHasLastTarget)
        {
            Blackboard.SetValueAsBool(HasTargetKey.SelectedKeyName, false);
            if (!DetectedTargetLocationKey.SelectedKeyName.IsNone())
            {
                Blackboard.ClearValue(DetectedTargetLocationKey.SelectedKeyName);
            }
            if (!DetectionConfidenceKey.SelectedKeyName.IsNone())
            {
                Blackboard.ClearValue(DetectionConfidenceKey.SelectedKeyName);
            }
            WriteDetectedClassBlackboard(Blackboard, DetectedClassIdKey, DetectedClassLabelKey, -1, FString());
            return false;
        }

        const bool bExpired = LoseTargetAfterSeconds > 0.f &&
            (CurrentTime - Memory.LastDetectedTime) > LoseTargetAfterSeconds;
        if (bExpired)
        {
            ClearTargetDetectionMemory(
                Blackboard,
                HasTargetKey,
                DetectedTargetLocationKey,
                DetectionConfidenceKey,
                DetectedClassIdKey,
                DetectedClassLabelKey,
                Memory);
            return false;
        }

        WriteTargetBlackboard(
            Blackboard,
            HasTargetKey,
            DetectedTargetLocationKey,
            DetectionConfidenceKey,
            DetectedClassIdKey,
            DetectedClassLabelKey,
            Memory.LastTargetLocation,
            Memory.LastConfidence,
            Memory.LastClassId,
            Memory.LastClassLabel);
        return true;
    }

    bool ApplyLockedTargetMemory(
        UBlackboardComponent& Blackboard,
        const FBlackboardKeySelector& HasTargetKey,
        const FBlackboardKeySelector& DetectedTargetLocationKey,
        const FBlackboardKeySelector& DetectionConfidenceKey,
        const FBlackboardKeySelector& DetectedClassIdKey,
        const FBlackboardKeySelector& DetectedClassLabelKey,
        FCrowTargetDetectionMemory& Memory,
        const FVector& DetectedTarget,
        float Confidence,
        int32 ClassId,
        const FString& ClassLabel,
        float CurrentTime,
        float SameTargetLocationThreshold,
        float NewTargetConfirmationSeconds,
        float PendingTargetStabilityThreshold,
        bool& bOutSameTarget,
        bool& bOutPendingTarget,
        bool& bOutCommittedNewTarget,
        float& OutPendingTargetAge,
        float& OutPlanarDelta)
    {
        bOutPendingTarget = false;
        bOutCommittedNewTarget = false;
        OutPendingTargetAge = 0.f;

        const bool bHasActiveTarget = Memory.bHasLastTarget &&
            !HasTargetKey.SelectedKeyName.IsNone() &&
            Blackboard.GetValueAsBool(HasTargetKey.SelectedKeyName);
        OutPlanarDelta = bHasActiveTarget
            ? CalculatePlanarDistance(DetectedTarget, Memory.LastTargetLocation)
            : 0.f;
        bOutSameTarget = bHasActiveTarget &&
            SameTargetLocationThreshold > 0.f &&
            OutPlanarDelta <= SameTargetLocationThreshold;

        if (!bHasActiveTarget || bOutSameTarget)
        {
            if (!bHasActiveTarget)
            {
                Memory.LastTargetLocation = DetectedTarget;
                Memory.bHasLastTarget = true;
                bOutCommittedNewTarget = true;
            }
            Memory.bHasPendingTarget = false;
        }
        else
        {
            const float EffectivePendingTargetStabilityThreshold = PendingTargetStabilityThreshold > 0.f
                ? PendingTargetStabilityThreshold
                : SameTargetLocationThreshold;
            const float PendingDelta = Memory.bHasPendingTarget
                ? CalculatePlanarDistance(DetectedTarget, Memory.PendingTargetLocation)
                : TNumericLimits<float>::Max();
            if (!Memory.bHasPendingTarget ||
                (EffectivePendingTargetStabilityThreshold > 0.f && PendingDelta > EffectivePendingTargetStabilityThreshold))
            {
                Memory.PendingTargetFirstDetectedTime = CurrentTime;
                Memory.bHasPendingTarget = true;
            }

            Memory.PendingTargetLocation = DetectedTarget;

            Memory.PendingConfidence = Confidence;
            Memory.PendingClassId = ClassId;
            Memory.PendingClassLabel = ClassLabel;
            OutPendingTargetAge = CurrentTime - Memory.PendingTargetFirstDetectedTime;

            if (NewTargetConfirmationSeconds <= 0.f ||
                OutPendingTargetAge >= NewTargetConfirmationSeconds)
            {
                Memory.LastTargetLocation = Memory.PendingTargetLocation;
                Memory.bHasLastTarget = true;
                Memory.bHasPendingTarget = false;
                bOutCommittedNewTarget = true;
                OutPendingTargetAge = 0.f;
            }
            else
            {
                bOutPendingTarget = true;
            }
        }

        Memory.LastDetectedTime = CurrentTime;
        if (!bOutPendingTarget)
        {
            Memory.LastConfidence = Confidence;
            Memory.LastClassId = ClassId;
            Memory.LastClassLabel = ClassLabel;
        }

        WriteTargetBlackboard(
            Blackboard,
            HasTargetKey,
            DetectedTargetLocationKey,
            DetectionConfidenceKey,
            DetectedClassIdKey,
            DetectedClassLabelKey,
            Memory.LastTargetLocation,
            Memory.LastConfidence,
            Memory.LastClassId,
            Memory.LastClassLabel);

        return bOutSameTarget;
    }

    bool ApplySharedDetection(
        UBlackboardComponent& Blackboard,
        const AActor& ControlledActor,
        const FBlackboardKeySelector& HasTargetKey,
        const FBlackboardKeySelector& DetectedTargetLocationKey,
        const FBlackboardKeySelector& DetectionConfidenceKey,
        const FBlackboardKeySelector& DetectedClassIdKey,
        const FBlackboardKeySelector& DetectedClassLabelKey,
        const TArray<int32>& ActionableClassIds,
        const TArray<FName>& ActionableClassLabels,
        FCrowTargetDetectionMemory& Memory,
        float CurrentTime,
        float MaxAgeSeconds,
        float MaxReporterDistance,
        float SameTargetLocationThreshold,
        float NewTargetConfirmationSeconds,
        float PendingTargetStabilityThreshold,
        bool bDrawDebug,
        bool bLogDebug)
    {
        UWorld* World = ControlledActor.GetWorld();
        UCrowDetectionShareSubsystem* ShareSubsystem = World ? World->GetSubsystem<UCrowDetectionShareSubsystem>() : nullptr;
        if (!ShareSubsystem)
        {
            return false;
        }

        FVector SharedTarget = FVector::ZeroVector;
        float SharedConfidence = 0.f;
        int32 SharedClassId = -1;
        FString SharedClassLabel;
        if (!ShareSubsystem->GetBestRecentTargetDetection(
            &ControlledActor,
            MaxAgeSeconds,
            MaxReporterDistance,
            ActionableClassIds,
            ActionableClassLabels,
            SharedTarget,
            SharedConfidence,
            SharedClassId,
            SharedClassLabel))
        {
            return false;
        }

        Memory.LastRawTargetLocation = SharedTarget;
        Memory.LastRawTargetTime = CurrentTime;
        Memory.TargetVelocity = FVector::ZeroVector;
        Memory.bHasRawTarget = true;

        bool bSameTarget = false;
        bool bPendingTarget = false;
        bool bCommittedNewTarget = false;
        float PendingAge = 0.f;
        float PlanarDelta = 0.f;
        ApplyLockedTargetMemory(
            Blackboard,
            HasTargetKey,
            DetectedTargetLocationKey,
            DetectionConfidenceKey,
            DetectedClassIdKey,
            DetectedClassLabelKey,
            Memory,
            SharedTarget,
            SharedConfidence,
            SharedClassId,
            SharedClassLabel,
            CurrentTime,
            SameTargetLocationThreshold,
            NewTargetConfirmationSeconds,
            PendingTargetStabilityThreshold,
            bSameTarget,
            bPendingTarget,
            bCommittedNewTarget,
            PendingAge,
            PlanarDelta);

        PrintCrowDetectionDebug(
            ControlledActor,
            FString::Printf(
                TEXT("shared %s target=%s detected=%s planarDelta=%.1f threshold=%.1f pendingAge=%.2f confirm=%.2f class=%s[%d] conf=%.2f"),
                bSameTarget ? TEXT("same") : (bPendingTarget ? TEXT("pending") : (bCommittedNewTarget ? TEXT("new") : TEXT("locked"))),
                *Memory.LastTargetLocation.ToCompactString(),
                *SharedTarget.ToCompactString(),
                PlanarDelta,
                SameTargetLocationThreshold,
                PendingAge,
                NewTargetConfirmationSeconds,
                *SharedClassLabel,
                SharedClassId,
                SharedConfidence),
            FColor::Blue,
            1,
            bDrawDebug,
            bLogDebug);

        return true;
    }
}

UBTServ_UpdateCrowTargetDetection::UBTServ_UpdateCrowTargetDetection()
{
    NodeName = TEXT("Update Crow YOLO Target Detection");
    Interval = 0.1f;
    RandomDeviation = 0.f;

    HasTargetKey.SelectedKeyName = TEXT("HasDetectedTarget");
    DetectedTargetLocationKey.SelectedKeyName = TEXT("DetectedTargetLocation");
    DetectionConfidenceKey.SelectedKeyName = TEXT("DetectionConfidence");
    DetectedClassIdKey.SelectedKeyName = TEXT("DetectedClassId");
    DetectedClassLabelKey.SelectedKeyName = TEXT("DetectedClassLabel");
}

uint16 UBTServ_UpdateCrowTargetDetection::GetInstanceMemorySize() const
{
    return sizeof(FCrowTargetDetectionMemory);
}

void UBTServ_UpdateCrowTargetDetection::TickNode(
    UBehaviorTreeComponent& OwnerComp,
    uint8* NodeMemory,
    float DeltaSeconds)
{
    Super::TickNode(OwnerComp, NodeMemory, DeltaSeconds);

    UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent();
    AAIController* AIController = OwnerComp.GetAIOwner();
    APawn* ControlledPawn = AIController ? AIController->GetPawn() : nullptr;
    if (!Blackboard || !ControlledPawn)
    {
        return;
    }

    FCrowTargetDetectionMemory* Memory = reinterpret_cast<FCrowTargetDetectionMemory*>(NodeMemory);
    const float CurrentTime = ControlledPawn->GetWorld() ? ControlledPawn->GetWorld()->GetTimeSeconds() : 0.f;

    UMyActorComponent* Detector = ControlledPawn->FindComponentByClass<UMyActorComponent>();
    UCameraComponent* Camera = ControlledPawn->FindComponentByClass<UCameraComponent>();
    const ABotCharacter* BotCharacter = Cast<ABotCharacter>(ControlledPawn);
    const bool bUseFlockSharedDetections = !BotCharacter || BotCharacter->ShouldUseFlockSharedDetections();
    const bool bPublishDetectionsToFlock = !BotCharacter || BotCharacter->ShouldPublishDetectionsToFlock();
    const float SharedDetectionMaxAgeSeconds = BotCharacter ? BotCharacter->GetSharedDetectionMaxAgeSeconds() : 1.5f;
    const float SharedDetectionMaxReporterDistance = BotCharacter ? BotCharacter->GetSharedDetectionMaxReporterDistance() : 6000.f;
    if (!Detector || !Camera || Detector->LastFrameSourceWidth <= 0 || Detector->LastFrameSourceHeight <= 0)
    {
        if (bUseFlockSharedDetections && ApplySharedDetection(
            *Blackboard,
            *ControlledPawn,
            HasTargetKey,
            DetectedTargetLocationKey,
            DetectionConfidenceKey,
            DetectedClassIdKey,
            DetectedClassLabelKey,
            ActionableClassIds,
            ActionableClassLabels,
            *Memory,
            CurrentTime,
            SharedDetectionMaxAgeSeconds,
            SharedDetectionMaxReporterDistance,
            SameTargetLocationThreshold,
            NewTargetConfirmationSeconds,
            PendingTargetStabilityThreshold,
            bDrawDebug,
            bLogDebug))
        {
            return;
        }

        KeepMemoryActive(
            *Blackboard,
            HasTargetKey,
            DetectedTargetLocationKey,
            DetectionConfidenceKey,
            DetectedClassIdKey,
            DetectedClassLabelKey,
            *Memory,
            CurrentTime,
            LoseTargetAfterSeconds);
        PrintCrowDetectionDebug(
            *ControlledPawn,
            FString::Printf(
                TEXT("unavailable detector=%s camera=%s source=%dx%d"),
                Detector ? TEXT("yes") : TEXT("no"),
                Camera ? TEXT("yes") : TEXT("no"),
                Detector ? Detector->LastFrameSourceWidth : 0,
                Detector ? Detector->LastFrameSourceHeight : 0),
            FColor::Yellow,
            3,
            bDrawDebug,
            bLogDebug);
        return;
    }

    const bool bHasFreshFrame = Detector->LastFrameSequence > 0
        && (CurrentTime - Detector->LastFrameTimeSeconds) <= MaxDetectionFrameAgeSeconds;
    if (!bHasFreshFrame)
    {
        if (bUseFlockSharedDetections && ApplySharedDetection(
            *Blackboard,
            *ControlledPawn,
            HasTargetKey,
            DetectedTargetLocationKey,
            DetectionConfidenceKey,
            DetectedClassIdKey,
            DetectedClassLabelKey,
            ActionableClassIds,
            ActionableClassLabels,
            *Memory,
            CurrentTime,
            SharedDetectionMaxAgeSeconds,
            SharedDetectionMaxReporterDistance,
            SameTargetLocationThreshold,
            NewTargetConfirmationSeconds,
            PendingTargetStabilityThreshold,
            bDrawDebug,
            bLogDebug))
        {
            return;
        }

        KeepMemoryActive(
            *Blackboard,
            HasTargetKey,
            DetectedTargetLocationKey,
            DetectionConfidenceKey,
            DetectedClassIdKey,
            DetectedClassLabelKey,
            *Memory,
            CurrentTime,
            LoseTargetAfterSeconds);
        PrintCrowDetectionDebug(
            *ControlledPawn,
            FString::Printf(
                TEXT("stale frame seq=%d age=%.2f memory=%s"),
                Detector->LastFrameSequence,
                CurrentTime - Detector->LastFrameTimeSeconds,
                Memory->bHasLastTarget ? TEXT("active") : TEXT("none")),
            FColor::Yellow,
            6,
            bDrawDebug,
            bLogDebug);
        return;
    }

    if (Detector->LastFrameSequence == Memory->LastProcessedFrameSequence)
    {
        KeepMemoryActive(
            *Blackboard,
            HasTargetKey,
            DetectedTargetLocationKey,
            DetectionConfidenceKey,
            DetectedClassIdKey,
            DetectedClassLabelKey,
            *Memory,
            CurrentTime,
            LoseTargetAfterSeconds);
        return;
    }
    Memory->LastProcessedFrameSequence = Detector->LastFrameSequence;

    if (Memory->bHasLastTarget &&
        !HasTargetKey.SelectedKeyName.IsNone() &&
        !Blackboard->GetValueAsBool(HasTargetKey.SelectedKeyName))
    {
        const int32 ProcessingFrameSequence = Memory->LastProcessedFrameSequence;
        ClearTargetDetectionMemory(
            *Blackboard,
            HasTargetKey,
            DetectedTargetLocationKey,
            DetectionConfidenceKey,
            DetectedClassIdKey,
            DetectedClassLabelKey,
            *Memory);
        Memory->LastProcessedFrameSequence = ProcessingFrameSequence;
    }

    FDetectionBox TargetBox;
    bool bMatchedTrackedBox = false;
    float TrackMatchIoU = 0.f;
    float TrackMatchDistance = 0.f;
    if (!FindTrackedTargetDetection(
        *Detector,
        *Memory,
        ActionableClassIds,
        ActionableClassLabels,
        MinAcceptedConfidence,
        bEnableYoloBoxTracking,
        TrackMatchMinIoU,
        TrackMatchMaxCenterDistance,
        TrackSwitchConfidenceMargin,
        TargetBox,
        bMatchedTrackedBox,
        TrackMatchIoU,
        TrackMatchDistance))
    {
        Memory->ConsecutiveDetections = 0;
        ++Memory->ConsecutiveMisses;

        bool bUsedSharedFallback = false;
        if (bUseFlockSharedDetections)
        {
            bUsedSharedFallback = ApplySharedDetection(
                *Blackboard,
                *ControlledPawn,
                HasTargetKey,
                DetectedTargetLocationKey,
                DetectionConfidenceKey,
                DetectedClassIdKey,
                DetectedClassLabelKey,
                ActionableClassIds,
                ActionableClassLabels,
                *Memory,
                CurrentTime,
                SharedDetectionMaxAgeSeconds,
                SharedDetectionMaxReporterDistance,
                SameTargetLocationThreshold,
                NewTargetConfirmationSeconds,
                PendingTargetStabilityThreshold,
                bDrawDebug,
                bLogDebug);
        }

        const FSceneDepthResolveStats EmptyDepthStats;
        AppendDepthDetectionMetricRow(
            *ControlledPawn,
            *Detector,
            TargetLocationOffset,
            bUsedSharedFallback ? TEXT("no_accepted_box_shared") : TEXT("no_accepted_box"),
            false,
            bUsedSharedFallback,
            nullptr,
            nullptr,
            nullptr,
            TargetRayBoxVerticalBias,
            0.f,
            0,
            0,
            FMath::Max(1, MinSceneDepthClusterSamples),
            FMath::Clamp(MinSceneDepthClusterSampleRatio, 0.f, 1.f),
            EmptyDepthStats,
            nullptr,
            nullptr,
            false,
            0.f,
            0.f,
            0.f,
            false,
            Memory->ConsecutiveDetections,
            Memory->ConsecutiveMisses);

        if (bUsedSharedFallback)
        {
            return;
        }

        KeepMemoryActive(
            *Blackboard,
            HasTargetKey,
            DetectedTargetLocationKey,
            DetectionConfidenceKey,
            DetectedClassIdKey,
            DetectedClassLabelKey,
            *Memory,
            CurrentTime,
            LoseTargetAfterSeconds);
        PrintCrowDetectionDebug(
            *ControlledPawn,
            FString::Printf(
                TEXT("detections=%d no accepted target source=%dx%d seq=%d misses=%d memory=%s"),
                Detector->LastFrameDetections.Num(),
                Detector->LastFrameSourceWidth,
                Detector->LastFrameSourceHeight,
                Detector->LastFrameSequence,
                Memory->ConsecutiveMisses,
                Memory->bHasLastTarget ? TEXT("active") : TEXT("none")),
            FColor::Orange,
            4,
            bDrawDebug,
            bLogDebug);
        return;
    }

    if (bEnableYoloBoxTracking && Memory->bHasTrackedBox && bMatchedTrackedBox)
    {
        TargetBox = SmoothBox(Memory->LastTrackedBox, TargetBox, DeltaSeconds, TrackBoxSmoothingSpeed);
    }
    Memory->LastTrackedBox = TargetBox;
    Memory->bHasTrackedBox = true;

    const FVector RayOrigin = Camera->GetComponentLocation();
    const FVector2D BoxMin = GetBoxMin(TargetBox);
    const FVector2D BoxMax = GetBoxMax(TargetBox);
    const FVector2D TargetPixel(
        FMath::Clamp(TargetBox.Center.X, 0.f, FMath::Max(static_cast<float>(Detector->LastFrameSourceWidth - 1), 0.f)),
        FMath::Clamp(
            TargetBox.Center.Y + (TargetBox.Size.Y * TargetRayBoxVerticalBias),
            0.f,
            FMath::Max(static_cast<float>(Detector->LastFrameSourceHeight - 1), 0.f)));
    const FVector DetectionBoxRayDirection = BuildCameraRayDirection(
        *Camera,
        TargetPixel,
        Detector->LastFrameSourceWidth,
        Detector->LastFrameSourceHeight);
    FVector RayDirection = DetectionBoxRayDirection;

    FVector TargetLocation = FVector::ZeroVector;
    float SceneDepth = 0.f;
    FVector2D ResolvedDepthPixel = FVector2D::ZeroVector;
    int32 DepthSampleCount = 0;
    int32 DepthClusterSampleCount = 0;
    FSceneDepthResolveStats DepthResolveStats;
    const bool bHasActiveLockedTarget = Memory->bHasLastTarget &&
        !HasTargetKey.SelectedKeyName.IsNone() &&
        Blackboard->GetValueAsBool(HasTargetKey.SelectedKeyName);
    const bool bHasRecentRawTarget = Memory->bHasRawTarget &&
        CurrentTime - Memory->LastRawTargetTime <= LoseTargetAfterSeconds;
    const bool bHasDepthReferenceTarget = bHasActiveLockedTarget || bHasRecentRawTarget;
    const FVector DepthReferenceTargetLocation = bHasActiveLockedTarget
        ? Memory->LastTargetLocation
        : Memory->LastRawTargetLocation;
    const int32 RequiredDepthClusterSamples = FMath::Max(1, MinSceneDepthClusterSamples);
    const float RequiredDepthClusterRatio = FMath::Clamp(MinSceneDepthClusterSampleRatio, 0.f, 1.f);

    if (!TryResolveTargetFromSceneDepth(
        *Detector,
        *Camera,
        RayOrigin,
        BoxMin,
        BoxMax,
        TargetPixel,
        TraceDistance,
        bHasDepthReferenceTarget,
        DepthReferenceTargetLocation,
        FMath::Max(MaxTrackedTargetJumpDistance, SameTargetLocationThreshold) * 2.f,
        TargetLocation,
        SceneDepth,
        ResolvedDepthPixel,
        DepthSampleCount,
        DepthClusterSampleCount,
        DepthResolveStats))
    {
        Memory->ConsecutiveDetections = 0;
        ++Memory->ConsecutiveMisses;

        bool bUsedSharedFallback = false;
        if (bUseFlockSharedDetections)
        {
            bUsedSharedFallback = ApplySharedDetection(
                *Blackboard,
                *ControlledPawn,
                HasTargetKey,
                DetectedTargetLocationKey,
                DetectionConfidenceKey,
                DetectedClassIdKey,
                DetectedClassLabelKey,
                ActionableClassIds,
                ActionableClassLabels,
                *Memory,
                CurrentTime,
                SharedDetectionMaxAgeSeconds,
                SharedDetectionMaxReporterDistance,
                SameTargetLocationThreshold,
                NewTargetConfirmationSeconds,
                PendingTargetStabilityThreshold,
                bDrawDebug,
                bLogDebug);
        }

        AppendDepthDetectionMetricRow(
            *ControlledPawn,
            *Detector,
            TargetLocationOffset,
            bUsedSharedFallback ? TEXT("depth_miss_shared") : TEXT("depth_miss"),
            false,
            bUsedSharedFallback,
            &TargetBox,
            &TargetPixel,
            nullptr,
            TargetRayBoxVerticalBias,
            SceneDepth,
            DepthSampleCount,
            DepthClusterSampleCount,
            RequiredDepthClusterSamples,
            RequiredDepthClusterRatio,
            DepthResolveStats,
            nullptr,
            nullptr,
            bMatchedTrackedBox,
            TrackMatchIoU,
            TrackMatchDistance,
            0.f,
            false,
            Memory->ConsecutiveDetections,
            Memory->ConsecutiveMisses);

        if (bUsedSharedFallback)
        {
            return;
        }

        KeepMemoryActive(
            *Blackboard,
            HasTargetKey,
            DetectedTargetLocationKey,
            DetectionConfidenceKey,
            DetectedClassIdKey,
            DetectedClassLabelKey,
            *Memory,
            CurrentTime,
            LoseTargetAfterSeconds);
        PrintCrowDetectionDebug(
            *ControlledPawn,
            FString::Printf(
                TEXT("scene depth miss targetPixel=%s boxMin=%s boxMax=%s ray=%s depthSize=%dx%d depthPixels=%d tested=%d valid=%d tooNear=%d tooFar=%d nonFinite=%d depthMin=%.1f depthMax=%.1f depthAvg=%.1f minPixel=%s maxPixel=%s samples=%d cluster=%d clusterScore=%.2f clusterPixelDist=%.1f clusterRefDist=%.1f misses=%d"),
                *TargetPixel.ToString(),
                *BoxMin.ToString(),
                *BoxMax.ToString(),
                *RayDirection.ToCompactString(),
                Detector->LastFrameSceneDepthWidth,
                Detector->LastFrameSceneDepthHeight,
                Detector->LastFrameSceneDepth.Num(),
                DepthResolveStats.Tested,
                DepthResolveStats.Valid,
                DepthResolveStats.TooNear,
                DepthResolveStats.TooFar,
                DepthResolveStats.NonFinite,
                DepthResolveStats.MinDepth == FLT_MAX ? 0.f : DepthResolveStats.MinDepth,
                DepthResolveStats.MaxDepth == -FLT_MAX ? 0.f : DepthResolveStats.MaxDepth,
                DepthResolveStats.GetAverageDepth(),
                *DepthResolveStats.MinDepthPixel.ToString(),
                *DepthResolveStats.MaxDepthPixel.ToString(),
                DepthSampleCount,
                DepthClusterSampleCount,
                DepthResolveStats.ClusterScore,
                DepthResolveStats.ClusterPixelDistance,
                DepthResolveStats.ClusterReferenceDistance,
                Memory->ConsecutiveMisses),
            FColor::Orange,
            9,
            bDrawDebug,
            bLogDebug);
        return;
    }

    const float DepthClusterSampleRatio = DepthSampleCount > 0
        ? static_cast<float>(DepthClusterSampleCount) / static_cast<float>(DepthSampleCount)
        : 0.f;
    if (DepthClusterSampleCount < RequiredDepthClusterSamples ||
        DepthClusterSampleRatio < RequiredDepthClusterRatio)
    {
        Memory->ConsecutiveDetections = 0;
        ++Memory->ConsecutiveMisses;
        const FVector MetricTargetLocation = TargetLocation + TargetLocationOffset;
        AppendDepthDetectionMetricRow(
            *ControlledPawn,
            *Detector,
            TargetLocationOffset,
            TEXT("weak_cluster"),
            false,
            false,
            &TargetBox,
            &TargetPixel,
            &ResolvedDepthPixel,
            TargetRayBoxVerticalBias,
            SceneDepth,
            DepthSampleCount,
            DepthClusterSampleCount,
            RequiredDepthClusterSamples,
            RequiredDepthClusterRatio,
            DepthResolveStats,
            &MetricTargetLocation,
            nullptr,
            bMatchedTrackedBox,
            TrackMatchIoU,
            TrackMatchDistance,
            0.f,
            false,
            Memory->ConsecutiveDetections,
            Memory->ConsecutiveMisses);

        KeepMemoryActive(
            *Blackboard,
            HasTargetKey,
            DetectedTargetLocationKey,
            DetectionConfidenceKey,
            DetectedClassIdKey,
            DetectedClassLabelKey,
            *Memory,
            CurrentTime,
            LoseTargetAfterSeconds);
        PrintCrowDetectionDebug(
            *ControlledPawn,
            FString::Printf(
                TEXT("scene depth weak cluster targetPixel=%s depthPixel=%s boxMin=%s boxMax=%s depth=%.1f samples=%d cluster=%d minCluster=%d ratio=%.3f minRatio=%.3f clusterScore=%.2f clusterPixelDist=%.1f clusterRefDist=%.1f misses=%d seq=%d"),
                *TargetPixel.ToString(),
                *ResolvedDepthPixel.ToString(),
                *BoxMin.ToString(),
                *BoxMax.ToString(),
                SceneDepth,
                DepthSampleCount,
                DepthClusterSampleCount,
                RequiredDepthClusterSamples,
                DepthClusterSampleRatio,
                RequiredDepthClusterRatio,
                DepthResolveStats.ClusterScore,
                DepthResolveStats.ClusterPixelDistance,
                DepthResolveStats.ClusterReferenceDistance,
                Memory->ConsecutiveMisses,
                Detector->LastFrameSequence),
            FColor::Orange,
            9,
            bDrawDebug,
            bLogDebug);
        return;
    }

    TargetLocation += TargetLocationOffset;

    if (bRequirePlayerResolvedTarget)
    {
        const FResolvedActorMetric TargetActorMetric = FindResolvedActorMetric(
            *ControlledPawn,
            &TargetLocation,
            PlayerTargetMatchRadius);
        if (!TargetActorMetric.bMatchesResolvedTarget || !TargetActorMetric.bIsPlayer)
        {
            Memory->ConsecutiveDetections = 0;
            ++Memory->ConsecutiveMisses;
            AppendDepthDetectionMetricRow(
                *ControlledPawn,
                *Detector,
                TargetLocationOffset,
                TargetActorMetric.ActorName.IsEmpty() ? TEXT("unresolved_target") : TEXT("non_player_target"),
                false,
                false,
                &TargetBox,
                &TargetPixel,
                &ResolvedDepthPixel,
                TargetRayBoxVerticalBias,
                SceneDepth,
                DepthSampleCount,
                DepthClusterSampleCount,
                RequiredDepthClusterSamples,
                RequiredDepthClusterRatio,
                DepthResolveStats,
                &TargetLocation,
                nullptr,
                bMatchedTrackedBox,
                TrackMatchIoU,
                TrackMatchDistance,
                0.f,
                false,
                Memory->ConsecutiveDetections,
                Memory->ConsecutiveMisses);

            KeepMemoryActive(
                *Blackboard,
                HasTargetKey,
                DetectedTargetLocationKey,
                DetectionConfidenceKey,
                DetectedClassIdKey,
                DetectedClassLabelKey,
                *Memory,
                CurrentTime,
                LoseTargetAfterSeconds);
            PrintCrowDetectionDebug(
                *ControlledPawn,
                FString::Printf(
                    TEXT("reject target actor=%s class=%s isPlayer=%s match=%s dist=%.1f radius=%.1f target=%s detectionClass=%s[%d] conf=%.2f seq=%d misses=%d"),
                    TargetActorMetric.ActorName.IsEmpty() ? TEXT("None") : *TargetActorMetric.ActorName,
                    TargetActorMetric.ActorClassName.IsEmpty() ? TEXT("None") : *TargetActorMetric.ActorClassName,
                    TargetActorMetric.bIsPlayer ? TEXT("true") : TEXT("false"),
                    TargetActorMetric.bMatchesResolvedTarget ? TEXT("true") : TEXT("false"),
                    TargetActorMetric.Distance,
                    PlayerTargetMatchRadius,
                    *TargetLocation.ToCompactString(),
                    *TargetBox.ClassLabel,
                    TargetBox.ClassId,
                    TargetBox.Confidence,
                    Detector->LastFrameSequence,
                    Memory->ConsecutiveMisses),
                FColor::Red,
                10,
                bDrawDebug,
                bLogDebug);
            return;
        }
    }

    bool bClampedTargetJump = false;
    float RawTargetJumpDistance = 0.f;
    if (MaxTrackedTargetJumpDistance > 0.f &&
        Memory->bHasRawTarget &&
        CurrentTime - Memory->LastRawTargetTime <= LoseTargetAfterSeconds)
    {
        RawTargetJumpDistance = FVector::Dist(TargetLocation, Memory->LastRawTargetLocation);
        if (RawTargetJumpDistance > MaxTrackedTargetJumpDistance)
        {
            const FVector ClampedOffset = (TargetLocation - Memory->LastRawTargetLocation).GetClampedToMaxSize(MaxTrackedTargetJumpDistance);
            TargetLocation = Memory->LastRawTargetLocation + ClampedOffset;
            Memory->TargetVelocity = FVector::ZeroVector;
            bClampedTargetJump = true;
        }
    }

    FVector PredictedTargetLocation = TargetLocation;
    if (bPredictTrackedTargetMotion && Memory->bHasRawTarget && bMatchedTrackedBox && !bClampedTargetJump)
    {
        const float TimeSinceLastRawTarget = CurrentTime - Memory->LastRawTargetTime;
        if (TimeSinceLastRawTarget > KINDA_SMALL_NUMBER && TimeSinceLastRawTarget < LoseTargetAfterSeconds)
        {
            FVector InstantVelocity = (TargetLocation - Memory->LastRawTargetLocation) / TimeSinceLastRawTarget;
            if (TargetVelocitySmoothingSpeed > 0.f)
            {
                InstantVelocity = FMath::VInterpTo(
                    Memory->TargetVelocity,
                    InstantVelocity,
                    DeltaSeconds,
                    TargetVelocitySmoothingSpeed);
            }

            Memory->TargetVelocity = InstantVelocity;
            FVector PredictionOffset = Memory->TargetVelocity * TargetPredictionLeadSeconds;
            if (MaxTargetPredictionDistance > 0.f && PredictionOffset.SizeSquared() > FMath::Square(MaxTargetPredictionDistance))
            {
                PredictionOffset = PredictionOffset.GetClampedToMaxSize(MaxTargetPredictionDistance);
            }

            PredictedTargetLocation = TargetLocation + PredictionOffset;
        }
    }

    Memory->LastRawTargetLocation = TargetLocation;
    Memory->LastRawTargetTime = CurrentTime;
    Memory->bHasRawTarget = true;

    ++Memory->ConsecutiveDetections;
    Memory->ConsecutiveMisses = 0;

    const bool bConfirmed = Memory->ConsecutiveDetections >= FMath::Max(1, RequiredConsecutiveDetections);
    if (!bConfirmed)
    {
        AppendDepthDetectionMetricRow(
            *ControlledPawn,
            *Detector,
            TargetLocationOffset,
            TEXT("candidate"),
            true,
            false,
            &TargetBox,
            &TargetPixel,
            &ResolvedDepthPixel,
            TargetRayBoxVerticalBias,
            SceneDepth,
            DepthSampleCount,
            DepthClusterSampleCount,
            RequiredDepthClusterSamples,
            RequiredDepthClusterRatio,
            DepthResolveStats,
            &TargetLocation,
            &PredictedTargetLocation,
            bMatchedTrackedBox,
            TrackMatchIoU,
            TrackMatchDistance,
            RawTargetJumpDistance,
            bClampedTargetJump,
            Memory->ConsecutiveDetections,
            Memory->ConsecutiveMisses);

        KeepMemoryActive(
            *Blackboard,
            HasTargetKey,
            DetectedTargetLocationKey,
            DetectionConfidenceKey,
            DetectedClassIdKey,
            DetectedClassLabelKey,
            *Memory,
            CurrentTime,
            LoseTargetAfterSeconds);
        PrintCrowDetectionDebug(
            *ControlledPawn,
            FString::Printf(
                TEXT("candidate target=%s class=%s[%d] depth=%.1f conf=%.2f track=%s jump=%.1f clamped=%s pixel=%s depthPixel=%s samples=%d/%d clusterScore=%.2f clusterPixelDist=%.1f clusterRefDist=%.1f boxMin=%s boxMax=%s hits=%d/%d seq=%d"),
                *PredictedTargetLocation.ToCompactString(),
                *TargetBox.ClassLabel,
                TargetBox.ClassId,
                SceneDepth,
                TargetBox.Confidence,
                bMatchedTrackedBox ? TEXT("matched") : TEXT("new"),
                RawTargetJumpDistance,
                bClampedTargetJump ? TEXT("yes") : TEXT("no"),
                *TargetPixel.ToString(),
                *ResolvedDepthPixel.ToString(),
                DepthClusterSampleCount,
                DepthSampleCount,
                DepthResolveStats.ClusterScore,
                DepthResolveStats.ClusterPixelDistance,
                DepthResolveStats.ClusterReferenceDistance,
                *BoxMin.ToString(),
                *BoxMax.ToString(),
                Memory->ConsecutiveDetections,
                FMath::Max(1, RequiredConsecutiveDetections),
                Detector->LastFrameSequence),
            FColor::Yellow,
            5,
            bDrawDebug,
            bLogDebug);
        return;
    }

    bool bSameTargetRedetected = false;
    bool bPendingTarget = false;
    bool bCommittedNewTarget = false;
    float PendingTargetAge = 0.f;
    float TargetLocationDelta = 0.f;
    ApplyLockedTargetMemory(
        *Blackboard,
        HasTargetKey,
        DetectedTargetLocationKey,
        DetectionConfidenceKey,
        DetectedClassIdKey,
        DetectedClassLabelKey,
        *Memory,
        PredictedTargetLocation,
        TargetBox.Confidence,
        TargetBox.ClassId,
        TargetBox.ClassLabel,
        CurrentTime,
        SameTargetLocationThreshold,
        NewTargetConfirmationSeconds,
        PendingTargetStabilityThreshold,
        bSameTargetRedetected,
        bPendingTarget,
        bCommittedNewTarget,
        PendingTargetAge,
        TargetLocationDelta);

    const FVector MemorizedTargetLocation = Memory->LastTargetLocation;
    AppendDepthDetectionMetricRow(
        *ControlledPawn,
        *Detector,
        TargetLocationOffset,
        bSameTargetRedetected ? TEXT("same") : (bPendingTarget ? TEXT("pending") : (bCommittedNewTarget ? TEXT("new") : TEXT("locked"))),
        true,
        false,
        &TargetBox,
        &TargetPixel,
        &ResolvedDepthPixel,
        TargetRayBoxVerticalBias,
        SceneDepth,
        DepthSampleCount,
        DepthClusterSampleCount,
        RequiredDepthClusterSamples,
        RequiredDepthClusterRatio,
        DepthResolveStats,
        &TargetLocation,
        &PredictedTargetLocation,
        bMatchedTrackedBox,
        TrackMatchIoU,
        TrackMatchDistance,
        RawTargetJumpDistance,
        bClampedTargetJump,
        Memory->ConsecutiveDetections,
        Memory->ConsecutiveMisses);

    if (bPublishDetectionsToFlock)
    {
        if (UWorld* World = ControlledPawn->GetWorld())
        {
            if (UCrowDetectionShareSubsystem* ShareSubsystem = World->GetSubsystem<UCrowDetectionShareSubsystem>())
            {
                ShareSubsystem->PublishTargetDetection(
                    ControlledPawn,
                    MemorizedTargetLocation,
                    TargetBox.Confidence,
                    TargetBox.ClassId,
                    TargetBox.ClassLabel);
            }
        }
    }

    PrintCrowDetectionDebug(
        *ControlledPawn,
        FString::Printf(
            TEXT("%s target=%s detected=%s planarDelta=%.1f threshold=%.1f pendingAge=%.2f confirm=%.2f class=%s[%d] source=scene-depth depth=%.1f depthPixel=%s samples=%d/%d clusterScore=%.2f clusterPixelDist=%.1f clusterRefDist=%.1f track=%s iou=%.2f centerDist=%.2f jump=%.1f clamped=%s velocity=%s predicted=%s conf=%.2f pixel=%s box=%s size=%s detections=%d seq=%d hits=%d"),
            bSameTargetRedetected ? TEXT("same") : (bPendingTarget ? TEXT("pending") : (bCommittedNewTarget ? TEXT("new") : TEXT("locked"))),
            *MemorizedTargetLocation.ToCompactString(),
            *PredictedTargetLocation.ToCompactString(),
            TargetLocationDelta,
            SameTargetLocationThreshold,
            PendingTargetAge,
            NewTargetConfirmationSeconds,
            *TargetBox.ClassLabel,
            TargetBox.ClassId,
            SceneDepth,
            *ResolvedDepthPixel.ToString(),
            DepthClusterSampleCount,
            DepthSampleCount,
            DepthResolveStats.ClusterScore,
            DepthResolveStats.ClusterPixelDistance,
            DepthResolveStats.ClusterReferenceDistance,
            bMatchedTrackedBox ? TEXT("matched") : TEXT("new"),
            TrackMatchIoU,
            TrackMatchDistance,
            RawTargetJumpDistance,
            bClampedTargetJump ? TEXT("yes") : TEXT("no"),
            *Memory->TargetVelocity.ToCompactString(),
            *PredictedTargetLocation.ToCompactString(),
            TargetBox.Confidence,
            *TargetPixel.ToString(),
            *TargetBox.Center.ToString(),
            *TargetBox.Size.ToString(),
            Detector->LastFrameDetections.Num(),
            Detector->LastFrameSequence,
            Memory->ConsecutiveDetections),
        FColor::Green,
        5,
        bDrawDebug,
        bLogDebug);

    if (bDrawDebug && IsDetectionDebugEnabled())
    {
        if (UWorld* World = ControlledPawn->GetWorld())
        {
            DrawDebugSphere(World, MemorizedTargetLocation, 80.f, 12, FColor::Green, false, 0.15f);
            DrawDebugLine(World, RayOrigin, MemorizedTargetLocation, FColor::Green, false, 0.15f, 0, 2.f);
        }
    }
}
