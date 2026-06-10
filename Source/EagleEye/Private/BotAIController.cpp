#include "AI/BotAIController.h"

#include "AI/BotCharacter.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationSystem.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AIPerceptionTypes.h"
#include "Perception/AISenseConfig_Sight.h"

namespace
{
    bool AreRandomMovementSettingsNearlyEqual(
        const FBotRandomMovementSettings& A,
        const FBotRandomMovementSettings& B)
    {
        return A.bEnableRandomFlight == B.bEnableRandomFlight &&
            FMath::IsNearlyEqual(A.FlightRadius, B.FlightRadius) &&
            FMath::IsNearlyEqual(A.FlightAcceptanceRadius, B.FlightAcceptanceRadius) &&
            FMath::IsNearlyEqual(A.PreferredFlightHeight, B.PreferredFlightHeight) &&
            FMath::IsNearlyEqual(A.FlightHeightVariance, B.FlightHeightVariance) &&
            FMath::IsNearlyEqual(A.DestinationHoldMinSeconds, B.DestinationHoldMinSeconds) &&
            FMath::IsNearlyEqual(A.DestinationHoldMaxSeconds, B.DestinationHoldMaxSeconds) &&
            A.RandomFlightBlockedByKey == B.RandomFlightBlockedByKey &&
            A.bEnableRandomWalking == B.bEnableRandomWalking &&
            FMath::IsNearlyEqual(A.WalkRadius, B.WalkRadius) &&
            FMath::IsNearlyEqual(A.WalkAcceptanceRadius, B.WalkAcceptanceRadius) &&
            A.WalkingNavProjectionExtent.Equals(B.WalkingNavProjectionExtent) &&
            FMath::IsNearlyEqual(A.WalkDestinationHoldMinSeconds, B.WalkDestinationHoldMinSeconds) &&
            FMath::IsNearlyEqual(A.WalkDestinationHoldMaxSeconds, B.WalkDestinationHoldMaxSeconds) &&
            A.RandomWalkingBlockedByKey == B.RandomWalkingBlockedByKey;
    }

    bool HasCustomRandomMovementSettings(const FBotRandomMovementSettings& Settings)
    {
        const FBotRandomMovementSettings DefaultSettings;
        return !AreRandomMovementSettingsNearlyEqual(Settings, DefaultSettings);
    }

    FBotRandomMovementSettings SanitizeRandomMovementSettings(const FBotRandomMovementSettings& Settings)
    {
        FBotRandomMovementSettings Sanitized = Settings;
        Sanitized.FlightRadius = FMath::Max(0.f, Settings.FlightRadius);
        Sanitized.FlightAcceptanceRadius = FMath::Max(0.f, Settings.FlightAcceptanceRadius);
        Sanitized.FlightHeightVariance = FMath::Max(0.f, Settings.FlightHeightVariance);
        Sanitized.DestinationHoldMinSeconds = FMath::Max(0.f, Settings.DestinationHoldMinSeconds);
        Sanitized.DestinationHoldMaxSeconds = FMath::Max(
            Sanitized.DestinationHoldMinSeconds,
            Settings.DestinationHoldMaxSeconds);
        Sanitized.WalkRadius = FMath::Max(0.f, Settings.WalkRadius);
        Sanitized.WalkAcceptanceRadius = FMath::Max(0.f, Settings.WalkAcceptanceRadius);
        Sanitized.WalkingNavProjectionExtent = Settings.WalkingNavProjectionExtent.ComponentMax(FVector::ZeroVector);
        Sanitized.WalkDestinationHoldMinSeconds = FMath::Max(0.f, Settings.WalkDestinationHoldMinSeconds);
        Sanitized.WalkDestinationHoldMaxSeconds = FMath::Max(
            Sanitized.WalkDestinationHoldMinSeconds,
            Settings.WalkDestinationHoldMaxSeconds);
        return Sanitized;
    }
}

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

    ActiveRandomMovementSettings = SanitizeRandomMovementSettings(DefaultRandomMovementSettings);
    ResetRandomMovementState();

    if (const ABotCharacter* BotCharacter = Cast<ABotCharacter>(InPawn);
        BotCharacter && HasCustomRandomMovementSettings(BotCharacter->GetRandomMovementSettings()))
    {
        ApplyRandomMovementSettings(BotCharacter->GetRandomMovementSettings());
    }

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

void ABotAIController::ApplyRandomMovementSettings(const FBotRandomMovementSettings& InSettings)
{
    ActiveRandomMovementSettings = SanitizeRandomMovementSettings(InSettings);
    ResetRandomMovementState();
}

void ABotAIController::ResetRandomMovementState()
{
    bHasFlightOrigin = false;
    bHasFlightDestination = false;
    bHasWalkOrigin = false;
    bHasWalkDestination = false;
    DestinationHoldTimeRemaining = 0.f;
    WalkDestinationHoldTimeRemaining = 0.f;
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

    UBlackboardComponent* BlackboardComponent = GetBlackboardComponent();
    if (!BlackboardComponent)
    {
        return;
    }

    UpdateHealingTarget(DeltaSeconds);
    UpdateRandomFlight(DeltaSeconds);
    UpdateRandomWalking(DeltaSeconds);

    if (!BlackboardComponent->GetValueAsBool(HasDetectedTargetKey))
    {
        ClearFocus(EAIFocusPriority::Gameplay);
        return;
    }

    const FVector DetectedTargetLocation = BlackboardComponent->GetValueAsVector(DetectedTargetLocationKey);
    BlackboardComponent->SetValueAsVector(TargetLocationKey, DetectedTargetLocation);
    if (bFocusProjectileTarget)
    {
        SetFocalPoint(DetectedTargetLocation, EAIFocusPriority::Gameplay);
    }

    if (ABotCharacter* BotCharacter = Cast<ABotCharacter>(GetPawn()))
    {
        if (!BotCharacter->IsCloseDamageHealing() && !BotCharacter->IsProjectileHealing())
        {
            BotCharacter->TryThrowProjectileAtLocation(DetectedTargetLocation);
        }
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
    }

    UpdateBlackboardTarget(nullptr, false);
}

bool ABotAIController::IsPlayerTarget(const AActor* Actor) const
{
    const APawn* TargetPawn = Cast<APawn>(Actor);
    return TargetPawn && TargetPawn->IsPlayerControlled();
}

bool ABotAIController::ShouldUseRandomFlight(const APawn* ControlledPawn) const
{
    if (!ActiveRandomMovementSettings.bEnableRandomFlight || !ControlledPawn)
    {
        return false;
    }

    const ABotCharacter* BotCharacter = Cast<ABotCharacter>(ControlledPawn);
    return !BotCharacter || BotCharacter->IsFlyingBot();
}

bool ABotAIController::ShouldUseRandomWalking(const APawn* ControlledPawn) const
{
    if (!ActiveRandomMovementSettings.bEnableRandomWalking || !ControlledPawn)
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

bool ABotAIController::IsValidHealingTarget(const ABotCharacter* HealerBot, const ABotCharacter* TargetBot) const
{
    if (!IsValid(HealerBot) || !IsValid(TargetBot) || HealerBot == TargetBot || TargetBot->IsDead())
    {
        return false;
    }

    if (!TargetBot->NeedsHealing(HealingHealthPercentThreshold))
    {
        return false;
    }

    if (HealingSearchRadius > 0.f &&
        FVector::DistSquared(HealerBot->GetActorLocation(), TargetBot->GetActorLocation()) >
            FMath::Square(HealingSearchRadius))
    {
        return false;
    }

    return true;
}

bool ABotAIController::ShouldUseMeleeHealing(const ABotCharacter& HealerBot, const ABotCharacter& TargetBot) const
{
    if (HealerBot.GetBotLocomotionMode() != TargetBot.GetBotLocomotionMode())
    {
        return false;
    }

    return CanReachHealingTargetForMelee(HealerBot, TargetBot);
}

bool ABotAIController::CanReachHealingTargetForMelee(const ABotCharacter& HealerBot, const ABotCharacter& TargetBot) const
{
    const float DistanceSq = FVector::DistSquared(HealerBot.GetActorLocation(), TargetBot.GetActorLocation());
    if (HealingMeleeApproachMaxRange > 0.f && DistanceSq > FMath::Square(HealingMeleeApproachMaxRange))
    {
        return false;
    }

    return HealerBot.IsFlyingBot() || HealerBot.IsWalkingBot();
}

ABotCharacter* ABotAIController::FindHealingTarget(const ABotCharacter* HealerBot) const
{
    if (!IsValid(HealerBot))
    {
        return nullptr;
    }

    UWorld* World = HealerBot->GetWorld();
    if (!World)
    {
        return nullptr;
    }

    ABotCharacter* BestTarget = nullptr;
    float BestHealthPercent = TNumericLimits<float>::Max();
    float BestDistanceSq = TNumericLimits<float>::Max();

    for (TActorIterator<ABotCharacter> It(World); It; ++It)
    {
        ABotCharacter* Candidate = *It;
        if (!IsValidHealingTarget(HealerBot, Candidate))
        {
            continue;
        }

        const float CandidateHealthPercent = Candidate->GetHealthPercent();
        const float CandidateDistanceSq = FVector::DistSquared(
            HealerBot->GetActorLocation(),
            Candidate->GetActorLocation());
        if (!BestTarget ||
            CandidateHealthPercent < BestHealthPercent ||
            (FMath::IsNearlyEqual(CandidateHealthPercent, BestHealthPercent) &&
                CandidateDistanceSq < BestDistanceSq))
        {
            BestTarget = Candidate;
            BestHealthPercent = CandidateHealthPercent;
            BestDistanceSq = CandidateDistanceSq;
        }
    }

    return BestTarget;
}

void ABotAIController::UpdateHealingTarget(float DeltaSeconds)
{
    ABotCharacter* HealerBot = Cast<ABotCharacter>(GetPawn());
    UBlackboardComponent* BlackboardComponent = GetBlackboardComponent();
    if (!BlackboardComponent)
    {
        return;
    }

    if (!bEnableAllyHealing ||
        !HealerBot ||
        (!HealerBot->IsCloseDamageHealing() && !HealerBot->IsProjectileHealing()))
    {
        CurrentHealingTarget = nullptr;
        bCurrentHealingTargetUsesMelee = false;
        return;
    }

    UWorld* World = GetWorld();
    if (!World)
    {
        ClearHealingTarget(*BlackboardComponent);
        return;
    }

    const float CurrentTime = World->GetTimeSeconds();
    ABotCharacter* TargetBot = CurrentHealingTarget.Get();
    const bool bShouldRefreshTarget =
        !IsValidHealingTarget(HealerBot, TargetBot) ||
        CurrentTime - LastHealingTargetSearchTime >= HealingTargetRefreshSeconds;
    if (bShouldRefreshTarget)
    {
        TargetBot = FindHealingTarget(HealerBot);
        CurrentHealingTarget = TargetBot;
        LastHealingTargetSearchTime = CurrentTime;
    }

    if (!TargetBot)
    {
        ClearHealingTarget(*BlackboardComponent);
        return;
    }

    const FVector TargetLocation = TargetBot->GetActorLocation();
    BlackboardComponent->SetValueAsBool(HasDetectedTargetKey, true);
    BlackboardComponent->SetValueAsBool(HasLineOfSightKey, true);
    BlackboardComponent->SetValueAsObject(TargetActorKey, TargetBot);
    BlackboardComponent->SetValueAsVector(TargetLocationKey, TargetLocation);
    BlackboardComponent->SetValueAsVector(DetectedTargetLocationKey, TargetLocation);
    SetFocalPoint(TargetLocation, EAIFocusPriority::Gameplay);

    bCurrentHealingTargetUsesMelee = HealerBot->IsCloseDamageHealing() && ShouldUseMeleeHealing(*HealerBot, *TargetBot);
    if (bCurrentHealingTargetUsesMelee)
    {
        float CloseDamageRange = HealerBot->GetCloseDamageRange();
        if (const UCapsuleComponent* TargetCapsule = TargetBot->GetCapsuleComponent())
        {
            CloseDamageRange += TargetCapsule->GetScaledCapsuleRadius();
        }

        if (CloseDamageRange > 0.f &&
            FVector::DistSquared(HealerBot->GetActorLocation(), TargetLocation) <= FMath::Square(CloseDamageRange))
        {
            HealerBot->TryApplyCloseDamageToActor(TargetBot);
        }

        MoveToActor(
            TargetBot,
            FMath::Max(0.f, HealingMeleeMoveAcceptanceRadius),
            false,
            true,
            true,
            nullptr,
            true);
        return;
    }

    if (HealerBot->IsProjectileHealing() &&
        (!HealerBot->IsCloseDamageHealing() || bUseRangedHealingWhenMeleeUnreachable))
    {
        HealerBot->TryThrowProjectileAtActor(TargetBot);

        const float ProjectileRange = HealerBot->GetProjectileAttackRange();
        const float DistanceSq = FVector::DistSquared(HealerBot->GetActorLocation(), TargetLocation);
        if (ProjectileRange > 0.f && DistanceSq > FMath::Square(ProjectileRange * 0.85f))
        {
            MoveToActor(
                TargetBot,
                FMath::Max(100.f, ProjectileRange * 0.75f),
                true,
                true,
                true,
                nullptr,
                true);
        }
        else
        {
            StopMovement();
        }
        return;
    }

    ClearHealingBlackboardTarget(*BlackboardComponent);
    ClearFocus(EAIFocusPriority::Gameplay);
}

void ABotAIController::ClearHealingTarget(UBlackboardComponent& BlackboardComponent)
{
    CurrentHealingTarget = nullptr;
    bCurrentHealingTargetUsesMelee = false;
    StopMovement();
    ClearFocus(EAIFocusPriority::Gameplay);
    ClearHealingBlackboardTarget(BlackboardComponent);
}

void ABotAIController::ClearHealingBlackboardTarget(UBlackboardComponent& BlackboardComponent)
{
    BlackboardComponent.SetValueAsBool(HasDetectedTargetKey, false);
    BlackboardComponent.SetValueAsBool(HasLineOfSightKey, false);
    BlackboardComponent.ClearValue(TargetActorKey);
    BlackboardComponent.ClearValue(TargetLocationKey);
    BlackboardComponent.ClearValue(DetectedTargetLocationKey);
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

    if (IsBlackboardKeyBlockingRandomMovement(ActiveRandomMovementSettings.RandomFlightBlockedByKey))
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

    if (ToDestination.SizeSquared() <= FMath::Square(ActiveRandomMovementSettings.FlightAcceptanceRadius))
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

    if (IsBlackboardKeyBlockingRandomMovement(ActiveRandomMovementSettings.RandomWalkingBlockedByKey))
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

    if (ToDestination.SizeSquared() <= FMath::Square(ActiveRandomMovementSettings.WalkAcceptanceRadius))
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
        ActiveRandomMovementSettings.WalkAcceptanceRadius,
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
    const float Distance = FMath::Sqrt(FMath::FRand()) * ActiveRandomMovementSettings.FlightRadius;
    const FVector Offset(
        FMath::Cos(Angle) * Distance,
        FMath::Sin(Angle) * Distance,
        ActiveRandomMovementSettings.PreferredFlightHeight + FMath::FRandRange(
            -ActiveRandomMovementSettings.FlightHeightVariance,
            ActiveRandomMovementSettings.FlightHeightVariance));

    CurrentFlightDestination = FlightOrigin + Offset;
    DestinationHoldTimeRemaining = FMath::FRandRange(
        ActiveRandomMovementSettings.DestinationHoldMinSeconds,
        ActiveRandomMovementSettings.DestinationHoldMaxSeconds);
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
            if (!NavSystem->ProjectPointToNavigation(
                WalkOrigin,
                ProjectedOrigin,
                ActiveRandomMovementSettings.WalkingNavProjectionExtent))
            {
                return;
            }

            WalkOrigin = ProjectedOrigin.Location;

            FNavLocation NavLocation;
            if (NavSystem->GetRandomReachablePointInRadius(
                WalkOrigin,
                ActiveRandomMovementSettings.WalkRadius,
                NavLocation))
            {
                CurrentWalkDestination = NavLocation.Location;
                WalkDestinationHoldTimeRemaining = FMath::FRandRange(
                    ActiveRandomMovementSettings.WalkDestinationHoldMinSeconds,
                    ActiveRandomMovementSettings.WalkDestinationHoldMaxSeconds);
                bHasWalkDestination = true;
                return;
            }
        }
    }
}
