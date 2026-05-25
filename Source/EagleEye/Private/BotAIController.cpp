#include "AI/BotAIController.h"

#include "AI/BotCharacter.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Engine/World.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationSystem.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AIPerceptionTypes.h"
#include "Perception/AISenseConfig_Sight.h"

ABotAIController::ABotAIController()
{
    PrimaryActorTick.bCanEverTick = true;

    PerceptionComponentRef = CreateDefaultSubobject<UAIPerceptionComponent>(TEXT("PerceptionComponent"));
    SetPerceptionComponent(*PerceptionComponentRef);

    SightConfig = CreateDefaultSubobject<UAISenseConfig_Sight>(TEXT("SightConfig"));
    SightConfig->SightRadius = SightRadius;
    SightConfig->LoseSightRadius = LoseSightRadius;
    SightConfig->PeripheralVisionAngleDegrees = PeripheralVisionAngleDegrees;
    SightConfig->SetMaxAge(MaxSightAge);
    SightConfig->DetectionByAffiliation.bDetectEnemies = true;
    SightConfig->DetectionByAffiliation.bDetectFriendlies = true;
    SightConfig->DetectionByAffiliation.bDetectNeutrals = true;

    if (bEnableAISightPerception)
    {
        PerceptionComponentRef->ConfigureSense(*SightConfig);
        PerceptionComponentRef->SetDominantSense(SightConfig->GetSenseImplementation());
    }
    PerceptionComponentRef->OnTargetPerceptionUpdated.AddDynamic(this, &ABotAIController::HandleTargetPerceptionUpdated);
}

void ABotAIController::OnPossess(APawn* InPawn)
{
    Super::OnPossess(InPawn);

    if (!PerceptionComponentRef)
    {
        PerceptionComponentRef = NewObject<UAIPerceptionComponent>(this, TEXT("PerceptionComponent_Runtime"));
        if (PerceptionComponentRef)
        {
            PerceptionComponentRef->RegisterComponent();
            SetPerceptionComponent(*PerceptionComponentRef);
        }
    }

    if (!SightConfig)
    {
        SightConfig = NewObject<UAISenseConfig_Sight>(this, TEXT("SightConfig_Runtime"));
    }

    if (!PerceptionComponentRef || !SightConfig)
    {
        UE_LOG(LogTemp, Error, TEXT("BotAIController: missing perception setup on possess."));
        StartBehaviorTreeForPawn(InPawn);
        return;
    }

    if (bEnableAISightPerception)
    {
        SightConfig->SightRadius = SightRadius;
        SightConfig->LoseSightRadius = LoseSightRadius;
        SightConfig->PeripheralVisionAngleDegrees = PeripheralVisionAngleDegrees;
        SightConfig->SetMaxAge(MaxSightAge);
        SightConfig->DetectionByAffiliation.bDetectEnemies = true;
        SightConfig->DetectionByAffiliation.bDetectFriendlies = true;
        SightConfig->DetectionByAffiliation.bDetectNeutrals = true;

        PerceptionComponentRef->ConfigureSense(*SightConfig);
        PerceptionComponentRef->SetDominantSense(SightConfig->GetSenseImplementation());
        if (!PerceptionComponentRef->OnTargetPerceptionUpdated.IsAlreadyBound(
            this,
            &ABotAIController::HandleTargetPerceptionUpdated))
        {
            PerceptionComponentRef->OnTargetPerceptionUpdated.AddDynamic(
                this,
                &ABotAIController::HandleTargetPerceptionUpdated);
        }
        PerceptionComponentRef->RequestStimuliListenerUpdate();
    }

    if (ShouldUseRandomFlight(InPawn))
    {
        FlightOrigin = InPawn->GetActorLocation();
        bHasFlightOrigin = true;
        PickRandomFlightDestination();
    }
    else if (ShouldUseRandomWalking(InPawn))
    {
        WalkOrigin = InPawn->GetActorLocation();
        bHasWalkOrigin = true;
        PickRandomWalkDestination();
    }

    StartBehaviorTreeForPawn(InPawn);
}

void ABotAIController::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    if (const ABotCharacter* BotCharacter = Cast<ABotCharacter>(GetPawn()))
    {
        if (BotCharacter->IsDead())
        {
            StopMovement();
            ClearFocus(EAIFocusPriority::Gameplay);
            return;
        }
    }

    UpdateRandomFlight(DeltaSeconds);
    UpdateRandomWalking(DeltaSeconds);

    UBlackboardComponent* BlackboardComponent = GetBlackboardComponent();
    if (!BlackboardComponent)
    {
        return;
    }

    if (!BlackboardComponent->GetValueAsBool(HasDetectedPersonKey))
    {
        ClearFocus(EAIFocusPriority::Gameplay);
        return;
    }

    const FVector NeuralTargetLocation = BlackboardComponent->GetValueAsVector(DetectedPersonLocationKey);
    BlackboardComponent->SetValueAsVector(TargetLocationKey, NeuralTargetLocation);
    if (bFocusProjectileTarget)
    {
        SetFocalPoint(NeuralTargetLocation, EAIFocusPriority::Gameplay);
    }
    else
    {
        ClearFocus(EAIFocusPriority::Gameplay);
    }

    if (ABotCharacter* BotCharacter = Cast<ABotCharacter>(GetPawn()))
    {
        BotCharacter->TryThrowProjectileAtLocation(NeuralTargetLocation);
    }
}

void ABotAIController::HandleTargetPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus)
{
    if (!bEnableAISightPerception)
    {
        return;
    }

    if (!IsPlayerTarget(Actor))
    {
        return;
    }

    if (Stimulus.WasSuccessfullySensed())
    {
        UpdateBlackboardTarget(Actor, true);
        CurrentTargetActor = Actor;
        LastKnownTargetLocation = Actor->GetActorLocation();
        LastTargetSeenTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
        bHasLastKnownTargetLocation = true;
        return;
    }

    if (CurrentTargetActor.Get() == Actor)
    {
        CurrentTargetActor = nullptr;
        if (IsValid(Actor))
        {
            LastKnownTargetLocation = Actor->GetActorLocation();
            bHasLastKnownTargetLocation = true;
        }
        LastTargetSeenTime = GetWorld() ? GetWorld()->GetTimeSeconds() : LastTargetSeenTime;
    }

    UpdateBlackboardTarget(nullptr, false);
}

bool ABotAIController::IsPlayerTarget(const AActor* Actor) const
{
    const APawn* Pawn = Cast<APawn>(Actor);
    return Pawn && Pawn->IsPlayerControlled();
}

bool ABotAIController::ShouldUseRandomFlight(const APawn* ControlledPawn) const
{
    if (!bEnableRandomFlight || !ControlledPawn)
    {
        return false;
    }

    const ABotCharacter* BotCharacter = Cast<ABotCharacter>(ControlledPawn);
    return !BotCharacter || BotCharacter->IsFlyingBot();
}

bool ABotAIController::ShouldUseRandomWalking(const APawn* ControlledPawn) const
{
    if (!bEnableRandomWalking || !ControlledPawn)
    {
        return false;
    }

    const ABotCharacter* BotCharacter = Cast<ABotCharacter>(ControlledPawn);
    return BotCharacter && BotCharacter->IsWalkingBot();
}

bool ABotAIController::IsBlackboardKeyBlockingRandomMovement(FName KeyName) const
{
    if (KeyName.IsNone())
    {
        return false;
    }

    if (const UBlackboardComponent* BlackboardComponent = GetBlackboardComponent())
    {
        return BlackboardComponent->GetValueAsBool(KeyName);
    }

    return false;
}

void ABotAIController::StartBehaviorTreeForPawn(APawn* InPawn)
{
    UBehaviorTree* BehaviorTreeToRun = DefaultBehaviorTree;

    if (const ABotCharacter* BotCharacter = Cast<ABotCharacter>(InPawn))
    {
        if (BotCharacter->GetBehaviorTreeAsset())
        {
            BehaviorTreeToRun = BotCharacter->GetBehaviorTreeAsset();
        }
    }

    if (!BehaviorTreeToRun || !BehaviorTreeToRun->BlackboardAsset)
    {
        return;
    }

    UBlackboardComponent* BlackboardComponent = nullptr;
    UseBlackboard(BehaviorTreeToRun->BlackboardAsset, BlackboardComponent);
    RunBehaviorTree(BehaviorTreeToRun);
}

void ABotAIController::UpdateBlackboardTarget(AActor* TargetActor, bool bHasLineOfSight)
{
    UBlackboardComponent* BlackboardComponent = GetBlackboardComponent();
    if (!BlackboardComponent)
    {
        return;
    }

    BlackboardComponent->SetValueAsBool(HasLineOfSightKey, bHasLineOfSight);
    BlackboardComponent->SetValueAsObject(TargetActorKey, TargetActor);

    if (IsValid(TargetActor))
    {
        BlackboardComponent->SetValueAsVector(TargetLocationKey, TargetActor->GetActorLocation());
    }
    else if (bHasLastKnownTargetLocation)
    {
        BlackboardComponent->SetValueAsVector(TargetLocationKey, LastKnownTargetLocation);
    }
    else
    {
        BlackboardComponent->ClearValue(TargetActorKey);
        BlackboardComponent->ClearValue(TargetLocationKey);
    }
}

void ABotAIController::UpdateRandomFlight(float DeltaSeconds)
{
    APawn* ControlledPawn = GetPawn();
    if (!ShouldUseRandomFlight(ControlledPawn))
    {
        return;
    }

    if (IsBlackboardKeyBlockingRandomMovement(RandomFlightBlockedByKey))
    {
        StopMovement();
        bHasFlightDestination = false;
        return;
    }

    if (!bHasFlightOrigin)
    {
        FlightOrigin = ControlledPawn->GetActorLocation();
        bHasFlightOrigin = true;
    }

    if (!bHasFlightDestination)
    {
        PickRandomFlightDestination();
    }

    const FVector CurrentLocation = ControlledPawn->GetActorLocation();
    const FVector ToDestination = CurrentFlightDestination - CurrentLocation;

    if (ToDestination.SizeSquared() <= FMath::Square(FlightAcceptanceRadius))
    {
        DestinationHoldTimeRemaining -= DeltaSeconds;
        if (DestinationHoldTimeRemaining <= 0.f)
        {
            PickRandomFlightDestination();
        }
        return;
    }

    ControlledPawn->AddMovementInput(ToDestination.GetSafeNormal(), 1.f, true);
}

void ABotAIController::UpdateRandomWalking(float DeltaSeconds)
{
    APawn* ControlledPawn = GetPawn();
    if (!ShouldUseRandomWalking(ControlledPawn))
    {
        return;
    }

    if (IsBlackboardKeyBlockingRandomMovement(RandomWalkingBlockedByKey))
    {
        StopMovement();
        bHasWalkDestination = false;
        return;
    }

    if (!bHasWalkOrigin)
    {
        WalkOrigin = ControlledPawn->GetActorLocation();
        bHasWalkOrigin = true;
    }

    if (!bHasWalkDestination)
    {
        PickRandomWalkDestination();
        if (!bHasWalkDestination)
        {
            return;
        }
    }

    FVector ToDestination = CurrentWalkDestination - ControlledPawn->GetActorLocation();
    ToDestination.Z = 0.f;

    if (ToDestination.SizeSquared() <= FMath::Square(WalkAcceptanceRadius))
    {
        WalkDestinationHoldTimeRemaining -= DeltaSeconds;
        if (WalkDestinationHoldTimeRemaining <= 0.f)
        {
            PickRandomWalkDestination();
        }
        return;
    }

    const EPathFollowingRequestResult::Type MoveResult = MoveToLocation(
        CurrentWalkDestination,
        WalkAcceptanceRadius,
        false,
        true,
        true);
    if (MoveResult == EPathFollowingRequestResult::Failed)
    {
        bHasWalkDestination = false;
        StopMovement();
    }
}

void ABotAIController::PickRandomFlightDestination()
{
    if (!bHasFlightOrigin)
    {
        return;
    }

    const float Angle = FMath::FRandRange(0.f, 2.f * PI);
    const float Distance = FMath::Sqrt(FMath::FRand()) * FlightRadius;
    const FVector Offset(
        FMath::Cos(Angle) * Distance,
        FMath::Sin(Angle) * Distance,
        PreferredFlightHeight + FMath::FRandRange(-FlightHeightVariance, FlightHeightVariance));

    CurrentFlightDestination = FlightOrigin + Offset;
    DestinationHoldTimeRemaining = FMath::FRandRange(
        DestinationHoldMinSeconds,
        FMath::Max(DestinationHoldMinSeconds, DestinationHoldMaxSeconds));
    bHasFlightDestination = true;
}

void ABotAIController::PickRandomWalkDestination()
{
    if (!bHasWalkOrigin)
    {
        return;
    }

    bHasWalkDestination = false;

    if (UWorld* World = GetWorld())
    {
        if (UNavigationSystemV1* NavSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World))
        {
            FNavLocation ProjectedOrigin;
            if (!NavSystem->ProjectPointToNavigation(WalkOrigin, ProjectedOrigin, WalkingNavProjectionExtent))
            {
                return;
            }

            WalkOrigin = ProjectedOrigin.Location;

            FNavLocation NavLocation;
            if (NavSystem->GetRandomReachablePointInRadius(WalkOrigin, WalkRadius, NavLocation))
            {
                CurrentWalkDestination = NavLocation.Location;
                WalkDestinationHoldTimeRemaining = FMath::FRandRange(
                    WalkDestinationHoldMinSeconds,
                    FMath::Max(WalkDestinationHoldMinSeconds, WalkDestinationHoldMaxSeconds));
                bHasWalkDestination = true;
                return;
            }
        }
    }
}

void ABotAIController::ClearChaseTarget()
{
    CurrentTargetActor = nullptr;
    bHasLastKnownTargetLocation = false;
    LastTargetSeenTime = -1.f;
    StopMovement();

    UBlackboardComponent* BlackboardComponent = GetBlackboardComponent();
    if (!BlackboardComponent)
    {
        return;
    }

    BlackboardComponent->SetValueAsBool(HasLineOfSightKey, false);
    BlackboardComponent->ClearValue(TargetActorKey);
    BlackboardComponent->ClearValue(TargetLocationKey);
}
