#include "AI/BTTask_FlyToBlackboardLocation.h"

#include "AIController.h"
#include "AI/BotCharacter.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Bool.h"
#include "Camera/CameraComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationSystem.h"

namespace
{
    struct FBTTaskFlyToBlackboardLocationMemory
    {
        float LastMovementDecisionLogTime = -FLT_MAX;
    };

    enum class ETargetMoveIntent : uint8
    {
        Hold,
        Approach,
        Retreat
    };

    const TCHAR* LexToString(ETargetMoveIntent Intent)
    {
        switch (Intent)
        {
        case ETargetMoveIntent::Approach:
            return TEXT("Approach");
        case ETargetMoveIntent::Retreat:
            return TEXT("Retreat");
        case ETargetMoveIntent::Hold:
        default:
            return TEXT("Hold");
        }
    }

    const TCHAR* LexToString(EBTLocomotionModeSource Source)
    {
        switch (Source)
        {
        case EBTLocomotionModeSource::ForceWalking:
            return TEXT("ForceWalking");
        case EBTLocomotionModeSource::ForceFlying:
            return TEXT("ForceFlying");
        case EBTLocomotionModeSource::BlackboardBool:
            return TEXT("BlackboardBool");
        case EBTLocomotionModeSource::BotLocomotion:
        default:
            return TEXT("BotLocomotion");
        }
    }

    const TCHAR* LexToString(EBTTargetMovementModeSource Source)
    {
        switch (Source)
        {
        case EBTTargetMovementModeSource::BlackboardBool:
            return TEXT("BlackboardBool");
        case EBTTargetMovementModeSource::TaskSettings:
        default:
            return TEXT("TaskSettings");
        }
    }

    const TCHAR* LexToString(EBTTargetMovementMode Mode)
    {
        switch (Mode)
        {
        case EBTTargetMovementMode::MaintainDistance:
            return TEXT("MaintainDistance");
        case EBTTargetMovementMode::ApproachTarget:
        default:
            return TEXT("ApproachTarget");
        }
    }

    float CalculatePlanarYaw(const FVector& Direction)
    {
        FVector FlatDirection = Direction;
        FlatDirection.Z = 0.f;
        if (FlatDirection.IsNearlyZero())
        {
            return 0.f;
        }

        return FlatDirection.Rotation().Yaw;
    }
}

UBTTask_FlyToBlackboardLocation::UBTTask_FlyToBlackboardLocation()
{
    NodeName = TEXT("Fly To Blackboard Location");
    bNotifyTick = true;

    TargetLocationKey.SelectedKeyName = TEXT("DetectedPersonLocation");
    UseFlyingMovementKey.SelectedKeyName = TEXT("UseFlyingMovement");
    UseFlyingMovementKey.AddBoolFilter(this, GET_MEMBER_NAME_CHECKED(UBTTask_FlyToBlackboardLocation, UseFlyingMovementKey));
    MaintainDistanceKey.SelectedKeyName = TEXT("MaintainDistance");
    MaintainDistanceKey.AddBoolFilter(this, GET_MEMBER_NAME_CHECKED(UBTTask_FlyToBlackboardLocation, MaintainDistanceKey));
}

EBTNodeResult::Type UBTTask_FlyToBlackboardLocation::ExecuteTask(
    UBehaviorTreeComponent& OwnerComp,
    uint8* NodeMemory)
{
    AAIController* AIController = OwnerComp.GetAIOwner();
    APawn* ControlledPawn = AIController ? AIController->GetPawn() : nullptr;
    UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent();
    if (!ControlledPawn || !Blackboard)
    {
        return EBTNodeResult::Failed;
    }

    FBTTaskFlyToBlackboardLocationMemory* Memory =
        reinterpret_cast<FBTTaskFlyToBlackboardLocationMemory*>(NodeMemory);
    if (Memory)
    {
        Memory->LastMovementDecisionLogTime = -FLT_MAX;
    }

    if (bDrawDebug && GEngine)
    {
        GEngine->AddOnScreenDebugMessage(
            reinterpret_cast<uint64>(this),
            0.5f,
            FColor::Cyan,
            TEXT("Fly task: started"));
    }
    return EBTNodeResult::InProgress;
}

void UBTTask_FlyToBlackboardLocation::TickTask(
    UBehaviorTreeComponent& OwnerComp,
    uint8* NodeMemory,
    float DeltaSeconds)
{
    Super::TickTask(OwnerComp, NodeMemory, DeltaSeconds);

    AAIController* AIController = OwnerComp.GetAIOwner();
    APawn* ControlledPawn = AIController ? AIController->GetPawn() : nullptr;
    UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent();
    if (!ControlledPawn || !Blackboard)
    {
        FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
        return;
    }

    const bool bUseFlyingMovement = ResolveUseFlyingMovement(*ControlledPawn, *Blackboard);
    const FVector CurrentLocation = ControlledPawn->GetActorLocation();
    const FVector TargetLocation = Blackboard->GetValueAsVector(TargetLocationKey.SelectedKeyName);
    FVector ToTarget = TargetLocation - CurrentLocation;
    if (!bUseFlyingMovement)
    {
        ToTarget.Z = 0.f;
    }
    const float Distance = ToTarget.Size();
    const FVector DirectionToTarget = ToTarget.GetSafeNormal();
    const bool bMaintainDistance = ResolveMaintainDistance(*Blackboard);
    const float ActiveDesiredTargetDistance = DesiredTargetDistance;
    const float ActiveDistanceTolerance = DistanceTolerance;

    float HoldRadius = AcceptanceRadius;
    float MoveDistance = 0.f;
    FVector MoveDirection = FVector::ZeroVector;
    FVector MoveGoal = TargetLocation;
    ETargetMoveIntent MoveIntent = ETargetMoveIntent::Hold;

    if (bMaintainDistance)
    {
        const float DistanceError = Distance - ActiveDesiredTargetDistance;
        HoldRadius = ActiveDistanceTolerance;

        if (FMath::Abs(DistanceError) > ActiveDistanceTolerance)
        {
            MoveIntent = DistanceError > 0.f ? ETargetMoveIntent::Approach : ETargetMoveIntent::Retreat;
            MoveDirection = DistanceError > 0.f ? DirectionToTarget : -DirectionToTarget;
            if (MoveDirection.IsNearlyZero())
            {
                MoveDirection = -ControlledPawn->GetActorForwardVector();
                if (!bUseFlyingMovement)
                {
                    MoveDirection.Z = 0.f;
                }
                MoveDirection = MoveDirection.GetSafeNormal();
            }
            MoveDistance = FMath::Abs(DistanceError);
            MoveGoal = CurrentLocation + (MoveDirection * MoveDistance);
        }
    }
    else if (Distance > HoldRadius)
    {
        MoveIntent = ETargetMoveIntent::Approach;
        MoveDirection = DirectionToTarget;
        MoveDistance = FMath::Max(0.f, Distance - HoldRadius);
        MoveGoal = TargetLocation;
    }

    if (!bUseFlyingMovement)
    {
        MoveGoal.Z = CurrentLocation.Z;
    }

    const bool bIsRetreating = MoveIntent == ETargetMoveIntent::Retreat;
    const bool bShouldFaceMoveDirection = !bUseFlyingMovement &&
        bRotateWalkingTowardMoveDirection &&
        !MoveDirection.IsNearlyZero() &&
        (!bFaceTargetWhileRetreating || !bIsRetreating);
    const TCHAR* RotationDriver = bShouldFaceMoveDirection ? TEXT("MoveDirection") : TEXT("TargetDirection");
    FRotator LastDesiredRotation = ControlledPawn->GetActorRotation();
    FRotator LastCameraForwardOffset = FRotator::ZeroRotator;
    float LastYawErrorDegrees = 0.f;

    const auto LogMovementDecision = [&, NodeMemory](const TCHAR* MovementDriver, int32 NavResult = INDEX_NONE)
    {
        if (!bLogMovementDecision)
        {
            return;
        }

        UWorld* World = ControlledPawn->GetWorld();
        if (!World)
        {
            return;
        }

        FBTTaskFlyToBlackboardLocationMemory* Memory =
            reinterpret_cast<FBTTaskFlyToBlackboardLocationMemory*>(NodeMemory);
        const float CurrentTime = World->GetTimeSeconds();
        if (Memory && CurrentTime - Memory->LastMovementDecisionLogTime < MovementDecisionLogInterval)
        {
            return;
        }

        if (Memory)
        {
            Memory->LastMovementDecisionLogTime = CurrentTime;
        }

        UE_LOG(
            LogTemp,
            Log,
            TEXT("BTMoveDecision: pawn=%s intent=%s driver=%s navResult=%d rotation=%s locomotion=%s/%s targetMode=%s/%s taskMode=%s dist=%.1f desired=%.1f tolerance=%.1f moveDistance=%.1f target=%s goal=%s direction=%s targetDirection=%s actorRot=%s desiredRot=%s yawError=%.1f cameraOffset=%s alignCamera=%s targetKey=%s maintainKey=%s maintainValue=%s useFlyingKey=%s useFlyingValue=%s"),
            *ControlledPawn->GetName(),
            LexToString(MoveIntent),
            MovementDriver,
            NavResult,
            RotationDriver,
            bUseFlyingMovement ? TEXT("Flying") : TEXT("Walking"),
            LexToString(LocomotionModeSource),
            bMaintainDistance ? TEXT("MaintainDistance") : TEXT("ApproachTarget"),
            LexToString(TargetMovementModeSource),
            LexToString(TargetMovementMode),
            Distance,
            bMaintainDistance ? ActiveDesiredTargetDistance : 0.f,
            bMaintainDistance ? ActiveDistanceTolerance : HoldRadius,
            MoveDistance,
            *TargetLocation.ToCompactString(),
            *MoveGoal.ToCompactString(),
            *MoveDirection.ToCompactString(),
            *DirectionToTarget.ToCompactString(),
            *ControlledPawn->GetActorRotation().ToCompactString(),
            *LastDesiredRotation.ToCompactString(),
            LastYawErrorDegrees,
            *LastCameraForwardOffset.ToCompactString(),
            bAlignCameraForwardToTarget ? TEXT("true") : TEXT("false"),
            *TargetLocationKey.SelectedKeyName.ToString(),
            *MaintainDistanceKey.SelectedKeyName.ToString(),
            bMaintainDistance ? TEXT("true") : TEXT("false"),
            *UseFlyingMovementKey.SelectedKeyName.ToString(),
            bUseFlyingMovement ? TEXT("true") : TEXT("false"));
    };

    const FVector RotationDirection = bShouldFaceMoveDirection ? MoveDirection : DirectionToTarget;
    if (bRotateTowardTarget && !RotationDirection.IsNearlyZero())
    {
        if (AIController)
        {
            AIController->ClearFocus(EAIFocusPriority::Gameplay);
        }

        FRotator DesiredRotation = RotationDirection.Rotation() + RotationOffset;
        if (bAlignCameraForwardToTarget)
        {
            if (const UCameraComponent* Camera = ControlledPawn->FindComponentByClass<UCameraComponent>())
            {
                const float ActorForwardYaw = CalculatePlanarYaw(ControlledPawn->GetActorForwardVector());
                const float CameraForwardYaw = CalculatePlanarYaw(Camera->GetForwardVector());
                const float CameraYawOffset = FMath::FindDeltaAngleDegrees(ActorForwardYaw, CameraForwardYaw);
                LastCameraForwardOffset = FRotator(0.f, CameraYawOffset, 0.f);
                DesiredRotation = FRotator(
                    RotationDirection.Rotation().Pitch,
                    RotationDirection.Rotation().Yaw - CameraYawOffset,
                    0.f) + RotationOffset;
            }
        }
        if (!bUsePitchRotation || !bUseFlyingMovement)
        {
            DesiredRotation.Pitch = 0.f;
        }
        DesiredRotation.Roll = 0.f;
        DesiredRotation.Normalize();
        LastDesiredRotation = DesiredRotation;
        LastYawErrorDegrees = FMath::FindDeltaAngleDegrees(
            ControlledPawn->GetActorRotation().Yaw,
            DesiredRotation.Yaw);

        const FRotator NewRotation = FMath::RInterpTo(
            ControlledPawn->GetActorRotation(),
            DesiredRotation,
            DeltaSeconds,
            RotationInterpSpeed);
        if (AIController)
        {
            AIController->SetControlRotation(NewRotation);
        }
        ControlledPawn->SetActorRotation(NewRotation);
        LastYawErrorDegrees = FMath::FindDeltaAngleDegrees(
            NewRotation.Yaw,
            DesiredRotation.Yaw);
    }

    const bool bShouldHold = MoveDirection.IsNearlyZero()
        || (!bMaintainDistance && ToTarget.SizeSquared() <= FMath::Square(HoldRadius));
    if (bShouldHold)
    {
        if (ACharacter* Character = Cast<ACharacter>(ControlledPawn))
        {
            if (UCharacterMovementComponent* MovementComponent = Character->GetCharacterMovement())
            {
                if (bUseFlyingMovement)
                {
                    MovementComponent->Velocity = FVector::ZeroVector;
                }
                else
                {
                    MovementComponent->Velocity.X = 0.f;
                    MovementComponent->Velocity.Y = 0.f;
                }
            }
        }

        if (!bUseFlyingMovement && AIController)
        {
            AIController->StopMovement();
        }

        LogMovementDecision(TEXT("Hold"));

        if (bDrawDebug && GEngine)
        {
            GEngine->AddOnScreenDebugMessage(
                reinterpret_cast<uint64>(this),
                0.2f,
                FColor::Cyan,
                FString::Printf(
                    TEXT("Fly task: holding target dist %.0f desired %.0f"),
                    Distance,
                    bMaintainDistance ? ActiveDesiredTargetDistance : 0.f));
        }
        return;
    }

    if (ACharacter* Character = Cast<ACharacter>(ControlledPawn))
    {
        if (UCharacterMovementComponent* MovementComponent = Character->GetCharacterMovement())
        {
            if (bUseFlyingMovement && MovementComponent->MovementMode != MOVE_Flying)
            {
                MovementComponent->SetMovementMode(MOVE_Flying);
            }
            else if (!bUseFlyingMovement && MovementComponent->MovementMode == MOVE_Flying)
            {
                MovementComponent->SetMovementMode(MOVE_Walking);
            }

            if (bUseFlyingMovement)
            {
                const float MovementSpeed = FMath::Max(MinFlySpeed, MovementComponent->GetMaxSpeed() * MovementScale);
                MovementComponent->Velocity = MoveDirection * MovementSpeed;
                LogMovementDecision(TEXT("DirectFlightVelocity"));
            }
            else
            {
                bool bMovedWithNavMesh = false;
                EPathFollowingRequestResult::Type MoveResult = EPathFollowingRequestResult::Failed;
                if (bUseNavMeshForWalking && AIController)
                {
                    if (UWorld* World = ControlledPawn->GetWorld())
                    {
                        if (UNavigationSystemV1* NavSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World))
                        {
                            FNavLocation ProjectedGoal;
                            if (NavSystem->ProjectPointToNavigation(MoveGoal, ProjectedGoal, NavProjectionExtent))
                            {
                                MoveResult = AIController->MoveToLocation(
                                    ProjectedGoal.Location,
                                    WalkingMoveAcceptanceRadius,
                                    false,
                                    true,
                                    false,
                                    true,
                                    nullptr,
                                    bAllowPartialNavPath);
                                bMovedWithNavMesh = MoveResult != EPathFollowingRequestResult::Failed;
                            }
                        }
                    }
                }

                if (!bMovedWithNavMesh && bFallbackToDirectWalkingWhenNavMoveFails)
                {
                    ControlledPawn->AddMovementInput(MoveDirection, MovementScale, true);
                }

                LogMovementDecision(
                    bMovedWithNavMesh ? TEXT("NavMove") : TEXT("DirectWalkFallback"),
                    static_cast<int32>(MoveResult));
            }
        }
    }
    else
    {
        ControlledPawn->AddMovementInput(MoveDirection, MovementScale, true);
        LogMovementDecision(TEXT("DirectPawnInput"));
    }

    if (AIController && bUpdateFocus && (bUseFlyingMovement || !bRotateWalkingTowardMoveDirection))
    {
        AIController->SetFocalPoint(TargetLocation);
    }
    else if (AIController && !bUseFlyingMovement && bRotateWalkingTowardMoveDirection)
    {
        AIController->ClearFocus(EAIFocusPriority::Gameplay);
    }

    if (bDrawDebug)
    {
        if (GEngine)
        {
            GEngine->AddOnScreenDebugMessage(
                reinterpret_cast<uint64>(this),
                0.2f,
                FColor::Cyan,
                FString::Printf(
                    TEXT("Fly task: cur=%s target=%s dist=%.0f"),
                    *ControlledPawn->GetActorLocation().ToCompactString(),
                    *TargetLocation.ToCompactString(),
                    Distance));
        }

        if (UWorld* World = ControlledPawn->GetWorld())
        {
            DrawDebugSphere(World, TargetLocation, HoldRadius, 12, FColor::Cyan, false, 0.2f);
            if (bMaintainDistance)
            {
                DrawDebugSphere(World, TargetLocation, ActiveDesiredTargetDistance, 24, FColor::Emerald, false, 0.2f);
            }
            DrawDebugSphere(World, MoveGoal, 60.f, 12, bIsRetreating ? FColor::Orange : FColor::Blue, false, 0.2f);
            DrawDebugLine(World, ControlledPawn->GetActorLocation(), TargetLocation, FColor::Cyan, false, 0.2f, 0, 2.f);
            DrawDebugLine(World, ControlledPawn->GetActorLocation(), MoveGoal, bIsRetreating ? FColor::Orange : FColor::Blue, false, 0.2f, 0, 3.f);
        }
    }
}

uint16 UBTTask_FlyToBlackboardLocation::GetInstanceMemorySize() const
{
    return sizeof(FBTTaskFlyToBlackboardLocationMemory);
}

void UBTTask_FlyToBlackboardLocation::OnTaskFinished(
    UBehaviorTreeComponent& OwnerComp,
    uint8* NodeMemory,
    EBTNodeResult::Type TaskResult)
{
    Super::OnTaskFinished(OwnerComp, NodeMemory, TaskResult);

    AAIController* AIController = OwnerComp.GetAIOwner();
    APawn* ControlledPawn = AIController ? AIController->GetPawn() : nullptr;
    UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent();
    const bool bUseFlyingMovement = ControlledPawn && Blackboard
        ? ResolveUseFlyingMovement(*ControlledPawn, *Blackboard)
        : true;
    if (ACharacter* Character = Cast<ACharacter>(ControlledPawn))
    {
        if (UCharacterMovementComponent* MovementComponent = Character->GetCharacterMovement())
        {
            if (bUseFlyingMovement)
            {
                MovementComponent->Velocity = FVector::ZeroVector;
            }
            else
            {
                MovementComponent->Velocity.X = 0.f;
                MovementComponent->Velocity.Y = 0.f;
            }
        }
    }

    if (AIController)
    {
        AIController->StopMovement();
        AIController->ClearFocus(EAIFocusPriority::Gameplay);
    }
}

bool UBTTask_FlyToBlackboardLocation::ResolveUseFlyingMovement(
    const APawn& ControlledPawn,
    const UBlackboardComponent& Blackboard) const
{
    switch (LocomotionModeSource)
    {
    case EBTLocomotionModeSource::ForceWalking:
        return false;
    case EBTLocomotionModeSource::ForceFlying:
        return true;
    case EBTLocomotionModeSource::BlackboardBool:
        if (!UseFlyingMovementKey.SelectedKeyName.IsNone())
        {
            return Blackboard.GetValueAsBool(UseFlyingMovementKey.SelectedKeyName);
        }
        break;
    case EBTLocomotionModeSource::BotLocomotion:
    default:
        break;
    }

    const ABotCharacter* BotCharacter = Cast<ABotCharacter>(&ControlledPawn);
    return !BotCharacter || BotCharacter->IsFlyingBot();
}

bool UBTTask_FlyToBlackboardLocation::ResolveMaintainDistance(const UBlackboardComponent& Blackboard) const
{
    if (TargetMovementModeSource == EBTTargetMovementModeSource::BlackboardBool &&
        !MaintainDistanceKey.SelectedKeyName.IsNone())
    {
        return Blackboard.GetValueAsBool(MaintainDistanceKey.SelectedKeyName);
    }

    return TargetMovementMode == EBTTargetMovementMode::MaintainDistance;
}
