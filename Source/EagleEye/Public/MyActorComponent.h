// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "Math/MathFwd.h"
#include "OpenCVHeaders.h"
#include <string>
#include <fstream>
#include <vector>
#include "ImageUtils.h" // For FImageUtils
#include "Misc/FileHelper.h" // For File I/O
#include "Serialization/Archive.h"
#include "HAL/PlatformFileManager.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Texture2D.h"
#include "RenderUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "Camera/CameraComponent.h"
#include "Blueprint/UserWidget.h"
#include "Components/Image.h"
#include "Engine/Texture2D.h"
#include "RenderUtils.h"
#include "DetectionResult.h"
#include "DetectionInferenceTypes.h"
#include "RHIGPUReadback.h"
#include <atomic>
#include <thread>
#if WITH_TENSORRT
#include <cuda_runtime_api.h>
#endif
#if WITH_ONNXRUNTIME
#include <onnxruntime_cxx_api.h>
#endif
#include "HAL/CriticalSection.h"
#include "Containers/Queue.h"
#include "Containers/UnrealString.h"
#include "MyActorComponent.generated.h"

#if WITH_TENSORRT
namespace nvinfer1
{
    class IRuntime;
    class ICudaEngine;
    class IExecutionContext;
}
#endif

class USceneCaptureComponent2D;

UCLASS(BlueprintType, Blueprintable, ClassGroup=(Detection), meta=(BlueprintSpawnableComponent, DisplayName="Detection Component"), Config=Game)
class EAGLEEYE_API UMyActorComponent : public USceneComponent
{
	GENERATED_BODY()
    
public:	
	// Sets default values for this component's properties
	UMyActorComponent();
	
    UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category="Detection")
    TArray<FDetectionResult> LastFrameDetections;

    UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category="Detection")
    int32 LastFrameSourceWidth = 0;

    UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category="Detection")
    int32 LastFrameSourceHeight = 0;

    UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category="Detection")
    int32 LastFrameSequence = 0;

    UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category="Detection")
    float LastFrameTimeSeconds = -FLT_MAX;

    UPROPERTY(Transient)
    TArray<float> LastFrameSceneDepth;

    UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category="Detection|Depth")
    int32 LastFrameSceneDepthWidth = 0;

    UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category="Detection|Depth")
    int32 LastFrameSceneDepthHeight = 0;

    UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category="Detection|Depth")
    float LastFrameSceneDepthTimeSeconds = -FLT_MAX;

    UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category="Detection|FOV Metrics")
    int32 FovDetectionTruePositiveCount = 0;

    UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category="Detection|FOV Metrics")
    int32 FovDetectionTrueNegativeCount = 0;

    UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category="Detection|FOV Metrics")
    int32 FovDetectionFalsePositiveCount = 0;

    UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category="Detection|FOV Metrics")
    int32 FovDetectionFalseNegativeCount = 0;

    UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category="Detection|FOV Metrics")
    int32 FovDetectionUnavailableSampleCount = 0;

    UFUNCTION()
    void TickCapture();

    void SetUseOwnerCameraCapture(bool bEnabled);
    void SetCaptureFPS(float InCaptureFPS);
    void SetCaptureResolution(int32 InWidth, int32 InHeight);
    void SetMaxOwnerCameraCaptureDistance(float InDistance) { MaxOwnerCameraCaptureDistance = InDistance; }
    void SetHideOwnerFromOwnerCameraCapture(bool bEnabled) { bHideOwnerFromOwnerCameraCapture = bEnabled; }
    void SetRecordOwnerCameraCaptureVideo(bool bEnabled);
    void SetRecordOwnerCameraWhenDetectionSkipped(bool bEnabled) { bRecordOwnerCameraWhenDetectionSkipped = bEnabled; }
    void SetOwnerCameraVideoOutputPath(const FString& InOutputPath) { OwnerCameraVideoOutputPath = InOutputPath; }
    void SetOwnerCameraVideoEncoderPath(const FString& InEncoderPath) { OwnerCameraVideoEncoderPath = InEncoderPath.IsEmpty() ? TEXT("ffmpeg") : InEncoderPath; }
    void SetMaxQueuedOwnerCameraVideoFrames(int32 InMaxQueuedFrames) { MaxQueuedOwnerCameraVideoFrames = FMath::Clamp(InMaxQueuedFrames, 1, 120); }
    void SetApplyOwnerCameraVideoGammaCorrection(bool bEnabled) { bApplyOwnerCameraVideoGammaCorrection = bEnabled; }
    void SetUseSharedVisionModel(bool bEnabled) { bUseSharedVisionModel = bEnabled; }
    void SetSharedVisionModelHost(bool bEnabled) { bSharedVisionModelHost = bEnabled; }
    void SetUseFovOnlyPersonDetection(bool bEnabled) { bUseFovOnlyPersonDetection = bEnabled; }
    void SetLogFovDetectionMetrics(bool bEnabled);
    bool ShouldLogFrameTimings() const;
    void ApplyRuntimeDetectionSettingsFromConfig(bool bReloadModel = true);

    UFUNCTION(BlueprintCallable, Category="Detection|Model")
    bool EnsureModelLoaded();

    UFUNCTION(BlueprintPure, Category="Detection|Model")
    bool IsModelLoaded() const { return bIsModelLoaded; }

    TArray<FDetectionResult> ProcessSharedVisionFrame(
        const TArray<FColor>& Bitmap,
        int32 Width,
        int32 Height,
        double* OutInferenceMs = nullptr);

    void RequestInferenceShutdown();
    int32 BeginSharedVisionRequest();
    void ConsumeSharedVisionResult(TArray<FDetectionResult>&& Detections, int32 SourceWidth, int32 SourceHeight, int32 RequestSerial);

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
    void StartWorker();
    void StopWorker();
    void RequestOnnxRuntimeInferenceTerminate();
    void ApplyProjectDetectionSettings();
    void ApplySharedVisionFrameSourceSettings();
    void ResolveConfiguredRuntimePaths();
    void CaptureAndEnqueue(bool bSubmitDetection = true);
    void CopyResultsFromWorker(); // game-thread copy from shared buffer
    bool CaptureViewportToPixels(TArray<FColor>& OutPixels, int32& OutWidth, int32& OutHeight);
    bool CaptureSceneToPixels(TArray<FColor>& OutPixels, int32& OutWidth, int32& OutHeight);
    bool CaptureOwnerSceneDepthToBuffer(TArray<float>& OutDepth, int32& OutWidth, int32& OutHeight);
    void PublishOwnerSceneDepth(TArray<float>&& Depth, int32 Width, int32 Height);
    void ClearLastFrameSceneDepth();
    void ResetOwnerCameraReadback();
    void AdvanceOwnerCameraReadbackGeneration();
    bool PollAsyncOwnerCameraReadback(TArray<FColor>& OutPixels, int32& OutWidth, int32& OutHeight);
    bool EnqueueAsyncOwnerCameraReadback();
    bool EnsureOwnerCameraCapture();
    void ReleaseOwnerCameraCaptureResources();
    bool ShouldSkipOwnerCameraCapture() const;
    bool ShouldRecordOwnerCameraVideo() const;
    void PublishFovOnlyDetectionFrame();
    bool BuildFovOnlyPersonDetection(
        TArray<FDetectionResult>& OutDetections,
        int32& OutSourceWidth,
        int32& OutSourceHeight,
        float& OutDistance,
        float& OutHorizontalAngleDegrees,
        float& OutVerticalAngleDegrees);
    bool EvaluatePlayerInOwnerCameraFov(
        int32 SourceWidth,
        int32 SourceHeight,
        FVector2D& OutPixel,
        float& OutDistance,
        float& OutHorizontalAngleDegrees,
        float& OutVerticalAngleDegrees,
        bool& bOutHasEvaluation) const;
    bool HasPersonDetection(const TArray<FDetectionResult>& Detections) const;
    void InitFovDetectionMetricsTable();
    void AppendFovDetectionMetricsTableRow(
        const TCHAR* SourceLabel,
        const TCHAR* Status,
        const TCHAR* Outcome,
        bool bExpectedInFov,
        bool bActualPersonDetected,
        float Distance,
        float HorizontalAngleDegrees,
        float VerticalAngleDegrees,
        const FVector2D& ExpectedPixel,
        int32 SourceWidth,
        int32 SourceHeight,
        int32 DetectionCount);
    void LogFovDetectionMetricSample(
        const TCHAR* SourceLabel,
        const TArray<FDetectionResult>& Detections,
        int32 SourceWidth,
        int32 SourceHeight);
    void ConfigureOwnerCaptureVisibility(AActor* Owner);
    void RecordOwnerCameraVideoFrame(const TArray<FColor>& Pixels, int32 Width, int32 Height);
    bool EnsureOwnerCameraVideoWriter(int32 Width, int32 Height);
    void StartOwnerCameraVideoWorker();
    void OwnerCameraVideoWorkerLoop();
    void WriteOwnerCameraVideoFrameSync(const TArray<FColor>& Pixels, int32 Width, int32 Height);
    void CloseOwnerCameraVideoWriter(bool bWaitForEncoder = true);
    void FinalizeOwnerCameraVideoWriter(bool bWaitForEncoder = true);
    FString ResolveOwnerCameraVideoPath() const;
    void ClearPublishedResults();


    // --- Frame container passed to worker ---
    struct FFrameData
    {
        TArray<FColor> Pixels;
        int32 Width = 0;
        int32 Height = 0;
    };

    float CaptureFPS = 60.0f;

    int32 OnnxInputSize = 640;

    bool bUseOwnerCameraCapture = false;

    int32 CaptureWidth = 640;

    int32 CaptureHeight = 640;

    float MaxOwnerCameraCaptureDistance = 0.f;

    bool bUseAsyncOwnerCameraReadback = true;

    bool bHideOwnerFromOwnerCameraCapture = true;

    bool bRecordOwnerCameraCaptureVideo = false;

    bool bRecordOwnerCameraWhenDetectionSkipped = true;

    FString OwnerCameraVideoOutputPath;

    FString OwnerCameraVideoEncoderPath = TEXT("ffmpeg");

    int32 MaxQueuedOwnerCameraVideoFrames = 2;

    bool bApplyOwnerCameraVideoGammaCorrection = true;

    bool bUseSharedVisionModel = false;

    bool bSharedVisionModelHost = false;

    bool bUseFovOnlyPersonDetection = false;

    UPROPERTY(EditAnywhere, Category="Detection|Metrics")
    bool bLogFovDetectionMetrics = false;

    UPROPERTY(EditAnywhere, Category="Detection|Metrics")
    bool bResetFovDetectionMetricsTableOnBeginPlay = true;

    UPROPERTY(EditAnywhere, Category="Detection|Metrics")
    FString FovDetectionMetricsCsvPath;

    float FovOnlyDetectionMaxDistance = 8000.f;

    int32 FovOnlySyntheticBoxSizePixels = 64;

    UPROPERTY(EditAnywhere, Category="Detection|Logging")
    bool bLogFrameTimings = false;

    bool bStaggerInitialCapture = true;

    float MaxInitialCaptureDelay = 0.75f;

    UPROPERTY()
    USceneCaptureComponent2D* OwnerSceneCapture = nullptr;

    UPROPERTY()
    UTextureRenderTarget2D* OwnerCaptureRenderTarget = nullptr;

    UPROPERTY()
    USceneCaptureComponent2D* OwnerDepthSceneCapture = nullptr;

    UPROPERTY()
    UTextureRenderTarget2D* OwnerDepthRenderTarget = nullptr;

    TSharedPtr<FRHIGPUTextureReadback, ESPMode::ThreadSafe> PendingOwnerCameraReadback;
    TWeakObjectPtr<UWorld> PendingOwnerCameraReadbackWorld;
    uint64 OwnerCameraReadbackGeneration = 0;
    uint64 PendingOwnerCameraReadbackGeneration = 0;
    uint64 PendingOwnerCameraReadbackFrameNumber = 0;
    int32 PendingOwnerCameraReadbackWidth = 0;
    int32 PendingOwnerCameraReadbackHeight = 0;
    double PendingOwnerCameraReadbackSubmitTimeSeconds = 0.0;
    double OwnerCameraReadbackWarmupEndSeconds = 0.0;
    TArray<float> PendingOwnerCameraReadbackDepth;
    int32 PendingOwnerCameraReadbackDepthWidth = 0;
    int32 PendingOwnerCameraReadbackDepthHeight = 0;
    FString ResolvedFovDetectionMetricsCsvPath;
    bool bFovDetectionMetricsTableInitialized = false;

    struct FOwnerCameraVideoFrame
    {
        TArray<FColor> Pixels;
        int32 Width = 0;
        int32 Height = 0;
    };

    TQueue<TSharedPtr<FOwnerCameraVideoFrame>, EQueueMode::Mpsc> OwnerCameraVideoQueue;
    std::thread* OwnerCameraVideoThread = nullptr;
    std::atomic<bool> bOwnerCameraVideoWorkerRunning{false};
    std::atomic<bool> bOwnerCameraVideoFastShutdown{false};
    std::atomic<bool> bOwnerCameraVideoFinalizing{false};
    std::atomic<int32> PendingOwnerCameraVideoFrames{0};
    int32 DroppedOwnerCameraVideoFrames = 0;

    FString ActiveOwnerCameraVideoPath;
    FProcHandle OwnerCameraVideoProcess;
    void* OwnerCameraVideoPipeWrite = nullptr;
    int32 OwnerCameraVideoWidth = 0;
    int32 OwnerCameraVideoHeight = 0;
    int32 OwnerCameraVideoFPS = 0;
    int32 OwnerCameraVideoFrameCount = 0;
    bool bOwnerCameraVideoWriterFailed = false;

    // Single-latest-frame storage
    FCriticalSection FrameMutex;
    TSharedPtr<FFrameData> LatestFrame;

    // Shared results (worker writes, game thread reads)
    FCriticalSection ResultsMutex;
    TArray<FDetectionResult> ResultsShared; // guarded by ResultsMutex
    int32 ResultsSourceWidth = 0;  // guarded by ResultsMutex
    int32 ResultsSourceHeight = 0; // guarded by ResultsMutex
    int32 ResultsSequence = 0;    // guarded by ResultsMutex
    float ResultsTimeSeconds = -FLT_MAX; // guarded by ResultsMutex

    // Worker control
    FTimerHandle TimerHandle_Capture;
    std::thread* WorkerThread = nullptr;
    std::atomic<bool> bWorkerRunning{false};
    std::atomic<bool> bIsEndingPlay{false};
    std::atomic<bool> bInferenceShutdownRequested{false};
    std::atomic<int32> SharedVisionRequestSerial{0};

    // YOLO state (paths only; net will be created inside worker)
    std::string WeightsPath;
    std::string NamesPath;
    FCriticalSection InferenceMutex;

    FString ModelPathOverride;

    FString NamesPathOverride;

    EDetectionInferenceBackend InferenceBackend = EDetectionInferenceBackend::Auto;

    EOnnxRuntimeExecutionProvider OnnxRuntimeExecutionProvider = EOnnxRuntimeExecutionProvider::Auto;

    bool bOpenCVDNNPreferCUDA = true;

    bool bOpenCVDNNUseFP16 = true;

    std::vector<std::string> ClassNames;

    std::atomic<bool> bIsModelLoaded{false};
    bool bIsOnnxModel = false;
    int32 ModelInputWidth = 640;
    int32 ModelInputHeight = 640;

    UPROPERTY(EditAnywhere, Category="Detection|Logging")
    bool bLogPerf = false;

    float ConfidenceThreshold = 0.25f;

    float NmsThreshold = 0.45f;

    bool bUseLetterbox = true;

    int32 LetterboxValue = 114;

    UPROPERTY(EditAnywhere, Config, Category="Detection|Benchmark")
    bool bRecordFrameTimes = false;

    UPROPERTY(EditAnywhere, Config, Category="Detection|Benchmark")
    FString FrameTimeCsvPath;

    UPROPERTY(EditAnywhere, Config, Category="Detection|Benchmark")
    bool bResetFrameTimeLogOnBeginPlay = true;

    UPROPERTY(EditAnywhere, Config, Category="Detection|Benchmark", meta=(ClampMin="1", ClampMax="600"))
    int32 FrameTimeFlushInterval = 60;

    bool LoadYOLO();
    void ReleaseTensorRT();
    void ReleaseOnnxRuntime();
    TArray<EDetectionInferenceBackend> BuildAutomaticInferenceBackendCandidates(const FString& ModelPathUE) const;
    EDetectionInferenceBackend DetectAutomaticInferenceBackend(const FString& ModelPathUE) const;
    EDetectionInferenceBackend ResolveEffectiveInferenceBackend(const FString& ModelPathUE) const;
    EOnnxRuntimeExecutionProvider ResolveEffectiveOnnxRuntimeProvider() const;
    FString ResolveModelPathForBackend(const FString& ModelPathUE, EDetectionInferenceBackend Backend) const;
    bool LoadOnnxRuntime(const FString& ModelPathUE, bool bForceCPUProvider = false);
    bool ShouldForceOnnxRuntimeCpuAfterGpuCrash() const;
    void MarkOnnxRuntimeGpuSessionActive() const;
    void ClearOnnxRuntimeGpuSessionActive() const;
    bool RunTensorRT();
    bool RunTensorRTInference_BG(const cv::Mat& ModelInputBGR);
    bool RunOnnxRuntimeInference_BG(const cv::Mat& ModelInputBGR);
    bool RunOpenCVDNNInference_BG(const cv::Mat& ModelInputBGR);
    void InitFrameTimingLog();
    void AppendFrameTimingLogLine(int32 Sequence, int32 Width, int32 Height, int32 DetectionCount, double TotalMs, double InferMs);
    void FlushFrameTimingLog(bool bForce);
    FString GetFrameTimingRuntimeFileSuffix() const;
    FString GetFrameTimingRuntimeLabel() const;
    FString ResolveFrameTimeCsvPathForCurrentRuntime() const;
    void ResetInferenceOutputState();

    EDetectionInferenceBackend EffectiveInferenceBackend = EDetectionInferenceBackend::OpenCVDNN;

#if WITH_TENSORRT
    nvinfer1::IRuntime* TrtRuntime = nullptr;
    nvinfer1::ICudaEngine* TrtEngine = nullptr;
    nvinfer1::IExecutionContext* TrtContext = nullptr;
    cudaStream_t TrtStream = nullptr;
    void* TrtInputDevice = nullptr;
    void* TrtOutputDevice = nullptr;
#endif
    TArray<float> TrtInputHost;
    TArray<float> TrtOutputHost;
    int32 TrtInputElements = 0;
    int32 TrtOutputElements = 0;
    int32 TrtOutputChannels = 0;
    int32 TrtOutputDetections = 0;
#if WITH_ONNXRUNTIME
    TUniquePtr<Ort::Env> OnnxRuntimeEnv;
    TUniquePtr<Ort::SessionOptions> OnnxRuntimeSessionOptions;
    TUniquePtr<Ort::Session> OnnxRuntimeSession;
    TUniquePtr<Ort::RunOptions> OnnxRuntimeRunOptions;
    FCriticalSection OnnxRuntimeRunOptionsMutex;
    TArray<float> OnnxRuntimeInputHost;
    TArray<int64_t> OnnxRuntimeInputShape;
    std::string OnnxRuntimeInputName;
    TArray<std::string> OnnxRuntimeOutputNames;
    EOnnxRuntimeExecutionProvider EffectiveOnnxRuntimeExecutionProvider = EOnnxRuntimeExecutionProvider::CPU;
    bool bOnnxRuntimeUsingGpuProvider = false;
#endif
    cv::dnn::Net OpenCVDnnNet;
    FString ResolvedFrameTimeCsvPath;
    TArray<FString> FrameTimeLogBuffer;
    bool bFrameTimingLogInitialized = false;
    FCriticalSection FrameTimeLogMutex;
public:
    void get_class_names();
    // void get_yolo_net(const std::string& Cfg, const std::string& Weights); // keep if you want, but we’ll init in worker
    // IMPORTANT: Make sure ProcessWithOpenCV touches no Unreal APIs.
    // It should accept raw pixels/size and return detections only.
    TArray<FDetectionResult> ProcessWithOpenCV_BG(const TArray<FColor>& Bitmap, int32 Width, int32 Height, double* OutInferenceMs = nullptr);
//     void TestOpenCV();

//     UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category="Detection")
//     TArray<FDetectionResult> LastFrameDetections;

//     UFUNCTION(BlueprintCallable, Category="Detection")
//     TArray<FDetectionResult> GetLastFrameDetections() const { return LastFrameDetections; }

//     // Example: call this from your YOLO update loop
//     void UpdateDetections(const TArray<FDetectionResult>& NewDetections);

// protected:
// 	// Called when the game starts
// 	virtual void BeginPlay() override;

// public:
//     UFUNCTION(BlueprintCallable, Category="Camera Processing")
//     void CaptureAndProcess(int threshold);
    
//     UPROPERTY(BlueprintReadOnly)
//     UTexture2D* OverlayText;

//     FTimerHandle TimerHandle_Capture;
// private:
//     UPROPERTY()
//     USceneCaptureComponent2D* SceneCaptureComponent;

//     UPROPERTY()
//     UTextureRenderTarget2D* RenderTarget;

//     cv::dnn::Net yolo_net;

//     std::vector<std::string> class_names;

//     void InitializeSceneCapture();
    
//     void get_class_names(const std::string& FilePath);

//     void get_yolo_net(const std::string& FilePath1, const std::string& FilePath2);

//     TArray<FDetectionResult> ProcessWithOpenCV(const TArray<FColor>& Bitmap, int32 Width, int32 Height, int threshold, cv::dnn::Net& net);


// public:	
// 	// Called every frame
// 	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
    
//     UFUNCTION()
// 	void TickCapture();

};
