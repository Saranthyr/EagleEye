#pragma once

#include "CoreMinimal.h"
#include "DetectionInferenceTypes.h"
#include "Engine/DeveloperSettings.h"
#include "EagleEyeDetectionSettings.generated.h"

UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="Detection"))
class EAGLEEYE_API UEagleEyeDetectionSettings : public UDeveloperSettings
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, Config, Category="Model")
    EDetectionInferenceBackend InferenceBackend = EDetectionInferenceBackend::Auto;

    UPROPERTY(EditAnywhere, Config, Category="Model")
    EOnnxRuntimeExecutionProvider OnnxRuntimeExecutionProvider = EOnnxRuntimeExecutionProvider::Auto;

    UPROPERTY(EditAnywhere, Config, Category="Model")
    FString ModelPathOverride = TEXT("yolo26x.plan");

    UPROPERTY(EditAnywhere, Config, Category="Model")
    FString NamesPathOverride = TEXT("coco.names");

    UPROPERTY(EditAnywhere, Config, Category="Model")
    FString DarknetCfgPathOverride = TEXT("yolov7.cfg");

    UPROPERTY(EditAnywhere, Config, Category="Model")
    bool bOpenCVDNNPreferCUDA = true;

    UPROPERTY(EditAnywhere, Config, Category="Model")
    bool bOpenCVDNNUseFP16 = true;

    UPROPERTY(EditAnywhere, Config, Category="Preprocess", meta=(ClampMin="160", ClampMax="1280"))
    int32 OnnxInputSize = 640;

    UPROPERTY(EditAnywhere, Config, Category="Preprocess")
    bool bUseLetterbox = true;

    UPROPERTY(EditAnywhere, Config, Category="Preprocess", meta=(ClampMin="0", ClampMax="255"))
    int32 LetterboxValue = 114;

    UPROPERTY(EditAnywhere, Config, Category="Postprocess", meta=(ClampMin="0.01", ClampMax="0.99"))
    float ConfidenceThreshold = 0.25f;

    UPROPERTY(EditAnywhere, Config, Category="Postprocess", meta=(ClampMin="0.01", ClampMax="0.99"))
    float NmsThreshold = 0.45f;

    UPROPERTY(EditAnywhere, Config, Category="Shared Detection", meta=(ClampMin="0"))
    int32 MaxActiveSharedDetectionBots = 2;

    UPROPERTY(EditAnywhere, Config, Category="Shared Detection", meta=(ClampMin="0.0"))
    float SharedDetectionMaxBotDistance = 8000.f;

    UPROPERTY(EditAnywhere, Config, Category="Benchmark")
    bool bRecordFrameTimes = false;

    UPROPERTY(EditAnywhere, Config, Category="Benchmark")
    FString FrameTimeCsvPath;

    UPROPERTY(EditAnywhere, Config, Category="Benchmark")
    bool bResetFrameTimeLogOnBeginPlay = true;

    UPROPERTY(EditAnywhere, Config, Category="Benchmark", meta=(ClampMin="1", ClampMax="600"))
    int32 FrameTimeFlushInterval = 60;

    UPROPERTY(EditAnywhere, Config, Category="Logging")
    bool bEnablePathfindingDecisionLogs = true;

    UPROPERTY(EditAnywhere, Config, Category="Logging")
    bool bEnablePathfindingObjectLogs = false;

    UPROPERTY(EditAnywhere, Config, Category="Logging")
    bool bEnableDetectionDebugLogs = true;

    UPROPERTY(EditAnywhere, Config, Category="Logging")
    bool bEnableDetectionPerformanceLogs = false;

    UPROPERTY(EditAnywhere, Config, Category="Logging")
    bool bEnableDetectionMetricLogs = false;
};
