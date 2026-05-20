#include "AI/BTTask_FlyToBlackboardLocation.h"

#include "AIController.h"
#include "AI/BotCharacter.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationSystem.h"

UBTTask_FlyToBlackboardLocation::UBTTask_FlyToBlackboardLocation()
{
    NodeName = TEXT("Fly To Blackboard Location");
    bNotifyTick = true;

    TargetLocationKey.SelectedKeyName = TEXT("DetectedPersonLocation");
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

    const ABotCharacter* BotCharacter = Cast<ABotCharacter>(ControlledPawn);
    const bool bUseFlyingMovement = !BotCharacter || BotCharacter->IsFlyingBot();
    const FVector TargetLocation = Blackboard->GetValueAsVector(TargetLocationKey.SelectedKeyName);
    FVector ToTarget = TargetLocation - ControlledPawn->GetActorLocation();
    if (!bUseFlyingMovement)
    {
        ToTarget.Z = 0.f;
    }
    const float Distance = ToTarget.Size();
    const FVector DirectionToTarget = ToTarget.GetSafeNormal();

    float HoldRadius = AcceptanceRadius;
    FVector MoveDirection = DirectionToTarget;
    if (bMaintainDistanceFromTarget)
    {
        const float DistanceError = Distance - DesiredTargetDistance;
        HoldRadius = DistanceTolerance;

        if (FMath::Abs(DistanceError) > DistanceTolerance)
        {
            MoveDirection = DistanceError > 0.f ? DirectionToTarget : -DirectionToTarget;
            if (MoveDirection.IsNearlyZero())
            {
                MoveDirection = -ControlledPawn->GetActorForwardVector().GetSafeNormal();
            }
        }
        else
        {
            MoveDirection = FVector::ZeroVector;
        }
    }

    const FVector RotationDirection = (!bUseFlyingMovement && bRotateWalkingTowardMoveDirection && !MoveDirection.IsNearlyZero())
        ? MoveDirection
        : DirectionToTarget;
    if (bRotateTowardTarget && !RotationDirection.IsNearlyZero())
    {
        FRotator DesiredRotation = RotationDirection.Rotation() + RotationOffset;
        if (!bUsePitchRotation || !bUseFlyingMovement)
        {
            DesiredRotation.Pitch = 0.f;
        }
        DesiredRotation.Roll = 0.f;

        const FRotator NewRotation = FMath::RInterpTo(
            ControlledPawn->GetActorRotation(),
            DesiredRotation,
            DeltaSeconds,
            RotationInterpSpeed);
        ControlledPawn->SetActorRotation(NewRotation);
    }

    const bool bShouldHold = MoveDirection.IsNearlyZero()
        || (!bMaintainDistanceFromTarget && ToTarget.SizeSquared() <= FMath::Square(HoldRadius));
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

        if (bDrawDebug && GEngine)
        {
            GEngine->AddOnScreenDebugMessage(
                reinterpret_cast<uint64>(this),
                0.2f,
                FColor::Cyan,
                FString::Printf(
                    TEXT("Fly task: holding target dist %.0f desired %.0f"),
                    Distance,
                    bMaintainDistanceFromTarget ? DesiredTargetDistance : 0.f));
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
            }
            else
            {
                bool bMovedWithNavMesh = false;
                if (bUseNavMeshForWalking && AIController)
                {
                    FVector MoveGoal = TargetLocation;
                    if (bMaintainDistanceFromTarget)
                    {
                        MoveGoal = TargetLocation - (DirectionToTarget * DesiredTargetDistance);
                    }
                    MoveGoal.Z = ControlledPawn->GetActorLocation().Z;

                    if (UWorld* World = ControlledPawn->GetWorld())
                    {
                        if (UNavigationSystemV1* NavSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World))
                        {
                            FNavLocation ProjectedGoal;
                            if (NavSystem->ProjectPointToNavigation(MoveGoal, ProjectedGoal, NavProjectionExtent))
                            {
                                const EPathFollowingRequestResult::Type MoveResult = AIController->MoveToLocation(
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
            }
        }
    }
    else
    {
        ControlledPawn->AddMovementInput(MoveDirection, MovementScale, true);
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
            if (bMaintainDistanceFromTarget)
            {
                DrawDebugSphere(World, TargetLocation, DesiredTargetDistance, 24, FColor::Emerald, false, 0.2f);
            }
            DrawDebugLine(World, ControlledPawn->GetActorLocation(), TargetLocation, FColor::Cyan, false, 0.2f, 0, 2.f);
        }
    }
}

void UBTTask_FlyToBlackboardLocation::OnTaskFinished(
    UBehaviorTreeComponent& OwnerComp,
    uint8* NodeMemory,
    EBTNodeResult::Type TaskResult)
{
    Super::OnTaskFinished(OwnerComp, NodeMemory, TaskResult);

    AAIController* AIController = OwnerComp.GetAIOwner();
    APawn* ControlledPawn = AIController ? AIController->GetPawn() : nullptr;
    const ABotCharacter* BotCharacter = Cast<ABotCharacter>(ControlledPawn);
    const bool bUseFlyingMovement = !BotCharacter || BotCharacter->IsFlyingBot();
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
