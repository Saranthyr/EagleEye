#include "AI/BTServ_UpdateCrowPersonDetection.h"

#include "AIController.h"
#include "AI/CrowDetectionShareSubsystem.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Camera/CameraComponent.h"
#include "DetectionResult.h"
#include "DrawDebugHelpers.h"
#include "EagleEyeDetectionSettings.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
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

    struct FCrowPersonDetectionMemory
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

    bool FindTrackedPersonDetection(
        const UMyActorComponent& Detector,
        const FCrowPersonDetectionMemory& Memory,
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
        double DepthSum = 0.0;
        FVector2D MinDepthPixel = FVector2D::ZeroVector;
        FVector2D MaxDepthPixel = FVector2D::ZeroVector;

        float GetAverageDepth() const
        {
            return Tested > NonFinite ? static_cast<float>(DepthSum / static_cast<double>(Tested - NonFinite)) : 0.f;
        }
    };

    bool TryResolveTargetFromSceneDepth(
        const UMyActorComponent& Detector,
        const UCameraComponent& Camera,
        const FVector& RayOrigin,
        const FVector2D& BoxMin,
        const FVector2D& BoxMax,
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

        int32 ReferenceIndex = FMath::Clamp(FMath::FloorToInt((Samples.Num() - 1) * 0.1f), 0, Samples.Num() - 1);
        if (bHasReferenceTarget && MaxReferenceTargetDistance > 0.f)
        {
            const float MaxReferenceDistanceSquared = FMath::Square(MaxReferenceTargetDistance);
            float BestReferenceDistanceSquared = MaxReferenceDistanceSquared;
            int32 BestReferenceIndex = INDEX_NONE;
            const FVector CameraForward = Camera.GetComponentTransform().GetUnitAxis(EAxis::X);
            for (int32 SampleIndex = 0; SampleIndex < Samples.Num(); ++SampleIndex)
            {
                const FDepthSample& Sample = Samples[SampleIndex];
                const FVector SampleRayDirection = BuildCameraRayDirection(
                    Camera,
                    Sample.Pixel,
                    DepthWidth,
                    DepthHeight);
                const float ForwardDot = FVector::DotProduct(SampleRayDirection, CameraForward);
                if (ForwardDot <= KINDA_SMALL_NUMBER)
                {
                    continue;
                }

                const float RayDistance = Sample.Depth / ForwardDot;
                if (!FMath::IsFinite(RayDistance) || RayDistance <= 0.f || RayDistance > EffectiveMaxSceneDepth)
                {
                    continue;
                }

                const FVector SampleLocation = RayOrigin + (SampleRayDirection * RayDistance);
                const float ReferenceDistanceSquared = FVector::DistSquared2D(SampleLocation, ReferenceTargetLocation);
                if (ReferenceDistanceSquared < BestReferenceDistanceSquared)
                {
                    BestReferenceDistanceSquared = ReferenceDistanceSquared;
                    BestReferenceIndex = SampleIndex;
                }
            }

            if (BestReferenceIndex != INDEX_NONE)
            {
                ReferenceIndex = BestReferenceIndex;
            }
        }

        const float ReferenceDepth = Samples[ReferenceIndex].Depth;
        const float ClusterTolerance = FMath::Clamp(ReferenceDepth * 0.08f, 25.f, 250.f);
        double DepthSum = 0.0;
        FVector2D PixelSum = FVector2D::ZeroVector;
        for (const FDepthSample& Sample : Samples)
        {
            if (FMath::Abs(Sample.Depth - ReferenceDepth) > ClusterTolerance)
            {
                continue;
            }

            DepthSum += Sample.Depth;
            PixelSum += Sample.Pixel;
            ++OutClusterSampleCount;
        }

        if (OutClusterSampleCount <= 0)
        {
            return false;
        }

        OutSceneDepth = static_cast<float>(DepthSum / OutClusterSampleCount);
        OutDepthPixel = PixelSum / static_cast<float>(OutClusterSampleCount);
        const FVector RayDirection = BuildCameraRayDirection(
            Camera,
            OutDepthPixel,
            DepthWidth,
            DepthHeight);

        const FVector CameraForward = Camera.GetComponentTransform().GetUnitAxis(EAxis::X);
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

    APawn* GetPlayerPawn(const UObject* WorldContext)
    {
        APlayerController* PlayerController = UGameplayStatics::GetPlayerController(WorldContext, 0);
        return PlayerController ? PlayerController->GetPawn() : nullptr;
    }

    uint64 MakeCrowDebugKey(const AActor& ControlledActor, uint32 Slot)
    {
        return (static_cast<uint64>(ControlledActor.GetUniqueID()) << 8) | Slot;
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

        if (bDrawDebug && GEngine)
        {
            GEngine->AddOnScreenDebugMessage(
                MakeCrowDebugKey(ControlledActor, Slot),
                Duration,
                Color,
                FullMessage);
        }

        const UEagleEyeDetectionSettings* Settings = GetDefault<UEagleEyeDetectionSettings>();
        if (bLogDebug || (Settings && Settings->bEnableDetectionDebugLogs))
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
        const FBlackboardKeySelector& HasPersonKey,
        const FBlackboardKeySelector& DetectedPersonLocationKey,
        const FBlackboardKeySelector& DetectionConfidenceKey,
        const FBlackboardKeySelector& DetectedClassIdKey,
        const FBlackboardKeySelector& DetectedClassLabelKey,
        const FVector& TargetLocation,
        float Confidence,
        int32 ClassId,
        const FString& ClassLabel)
    {
        Blackboard.SetValueAsBool(HasPersonKey.SelectedKeyName, true);
        Blackboard.SetValueAsVector(DetectedPersonLocationKey.SelectedKeyName, TargetLocation);
        Blackboard.SetValueAsFloat(DetectionConfidenceKey.SelectedKeyName, Confidence);
        WriteDetectedClassBlackboard(Blackboard, DetectedClassIdKey, DetectedClassLabelKey, ClassId, ClassLabel);
    }

    float CalculatePlanarDistance(const FVector& A, const FVector& B)
    {
        FVector Offset = A - B;
        Offset.Z = 0.f;
        return Offset.Size();
    }

    void ClearPersonDetectionMemory(
        UBlackboardComponent& Blackboard,
        const FBlackboardKeySelector& HasPersonKey,
        const FBlackboardKeySelector& DetectedPersonLocationKey,
        const FBlackboardKeySelector& DetectionConfidenceKey,
        const FBlackboardKeySelector& DetectedClassIdKey,
        const FBlackboardKeySelector& DetectedClassLabelKey,
        FCrowPersonDetectionMemory& Memory)
    {
        Memory = FCrowPersonDetectionMemory();

        Blackboard.SetValueAsBool(HasPersonKey.SelectedKeyName, false);
        if (!DetectedPersonLocationKey.SelectedKeyName.IsNone())
        {
            Blackboard.ClearValue(DetectedPersonLocationKey.SelectedKeyName);
        }
        if (!DetectionConfidenceKey.SelectedKeyName.IsNone())
        {
            Blackboard.ClearValue(DetectionConfidenceKey.SelectedKeyName);
        }
        WriteDetectedClassBlackboard(Blackboard, DetectedClassIdKey, DetectedClassLabelKey, -1, FString());
    }

    bool KeepMemoryActive(
        UBlackboardComponent& Blackboard,
        const FBlackboardKeySelector& HasPersonKey,
        const FBlackboardKeySelector& DetectedPersonLocationKey,
        const FBlackboardKeySelector& DetectionConfidenceKey,
        const FBlackboardKeySelector& DetectedClassIdKey,
        const FBlackboardKeySelector& DetectedClassLabelKey,
        FCrowPersonDetectionMemory& Memory,
        float CurrentTime,
        float LosePersonAfterSeconds)
    {
        const bool bActionCompleted = Memory.bHasLastTarget &&
            !HasPersonKey.SelectedKeyName.IsNone() &&
            !Blackboard.GetValueAsBool(HasPersonKey.SelectedKeyName);
        if (bActionCompleted)
        {
            ClearPersonDetectionMemory(
                Blackboard,
                HasPersonKey,
                DetectedPersonLocationKey,
                DetectionConfidenceKey,
                DetectedClassIdKey,
                DetectedClassLabelKey,
                Memory);
            return false;
        }

        if (!Memory.bHasLastTarget)
        {
            Blackboard.SetValueAsBool(HasPersonKey.SelectedKeyName, false);
            if (!DetectedPersonLocationKey.SelectedKeyName.IsNone())
            {
                Blackboard.ClearValue(DetectedPersonLocationKey.SelectedKeyName);
            }
            if (!DetectionConfidenceKey.SelectedKeyName.IsNone())
            {
                Blackboard.ClearValue(DetectionConfidenceKey.SelectedKeyName);
            }
            WriteDetectedClassBlackboard(Blackboard, DetectedClassIdKey, DetectedClassLabelKey, -1, FString());
            return false;
        }

        const bool bExpired = LosePersonAfterSeconds > 0.f &&
            (CurrentTime - Memory.LastDetectedTime) > LosePersonAfterSeconds;
        if (bExpired)
        {
            ClearPersonDetectionMemory(
                Blackboard,
                HasPersonKey,
                DetectedPersonLocationKey,
                DetectionConfidenceKey,
                DetectedClassIdKey,
                DetectedClassLabelKey,
                Memory);
            return false;
        }

        WriteTargetBlackboard(
            Blackboard,
            HasPersonKey,
            DetectedPersonLocationKey,
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
        const FBlackboardKeySelector& HasPersonKey,
        const FBlackboardKeySelector& DetectedPersonLocationKey,
        const FBlackboardKeySelector& DetectionConfidenceKey,
        const FBlackboardKeySelector& DetectedClassIdKey,
        const FBlackboardKeySelector& DetectedClassLabelKey,
        FCrowPersonDetectionMemory& Memory,
        const FVector& DetectedTarget,
        float Confidence,
        int32 ClassId,
        const FString& ClassLabel,
        float CurrentTime,
        float SameTargetLocationThreshold,
        float NewTargetConfirmationSeconds,
        bool& bOutSameTarget,
        bool& bOutPendingTarget,
        bool& bOutCommittedNewTarget,
        float& OutPlanarDelta)
    {
        bOutPendingTarget = false;
        bOutCommittedNewTarget = false;

        const bool bHasActiveTarget = Memory.bHasLastTarget &&
            !HasPersonKey.SelectedKeyName.IsNone() &&
            Blackboard.GetValueAsBool(HasPersonKey.SelectedKeyName);
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
                Memory.bHasLastTarget = true;
                bOutCommittedNewTarget = true;
            }
            Memory.LastTargetLocation = DetectedTarget;
            Memory.bHasPendingTarget = false;
        }
        else
        {
            const float PendingDelta = Memory.bHasPendingTarget
                ? CalculatePlanarDistance(DetectedTarget, Memory.PendingTargetLocation)
                : TNumericLimits<float>::Max();
            if (!Memory.bHasPendingTarget ||
                (SameTargetLocationThreshold > 0.f && PendingDelta > SameTargetLocationThreshold))
            {
                Memory.PendingTargetFirstDetectedTime = CurrentTime;
                Memory.bHasPendingTarget = true;
            }

            Memory.PendingTargetLocation = DetectedTarget;

            Memory.PendingConfidence = Confidence;
            Memory.PendingClassId = ClassId;
            Memory.PendingClassLabel = ClassLabel;

            if (NewTargetConfirmationSeconds <= 0.f ||
                CurrentTime - Memory.PendingTargetFirstDetectedTime >= NewTargetConfirmationSeconds)
            {
                Memory.LastTargetLocation = Memory.PendingTargetLocation;
                Memory.bHasLastTarget = true;
                Memory.bHasPendingTarget = false;
                bOutCommittedNewTarget = true;
            }
            else
            {
                bOutPendingTarget = true;
            }
        }

        Memory.LastDetectedTime = CurrentTime;
        Memory.LastConfidence = Confidence;
        Memory.LastClassId = ClassId;
        Memory.LastClassLabel = ClassLabel;

        WriteTargetBlackboard(
            Blackboard,
            HasPersonKey,
            DetectedPersonLocationKey,
            DetectionConfidenceKey,
            DetectedClassIdKey,
            DetectedClassLabelKey,
            Memory.LastTargetLocation,
            Confidence,
            ClassId,
            ClassLabel);

        return bOutSameTarget;
    }

    bool ApplySharedDetection(
        UBlackboardComponent& Blackboard,
        const AActor& ControlledActor,
        const FBlackboardKeySelector& HasPersonKey,
        const FBlackboardKeySelector& DetectedPersonLocationKey,
        const FBlackboardKeySelector& DetectionConfidenceKey,
        const FBlackboardKeySelector& DetectedClassIdKey,
        const FBlackboardKeySelector& DetectedClassLabelKey,
        const TArray<int32>& ActionableClassIds,
        const TArray<FName>& ActionableClassLabels,
        FCrowPersonDetectionMemory& Memory,
        float CurrentTime,
        float MaxAgeSeconds,
        float MaxReporterDistance,
        float SameTargetLocationThreshold,
        float NewTargetConfirmationSeconds,
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
        float PlanarDelta = 0.f;
        ApplyLockedTargetMemory(
            Blackboard,
            HasPersonKey,
            DetectedPersonLocationKey,
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
            bSameTarget,
            bPendingTarget,
            bCommittedNewTarget,
            PlanarDelta);

        PrintCrowDetectionDebug(
            ControlledActor,
            FString::Printf(
                TEXT("shared %s target=%s detected=%s planarDelta=%.1f threshold=%.1f confirm=%.2f class=%s[%d] conf=%.2f"),
                bSameTarget ? TEXT("same") : (bPendingTarget ? TEXT("pending") : (bCommittedNewTarget ? TEXT("new") : TEXT("locked"))),
                *Memory.LastTargetLocation.ToCompactString(),
                *SharedTarget.ToCompactString(),
                PlanarDelta,
                SameTargetLocationThreshold,
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

UBTServ_UpdateCrowPersonDetection::UBTServ_UpdateCrowPersonDetection()
{
    NodeName = TEXT("Update Crow YOLO Target Detection");
    Interval = 0.1f;
    RandomDeviation = 0.f;

    HasPersonKey.SelectedKeyName = TEXT("HasDetectedPerson");
    DetectedPersonLocationKey.SelectedKeyName = TEXT("DetectedPersonLocation");
    DetectionConfidenceKey.SelectedKeyName = TEXT("DetectionConfidence");
    DetectedClassIdKey.SelectedKeyName = TEXT("DetectedClassId");
    DetectedClassLabelKey.SelectedKeyName = TEXT("DetectedClassLabel");

    ActionableClassIds.Add(0);
    ActionableClassLabels.Add(TEXT("person"));
}

uint16 UBTServ_UpdateCrowPersonDetection::GetInstanceMemorySize() const
{
    return sizeof(FCrowPersonDetectionMemory);
}

void UBTServ_UpdateCrowPersonDetection::TickNode(
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

    FCrowPersonDetectionMemory* Memory = reinterpret_cast<FCrowPersonDetectionMemory*>(NodeMemory);
    const float CurrentTime = ControlledPawn->GetWorld() ? ControlledPawn->GetWorld()->GetTimeSeconds() : 0.f;

    APawn* PlayerPawn = GetPlayerPawn(ControlledPawn);
    if (bAllowPlayerPawnLocationFallback &&
        bAlwaysFollowPlayerPawn &&
        IsValid(PlayerPawn) &&
        PlayerPawn != ControlledPawn)
    {
        const FVector TargetLocation = PlayerPawn->GetActorLocation() + TargetLocationOffset;
        Memory->LastTargetLocation = TargetLocation;
        Memory->LastRawTargetLocation = TargetLocation;
        Memory->LastRawTargetTime = CurrentTime;
        Memory->LastDetectedTime = CurrentTime;
        Memory->LastConfidence = 1.f;
        Memory->LastClassId = 0;
        Memory->LastClassLabel = TEXT("person");
        Memory->TargetVelocity = FVector::ZeroVector;
        Memory->bHasLastTarget = true;
        Memory->bHasRawTarget = true;

        Blackboard->SetValueAsBool(HasPersonKey.SelectedKeyName, true);
        Blackboard->SetValueAsVector(DetectedPersonLocationKey.SelectedKeyName, TargetLocation);
        Blackboard->SetValueAsFloat(DetectionConfidenceKey.SelectedKeyName, 1.f);
        WriteDetectedClassBlackboard(
            *Blackboard,
            DetectedClassIdKey,
            DetectedClassLabelKey,
            Memory->LastClassId,
            Memory->LastClassLabel);

        PrintCrowDetectionDebug(
            *ControlledPawn,
            FString::Printf(TEXT("always-follow target=%s"), *TargetLocation.ToCompactString()),
            FColor::Green,
            2,
            bDrawDebug,
            bLogDebug);
        return;
    }

    UMyActorComponent* Detector = ControlledPawn->FindComponentByClass<UMyActorComponent>();
    UCameraComponent* Camera = ControlledPawn->FindComponentByClass<UCameraComponent>();
    if (!Detector || !Camera || Detector->LastFrameSourceWidth <= 0 || Detector->LastFrameSourceHeight <= 0)
    {
        if (bUseFlockSharedDetections && ApplySharedDetection(
            *Blackboard,
            *ControlledPawn,
            HasPersonKey,
            DetectedPersonLocationKey,
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
            bDrawDebug,
            bLogDebug))
        {
            return;
        }

        KeepMemoryActive(
            *Blackboard,
            HasPersonKey,
            DetectedPersonLocationKey,
            DetectionConfidenceKey,
            DetectedClassIdKey,
            DetectedClassLabelKey,
            *Memory,
            CurrentTime,
            LosePersonAfterSeconds);
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
            HasPersonKey,
            DetectedPersonLocationKey,
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
            bDrawDebug,
            bLogDebug))
        {
            return;
        }

        KeepMemoryActive(
            *Blackboard,
            HasPersonKey,
            DetectedPersonLocationKey,
            DetectionConfidenceKey,
            DetectedClassIdKey,
            DetectedClassLabelKey,
            *Memory,
            CurrentTime,
            LosePersonAfterSeconds);
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
            HasPersonKey,
            DetectedPersonLocationKey,
            DetectionConfidenceKey,
            DetectedClassIdKey,
            DetectedClassLabelKey,
            *Memory,
            CurrentTime,
            LosePersonAfterSeconds);
        return;
    }
    Memory->LastProcessedFrameSequence = Detector->LastFrameSequence;

    if (Memory->bHasLastTarget &&
        !HasPersonKey.SelectedKeyName.IsNone() &&
        !Blackboard->GetValueAsBool(HasPersonKey.SelectedKeyName))
    {
        const int32 ProcessingFrameSequence = Memory->LastProcessedFrameSequence;
        ClearPersonDetectionMemory(
            *Blackboard,
            HasPersonKey,
            DetectedPersonLocationKey,
            DetectionConfidenceKey,
            DetectedClassIdKey,
            DetectedClassLabelKey,
            *Memory);
        Memory->LastProcessedFrameSequence = ProcessingFrameSequence;
    }

    FDetectionBox PersonBox;
    bool bMatchedTrackedBox = false;
    float TrackMatchIoU = 0.f;
    float TrackMatchDistance = 0.f;
    if (!FindTrackedPersonDetection(
        *Detector,
        *Memory,
        ActionableClassIds,
        ActionableClassLabels,
        MinAcceptedConfidence,
        bEnableYoloBoxTracking,
        TrackMatchMinIoU,
        TrackMatchMaxCenterDistance,
        TrackSwitchConfidenceMargin,
        PersonBox,
        bMatchedTrackedBox,
        TrackMatchIoU,
        TrackMatchDistance))
    {
        Memory->ConsecutiveDetections = 0;
        ++Memory->ConsecutiveMisses;

        if (bUseFlockSharedDetections && ApplySharedDetection(
            *Blackboard,
            *ControlledPawn,
            HasPersonKey,
            DetectedPersonLocationKey,
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
            bDrawDebug,
            bLogDebug))
        {
            return;
        }

        KeepMemoryActive(
            *Blackboard,
            HasPersonKey,
            DetectedPersonLocationKey,
            DetectionConfidenceKey,
            DetectedClassIdKey,
            DetectedClassLabelKey,
            *Memory,
            CurrentTime,
            LosePersonAfterSeconds);
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
        PersonBox = SmoothBox(Memory->LastTrackedBox, PersonBox, DeltaSeconds, TrackBoxSmoothingSpeed);
    }
    Memory->LastTrackedBox = PersonBox;
    Memory->bHasTrackedBox = true;

    const FVector RayOrigin = Camera->GetComponentLocation();
    const FVector2D BoxMin = GetBoxMin(PersonBox);
    const FVector2D BoxMax = GetBoxMax(PersonBox);
    const FVector2D TargetPixel(
        FMath::Clamp(PersonBox.Center.X, 0.f, FMath::Max(static_cast<float>(Detector->LastFrameSourceWidth - 1), 0.f)),
        FMath::Clamp(
            PersonBox.Center.Y + (PersonBox.Size.Y * TargetRayBoxVerticalBias),
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
        !HasPersonKey.SelectedKeyName.IsNone() &&
        Blackboard->GetValueAsBool(HasPersonKey.SelectedKeyName);
    const bool bHasRecentRawTarget = Memory->bHasRawTarget &&
        CurrentTime - Memory->LastRawTargetTime <= LosePersonAfterSeconds;
    const bool bHasDepthReferenceTarget = bHasActiveLockedTarget || bHasRecentRawTarget;
    const FVector DepthReferenceTargetLocation = bHasActiveLockedTarget
        ? Memory->LastTargetLocation
        : Memory->LastRawTargetLocation;

    if (!TryResolveTargetFromSceneDepth(
        *Detector,
        *Camera,
        RayOrigin,
        BoxMin,
        BoxMax,
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

        if (bUseFlockSharedDetections && ApplySharedDetection(
            *Blackboard,
            *ControlledPawn,
            HasPersonKey,
            DetectedPersonLocationKey,
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
            bDrawDebug,
            bLogDebug))
        {
            return;
        }

        KeepMemoryActive(
            *Blackboard,
            HasPersonKey,
            DetectedPersonLocationKey,
            DetectionConfidenceKey,
            DetectedClassIdKey,
            DetectedClassLabelKey,
            *Memory,
            CurrentTime,
            LosePersonAfterSeconds);
        PrintCrowDetectionDebug(
            *ControlledPawn,
            FString::Printf(
                TEXT("scene depth miss targetPixel=%s boxMin=%s boxMax=%s ray=%s depthSize=%dx%d depthPixels=%d tested=%d valid=%d tooNear=%d tooFar=%d nonFinite=%d depthMin=%.1f depthMax=%.1f depthAvg=%.1f minPixel=%s maxPixel=%s samples=%d cluster=%d misses=%d"),
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
                Memory->ConsecutiveMisses),
            FColor::Orange,
            9,
            bDrawDebug,
            bLogDebug);
        return;
    }

    const int32 RequiredDepthClusterSamples = FMath::Max(1, MinSceneDepthClusterSamples);
    const float DepthClusterSampleRatio = DepthSampleCount > 0
        ? static_cast<float>(DepthClusterSampleCount) / static_cast<float>(DepthSampleCount)
        : 0.f;
    const float RequiredDepthClusterRatio = FMath::Clamp(MinSceneDepthClusterSampleRatio, 0.f, 1.f);
    if (DepthClusterSampleCount < RequiredDepthClusterSamples ||
        DepthClusterSampleRatio < RequiredDepthClusterRatio)
    {
        Memory->ConsecutiveDetections = 0;
        ++Memory->ConsecutiveMisses;

        KeepMemoryActive(
            *Blackboard,
            HasPersonKey,
            DetectedPersonLocationKey,
            DetectionConfidenceKey,
            DetectedClassIdKey,
            DetectedClassLabelKey,
            *Memory,
            CurrentTime,
            LosePersonAfterSeconds);
        PrintCrowDetectionDebug(
            *ControlledPawn,
            FString::Printf(
                TEXT("scene depth weak cluster targetPixel=%s depthPixel=%s boxMin=%s boxMax=%s depth=%.1f samples=%d cluster=%d minCluster=%d ratio=%.3f minRatio=%.3f misses=%d seq=%d"),
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
                Memory->ConsecutiveMisses,
                Detector->LastFrameSequence),
            FColor::Orange,
            9,
            bDrawDebug,
            bLogDebug);
        return;
    }

    TargetLocation += TargetLocationOffset;

    bool bClampedTargetJump = false;
    float RawTargetJumpDistance = 0.f;
    if (MaxTrackedTargetJumpDistance > 0.f &&
        Memory->bHasRawTarget &&
        CurrentTime - Memory->LastRawTargetTime <= LosePersonAfterSeconds)
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
        if (TimeSinceLastRawTarget > KINDA_SMALL_NUMBER && TimeSinceLastRawTarget < LosePersonAfterSeconds)
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

    const bool bHasRealTarget = IsValid(PlayerPawn) && PlayerPawn != ControlledPawn;
    const FVector RealPawnLocation = bHasRealTarget ? PlayerPawn->GetActorLocation() : FVector::ZeroVector;
    const FVector RealComparableTargetLocation = bHasRealTarget
        ? RealPawnLocation + TargetLocationOffset
        : FVector::ZeroVector;
    const float RealTargetDelta2D = bHasRealTarget
        ? CalculatePlanarDistance(PredictedTargetLocation, RealComparableTargetLocation)
        : -1.f;
    const FString RealPawnLocationText = bHasRealTarget
        ? RealPawnLocation.ToCompactString()
        : FString(TEXT("None"));
    const FString RealComparableTargetLocationText = bHasRealTarget
        ? RealComparableTargetLocation.ToCompactString()
        : FString(TEXT("None"));

    Memory->LastRawTargetLocation = TargetLocation;
    Memory->LastRawTargetTime = CurrentTime;
    Memory->bHasRawTarget = true;

    ++Memory->ConsecutiveDetections;
    Memory->ConsecutiveMisses = 0;

    const bool bConfirmed = Memory->ConsecutiveDetections >= FMath::Max(1, RequiredConsecutiveDetections);
    if (!bConfirmed)
    {
        KeepMemoryActive(
            *Blackboard,
            HasPersonKey,
            DetectedPersonLocationKey,
            DetectionConfidenceKey,
            DetectedClassIdKey,
            DetectedClassLabelKey,
            *Memory,
            CurrentTime,
            LosePersonAfterSeconds);
        PrintCrowDetectionDebug(
            *ControlledPawn,
            FString::Printf(
                TEXT("candidate target=%s realPawn=%s realTarget=%s realDelta2D=%.1f depth=%.1f conf=%.2f track=%s jump=%.1f clamped=%s pixel=%s depthPixel=%s samples=%d/%d boxMin=%s boxMax=%s hits=%d/%d seq=%d"),
                *PredictedTargetLocation.ToCompactString(),
                *RealPawnLocationText,
                *RealComparableTargetLocationText,
                RealTargetDelta2D,
                SceneDepth,
                PersonBox.Confidence,
                bMatchedTrackedBox ? TEXT("matched") : TEXT("new"),
                RawTargetJumpDistance,
                bClampedTargetJump ? TEXT("yes") : TEXT("no"),
                *TargetPixel.ToString(),
                *ResolvedDepthPixel.ToString(),
                DepthClusterSampleCount,
                DepthSampleCount,
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
    float TargetLocationDelta = 0.f;
    ApplyLockedTargetMemory(
        *Blackboard,
        HasPersonKey,
        DetectedPersonLocationKey,
        DetectionConfidenceKey,
        DetectedClassIdKey,
        DetectedClassLabelKey,
        *Memory,
        PredictedTargetLocation,
        PersonBox.Confidence,
        PersonBox.ClassId,
        PersonBox.ClassLabel,
        CurrentTime,
        SameTargetLocationThreshold,
        NewTargetConfirmationSeconds,
        bSameTargetRedetected,
        bPendingTarget,
        bCommittedNewTarget,
        TargetLocationDelta);

    const FVector MemorizedTargetLocation = Memory->LastTargetLocation;

    if (bPublishDetectionsToFlock)
    {
        if (UWorld* World = ControlledPawn->GetWorld())
        {
            if (UCrowDetectionShareSubsystem* ShareSubsystem = World->GetSubsystem<UCrowDetectionShareSubsystem>())
            {
                ShareSubsystem->PublishTargetDetection(
                    ControlledPawn,
                    MemorizedTargetLocation,
                    PersonBox.Confidence,
                    PersonBox.ClassId,
                    PersonBox.ClassLabel);
            }
        }
    }

    PrintCrowDetectionDebug(
        *ControlledPawn,
        FString::Printf(
            TEXT("%s target=%s detected=%s realPawn=%s realTarget=%s realDelta2D=%.1f planarDelta=%.1f threshold=%.1f class=%s[%d] source=scene-depth depth=%.1f depthPixel=%s samples=%d/%d track=%s iou=%.2f centerDist=%.2f jump=%.1f clamped=%s velocity=%s predicted=%s conf=%.2f pixel=%s box=%s size=%s detections=%d seq=%d hits=%d"),
            bSameTargetRedetected ? TEXT("same") : (bPendingTarget ? TEXT("pending") : (bCommittedNewTarget ? TEXT("new") : TEXT("locked"))),
            *MemorizedTargetLocation.ToCompactString(),
            *PredictedTargetLocation.ToCompactString(),
            *RealPawnLocationText,
            *RealComparableTargetLocationText,
            RealTargetDelta2D,
            TargetLocationDelta,
            SameTargetLocationThreshold,
            *PersonBox.ClassLabel,
            PersonBox.ClassId,
            SceneDepth,
            *ResolvedDepthPixel.ToString(),
            DepthClusterSampleCount,
            DepthSampleCount,
            bMatchedTrackedBox ? TEXT("matched") : TEXT("new"),
            TrackMatchIoU,
            TrackMatchDistance,
            RawTargetJumpDistance,
            bClampedTargetJump ? TEXT("yes") : TEXT("no"),
            *Memory->TargetVelocity.ToCompactString(),
            *PredictedTargetLocation.ToCompactString(),
            PersonBox.Confidence,
            *TargetPixel.ToString(),
            *PersonBox.Center.ToString(),
            *PersonBox.Size.ToString(),
            Detector->LastFrameDetections.Num(),
            Detector->LastFrameSequence,
            Memory->ConsecutiveDetections),
        FColor::Green,
        5,
        bDrawDebug,
        bLogDebug);

    if (bDrawDebug)
    {
        if (UWorld* World = ControlledPawn->GetWorld())
        {
            DrawDebugSphere(World, MemorizedTargetLocation, 80.f, 12, FColor::Green, false, 0.15f);
            DrawDebugLine(World, RayOrigin, MemorizedTargetLocation, FColor::Green, false, 0.15f, 0, 2.f);
        }
    }
}
