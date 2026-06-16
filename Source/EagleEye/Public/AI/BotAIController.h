#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "AI/BotRandomMovementSettings.h"
#include "Perception/AIPerceptionTypes.h"
#include "BotAIController.generated.h"

class UAIPerceptionComponent;
class UAISenseConfig_Sight;
class UBehaviorTree;
class UBlackboardComponent;
class ABotCharacter;

UCLASS()
class EAGLEEYE_API ABotAIController : public AAIController
{
    GENERATED_BODY()

public:
    ABotAIController();

    virtual void OnPossess(APawn* InPawn) override;
    virtual void Tick(float DeltaSeconds) override;

    UFUNCTION(BlueprintCallable, Category="AI|Random Movement")
    void ApplyRandomMovementSettings(const FBotRandomMovementSettings& InSettings);

    UFUNCTION(BlueprintCallable, Category="AI|Random Movement")
    void PushRandomMovementBlock();

    UFUNCTION(BlueprintCallable, Category="AI|Random Movement")
    void PopRandomMovementBlock();

protected:
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AI")
    TObjectPtr<UAIPerceptionComponent> PerceptionComponentRef;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AI")
    TObjectPtr<UAISenseConfig_Sight> SightConfig;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="AI")
    TObjectPtr<UBehaviorTree> DefaultBehaviorTree;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Blackboard")
    FName TargetActorKey = TEXT("TargetActor");

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Blackboard")
    FName TargetLocationKey = TEXT("TargetLocation");

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Blackboard")
    FName HasLineOfSightKey = TEXT("HasLineOfSight");

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Blackboard", meta=(DisplayName="Has Detected Target Key"))
    FName HasDetectedTargetKey = TEXT("HasDetectedTarget");

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Blackboard", meta=(DisplayName="Detected Target Location Key"))
    FName DetectedTargetLocationKey = TEXT("DetectedTargetLocation");

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat")
    bool bFocusProjectileTarget = false;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Perception")
    bool bEnableAISightPerception = false;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Perception")
    float SightRadius = 1800.f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Perception")
    float LoseSightRadius = 2200.f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Perception")
    float PeripheralVisionAngleDegrees = 70.f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Perception")
    float MaxSightAge = 2.5f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Healing")
    bool bEnableAllyHealing = true;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Healing", meta=(ClampMin="0.0", ClampMax="1.0"))
    float HealingHealthPercentThreshold = 0.65f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Healing", meta=(ClampMin="0.0"))
    float HealingSearchRadius = 5000.f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Healing", meta=(ClampMin="0.0"))
    float HealingTargetRefreshSeconds = 0.25f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Healing", meta=(ClampMin="0.0"))
    float HealingMeleeApproachMaxRange = 3000.f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Healing", meta=(ClampMin="0.0"))
    float HealingMeleeMoveAcceptanceRadius = 55.f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Healing")
    bool bUseRangedHealingWhenMeleeUnreachable = true;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Healing")
    bool bRequireHealingLineOfSight = true;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Healing", meta=(ClampMin="0.0"))
    float HealingLineOfSightHeightOffset = 80.f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="AI|Random Movement")
    FBotRandomMovementSettings DefaultRandomMovementSettings;

private:
    UFUNCTION()
    void HandleTargetPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus);

    bool IsPlayerTarget(const AActor* Actor) const;
    bool ShouldUseRandomFlight(const APawn* ControlledPawn) const;
    bool ShouldUseRandomWalking(const APawn* ControlledPawn) const;
    bool IsBlackboardKeyBlockingRandomMovement(FName KeyName) const;
    bool IsValidHealingTarget(const ABotCharacter* HealerBot, const ABotCharacter* TargetBot) const;
    bool HasHealingLineOfSight(const ABotCharacter& HealerBot, const ABotCharacter& TargetBot) const;
    bool ShouldUseMeleeHealing(const ABotCharacter& HealerBot, const ABotCharacter& TargetBot) const;
    bool CanReachHealingTargetForMelee(const ABotCharacter& HealerBot, const ABotCharacter& TargetBot) const;
    ABotCharacter* FindHealingTarget(const ABotCharacter* HealerBot) const;
    void StartBehaviorTreeForPawn(APawn* InPawn);
    void UpdateBlackboardTarget(AActor* TargetActor, bool bHasLineOfSight);
    void UpdateHealingTarget(float DeltaSeconds);
    void ClearHealingTarget(UBlackboardComponent& BlackboardComponent);
    void ClearHealingBlackboardTarget(UBlackboardComponent& BlackboardComponent);
    void ResetRandomMovementState();
    bool IsRandomMovementBlocked() const;
    void UpdateRandomFlight(float DeltaSeconds);
    void UpdateRandomWalking(float DeltaSeconds);
    void PickRandomFlightDestination();
    void PickRandomWalkDestination();

    FBotRandomMovementSettings ActiveRandomMovementSettings;
    TWeakObjectPtr<AActor> CurrentTargetActor;
    FVector LastKnownTargetLocation = FVector::ZeroVector;
    FVector FlightOrigin = FVector::ZeroVector;
    FVector CurrentFlightDestination = FVector::ZeroVector;
    FVector WalkOrigin = FVector::ZeroVector;
    FVector CurrentWalkDestination = FVector::ZeroVector;
    TWeakObjectPtr<ABotCharacter> CurrentHealingTarget;
    float DestinationHoldTimeRemaining = 0.f;
    float WalkDestinationHoldTimeRemaining = 0.f;
    float LastHealingTargetSearchTime = -FLT_MAX;
    bool bCurrentHealingTargetUsesMelee = false;
    bool bHasLastKnownTargetLocation = false;
    bool bHasFlightOrigin = false;
    bool bHasFlightDestination = false;
    bool bHasWalkOrigin = false;
    bool bHasWalkDestination = false;
    bool bRandomWalkMoveActive = false;
    int32 RandomMovementBlockCount = 0;
};
