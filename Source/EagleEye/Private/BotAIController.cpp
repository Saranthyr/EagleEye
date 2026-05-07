#include "AI/BotAIController.h"

#include "AI/BotCharacter.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Engine/World.h"
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

    PerceptionComponentRef->ConfigureSense(*SightConfig);
    PerceptionComponentRef->SetDominantSense(SightConfig->GetSenseImplementation());
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

    if (InPawn)
    {
        FlightOrigin = InPawn->GetActorLocation();
        bHasFlightOrigin = true;
        PickRandomFlightDestination();
    }

    StartBehaviorTreeForPawn(InPawn);
}

void ABotAIController::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    UpdateRandomFlight(DeltaSeconds);

    UBlackboardComponent* BlackboardComponent = GetBlackboardComponent();
    if (!BlackboardComponent)
    {
        return;
    }

    AActor* TargetActor = Cast<AActor>(BlackboardComponent->GetValueAsObject(TargetActorKey));
    if (!IsValid(TargetActor))
    {
        return;
    }

    BlackboardComponent->SetValueAsVector(TargetLocationKey, TargetActor->GetActorLocation());
}

void ABotAIController::HandleTargetPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus)
{
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
    if (!bEnableRandomFlight)
    {
        return;
    }

    APawn* ControlledPawn = GetPawn();
    if (!ControlledPawn)
    {
        return;
    }

    if (!RandomFlightBlockedByKey.IsNone())
    {
        if (const UBlackboardComponent* BlackboardComponent = GetBlackboardComponent())
        {
            if (BlackboardComponent->GetValueAsBool(RandomFlightBlockedByKey))
            {
                return;
            }
        }
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

    ControlledPawn->AddMovementInput(ToDestination.GetSafeNormal(), 1.f);
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
