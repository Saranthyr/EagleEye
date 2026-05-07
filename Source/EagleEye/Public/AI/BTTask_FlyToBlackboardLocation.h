#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "BTTask_FlyToBlackboardLocation.generated.h"

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

    UPROPERTY(EditAnywhere, Category="Blackboard")
    FBlackboardKeySelector TargetLocationKey;

    UPROPERTY(EditAnywhere, Category="Flight", meta=(ClampMin="0.0"))
    float AcceptanceRadius = 220.f;

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

    UPROPERTY(EditAnywhere, Category="Rotation")
    bool bRotateTowardTarget = true;

    UPROPERTY(EditAnywhere, Category="Rotation")
    bool bUsePitchRotation = true;

    UPROPERTY(EditAnywhere, Category="Rotation", meta=(ClampMin="0.0"))
    float RotationInterpSpeed = 8.f;

    UPROPERTY(EditAnywhere, Category="Rotation")
    FRotator RotationOffset = FRotator::ZeroRotator;

    UPROPERTY(EditAnywhere, Category="Debug")
    bool bDrawDebug = false;
};
