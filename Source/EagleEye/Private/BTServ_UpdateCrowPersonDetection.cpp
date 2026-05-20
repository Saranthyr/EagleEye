#include "AI/BTServ_UpdateCrowPersonDetection.h"

#include "AIController.h"
#include "AI/CrowDetectionShareSubsystem.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Camera/CameraComponent.h"
#include "DetectionResult.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "MyActorComponent.h"

namespace
{
    struct FCrowPersonDetectionMemory
    {
        FVector LastTargetLocation = FVector::ZeroVector;
        float LastDetectedTime = -FLT_MAX;
        float LastConfidence = 0.f;
        int32 LastProcessedFrameSequence = 0;
        int32 ConsecutiveDetections = 0;
        int32 ConsecutiveMisses = 0;
        bool bHasLastTarget = false;
    };

    struct FDetectionBox
    {
        FVector2D Center = FVector2D::ZeroVector;
        FVector2D Size = FVector2D::ZeroVector;
        float Confidence = 0.f;
        float Area = 0.f;
    };

    bool IsPersonDetection(const FDetectionResult& Detection)
    {
        return Detection.ClassId == 0 || Detection.Label.StartsWith(TEXT("person"), ESearchCase::IgnoreCase);
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
        return true;
    }

    bool FindBestPersonDetection(const UMyActorComponent& Detector, float MinAcceptedConfidence, FDetectionBox& OutBox)
    {
        bool bFound = false;
        float BestScore = -1.f;

        for (const FDetectionResult& Detection : Detector.LastFrameDetections)
        {
            if (!IsPersonDetection(Detection))
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

            const float Score = Box.Confidence > 0.f ? Box.Confidence : Box.Area;
            if (!bFound || Score > BestScore)
            {
                OutBox = Box;
                BestScore = Score;
                bFound = true;
            }
        }

        return bFound;
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

    bool TrySnapToPlayerOnRay(
        const UObject* WorldContext,
        const APawn& ControlledPawn,
        const FVector& RayOrigin,
        const FVector& RayDirection,
        float TraceDistance,
        float SnapRadius,
        FVector& OutLocation)
    {
        APlayerController* PlayerController = UGameplayStatics::GetPlayerController(WorldContext, 0);
        APawn* PlayerPawn = PlayerController ? PlayerController->GetPawn() : nullptr;
        if (!IsValid(PlayerPawn) || PlayerPawn == &ControlledPawn)
        {
            return false;
        }

        const FVector ToPlayer = PlayerPawn->GetActorLocation() - RayOrigin;
        const float AlongRay = FVector::DotProduct(ToPlayer, RayDirection);
        if (AlongRay < 0.f || AlongRay > TraceDistance)
        {
            return false;
        }

        const FVector ClosestPoint = RayOrigin + (RayDirection * AlongRay);
        if (FVector::DistSquared(ClosestPoint, PlayerPawn->GetActorLocation()) > FMath::Square(SnapRadius))
        {
            return false;
        }

        OutLocation = PlayerPawn->GetActorLocation();
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

        if (bLogDebug)
        {
            UE_LOG(LogTemp, Log, TEXT("CrowDetectionDebug: %s"), *FullMessage);
        }
    }

    bool KeepMemoryActive(
        UBlackboardComponent& Blackboard,
        const FBlackboardKeySelector& HasPersonKey,
        const FBlackboardKeySelector& DetectedPersonLocationKey,
        const FBlackboardKeySelector& DetectionConfidenceKey,
        const FCrowPersonDetectionMemory& Memory,
        float CurrentTime,
        float LosePersonAfterSeconds)
    {
        if (!Memory.bHasLastTarget || (CurrentTime - Memory.LastDetectedTime) > LosePersonAfterSeconds)
        {
            Blackboard.SetValueAsBool(HasPersonKey.SelectedKeyName, false);
            return false;
        }

        Blackboard.SetValueAsBool(HasPersonKey.SelectedKeyName, true);
        Blackboard.SetValueAsVector(DetectedPersonLocationKey.SelectedKeyName, Memory.LastTargetLocation);
        Blackboard.SetValueAsFloat(DetectionConfidenceKey.SelectedKeyName, Memory.LastConfidence);
        return true;
    }

    bool ApplySharedDetection(
        UBlackboardComponent& Blackboard,
        const AActor& ControlledActor,
        const FBlackboardKeySelector& HasPersonKey,
        const FBlackboardKeySelector& DetectedPersonLocationKey,
        const FBlackboardKeySelector& DetectionConfidenceKey,
        FCrowPersonDetectionMemory& Memory,
        float CurrentTime,
        float MaxAgeSeconds,
        float MaxReporterDistance,
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
        if (!ShareSubsystem->GetBestRecentPersonDetection(
            &ControlledActor,
            MaxAgeSeconds,
            MaxReporterDistance,
            SharedTarget,
            SharedConfidence))
        {
            return false;
        }

        Memory.LastTargetLocation = SharedTarget;
        Memory.LastDetectedTime = CurrentTime;
        Memory.LastConfidence = SharedConfidence;
        Memory.bHasLastTarget = true;

        Blackboard.SetValueAsBool(HasPersonKey.SelectedKeyName, true);
        Blackboard.SetValueAsVector(DetectedPersonLocationKey.SelectedKeyName, SharedTarget);
        Blackboard.SetValueAsFloat(DetectionConfidenceKey.SelectedKeyName, SharedConfidence);

        PrintCrowDetectionDebug(
            ControlledActor,
            FString::Printf(TEXT("shared target=%s conf=%.2f"), *SharedTarget.ToCompactString(), SharedConfidence),
            FColor::Blue,
            1,
            bDrawDebug,
            bLogDebug);

        return true;
    }
}

UBTServ_UpdateCrowPersonDetection::UBTServ_UpdateCrowPersonDetection()
{
    NodeName = TEXT("Update Crow Person Detection");
    Interval = 0.1f;
    RandomDeviation = 0.f;

    HasPersonKey.SelectedKeyName = TEXT("HasDetectedPerson");
    DetectedPersonLocationKey.SelectedKeyName = TEXT("DetectedPersonLocation");
    DetectionConfidenceKey.SelectedKeyName = TEXT("DetectionConfidence");
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
    if (bAlwaysFollowPlayerPawn && IsValid(PlayerPawn) && PlayerPawn != ControlledPawn)
    {
        const FVector TargetLocation = PlayerPawn->GetActorLocation() + TargetLocationOffset;
        Memory->LastTargetLocation = TargetLocation;
        Memory->LastDetectedTime = CurrentTime;
        Memory->LastConfidence = 1.f;
        Memory->bHasLastTarget = true;

        Blackboard->SetValueAsBool(HasPersonKey.SelectedKeyName, true);
        Blackboard->SetValueAsVector(DetectedPersonLocationKey.SelectedKeyName, TargetLocation);
        Blackboard->SetValueAsFloat(DetectionConfidenceKey.SelectedKeyName, 1.f);

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
            *Memory,
            CurrentTime,
            SharedDetectionMaxAgeSeconds,
            SharedDetectionMaxReporterDistance,
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
            *Memory,
            CurrentTime,
            SharedDetectionMaxAgeSeconds,
            SharedDetectionMaxReporterDistance,
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
            *Memory,
            CurrentTime,
            LosePersonAfterSeconds);
        return;
    }
    Memory->LastProcessedFrameSequence = Detector->LastFrameSequence;

    FDetectionBox PersonBox;
    if (!FindBestPersonDetection(*Detector, MinAcceptedConfidence, PersonBox))
    {
        Memory->ConsecutiveDetections = 0;
        ++Memory->ConsecutiveMisses;

        if (bUseFlockSharedDetections && ApplySharedDetection(
            *Blackboard,
            *ControlledPawn,
            HasPersonKey,
            DetectedPersonLocationKey,
            DetectionConfidenceKey,
            *Memory,
            CurrentTime,
            SharedDetectionMaxAgeSeconds,
            SharedDetectionMaxReporterDistance,
            bDrawDebug,
            bLogDebug))
        {
            return;
        }

        if (Memory->ConsecutiveMisses > MaxConsecutiveDetectionMisses)
        {
            Blackboard->SetValueAsBool(HasPersonKey.SelectedKeyName, false);
            PrintCrowDetectionDebug(
                *ControlledPawn,
                FString::Printf(
                    TEXT("detections=%d no accepted person seq=%d misses=%d"),
                    Detector->LastFrameDetections.Num(),
                    Detector->LastFrameSequence,
                    Memory->ConsecutiveMisses),
                FColor::Red,
                4,
                bDrawDebug,
                bLogDebug);
            return;
        }

        KeepMemoryActive(
            *Blackboard,
            HasPersonKey,
            DetectedPersonLocationKey,
            DetectionConfidenceKey,
            *Memory,
            CurrentTime,
            LosePersonAfterSeconds);
        PrintCrowDetectionDebug(
            *ControlledPawn,
            FString::Printf(
                TEXT("detections=%d no accepted person source=%dx%d seq=%d misses=%d memory=%s"),
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

    const FVector RayOrigin = Camera->GetComponentLocation();
    const FVector RayDirection = BuildCameraRayDirection(
        *Camera,
        PersonBox.Center,
        Detector->LastFrameSourceWidth,
        Detector->LastFrameSourceHeight);

    FVector TargetLocation = FVector::ZeroVector;
    FVector SnappedPlayerLocation = FVector::ZeroVector;
    const bool bSnappedToPlayer = TrySnapToPlayerOnRay(
        ControlledPawn,
        *ControlledPawn,
        RayOrigin,
        RayDirection,
        TraceDistance,
        PlayerRaySnapRadius,
        SnappedPlayerLocation);
    if (bPreferPlayerPawnLocation &&
        IsValid(PlayerPawn) &&
        PlayerPawn != ControlledPawn &&
        (!bRequireRaySnapForPlayerPawnLocation || bSnappedToPlayer))
    {
        TargetLocation = PlayerPawn->GetActorLocation();
    }
    else if (bSnappedToPlayer)
    {
        TargetLocation = SnappedPlayerLocation;
    }
    else
    {
        FHitResult Hit;
        FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(CrowPersonDetectionTrace), false, ControlledPawn);
        const FVector TraceEnd = RayOrigin + (RayDirection * TraceDistance);
        if (UWorld* World = ControlledPawn->GetWorld();
            World && World->LineTraceSingleByChannel(Hit, RayOrigin, TraceEnd, ECC_Visibility, QueryParams))
        {
            TargetLocation = Hit.ImpactPoint;
        }
        else
        {
            TargetLocation = RayOrigin + (RayDirection * FallbackTargetDistance);
        }
    }

    TargetLocation += TargetLocationOffset;
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
            *Memory,
            CurrentTime,
            LosePersonAfterSeconds);
        PrintCrowDetectionDebug(
            *ControlledPawn,
            FString::Printf(
                TEXT("candidate target=%s conf=%.2f hits=%d/%d seq=%d"),
                *TargetLocation.ToCompactString(),
                PersonBox.Confidence,
                Memory->ConsecutiveDetections,
                FMath::Max(1, RequiredConsecutiveDetections),
                Detector->LastFrameSequence),
            FColor::Yellow,
            5,
            bDrawDebug,
            bLogDebug);
        return;
    }

    const FVector SmoothedTargetLocation = Memory->bHasLastTarget
        ? FMath::VInterpTo(Memory->LastTargetLocation, TargetLocation, DeltaSeconds, TargetSmoothingSpeed)
        : TargetLocation;

    Memory->LastTargetLocation = SmoothedTargetLocation;
    Memory->LastDetectedTime = CurrentTime;
    Memory->LastConfidence = PersonBox.Confidence;
    Memory->bHasLastTarget = true;

    Blackboard->SetValueAsBool(HasPersonKey.SelectedKeyName, true);
    Blackboard->SetValueAsVector(DetectedPersonLocationKey.SelectedKeyName, SmoothedTargetLocation);
    Blackboard->SetValueAsFloat(DetectionConfidenceKey.SelectedKeyName, PersonBox.Confidence);

    if (bPublishDetectionsToFlock)
    {
        if (UWorld* World = ControlledPawn->GetWorld())
        {
            if (UCrowDetectionShareSubsystem* ShareSubsystem = World->GetSubsystem<UCrowDetectionShareSubsystem>())
            {
                ShareSubsystem->PublishPersonDetection(ControlledPawn, SmoothedTargetLocation, PersonBox.Confidence);
            }
        }
    }

    PrintCrowDetectionDebug(
        *ControlledPawn,
        FString::Printf(
            TEXT("person target=%s conf=%.2f box=%s size=%s detections=%d seq=%d hits=%d"),
            *SmoothedTargetLocation.ToCompactString(),
            PersonBox.Confidence,
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
            DrawDebugSphere(World, SmoothedTargetLocation, 80.f, 12, FColor::Green, false, 0.15f);
            DrawDebugLine(World, RayOrigin, SmoothedTargetLocation, FColor::Green, false, 0.15f, 0, 2.f);
        }
    }
}
