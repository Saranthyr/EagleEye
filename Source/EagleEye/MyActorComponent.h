// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "Math/MathFwd.h"
#include "PreOpenCVHeaders.h"
#include "OpenCVHelper.h"
#include <ThirdParty/OpenCV/include/opencv2/imgproc.hpp>
#include <ThirdParty/OpenCV/include/opencv2/highgui/highgui.hpp>
#include <ThirdParty/OpenCV/include/opencv2/core.hpp>
#include <ThirdParty/OpenCV/include/opencv2/imgcodecs.hpp>
#include <ThirdParty/OpenCV/include/opencv2/dnn.hpp>
#include <opencv2/dnn/dnn.hpp>
#include "PostOpenCVHeaders.h"
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
#include "Components/SceneCaptureComponent2D.h"
#include "Camera/CameraComponent.h"
#include "Blueprint/UserWidget.h"
#include "Components/Image.h"
#include "Engine/Texture2D.h"
#include "RenderUtils.h"
#include "DetectionResult.h"
#include <atomic>
#include "MyActorComponent.generated.h"

USTRUCT()
struct FPersistentDetection
{
    GENERATED_BODY()

    FDetectionResult Det;
    int FramesSinceLastSeen = 0;
};


UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class EAGLEEYE_API UMyActorComponent : public USceneComponent
{
	GENERATED_BODY()
    
public:	
	// Sets default values for this component's properties
	UMyActorComponent();
	
    UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category="Detection")
    TArray<FDetectionResult> LastFrameDetections;

    void InitializeScreenCapture();

    UFUNCTION()
    void TickCapture();

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    // ---- Capture & Worker orchestration ----
    void StartWorker();
    void StopWorker();
    void CaptureAndEnqueue(int Threshold);
    void CopyResultsFromWorker(); // game-thread copy from shared buffer

    // --- Frame container passed to worker ---
    struct FFrameData
    {
        TArray<FColor> Pixels;
        int32 Width = 0;
        int32 Height = 0;
        int32 Threshold = 0;
    };

    // Single-latest-frame queue (replace old frame with new)
    TQueue<TSharedPtr<FFrameData>, EQueueMode::Mpsc> FrameQueue;

    // Shared results (worker writes, game thread reads)
    FCriticalSection ResultsMutex;
    TArray<FDetectionResult> ResultsShared; // guarded by ResultsMutex

    // Worker control
    FTimerHandle TimerHandle_Capture;
    FTimerHandle TimerHandle_PullResults;
    TFuture<void> WorkerFuture;
    std::atomic<bool> bWorkerRunning{false};

    // YOLO state (paths only; net will be created inside worker)
    std::string WeightsPath;
    std::string CfgPath;
    std::string NamesPath;

    cv::dnn::Net YoloNet;
    std::vector<std::string> ClassNames;

    bool bIsModelLoaded = false;

    bool LoadYOLO();
    TArray<FPersistentDetection> PersistentDetections;
    int32 MaxMissedFrames = 3; 
public:
    void get_class_names();
    // void get_yolo_net(const std::string& Cfg, const std::string& Weights); // keep if you want, but we’ll init in worker
    // IMPORTANT: Make sure ProcessWithOpenCV touches no Unreal APIs.
    // It should accept raw pixels/size and return detections only.
    TArray<FDetectionResult> ProcessWithOpenCV_BG(const TArray<FColor>& Bitmap, int32 Width, int32 Height, int Threshold, cv::dnn::Net& Net);
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
