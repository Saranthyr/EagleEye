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

private:
    UFUNCTION()
    void HandleTargetPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus);

    bool IsPlayerTarget(const AActor* Actor) const;
    void StartBehaviorTreeForPawn(APawn* InPawn);
    void UpdateBlackboardTarget(AActor* TargetActor, bool bHasLineOfSight);
    void UpdateRandomFlight(float DeltaSeconds);
    void PickRandomFlightDestination();
    void ClearChaseTarget();

    TWeakObjectPtr<AActor> CurrentTargetActor;
    FVector LastKnownTargetLocation = FVector::ZeroVector;
    FVector FlightOrigin = FVector::ZeroVector;
    FVector CurrentFlightDestination = FVector::ZeroVector;
    float LastTargetSeenTime = -1.f;
    float DestinationHoldTimeRemaining = 0.f;
    bool bHasLastKnownTargetLocation = false;
    bool bHasFlightOrigin = false;
    bool bHasFlightDestination = false;
};
