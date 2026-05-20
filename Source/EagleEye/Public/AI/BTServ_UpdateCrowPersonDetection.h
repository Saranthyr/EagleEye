#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTService.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "BTServ_UpdateCrowPersonDetection.generated.h"

UCLASS()
class EAGLEEYE_API UBTServ_UpdateCrowPersonDetection : public UBTService
{
    GENERATED_BODY()

public:
    UBTServ_UpdateCrowPersonDetection();

protected:
    virtual uint16 GetInstanceMemorySize() const override;

    virtual void TickNode(
        UBehaviorTreeComponent& OwnerComp,
        uint8* NodeMemory,
        float DeltaSeconds) override;

    UPROPERTY(EditAnywhere, Category="Blackboard")
    FBlackboardKeySelector HasPersonKey;

    UPROPERTY(EditAnywhere, Category="Blackboard")
    FBlackboardKeySelector DetectedPersonLocationKey;

    UPROPERTY(EditAnywhere, Category="Blackboard")
    FBlackboardKeySelector DetectionConfidenceKey;

    UPROPERTY(EditAnywhere, Category="Detection", meta=(ClampMin="100.0"))
    float TraceDistance = 10000.f;

    UPROPERTY(EditAnywhere, Category="Detection", meta=(ClampMin="100.0"))
    float FallbackTargetDistance = 2500.f;

    UPROPERTY(EditAnywhere, Category="Detection", meta=(ClampMin="0.0"))
    float PlayerRaySnapRadius = 350.f;

    UPROPERTY(EditAnywhere, Category="Detection", meta=(ClampMin="0.0"))
    float LosePersonAfterSeconds = 1.25f;

    UPROPERTY(EditAnywhere, Category="Detection", meta=(ClampMin="0.0"))
    float MaxDetectionFrameAgeSeconds = 0.4f;

    UPROPERTY(EditAnywhere, Category="Detection", meta=(ClampMin="0.0", ClampMax="1.0"))
    float MinAcceptedConfidence = 0.35f;

    UPROPERTY(EditAnywhere, Category="Detection", meta=(ClampMin="1", ClampMax="10"))
    int32 RequiredConsecutiveDetections = 2;

    UPROPERTY(EditAnywhere, Category="Detection", meta=(ClampMin="0", ClampMax="10"))
    int32 MaxConsecutiveDetectionMisses = 2;

    UPROPERTY(EditAnywhere, Category="Detection", meta=(ClampMin="0.0"))
    float TargetSmoothingSpeed = 8.0f;

    UPROPERTY(EditAnywhere, Category="Detection")
    bool bAlwaysFollowPlayerPawn = false;

    UPROPERTY(EditAnywhere, Category="Detection")
    bool bPreferPlayerPawnLocation = true;

    UPROPERTY(EditAnywhere, Category="Detection", meta=(EditCondition="bPreferPlayerPawnLocation"))
    bool bRequireRaySnapForPlayerPawnLocation = true;

    UPROPERTY(EditAnywhere, Category="Detection")
    FVector TargetLocationOffset = FVector(0.f, 0.f, 150.f);

    UPROPERTY(EditAnywhere, Category="Shared Detection")
    bool bPublishDetectionsToFlock = true;

    UPROPERTY(EditAnywhere, Category="Shared Detection")
    bool bUseFlockSharedDetections = true;

    UPROPERTY(EditAnywhere, Category="Shared Detection", meta=(ClampMin="0.0"))
    float SharedDetectionMaxAgeSeconds = 1.5f;

    UPROPERTY(EditAnywhere, Category="Shared Detection", meta=(ClampMin="0.0"))
    float SharedDetectionMaxReporterDistance = 6000.f;

    UPROPERTY(EditAnywhere, Category="Debug")
    bool bDrawDebug = false;

    UPROPERTY(EditAnywhere, Category="Debug")
    bool bLogDebug = false;
};
