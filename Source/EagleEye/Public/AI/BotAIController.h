#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "Perception/AIPerceptionTypes.h"
#include "BotAIController.generated.h"

class UAIPerceptionComponent;
class UAISenseConfig_Sight;
class UBehaviorTree;

UCLASS()
class EAGLEEYE_API ABotAIController : public AAIController
{
    GENERATED_BODY()

public:
    ABotAIController();

    virtual void OnPossess(APawn* InPawn) override;
    virtual void Tick(float DeltaSeconds) override;

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

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Blackboard")
    FName HasDetectedPersonKey = TEXT("HasDetectedPerson");

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Blackboard")
    FName DetectedPersonLocationKey = TEXT("DetectedPersonLocation");

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

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Flight")
    bool bEnableRandomFlight = true;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Flight", meta=(ClampMin="0.0"))
    float FlightRadius = 1800.f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Flight", meta=(ClampMin="0.0"))
    float FlightAcceptanceRadius = 180.f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Flight")
    float PreferredFlightHeight = 500.f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Flight", meta=(ClampMin="0.0"))
    float FlightHeightVariance = 250.f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Flight", meta=(ClampMin="0.0"))
    float DestinationHoldMinSeconds = 0.5f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Flight", meta=(ClampMin="0.0"))
    float DestinationHoldMaxSeconds = 2.0f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Flight")
    FName RandomFlightBlockedByKey = TEXT("HasDetectedPerson");

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Walking")
    bool bEnableRandomWalking = true;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Walking", meta=(ClampMin="0.0"))
    float WalkRadius = 1200.f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Walking", meta=(ClampMin="0.0"))
    float WalkAcceptanceRadius = 120.f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Walking")
    FVector WalkingNavProjectionExtent = FVector(300.f, 300.f, 500.f);

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Walking", meta=(ClampMin="0.0"))
    float WalkDestinationHoldMinSeconds = 0.5f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Walking", meta=(ClampMin="0.0"))
    float WalkDestinationHoldMaxSeconds = 2.0f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Walking")
    FName RandomWalkingBlockedByKey = TEXT("HasDetectedPerson");

private:
    UFUNCTION()
    void HandleTargetPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus);

    bool IsPlayerTarget(const AActor* Actor) const;
    bool ShouldUseRandomFlight(const APawn* ControlledPawn) const;
    bool ShouldUseRandomWalking(const APawn* ControlledPawn) const;
    bool IsBlackboardKeyBlockingRandomMovement(FName KeyName) const;
    void StartBehaviorTreeForPawn(APawn* InPawn);
    void UpdateBlackboardTarget(AActor* TargetActor, bool bHasLineOfSight);
    void UpdateRandomFlight(float DeltaSeconds);
    void UpdateRandomWalking(float DeltaSeconds);
    void PickRandomFlightDestination();
    void PickRandomWalkDestination();
    void ClearChaseTarget();

    TWeakObjectPtr<AActor> CurrentTargetActor;
    FVector LastKnownTargetLocation = FVector::ZeroVector;
    FVector FlightOrigin = FVector::ZeroVector;
    FVector CurrentFlightDestination = FVector::ZeroVector;
    FVector WalkOrigin = FVector::ZeroVector;
    FVector CurrentWalkDestination = FVector::ZeroVector;
    float LastTargetSeenTime = -1.f;
    float DestinationHoldTimeRemaining = 0.f;
    float WalkDestinationHoldTimeRemaining = 0.f;
    bool bHasLastKnownTargetLocation = false;
    bool bHasFlightOrigin = false;
    bool bHasFlightDestination = false;
    bool bHasWalkOrigin = false;
    bool bHasWalkDestination = false;
};
