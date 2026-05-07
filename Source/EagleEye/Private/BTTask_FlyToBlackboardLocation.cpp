#include "AI/BTTask_FlyToBlackboardLocation.h"

#include "AIController.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"

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

    const FVector TargetLocation = Blackboard->GetValueAsVector(TargetLocationKey.SelectedKeyName);
    const FVector ToTarget = TargetLocation - ControlledPawn->GetActorLocation();
    const float Distance = ToTarget.Size();
    const FVector DirectionToTarget = ToTarget.GetSafeNormal();

    if (bRotateTowardTarget && !DirectionToTarget.IsNearlyZero())
    {
        FRotator DesiredRotation = DirectionToTarget.Rotation() + RotationOffset;
        if (!bUsePitchRotation)
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

    if (ToTarget.SizeSquared() <= FMath::Square(AcceptanceRadius))
    {
        if (ACharacter* Character = Cast<ACharacter>(ControlledPawn))
        {
            if (UCharacterMovementComponent* MovementComponent = Character->GetCharacterMovement())
            {
                MovementComponent->Velocity = FVector::ZeroVector;
            }
        }

        if (bDrawDebug && GEngine)
        {
            GEngine->AddOnScreenDebugMessage(
                reinterpret_cast<uint64>(this),
                0.2f,
                FColor::Cyan,
                FString::Printf(TEXT("Fly task: holding target dist %.0f"), Distance));
        }
        return;
    }

    const FVector MoveDirection = DirectionToTarget;
    if (ACharacter* Character = Cast<ACharacter>(ControlledPawn))
    {
        if (UCharacterMovementComponent* MovementComponent = Character->GetCharacterMovement())
        {
            if (MovementComponent->MovementMode != MOVE_Flying)
            {
                MovementComponent->SetMovementMode(MOVE_Flying);
            }

            const float MovementSpeed = FMath::Max(MinFlySpeed, MovementComponent->GetMaxSpeed() * MovementScale);
            MovementComponent->Velocity = MoveDirection * MovementSpeed;
        }
    }
    else
    {
        ControlledPawn->AddMovementInput(MoveDirection, MovementScale);
    }

    if (AIController && bUpdateFocus)
    {
        AIController->SetFocalPoint(TargetLocation);
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
            DrawDebugSphere(World, TargetLocation, AcceptanceRadius, 12, FColor::Cyan, false, 0.2f);
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
    if (ACharacter* Character = Cast<ACharacter>(ControlledPawn))
    {
        if (UCharacterMovementComponent* MovementComponent = Character->GetCharacterMovement())
        {
            MovementComponent->Velocity = FVector::ZeroVector;
        }
    }

    if (AIController)
    {
        AIController->ClearFocus(EAIFocusPriority::Gameplay);
    }
}
