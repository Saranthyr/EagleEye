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
#include "RHIGPUReadback.h"
#include <atomic>
#include <cuda_runtime_api.h>
#include "HAL/CriticalSection.h"
#include "Containers/UnrealString.h"
#include "MyActorComponent.generated.h"

namespace nvinfer1
{
    class IRuntime;
    class ICudaEngine;
    class IExecutionContext;
}

class USceneCaptureComponent2D;

UENUM(BlueprintType)
enum class EDetectionInferenceBackend : uint8
{
    TensorRT UMETA(DisplayName="TensorRT"),
    OpenCVDNN UMETA(DisplayName="OpenCV DNN")
};

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

    UFUNCTION()
    void TickCapture();

    void SetUseOwnerCameraCapture(bool bEnabled) { bUseOwnerCameraCapture = bEnabled; }
    void SetCaptureFPS(float InCaptureFPS) { CaptureFPS = InCaptureFPS; }
    void SetCaptureResolution(int32 InWidth, int32 InHeight);
    void SetMaxOwnerCameraCaptureDistance(float InDistance) { MaxOwnerCameraCaptureDistance = InDistance; }
    void SetMaxActiveOwnerCameraCaptures(int32 InMaxActive) { MaxActiveOwnerCameraCaptures = InMaxActive; }
    void SetUseSharedVisionModel(bool bEnabled) { bUseSharedVisionModel = bEnabled; }
    bool ShouldLogFrameTimings() const { return bLogFrameTimings; }

    TArray<FDetectionResult> ProcessSharedVisionFrame(
        const TArray<FColor>& Bitmap,
        int32 Width,
        int32 Height,
        int32 Threshold,
        double* OutInferenceMs = nullptr);

    void ConsumeSharedVisionResult(TArray<FDetectionResult>&& Detections, int32 SourceWidth, int32 SourceHeight);

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
    void StartWorker();
    void StopWorker();
    void CaptureAndEnqueue(int Threshold);
    void CopyResultsFromWorker(); // game-thread copy from shared buffer
    bool CaptureViewportToPixels(TArray<FColor>& OutPixels, int32& OutWidth, int32& OutHeight);
    bool CaptureSceneToPixels(TArray<FColor>& OutPixels, int32& OutWidth, int32& OutHeight);
    bool PollAsyncOwnerCameraReadback(TArray<FColor>& OutPixels, int32& OutWidth, int32& OutHeight);
    bool EnqueueAsyncOwnerCameraReadback();
    bool EnsureOwnerCameraCapture();
    bool ShouldSkipOwnerCameraCapture() const;
    void ClearPublishedResults();


    // --- Frame container passed to worker ---
    struct FFrameData
    {
        TArray<FColor> Pixels;
        int32 Width = 0;
        int32 Height = 0;
        int32 Threshold = 0;
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

    UPROPERTY(EditAnywhere, Category="Detection|Shared Model")
    bool bUseSharedVisionModel = false;

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

    // Single-latest-frame storage
    FCriticalSection FrameMutex;
    TSharedPtr<FFrameData> LatestFrame;

    // Shared results (worker writes, game thread reads)
    FCriticalSection ResultsMutex;
    TArray<FDetectionResult> ResultsShared; // guarded by ResultsMutex
    int32 ResultsSourceWidth = 0;  // guarded by ResultsMutex
    int32 ResultsSourceHeight = 0; // guarded by ResultsMutex
    int32 ResultsSequence = 0;    // guarded by ResultsMutex

    // Worker control
    FTimerHandle TimerHandle_Capture;
    TFuture<void> WorkerFuture;
    std::atomic<bool> bWorkerRunning{false};

    // YOLO state (paths only; net will be created inside worker)
    std::string WeightsPath;
    std::string CfgPath;
    std::string NamesPath;

    UPROPERTY(EditAnywhere, Category="Detection|Model")
    FString ModelPathOverride;

    UPROPERTY(EditAnywhere, Category="Detection|Model")
    FString DarknetCfgPathOverride;

    UPROPERTY(EditAnywhere, Category="Detection|Model")
    FString NamesPathOverride;

    UPROPERTY(EditAnywhere, Config, Category="Detection|Model")
    EDetectionInferenceBackend InferenceBackend = EDetectionInferenceBackend::TensorRT;

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
    bool RunTensorRT();
    bool RunTensorRTInference_BG(const cv::Mat& ModelInputBGR);
    bool RunOpenCVDNNInference_BG(const cv::Mat& ModelInputBGR);
    void InitFrameTimingLog();
    void AppendFrameTimingLogLine(int32 Sequence, int32 Width, int32 Height, int32 DetectionCount, double TotalMs, double InferMs);
    void FlushFrameTimingLog(bool bForce);

    nvinfer1::IRuntime* TrtRuntime = nullptr;
    nvinfer1::ICudaEngine* TrtEngine = nullptr;
    nvinfer1::IExecutionContext* TrtContext = nullptr;
    cudaStream_t TrtStream = nullptr;
    void* TrtInputDevice = nullptr;
    void* TrtOutputDevice = nullptr;
    TArray<float> TrtInputHost;
    TArray<float> TrtOutputHost;
    int32 TrtInputElements = 0;
    int32 TrtOutputElements = 0;
    int32 TrtOutputChannels = 0;
    int32 TrtOutputDetections = 0;
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
    TArray<FDetectionResult> ProcessWithOpenCV_BG(const TArray<FColor>& Bitmap, int32 Width, int32 Height, int Threshold, double* OutInferenceMs = nullptr);
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
