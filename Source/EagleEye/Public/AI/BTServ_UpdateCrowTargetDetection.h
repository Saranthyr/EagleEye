#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTService.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "BTServ_UpdateCrowTargetDetection.generated.h"

UCLASS()
class EAGLEEYE_API UBTServ_UpdateCrowTargetDetection : public UBTService
{
    GENERATED_BODY()

public:
    UBTServ_UpdateCrowTargetDetection();

protected:
    virtual uint16 GetInstanceMemorySize() const override;

    virtual void TickNode(
        UBehaviorTreeComponent& OwnerComp,
        uint8* NodeMemory,
        float DeltaSeconds) override;

    UPROPERTY(EditAnywhere, Category="Blackboard", meta=(DisplayName="Has Target Key"))
    FBlackboardKeySelector HasTargetKey;

    UPROPERTY(EditAnywhere, Category="Blackboard", meta=(DisplayName="Detected Target Location Key"))
    FBlackboardKeySelector DetectedTargetLocationKey;

    UPROPERTY(EditAnywhere, Category="Blackboard")
    FBlackboardKeySelector DetectionConfidenceKey;

    UPROPERTY(EditAnywhere, Category="Blackboard")
    FBlackboardKeySelector DetectedClassIdKey;

    UPROPERTY(EditAnywhere, Category="Blackboard")
    FBlackboardKeySelector DetectedClassLabelKey;

    UPROPERTY(EditAnywhere, Category="Detection|Target Classes")
    TArray<int32> ActionableClassIds;

    UPROPERTY(EditAnywhere, Category="Detection|Target Classes")
    TArray<FName> ActionableClassLabels;

    UPROPERTY(EditAnywhere, Category="Detection", meta=(ClampMin="100.0"))
    float TraceDistance = 10000.f;

    UPROPERTY(EditAnywhere, Category="Detection", meta=(ClampMin="0.0", DisplayName="Lose Target After Seconds"))
    float LoseTargetAfterSeconds = 1.25f;

    UPROPERTY(EditAnywhere, Category="Detection", meta=(ClampMin="0.0"))
    float SameTargetLocationThreshold = 450.f;

    UPROPERTY(EditAnywhere, Category="Detection", meta=(ClampMin="0.0"))
    float NewTargetConfirmationSeconds = 0.8f;

    UPROPERTY(EditAnywhere, Category="Detection", meta=(ClampMin="0.0"))
    float PendingTargetStabilityThreshold = 250.f;

    UPROPERTY(EditAnywhere, Category="Detection", meta=(ClampMin="0.0"))
    float MaxDetectionFrameAgeSeconds = 0.4f;

    UPROPERTY(EditAnywhere, Category="Detection", meta=(ClampMin="0.0", ClampMax="1.0"))
    float MinAcceptedConfidence = 0.35f;

    UPROPERTY(EditAnywhere, Category="Detection", meta=(ClampMin="1", ClampMax="10"))
    int32 RequiredConsecutiveDetections = 2;

    UPROPERTY(EditAnywhere, Category="Detection|Target Validation")
    bool bRequirePlayerResolvedTarget = false;

    UPROPERTY(EditAnywhere, Category="Detection|Target Validation", meta=(ClampMin="0.0", EditCondition="bRequirePlayerResolvedTarget"))
    float PlayerTargetMatchRadius = 450.f;

    UPROPERTY(EditAnywhere, Category="YOLO Tracking")
    bool bEnableYoloBoxTracking = true;

    UPROPERTY(EditAnywhere, Category="YOLO Tracking", meta=(ClampMin="0.0", ClampMax="1.0", EditCondition="bEnableYoloBoxTracking"))
    float TrackMatchMinIoU = 0.1f;

    UPROPERTY(EditAnywhere, Category="YOLO Tracking", meta=(ClampMin="0.0", ClampMax="1.0", EditCondition="bEnableYoloBoxTracking"))
    float TrackMatchMaxCenterDistance = 0.08f;

    UPROPERTY(EditAnywhere, Category="YOLO Tracking", meta=(ClampMin="0.0", EditCondition="bEnableYoloBoxTracking"))
    float TrackSwitchConfidenceMargin = 0.25f;

    UPROPERTY(EditAnywhere, Category="YOLO Tracking", meta=(ClampMin="0.0", EditCondition="bEnableYoloBoxTracking"))
    float TrackBoxSmoothingSpeed = 12.0f;

    UPROPERTY(EditAnywhere, Category="YOLO Tracking", meta=(ClampMin="-0.5", ClampMax="0.75"))
    float TargetRayBoxVerticalBias = 0.4f;

    UPROPERTY(EditAnywhere, Category="Detection|Scene Depth", meta=(ClampMin="1"))
    int32 MinSceneDepthClusterSamples = 12;

    UPROPERTY(EditAnywhere, Category="Detection|Scene Depth", meta=(ClampMin="0.0", ClampMax="1.0"))
    float MinSceneDepthClusterSampleRatio = 0.08f;

    UPROPERTY(EditAnywhere, Category="YOLO Tracking", meta=(ClampMin="0.0"))
    float MaxTrackedTargetJumpDistance = 350.0f;

    UPROPERTY(EditAnywhere, Category="YOLO Tracking")
    bool bPredictTrackedTargetMotion = true;

    UPROPERTY(EditAnywhere, Category="YOLO Tracking", meta=(ClampMin="0.0", EditCondition="bPredictTrackedTargetMotion"))
    float TargetPredictionLeadSeconds = 0.15f;

    UPROPERTY(EditAnywhere, Category="YOLO Tracking", meta=(ClampMin="0.0", EditCondition="bPredictTrackedTargetMotion"))
    float MaxTargetPredictionDistance = 150.0f;

    UPROPERTY(EditAnywhere, Category="YOLO Tracking", meta=(ClampMin="0.0", EditCondition="bPredictTrackedTargetMotion"))
    float TargetVelocitySmoothingSpeed = 6.0f;

    UPROPERTY(EditAnywhere, Category="Detection")
    FVector TargetLocationOffset = FVector(0.f, 0.f, 150.f);

    UPROPERTY(EditAnywhere, Category="Debug")
    bool bDrawDebug = false;

    UPROPERTY(EditAnywhere, Category="Debug")
    bool bLogDebug = true;
};
