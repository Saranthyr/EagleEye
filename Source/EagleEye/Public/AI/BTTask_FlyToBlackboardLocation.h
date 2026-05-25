#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "BTTask_FlyToBlackboardLocation.generated.h"

UENUM(BlueprintType)
enum class EBTTargetMovementMode : uint8
{
    ApproachTarget UMETA(DisplayName="Approach Target"),
    MaintainDistance UMETA(DisplayName="Maintain Distance")
};

UENUM(BlueprintType)
enum class EBTLocomotionModeSource : uint8
{
    BotLocomotion UMETA(DisplayName="Bot Locomotion"),
    ForceWalking UMETA(DisplayName="Force Walking"),
    ForceFlying UMETA(DisplayName="Force Flying"),
    BlackboardBool UMETA(DisplayName="Blackboard Bool")
};

UENUM(BlueprintType)
enum class EBTTargetMovementModeSource : uint8
{
    TaskSettings UMETA(DisplayName="Task Settings"),
    BlackboardBool UMETA(DisplayName="Blackboard Bool")
};

UCLASS()
class EAGLEEYE_API UBTTask_FlyToBlackboardLocation : public UBTTaskNode
{
    GENERATED_BODY()

public:
    UBTTask_FlyToBlackboardLocation();

protected:
    virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
    virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
    virtual void OnTaskFinished(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTNodeResult::Type TaskResult) override;
    virtual uint16 GetInstanceMemorySize() const override;

    UPROPERTY(EditAnywhere, Category="Blackboard")
    FBlackboardKeySelector TargetLocationKey;

    UPROPERTY(EditAnywhere, Category="Blackboard|Overrides")
    EBTLocomotionModeSource LocomotionModeSource = EBTLocomotionModeSource::BotLocomotion;

    UPROPERTY(EditAnywhere, Category="Blackboard|Overrides", meta=(EditCondition="LocomotionModeSource==EBTLocomotionModeSource::BlackboardBool", EditConditionHides))
    FBlackboardKeySelector UseFlyingMovementKey;

    UPROPERTY(EditAnywhere, Category="Blackboard|Overrides")
    EBTTargetMovementModeSource TargetMovementModeSource = EBTTargetMovementModeSource::TaskSettings;

    UPROPERTY(EditAnywhere, Category="Blackboard|Overrides", meta=(EditCondition="TargetMovementModeSource==EBTTargetMovementModeSource::BlackboardBool", EditConditionHides))
    FBlackboardKeySelector MaintainDistanceKey;

    UPROPERTY(EditAnywhere, Category="Flight", meta=(ClampMin="0.0"))
    float AcceptanceRadius = 220.f;

    UPROPERTY(EditAnywhere, Category="Target Movement", meta=(EditCondition="TargetMovementModeSource==EBTTargetMovementModeSource::TaskSettings", EditConditionHides))
    EBTTargetMovementMode TargetMovementMode = EBTTargetMovementMode::ApproachTarget;

    UPROPERTY(EditAnywhere, Category="Target Movement", meta=(ClampMin="0.0", EditCondition="TargetMovementMode==EBTTargetMovementMode::MaintainDistance || TargetMovementModeSource==EBTTargetMovementModeSource::BlackboardBool", EditConditionHides))
    float DesiredTargetDistance = 1200.f;

    UPROPERTY(EditAnywhere, Category="Target Movement", meta=(ClampMin="0.0", EditCondition="TargetMovementMode==EBTTargetMovementMode::MaintainDistance || TargetMovementModeSource==EBTTargetMovementModeSource::BlackboardBool", EditConditionHides))
    float DistanceTolerance = 150.f;

    UPROPERTY(EditAnywhere, Category="Flight", meta=(ClampMin="0.0", ClampMax="1.0"))
    float MovementScale = 1.f;

    UPROPERTY(EditAnywhere, Category="Flight", meta=(ClampMin="1.0"))
    float MinFlySpeed = 300.f;

    UPROPERTY(EditAnywhere, Category="Flight")
    bool bMoveActorDirectly = false;

    UPROPERTY(EditAnywhere, Category="Flight")
    bool bSweepDirectMove = false;

    UPROPERTY(EditAnywhere, Category="Flight")
    bool bUpdateFocus = false;

    UPROPERTY(EditAnywhere, Category="Navigation")
    bool bUseNavMeshForWalking = true;

    UPROPERTY(EditAnywhere, Category="Navigation", meta=(ClampMin="0.0", EditCondition="bUseNavMeshForWalking"))
    float WalkingMoveAcceptanceRadius = 120.f;

    UPROPERTY(EditAnywhere, Category="Navigation", meta=(EditCondition="bUseNavMeshForWalking"))
    FVector NavProjectionExtent = FVector(300.f, 300.f, 500.f);

    UPROPERTY(EditAnywhere, Category="Navigation", meta=(EditCondition="bUseNavMeshForWalking"))
    bool bAllowPartialNavPath = true;

    UPROPERTY(EditAnywhere, Category="Navigation", meta=(EditCondition="bUseNavMeshForWalking"))
    bool bFallbackToDirectWalkingWhenNavMoveFails = true;

    UPROPERTY(EditAnywhere, Category="Rotation")
    bool bRotateTowardTarget = true;

    UPROPERTY(EditAnywhere, Category="Rotation")
    bool bRotateWalkingTowardMoveDirection = true;

    UPROPERTY(EditAnywhere, Category="Rotation", meta=(EditCondition="bRotateWalkingTowardMoveDirection"))
    bool bFaceTargetWhileRetreating = true;

    UPROPERTY(EditAnywhere, Category="Rotation")
    bool bUsePitchRotation = true;

    UPROPERTY(EditAnywhere, Category="Rotation")
    bool bAlignCameraForwardToTarget = true;

    UPROPERTY(EditAnywhere, Category="Rotation", meta=(ClampMin="0.0"))
    float RotationInterpSpeed = 8.f;

    UPROPERTY(EditAnywhere, Category="Rotation")
    FRotator RotationOffset = FRotator::ZeroRotator;

    UPROPERTY(EditAnywhere, Category="Debug")
    bool bDrawDebug = false;

    UPROPERTY(EditAnywhere, Category="Debug")
    bool bLogMovementDecision = true;

    UPROPERTY(EditAnywhere, Category="Debug", meta=(ClampMin="0.05", EditCondition="bLogMovementDecision"))
    float MovementDecisionLogInterval = 1.0f;

private:
    bool ResolveUseFlyingMovement(const APawn& ControlledPawn, const UBlackboardComponent& Blackboard) const;
    bool ResolveMaintainDistance(const UBlackboardComponent& Blackboard) const;
};
