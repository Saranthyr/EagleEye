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

    UPROPERTY(EditAnywhere, Config, Category="Model", meta=(DisplayName="Model", GetOptions="GetAvailableModelNames"))
    FString ModelPathOverride = TEXT("yolo26x");

    UFUNCTION()
    TArray<FString> GetAvailableModelNames() const;

    UPROPERTY(EditAnywhere, Config, Category="Model")
    FString NamesPathOverride = TEXT("coco.names");

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

    UPROPERTY(EditAnywhere, Config, Category="Model Host")
    bool bPreloadModelHostOnBeginPlay = true;

    UPROPERTY(EditAnywhere, Config, Category="Model Host", meta=(ClampMin="1"))
    int32 MaxActiveModelUsers = 2;

    UPROPERTY(EditAnywhere, Config, Category="Model Host", meta=(ClampMin="1", ClampMax="120"))
    int32 MaxQueuedModelFrames = 2;

    UPROPERTY(EditAnywhere, Config, Category="Frame Source", meta=(ClampMin="1.0", ClampMax="120.0"))
    float FrameSourceFPS = 8.f;

    UPROPERTY(EditAnywhere, Config, Category="Frame Source", meta=(ClampMin="160", ClampMax="3840"))
    int32 FrameSourceWidth = 640;

    UPROPERTY(EditAnywhere, Config, Category="Frame Source", meta=(ClampMin="160", ClampMax="2160"))
    int32 FrameSourceHeight = 640;

    UPROPERTY(EditAnywhere, Config, Category="Frame Source", meta=(ClampMin="0.0"))
    float MaxModelUserDistanceToPlayer = 8000.f;

    UPROPERTY(EditAnywhere, Config, Category="Frame Source")
    bool bStaggerInitialFrameSourceCapture = true;

    UPROPERTY(EditAnywhere, Config, Category="Frame Source", meta=(ClampMin="0.0", ClampMax="5.0", EditCondition="bStaggerInitialFrameSourceCapture"))
    float MaxInitialFrameSourceCaptureDelay = 0.75f;

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
