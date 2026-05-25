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

UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent), Config=Game)
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

    void SetUseOwnerCameraCapture(bool bEnabled) { bUseOwnerCameraCapture = bEnabled; }
    void SetCaptureFPS(float InCaptureFPS);
    void SetCaptureResolution(int32 InWidth, int32 InHeight);
    void SetMaxOwnerCameraCaptureDistance(float InDistance) { MaxOwnerCameraCaptureDistance = InDistance; }
    void SetMaxActiveOwnerCameraCaptures(int32 InMaxActive) { MaxActiveOwnerCameraCaptures = InMaxActive; }
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
    void SetLogFovDetectionMetrics(bool bEnabled) { bLogFovDetectionMetrics = bEnabled; }
    bool ShouldLogFrameTimings() const { return bLogFrameTimings; }

    TArray<FDetectionResult> ProcessSharedVisionFrame(
        const TArray<FColor>& Bitmap,
        int32 Width,
        int32 Height,
        double* OutInferenceMs = nullptr);

    void ConsumeSharedVisionResult(TArray<FDetectionResult>&& Detections, int32 SourceWidth, int32 SourceHeight);

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
    void StartWorker();
    void StopWorker();
    void ApplyProjectDetectionSettings();
    void CaptureAndEnqueue(bool bSubmitDetection = true);
    void CopyResultsFromWorker(); // game-thread copy from shared buffer
    bool CaptureViewportToPixels(TArray<FColor>& OutPixels, int32& OutWidth, int32& OutHeight);
    bool CaptureSceneToPixels(TArray<FColor>& OutPixels, int32& OutWidth, int32& OutHeight);
    bool PollAsyncOwnerCameraReadback(TArray<FColor>& OutPixels, int32& OutWidth, int32& OutHeight);
    bool EnqueueAsyncOwnerCameraReadback();
    bool EnsureOwnerCameraCapture();
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

    UPROPERTY(EditAnywhere, Category="Detection|Performance", meta=(ClampMin="1.0", ClampMax="120.0"))
    float CaptureFPS = 60.0f;

    UPROPERTY(EditAnywhere, Category="Detection|Performance", meta=(ClampMin="160", ClampMax="1280"))
    int32 OnnxInputSize = 640;

    UPROPERTY(EditAnywhere, Category="Detection|Capture")
    bool bUseOwnerCameraCapture = false;

    UPROPERTY(EditAnywhere, Category="Detection|Capture", meta=(ClampMin="160", ClampMax="1920"))
    int32 CaptureWidth = 640;

    UPROPERTY(EditAnywhere, Category="Detection|Capture", meta=(ClampMin="160", ClampMax="1080"))
    int32 CaptureHeight = 640;

    UPROPERTY(EditAnywhere, Category="Detection|Capture", meta=(ClampMin="0.0"))
    float MaxOwnerCameraCaptureDistance = 0.f;

    UPROPERTY(EditAnywhere, Category="Detection|Capture", meta=(ClampMin="0"))
    int32 MaxActiveOwnerCameraCaptures = 0;

    UPROPERTY(EditAnywhere, Category="Detection|Capture")
    bool bUseAsyncOwnerCameraReadback = true;

    UPROPERTY(EditAnywhere, Category="Detection|Capture")
    bool bHideOwnerFromOwnerCameraCapture = true;

    UPROPERTY(EditAnywhere, Category="Detection|Recording")
    bool bRecordOwnerCameraCaptureVideo = false;

    UPROPERTY(EditAnywhere, Category="Detection|Recording")
    bool bRecordOwnerCameraWhenDetectionSkipped = true;

    UPROPERTY(EditAnywhere, Category="Detection|Recording")
    FString OwnerCameraVideoOutputPath;

    UPROPERTY(EditAnywhere, Category="Detection|Recording")
    FString OwnerCameraVideoEncoderPath = TEXT("ffmpeg");

    UPROPERTY(EditAnywhere, Category="Detection|Recording", meta=(ClampMin="1", ClampMax="120"))
    int32 MaxQueuedOwnerCameraVideoFrames = 2;

    UPROPERTY(EditAnywhere, Category="Detection|Recording")
    bool bApplyOwnerCameraVideoGammaCorrection = true;

    UPROPERTY(EditAnywhere, Category="Detection|Shared Model")
    bool bUseSharedVisionModel = false;

    UPROPERTY(EditAnywhere, Category="Detection|Shared Model")
    bool bSharedVisionModelHost = false;

    UPROPERTY(EditAnywhere, Category="Detection|FOV")
    bool bUseFovOnlyPersonDetection = false;

    UPROPERTY(EditAnywhere, Category="Detection|FOV")
    bool bLogFovDetectionMetrics = false;

    UPROPERTY(EditAnywhere, Category="Detection|FOV", meta=(ClampMin="0.0"))
    float FovOnlyDetectionMaxDistance = 8000.f;

    UPROPERTY(EditAnywhere, Category="Detection|FOV", meta=(ClampMin="1", ClampMax="512"))
    int32 FovOnlySyntheticBoxSizePixels = 64;

    UPROPERTY(EditAnywhere, Category="Detection|Performance")
    bool bLogFrameTimings = false;

    UPROPERTY(EditAnywhere, Category="Detection|Performance")
    bool bStaggerInitialCapture = true;

    UPROPERTY(EditAnywhere, Category="Detection|Performance", meta=(ClampMin="0.0", ClampMax="5.0"))
    float MaxInitialCaptureDelay = 0.75f;

    UPROPERTY()
    USceneCaptureComponent2D* OwnerSceneCapture = nullptr;

    UPROPERTY()
    UTextureRenderTarget2D* OwnerCaptureRenderTarget = nullptr;

    TUniquePtr<FRHIGPUTextureReadback> PendingOwnerCameraReadback;
    int32 PendingOwnerCameraReadbackWidth = 0;
    int32 PendingOwnerCameraReadbackHeight = 0;
    double PendingOwnerCameraReadbackSubmitTimeSeconds = 0.0;

    struct FOwnerCameraVideoFrame
    {
        TArray<FColor> Pixels;
        int32 Width = 0;
        int32 Height = 0;
    };

    TQueue<TSharedPtr<FOwnerCameraVideoFrame>, EQueueMode::Mpsc> OwnerCameraVideoQueue;
    TFuture<void> OwnerCameraVideoFuture;
    std::atomic<bool> bOwnerCameraVideoWorkerRunning{false};
    std::atomic<bool> bOwnerCameraVideoFastShutdown{false};
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
    TFuture<void> WorkerFuture;
    std::atomic<bool> bWorkerRunning{false};

    // YOLO state (paths only; net will be created inside worker)
    std::string WeightsPath;
    std::string CfgPath;
    std::string NamesPath;

    UPROPERTY(EditAnywhere, Config, Category="Detection|Model")
    FString ModelPathOverride;

    UPROPERTY(EditAnywhere, Config, Category="Detection|Model")
    FString DarknetCfgPathOverride;

    UPROPERTY(EditAnywhere, Config, Category="Detection|Model")
    FString NamesPathOverride;

    UPROPERTY(EditAnywhere, Config, Category="Detection|Model")
    EDetectionInferenceBackend InferenceBackend = EDetectionInferenceBackend::Auto;

    UPROPERTY(EditAnywhere, Config, Category="Detection|Model")
    EOnnxRuntimeExecutionProvider OnnxRuntimeExecutionProvider = EOnnxRuntimeExecutionProvider::Auto;

    UPROPERTY(EditAnywhere, Config, Category="Detection|Model")
    bool bOpenCVDNNPreferCUDA = true;

    UPROPERTY(EditAnywhere, Config, Category="Detection|Model")
    bool bOpenCVDNNUseFP16 = true;

    std::vector<std::string> ClassNames;

    bool bIsModelLoaded = false;
    bool bIsOnnxModel = false;
    int32 ModelInputWidth = 416;
    int32 ModelInputHeight = 416;

    UPROPERTY(EditAnywhere, Category="Detection|Performance")
    bool bLogPerf = false;

    UPROPERTY(EditAnywhere, Category="Detection|Postprocess", meta=(ClampMin="0.01", ClampMax="0.99"))
    float ConfidenceThreshold = 0.25f;

    UPROPERTY(EditAnywhere, Category="Detection|Postprocess", meta=(ClampMin="0.01", ClampMax="0.99"))
    float NmsThreshold = 0.45f;

    UPROPERTY(EditAnywhere, Category="Detection|Preprocess")
    bool bUseLetterbox = true;

    UPROPERTY(EditAnywhere, Category="Detection|Preprocess", meta=(ClampMin="0", ClampMax="255"))
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
    EDetectionInferenceBackend DetectAutomaticInferenceBackend(const FString& ModelPathUE) const;
    EDetectionInferenceBackend ResolveEffectiveInferenceBackend(const FString& ModelPathUE) const;
    FString ResolveModelPathForBackend(const FString& ModelPathUE, EDetectionInferenceBackend Backend) const;
    bool LoadOnnxRuntime(const FString& ModelPathUE);
    bool RunTensorRT();
    bool RunTensorRTInference_BG(const cv::Mat& ModelInputBGR);
    bool RunOnnxRuntimeInference_BG(const cv::Mat& ModelInputBGR);
    bool RunOpenCVDNNInference_BG(const cv::Mat& ModelInputBGR);
    void InitFrameTimingLog();
    void AppendFrameTimingLogLine(int32 Sequence, int32 Width, int32 Height, int32 DetectionCount, double TotalMs, double InferMs);
    void FlushFrameTimingLog(bool bForce);
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
    TArray<float> OnnxRuntimeInputHost;
    TArray<int64_t> OnnxRuntimeInputShape;
    std::string OnnxRuntimeInputName;
    TArray<std::string> OnnxRuntimeOutputNames;
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
