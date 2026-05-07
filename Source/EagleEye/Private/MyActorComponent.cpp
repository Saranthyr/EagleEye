// Fill out your copyright notice in the Description page of Project Settings.

#include "MyActorComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "AI/CrowDetectionShareSubsystem.h"
#include "AI/CrowVisionSubsystem.h"
#include "Components/SceneCaptureComponent2D.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformProcess.h"
#include "Async/Async.h"
#include "RHI.h"
#include "Misc/ScopeLock.h"
#include "Misc/Paths.h"
#include "Misc/ConfigCacheIni.h"
#include "HAL/FileManager.h"
#include "Math/UnrealMathUtility.h"
#include "RenderingThread.h"
#include "ScreenCaptureComponent.h"
#include <algorithm>
#include <NvInfer.h>
#include <NvInferVersion.h>

namespace
{
    class FTrtLogger : public nvinfer1::ILogger
    {
    public:
        void log(Severity severity, const char* msg) noexcept override
        {
            if (severity > Severity::kWARNING)
            {
                return;
            }
            const TCHAR* Prefix = (severity == Severity::kERROR || severity == Severity::kINTERNAL_ERROR)
                ? TEXT("TensorRT Error")
                : TEXT("TensorRT");
            UE_LOG(LogTemp, Warning, TEXT("%s: %s"), Prefix, *FString(UTF8_TO_TCHAR(msg)));
        }
    };

    FTrtLogger GTrtLogger;
    constexpr const char* kTrtInputName = "images";
    constexpr const char* kTrtOutputName = "output0";

    template <typename TObject>
    void DestroyTrtObject(TObject*& Obj)
    {
        if (!Obj)
        {
            return;
        }
#if NV_TENSORRT_MAJOR >= 10
        delete Obj;
#else
        Obj->destroy();
#endif
        Obj = nullptr;
    }

    int64 CalcTrtVolume(const nvinfer1::Dims& Dims)
    {
        int64 Volume = 1;
        for (int i = 0; i < Dims.nbDims; ++i)
        {
            if (Dims.d[i] <= 0)
            {
                return -1;
            }
            Volume *= Dims.d[i];
        }
        return Volume;
    }

    FString TrtDimsToString(const nvinfer1::Dims& Dims)
    {
        FString Out = TEXT("[");
        for (int i = 0; i < Dims.nbDims; ++i)
        {
            if (i > 0)
            {
                Out += TEXT(", ");
            }
            Out += FString::FromInt(Dims.d[i]);
        }
        Out += TEXT("]");
        return Out;
    }

    float CalcIoU(const cv::Rect& A, const cv::Rect& B)
    {
        const int x1 = std::max(A.x, B.x);
        const int y1 = std::max(A.y, B.y);
        const int x2 = std::min(A.x + A.width, B.x + B.width);
        const int y2 = std::min(A.y + A.height, B.y + B.height);
        const int w = std::max(0, x2 - x1);
        const int h = std::max(0, y2 - y1);
        const float inter = static_cast<float>(w * h);
        const float unionArea = static_cast<float>(A.area() + B.area()) - inter;
        return unionArea > 0.0f ? (inter / unionArea) : 0.0f;
    }

    void RectToCorners(const cv::Rect& Rect, TArray<FVector2D>& OutCorners)
    {
        OutCorners.Reset();
        OutCorners.Add(FVector2D(Rect.x, Rect.y));
        OutCorners.Add(FVector2D(Rect.x + Rect.width, Rect.y));
        OutCorners.Add(FVector2D(Rect.x + Rect.width, Rect.y + Rect.height));
        OutCorners.Add(FVector2D(Rect.x, Rect.y + Rect.height));
    }

    void ApplyNms(const std::vector<cv::Rect>& Boxes, const std::vector<float>& Scores, float ScoreThreshold, float NmsThreshold, std::vector<int>& Indices)
    {
        Indices.clear();
        if (Boxes.empty() || Scores.empty())
        {
            return;
        }

        std::vector<int> Order;
        Order.reserve(Scores.size());
        for (int i = 0; i < static_cast<int>(Scores.size()); ++i)
        {
            if (Scores[i] >= ScoreThreshold)
            {
                Order.push_back(i);
            }
        }

        std::sort(Order.begin(), Order.end(), [&](int A, int B)
        {
            return Scores[A] > Scores[B];
        });

        for (int idx : Order)
        {
            bool bKeep = true;
            for (int kept : Indices)
            {
                if (CalcIoU(Boxes[idx], Boxes[kept]) > NmsThreshold)
                {
                    bKeep = false;
                    break;
                }
            }
            if (bKeep)
            {
                Indices.push_back(idx);
            }
        }
    }

    FORCEINLINE float Sigmoidf(float V)
    {
        const float Clamped = FMath::Clamp(V, -60.0f, 60.0f);
        return 1.0f / (1.0f + FMath::Exp(-Clamped));
    }

    FString MatShapeToString(const cv::Mat& M)
    {
        FString Out = TEXT("[");
        for (int32 i = 0; i < M.dims; ++i)
        {
            if (i > 0)
            {
                Out += TEXT(", ");
            }
            Out += FString::FromInt(M.size[i]);
        }
        Out += TEXT("]");
        return Out;
    }
}

UMyActorComponent::UMyActorComponent()
{
	PrimaryComponentTick.bCanEverTick = true;

	// ...
}

void UMyActorComponent::SetCaptureResolution(int32 InWidth, int32 InHeight)
{
    CaptureWidth = FMath::Max(160, InWidth);
    CaptureHeight = FMath::Max(160, InHeight);
}

void UMyActorComponent::InitFrameTimingLog()
{
    if (!bRecordFrameTimes)
    {
        return;
    }

    FScopeLock Lock(&FrameTimeLogMutex);
    if (bFrameTimingLogInitialized)
    {
        return;
    }

    ResolvedFrameTimeCsvPath = FrameTimeCsvPath;
    if (ResolvedFrameTimeCsvPath.IsEmpty())
    {
        ResolvedFrameTimeCsvPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Profiling"), TEXT("DetectionFrameTimes.csv"));
    }

    const FString Directory = FPaths::GetPath(ResolvedFrameTimeCsvPath);
    if (!Directory.IsEmpty())
    {
        IFileManager::Get().MakeDirectory(*Directory, true);
    }

    if (bResetFrameTimeLogOnBeginPlay && IFileManager::Get().FileExists(*ResolvedFrameTimeCsvPath))
    {
        IFileManager::Get().Delete(*ResolvedFrameTimeCsvPath, false, true, true);
    }

    if (!IFileManager::Get().FileExists(*ResolvedFrameTimeCsvPath))
    {
        const FString Header = TEXT("frame_sequence,backend,source_width,source_height,total_ms,inference_ms,detections\n");
        FFileHelper::SaveStringToFile(Header, *ResolvedFrameTimeCsvPath, FFileHelper::EEncodingOptions::AutoDetect, &IFileManager::Get());
    }

    FrameTimeLogBuffer.Reset();
    bFrameTimingLogInitialized = true;
    UE_LOG(LogTemp, Log, TEXT("Frame timing log enabled: %s"), *ResolvedFrameTimeCsvPath);
}

void UMyActorComponent::FlushFrameTimingLog(bool bForce)
{
    if (!bRecordFrameTimes)
    {
        return;
    }

    FScopeLock Lock(&FrameTimeLogMutex);
    if (!bFrameTimingLogInitialized || FrameTimeLogBuffer.Num() == 0)
    {
        return;
    }

    const int32 FlushEvery = FMath::Max(1, FrameTimeFlushInterval);
    if (!bForce && FrameTimeLogBuffer.Num() < FlushEvery)
    {
        return;
    }

    FString Batch;
    Batch.Reserve(FrameTimeLogBuffer.Num() * 96);
    for (const FString& Line : FrameTimeLogBuffer)
    {
        Batch += Line;
    }

    FFileHelper::SaveStringToFile(
        Batch,
        *ResolvedFrameTimeCsvPath,
        FFileHelper::EEncodingOptions::AutoDetect,
        &IFileManager::Get(),
        FILEWRITE_Append);

    FrameTimeLogBuffer.Reset();
}

void UMyActorComponent::AppendFrameTimingLogLine(
    int32 Sequence,
    int32 Width,
    int32 Height,
    int32 DetectionCount,
    double TotalMs,
    double InferMs)
{
    if (!bRecordFrameTimes)
    {
        return;
    }

    const FString BackendLabel = (InferenceBackend == EDetectionInferenceBackend::TensorRT)
        ? TEXT("TensorRT")
        : TEXT("OpenCVDNN");
    const FString Line = FString::Printf(
        TEXT("%d,%s,%d,%d,%.4f,%.4f,%d\n"),
        Sequence,
        *BackendLabel,
        Width,
        Height,
        TotalMs,
        InferMs,
        DetectionCount);

    {
        FScopeLock Lock(&FrameTimeLogMutex);
        if (!bFrameTimingLogInitialized)
        {
            return;
        }
        FrameTimeLogBuffer.Add(Line);
    }

    FlushFrameTimingLog(false);
}

void UMyActorComponent::get_class_names() {
    ClassNames.clear();

    std::ifstream file(NamesPath);
    if (!file.is_open())
    {
        UE_LOG(LogTemp, Warning, TEXT("Failed to open class names file: %s"), *FString(NamesPath.c_str()));
        return;
    }

    std::string line;
    while (std::getline(file, line)) {
        ClassNames.push_back(line);
    }
}

void UMyActorComponent::BeginPlay() {
    Super::BeginPlay();
    // LoadConfig();

    // FString BackendFromIni;
    // if (GConfig && GConfig->GetString(TEXT("/Script/EagleEye.MyActorComponent"), TEXT("InferenceBackend"), BackendFromIni, GGameIni))
    // {
    //     if (BackendFromIni.Equals(TEXT("OpenCVDNN"), ESearchCase::IgnoreCase))
    //     {
    //         InferenceBackend = EDetectionInferenceBackend::OpenCVDNN;
    //     }
    //     else if (BackendFromIni.Equals(TEXT("TensorRT"), ESearchCase::IgnoreCase))
    //     {
    //         InferenceBackend = EDetectionInferenceBackend::TensorRT;
    //     }
    // }

    UE_LOG(LogTemp, Log, TEXT("Inference backend selected: %s"),
        (InferenceBackend == EDetectionInferenceBackend::TensorRT) ? TEXT("TensorRT") : TEXT("OpenCVDNN"));

    #if PLATFORM_WINDOWS
        UMyActorComponent::NamesPath = "C:\\Users\\Saranthyr\\Documents\\Unreal Projects\\EagleEye\\Source\\EagleEye\\coco.names";
        UMyActorComponent::WeightsPath = "C:\\Users\\Saranthyr\\Documents\\Unreal Projects\\EagleEye\\Source\\EagleEye\\yolov7.weights";
        UMyActorComponent::CfgPath = "C:\\Users\\Saranthyr\\Documents\\Unreal Projects\\EagleEye\\Source\\EagleEye\\yolov7.cfg";
    #elif PLATFORM_LINUX
        UMyActorComponent::NamesPath = "/home/saranthyr/Documents/Unreal Projects/EagleEye/Source/EagleEye/coco.names";
        UMyActorComponent::WeightsPath = "/home/saranthyr/Documents/Unreal Projects/EagleEye/Source/EagleEye/yolo11x.plan";
    #endif

    if (!ModelPathOverride.IsEmpty())
    {
        WeightsPath = std::string(TCHAR_TO_UTF8(*ModelPathOverride));
    }
    if (!DarknetCfgPathOverride.IsEmpty())
    {
        CfgPath = std::string(TCHAR_TO_UTF8(*DarknetCfgPathOverride));
    }
    if (!NamesPathOverride.IsEmpty())
    {
        NamesPath = std::string(TCHAR_TO_UTF8(*NamesPathOverride));
    }

    get_class_names();
    InitFrameTimingLog();
    if (!bUseSharedVisionModel)
    {
        StartWorker();
    }

    if (bUseOwnerCameraCapture)
    {
        if (UWorld* World = GetWorld())
        {
            if (UCrowDetectionShareSubsystem* ShareSubsystem = World->GetSubsystem<UCrowDetectionShareSubsystem>())
            {
                ShareSubsystem->RegisterDetector(GetOwner());
            }
            if (bUseSharedVisionModel)
            {
                if (UCrowVisionSubsystem* VisionSubsystem = World->GetSubsystem<UCrowVisionSubsystem>())
                {
                    VisionSubsystem->RegisterModelHost(this);
                }
            }
        }
    }

    const float ClampedCaptureFPS = FMath::Clamp(CaptureFPS, 1.0f, 120.0f);
    const float CaptureInterval = 1.0f / ClampedCaptureFPS;
    float InitialDelay = 0.f;
    if (bStaggerInitialCapture)
    {
        FRandomStream DelayStream(static_cast<int32>(GetUniqueID()));
        InitialDelay = DelayStream.FRandRange(0.f, FMath::Max(CaptureInterval, MaxInitialCaptureDelay));
    }

    GetWorld()->GetTimerManager().SetTimer(
        TimerHandle_Capture, this, &UMyActorComponent::TickCapture,
        CaptureInterval, true, InitialDelay
    );
}

void UMyActorComponent::EndPlay(const EEndPlayReason::Type EndPlayReason) {
    if (bUseOwnerCameraCapture)
    {
        if (UWorld* World = GetWorld())
        {
            if (UCrowDetectionShareSubsystem* ShareSubsystem = World->GetSubsystem<UCrowDetectionShareSubsystem>())
            {
                ShareSubsystem->UnregisterDetector(GetOwner());
            }
            if (bUseSharedVisionModel)
            {
                if (UCrowVisionSubsystem* VisionSubsystem = World->GetSubsystem<UCrowVisionSubsystem>())
                {
                    VisionSubsystem->UnregisterModelHost(this);
                }
            }
        }
    }

    if (!bUseSharedVisionModel)
    {
        StopWorker();
    }
    else
    {
        FlushFrameTimingLog(true);
    }
    Super::EndPlay(EndPlayReason);
}

void UMyActorComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (bUseSharedVisionModel)
    {
        return;
    }

    // Pull latest detections every frame to minimize overlay latency.
    CopyResultsFromWorker();
}

void UMyActorComponent::TickCapture() {
    if (bUseOwnerCameraCapture && ShouldSkipOwnerCameraCapture())
    {
        ClearPublishedResults();
        return;
    }

    // If the worker hasn't consumed the last captured frame yet, don't capture another.
    {
        FScopeLock Lock(&FrameMutex);
        if (LatestFrame.IsValid())
        {
            return;
        }
    }

    CaptureAndEnqueue(50);
}

bool UMyActorComponent::CaptureViewportToPixels(TArray<FColor>& OutPixels, int32& OutWidth, int32& OutHeight)
{
    OutWidth = 0;
    OutHeight = 0;

    if (!GEngine || !GEngine->GameViewport)
    {
        return false;
    }

    FViewport* Viewport = GEngine->GameViewport->Viewport;
    if (!Viewport)
    {
        return false;
    }

    const FIntPoint Size = Viewport->GetSizeXY();
    if (Size.X <= 0 || Size.Y <= 0)
    {
        return false;
    }

    OutPixels.Reset();
    if (!Viewport->ReadPixels(OutPixels))
    {
        return false;
    }

    if (OutPixels.Num() != Size.X * Size.Y)
    {
        return false;
    }

    OutWidth = Size.X;
    OutHeight = Size.Y;
    return true;
}

bool UMyActorComponent::EnsureOwnerCameraCapture()
{
    AActor* Owner = GetOwner();
    if (!Owner)
    {
        return false;
    }

    UCameraComponent* OwnerCamera = Owner->FindComponentByClass<UCameraComponent>();
    if (!OwnerCamera)
    {
        return false;
    }

    if (!OwnerCaptureRenderTarget)
    {
        OwnerCaptureRenderTarget = NewObject<UTextureRenderTarget2D>(this, TEXT("OwnerCameraDetectionRenderTarget"));
        OwnerCaptureRenderTarget->RenderTargetFormat = RTF_RGBA8;
        OwnerCaptureRenderTarget->ClearColor = FLinearColor::Black;
        OwnerCaptureRenderTarget->InitAutoFormat(FMath::Max(160, CaptureWidth), FMath::Max(160, CaptureHeight));
        OwnerCaptureRenderTarget->UpdateResourceImmediate(true);
    }

    if (!OwnerSceneCapture)
    {
        OwnerSceneCapture = NewObject<UScreenCaptureComponent>(Owner, TEXT("OwnerCameraDetectionCapture"));
        OwnerSceneCapture->SetupAttachment(OwnerCamera);
        OwnerSceneCapture->RegisterComponent();
        OwnerSceneCapture->SetRelativeTransform(FTransform::Identity);
        OwnerSceneCapture->TextureTarget = OwnerCaptureRenderTarget;
        OwnerSceneCapture->bCaptureEveryFrame = false;
        OwnerSceneCapture->bCaptureOnMovement = false;
        OwnerSceneCapture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
    }

    OwnerSceneCapture->FOVAngle = OwnerCamera->FieldOfView;
    OwnerSceneCapture->SetWorldLocationAndRotation(OwnerCamera->GetComponentLocation(), OwnerCamera->GetComponentRotation());
    OwnerSceneCapture->TextureTarget = OwnerCaptureRenderTarget;
    return true;
}

bool UMyActorComponent::ShouldSkipOwnerCameraCapture() const
{
    const AActor* Owner = GetOwner();
    if (!Owner)
    {
        return false;
    }

    if (const UWorld* World = GetWorld())
    {
        if (const UCrowDetectionShareSubsystem* ShareSubsystem = World->GetSubsystem<UCrowDetectionShareSubsystem>())
        {
            return !ShareSubsystem->ShouldRunDetector(
                const_cast<AActor*>(Owner),
                MaxActiveOwnerCameraCaptures,
                MaxOwnerCameraCaptureDistance);
        }
    }

    if (MaxOwnerCameraCaptureDistance <= 0.f)
    {
        return false;
    }

    const UWorld* World = GetWorld();
    if (!World)
    {
        return false;
    }
    const APlayerController* PlayerController = UGameplayStatics::GetPlayerController(World, 0);
    const APawn* PlayerPawn = PlayerController ? PlayerController->GetPawn() : nullptr;
    if (!PlayerPawn)
    {
        return false;
    }

    return FVector::DistSquared(Owner->GetActorLocation(), PlayerPawn->GetActorLocation())
        > FMath::Square(MaxOwnerCameraCaptureDistance);
}

void UMyActorComponent::ClearPublishedResults()
{
    {
        FScopeLock ResultsLock(&ResultsMutex);
        ResultsShared.Reset();
        ResultsSourceWidth = 0;
        ResultsSourceHeight = 0;
        ++ResultsSequence;
    }

    LastFrameDetections.Reset();
    LastFrameSourceWidth = 0;
    LastFrameSourceHeight = 0;
    ++LastFrameSequence;
}

bool UMyActorComponent::CaptureSceneToPixels(TArray<FColor>& OutPixels, int32& OutWidth, int32& OutHeight)
{
    const double CaptureStartSeconds = FPlatformTime::Seconds();
    OutWidth = 0;
    OutHeight = 0;

    double EnsureMs = 0.0;
    double SceneCaptureMs = 0.0;
    double ReadPixelsMs = 0.0;

    const double EnsureStartSeconds = FPlatformTime::Seconds();
    if (!EnsureOwnerCameraCapture() || !OwnerSceneCapture || !OwnerCaptureRenderTarget)
    {
        return false;
    }
    EnsureMs = (FPlatformTime::Seconds() - EnsureStartSeconds) * 1000.0;

    const double SceneCaptureStartSeconds = FPlatformTime::Seconds();
    OwnerSceneCapture->CaptureScene();
    SceneCaptureMs = (FPlatformTime::Seconds() - SceneCaptureStartSeconds) * 1000.0;

    FTextureRenderTargetResource* Resource = OwnerCaptureRenderTarget->GameThread_GetRenderTargetResource();
    if (!Resource)
    {
        return false;
    }

    TArray<FColor> LocalPixels;
    FReadSurfaceDataFlags ReadFlags(RCM_UNorm);
    ReadFlags.SetLinearToGamma(false);
    const double ReadPixelsStartSeconds = FPlatformTime::Seconds();
    if (!Resource->ReadPixels(LocalPixels, ReadFlags))
    {
        return false;
    }
    ReadPixelsMs = (FPlatformTime::Seconds() - ReadPixelsStartSeconds) * 1000.0;

    if (LocalPixels.Num() != OwnerCaptureRenderTarget->SizeX * OwnerCaptureRenderTarget->SizeY)
    {
        return false;
    }

    OutWidth = OwnerCaptureRenderTarget->SizeX;
    OutHeight = OwnerCaptureRenderTarget->SizeY;
    OutPixels = MoveTemp(LocalPixels);

    if (bLogFrameTimings)
    {
        const double TotalMs = (FPlatformTime::Seconds() - CaptureStartSeconds) * 1000.0;
        UE_LOG(LogTemp, Log, TEXT("DetectionCapture[%s]: total=%.2fms ensure=%.2fms capture=%.2fms readback=%.2fms size=%dx%d"),
            *GetNameSafe(GetOwner()),
            TotalMs,
            EnsureMs,
            SceneCaptureMs,
            ReadPixelsMs,
            OutWidth,
            OutHeight);
    }

    return true;
}

bool UMyActorComponent::PollAsyncOwnerCameraReadback(TArray<FColor>& OutPixels, int32& OutWidth, int32& OutHeight)
{
    OutWidth = 0;
    OutHeight = 0;

    if (!PendingOwnerCameraReadback.IsValid() || !PendingOwnerCameraReadback->IsReady())
    {
        return false;
    }

    const double PollStartSeconds = FPlatformTime::Seconds();
    int32 RowPitchInPixels = 0;
    int32 BufferHeight = 0;
    const FColor* SourcePixels = static_cast<const FColor*>(
        PendingOwnerCameraReadback->Lock(RowPitchInPixels, &BufferHeight));
    if (!SourcePixels)
    {
        PendingOwnerCameraReadback->Unlock();
        PendingOwnerCameraReadback.Reset();
        return false;
    }

    OutWidth = PendingOwnerCameraReadbackWidth;
    OutHeight = PendingOwnerCameraReadbackHeight;
    OutPixels.SetNumUninitialized(OutWidth * OutHeight);

    for (int32 Y = 0; Y < OutHeight; ++Y)
    {
        const FColor* SourceRow = SourcePixels + (Y * RowPitchInPixels);
        FColor* DestRow = OutPixels.GetData() + (Y * OutWidth);
        FMemory::Memcpy(DestRow, SourceRow, sizeof(FColor) * OutWidth);
    }

    PendingOwnerCameraReadback->Unlock();
    PendingOwnerCameraReadback.Reset();

    if (bLogFrameTimings)
    {
        const double NowSeconds = FPlatformTime::Seconds();
        UE_LOG(LogTemp, Log, TEXT("AsyncReadbackPoll[%s]: lock+copy=%.2fms latency=%.2fms row_pitch=%d buffer_h=%d size=%dx%d"),
            *GetNameSafe(GetOwner()),
            (NowSeconds - PollStartSeconds) * 1000.0,
            (NowSeconds - PendingOwnerCameraReadbackSubmitTimeSeconds) * 1000.0,
            RowPitchInPixels,
            BufferHeight,
            OutWidth,
            OutHeight);
    }

    PendingOwnerCameraReadbackWidth = 0;
    PendingOwnerCameraReadbackHeight = 0;
    PendingOwnerCameraReadbackSubmitTimeSeconds = 0.0;
    return true;
}

bool UMyActorComponent::EnqueueAsyncOwnerCameraReadback()
{
    const double EnqueueStartSeconds = FPlatformTime::Seconds();
    if (PendingOwnerCameraReadback.IsValid())
    {
        return false;
    }

    if (!EnsureOwnerCameraCapture() || !OwnerSceneCapture || !OwnerCaptureRenderTarget)
    {
        return false;
    }

    const double SceneCaptureStartSeconds = FPlatformTime::Seconds();
    OwnerSceneCapture->CaptureScene();
    const double SceneCaptureMs = (FPlatformTime::Seconds() - SceneCaptureStartSeconds) * 1000.0;

    FTextureRenderTargetResource* Resource = OwnerCaptureRenderTarget->GameThread_GetRenderTargetResource();
    if (!Resource)
    {
        return false;
    }

    const FTextureRHIRef TextureRHI = Resource->GetRenderTargetTexture();
    if (!TextureRHI.IsValid())
    {
        return false;
    }

    PendingOwnerCameraReadback = MakeUnique<FRHIGPUTextureReadback>(TEXT("CrowOwnerCameraReadback"));
    PendingOwnerCameraReadbackWidth = OwnerCaptureRenderTarget->SizeX;
    PendingOwnerCameraReadbackHeight = OwnerCaptureRenderTarget->SizeY;
    PendingOwnerCameraReadbackSubmitTimeSeconds = FPlatformTime::Seconds();
    FRHIGPUTextureReadback* Readback = PendingOwnerCameraReadback.Get();
    const FIntVector ReadbackSize(PendingOwnerCameraReadbackWidth, PendingOwnerCameraReadbackHeight, 1);

    ENQUEUE_RENDER_COMMAND(EnqueueCrowOwnerCameraReadback)(
        [Readback, TextureRHI, ReadbackSize](FRHICommandListImmediate& RHICmdList)
        {
            Readback->EnqueueCopy(RHICmdList, TextureRHI, FIntVector::ZeroValue, 0, ReadbackSize);
        });

    if (bLogFrameTimings)
    {
        UE_LOG(LogTemp, Log, TEXT("AsyncReadbackEnqueue[%s]: total=%.2fms capture=%.2fms size=%dx%d"),
            *GetNameSafe(GetOwner()),
            (FPlatformTime::Seconds() - EnqueueStartSeconds) * 1000.0,
            SceneCaptureMs,
            PendingOwnerCameraReadbackWidth,
            PendingOwnerCameraReadbackHeight);
    }

    return true;
}

void UMyActorComponent::CaptureAndEnqueue(int Threshold)
{
    const double CaptureAndEnqueueStartSeconds = FPlatformTime::Seconds();
    TArray<FColor> Pixels;
    int32 SourceWidth = 0;
    int32 SourceHeight = 0;

    bool bCaptured = false;
    if (bUseOwnerCameraCapture && bUseAsyncOwnerCameraReadback)
    {
        bCaptured = PollAsyncOwnerCameraReadback(Pixels, SourceWidth, SourceHeight);
        EnqueueAsyncOwnerCameraReadback();
    }
    else
    {
        bCaptured = bUseOwnerCameraCapture
            ? CaptureSceneToPixels(Pixels, SourceWidth, SourceHeight)
            : CaptureViewportToPixels(Pixels, SourceWidth, SourceHeight);
    }

    if (!bCaptured)
    {
        return;
    }

    if (bUseSharedVisionModel)
    {
        if (UWorld* World = GetWorld())
        {
            if (UCrowVisionSubsystem* VisionSubsystem = World->GetSubsystem<UCrowVisionSubsystem>())
            {
                VisionSubsystem->SubmitFrame(this, MoveTemp(Pixels), SourceWidth, SourceHeight, Threshold);
                if (bLogFrameTimings)
                {
                    const double TotalMs = (FPlatformTime::Seconds() - CaptureAndEnqueueStartSeconds) * 1000.0;
                    UE_LOG(LogTemp, Log, TEXT("DetectionSubmit[%s]: capture+submit=%.2fms size=%dx%d"),
                        *GetNameSafe(GetOwner()),
                        TotalMs,
                        SourceWidth,
                        SourceHeight);
                }
            }
        }
        return;
    }

    TSharedPtr<FFrameData> Frame = MakeShared<FFrameData>();
    Frame->Pixels = MoveTemp(Pixels);
    Frame->Width = SourceWidth;
    Frame->Height = SourceHeight;
    Frame->Threshold = Threshold;

    {
        FScopeLock Lock(&FrameMutex);
        LatestFrame = Frame;
    }

    if (bLogFrameTimings)
    {
        const double TotalMs = (FPlatformTime::Seconds() - CaptureAndEnqueueStartSeconds) * 1000.0;
        UE_LOG(LogTemp, Log, TEXT("DetectionEnqueue[%s]: capture+enqueue=%.2fms size=%dx%d"),
            *GetNameSafe(GetOwner()),
            TotalMs,
            SourceWidth,
            SourceHeight);
    }
}

void UMyActorComponent::StartWorker()
{
    if (bWorkerRunning.load()) return;
    bWorkerRunning.store(true);

    WorkerFuture = Async(EAsyncExecution::Thread, [this]()
    {
        try
        {
            while (bWorkerRunning.load())
            {
                TSharedPtr<FFrameData> Frame;
                {
                    FScopeLock Lock(&FrameMutex);
                    Frame = LatestFrame;
                    LatestFrame.Reset();
                }

                if (!Frame.IsValid())
                {
                    FPlatformProcess::Sleep(0.001f);
                    continue;
                }

                TArray<FDetectionResult> Det;
                double FrameInferMs = -1.0;
                const double FrameT0 = FPlatformTime::Seconds();
                try
                {
                    if (!bIsModelLoaded && !LoadYOLO())
                    {
                        Det.Reset();
                    }
                    else
                    {
                    Det = ProcessWithOpenCV_BG(Frame->Pixels, Frame->Width, Frame->Height, Frame->Threshold, &FrameInferMs);
                    }
                }
                catch (const cv::Exception& e)
                {
                    UE_LOG(LogTemp, Error, TEXT("OpenCV exception in worker: %s"), *FString(e.what()));
                    Det.Reset();
                }
                catch (const std::exception& e)
                {
                    UE_LOG(LogTemp, Error, TEXT("std::exception in worker: %s"), *FString(e.what()));
                    Det.Reset();
                }
                catch (...)
                {
                    UE_LOG(LogTemp, Error, TEXT("Unknown exception in worker"));
                    Det.Reset();
                }

                int32 LoggedSequence = 0;
                int32 LoggedDetectionCount = 0;
                {
                    FScopeLock Lock(&ResultsMutex);
                    ResultsShared = MoveTemp(Det);
                    ResultsSourceWidth = Frame->Width;
                    ResultsSourceHeight = Frame->Height;
                    ++ResultsSequence;
                    LoggedSequence = ResultsSequence;
                    LoggedDetectionCount = ResultsShared.Num();
                }
                const double FrameTotalMs = (FPlatformTime::Seconds() - FrameT0) * 1000.0;
                AppendFrameTimingLogLine(LoggedSequence, Frame->Width, Frame->Height, LoggedDetectionCount, FrameTotalMs, FrameInferMs);
            }
        }
        catch (const cv::Exception& e)
        {
            UE_LOG(LogTemp, Error, TEXT("OpenCV exception in worker startup: %s"), *FString(e.what()));
        }
        catch (const std::exception& e)
        {
            UE_LOG(LogTemp, Error, TEXT("std::exception in worker startup: %s"), *FString(e.what()));
        }
        catch (...)
        {
            UE_LOG(LogTemp, Error, TEXT("Unknown exception in worker startup"));
        }

        bWorkerRunning.store(false);
    });

}

void UMyActorComponent::StopWorker()
{
    bWorkerRunning.store(false);
    {
        FScopeLock Lock(&FrameMutex);
        LatestFrame.Reset();
    }
    if (WorkerFuture.IsValid())
    {
        WorkerFuture.Wait();
    }
    FlushFrameTimingLog(true);
    {
        FScopeLock Lock(&ResultsMutex);
        ResultsShared.Reset();
        ResultsSourceWidth = 0;
        ResultsSourceHeight = 0;
        ResultsSequence = 0;
    }
    LastFrameDetections.Reset();
    LastFrameSourceWidth = 0;
    LastFrameSourceHeight = 0;
    LastFrameSequence = 0;
    ReleaseTensorRT();
    OpenCVDnnNet = cv::dnn::Net();
}

void UMyActorComponent::CopyResultsFromWorker()
{
    TArray<FDetectionResult> LocalResults;
    int32 SourceWidth = 0;
    int32 SourceHeight = 0;
    int32 Sequence = 0;
    {
        FScopeLock Lock(&ResultsMutex);
        LocalResults = ResultsShared;
        SourceWidth = ResultsSourceWidth;
        SourceHeight = ResultsSourceHeight;
        Sequence = ResultsSequence;
    }

    if (SourceWidth <= 0 || SourceHeight <= 0)
    {
        LastFrameDetections.Reset();
        LastFrameSourceWidth = 0;
        LastFrameSourceHeight = 0;
        LastFrameSequence = 0;
        return;
    }

    LastFrameDetections = MoveTemp(LocalResults);
    LastFrameSourceWidth = SourceWidth;
    LastFrameSourceHeight = SourceHeight;
    LastFrameSequence = Sequence;
}

TArray<FDetectionResult> UMyActorComponent::ProcessSharedVisionFrame(
    const TArray<FColor>& Bitmap,
    int32 Width,
    int32 Height,
    int32 Threshold,
    double* OutInferenceMs)
{
    if (!bIsModelLoaded && !LoadYOLO())
    {
        return {};
    }

    const double StartSeconds = FPlatformTime::Seconds();
    TArray<FDetectionResult> Detections = ProcessWithOpenCV_BG(Bitmap, Width, Height, Threshold, OutInferenceMs);

    if (bLogFrameTimings)
    {
        UE_LOG(LogTemp, Log, TEXT("SharedInferenceHost[%s]: total=%.2fms infer=%.2fms detections=%d size=%dx%d"),
            *GetNameSafe(GetOwner()),
            (FPlatformTime::Seconds() - StartSeconds) * 1000.0,
            OutInferenceMs ? *OutInferenceMs : -1.0,
            Detections.Num(),
            Width,
            Height);
    }

    return Detections;
}

void UMyActorComponent::ConsumeSharedVisionResult(
    TArray<FDetectionResult>&& Detections,
    int32 SourceWidth,
    int32 SourceHeight)
{
    if (SourceWidth <= 0 || SourceHeight <= 0)
    {
        LastFrameDetections.Reset();
        LastFrameSourceWidth = 0;
        LastFrameSourceHeight = 0;
        ++LastFrameSequence;
        return;
    }

    LastFrameDetections = MoveTemp(Detections);
    LastFrameSourceWidth = SourceWidth;
    LastFrameSourceHeight = SourceHeight;
    ++LastFrameSequence;

    if (bLogFrameTimings)
    {
        UE_LOG(LogTemp, Log, TEXT("SharedResult[%s]: detections=%d size=%dx%d seq=%d"),
            *GetNameSafe(GetOwner()),
            LastFrameDetections.Num(),
            SourceWidth,
            SourceHeight,
            LastFrameSequence);
    }
}

	bool UMyActorComponent::LoadYOLO()
	{
	    try
	    {
	        cv::setUseOptimized(true);
	        cv::setNumThreads(FPlatformMisc::NumberOfCoresIncludingHyperthreads());

	        ReleaseTensorRT();
	        OpenCVDnnNet = cv::dnn::Net();

            // Always resolve effective backend from merged config at model-load time.
            // FString BackendFromIni;
            // if (GConfig && GConfig->GetString(TEXT("/Script/EagleEye.MyActorComponent"), TEXT("InferenceBackend"), BackendFromIni, GGameIni))
            // {
            //     if (BackendFromIni.Equals(TEXT("OpenCVDNN"), ESearchCase::IgnoreCase))
            //     {
            //         InferenceBackend = EDetectionInferenceBackend::OpenCVDNN;
            //     }
            //     else if (BackendFromIni.Equals(TEXT("TensorRT"), ESearchCase::IgnoreCase))
            //     {
            //         InferenceBackend = EDetectionInferenceBackend::TensorRT;
            //     }
            // }

            UE_LOG(LogTemp, Log, TEXT("LoadYOLO effective backend: %s"),
                (InferenceBackend == EDetectionInferenceBackend::TensorRT) ? TEXT("TensorRT") : TEXT("OpenCVDNN"));

	        const FString ModelPathUE = FString(WeightsPath.c_str());
	        if (!FPlatformFileManager::Get().GetPlatformFile().FileExists(*ModelPathUE))
	        {
	            UE_LOG(LogTemp, Error, TEXT("Model file not found: %s"), *ModelPathUE);
	            return false;
	        }

            if (InferenceBackend == EDetectionInferenceBackend::TensorRT)
            {
                bIsOnnxModel = ModelPathUE.EndsWith(TEXT(".onnx")) || ModelPathUE.EndsWith(TEXT(".plan"));
                if (!ModelPathUE.EndsWith(TEXT(".plan")))
                {
                    UE_LOG(LogTemp, Error, TEXT("TensorRT requires a .plan engine file, got: %s"), *ModelPathUE);
                    return false;
                }

                TArray<uint8> PlanData;
                if (!FFileHelper::LoadFileToArray(PlanData, *ModelPathUE) || PlanData.Num() == 0)
                {
                    UE_LOG(LogTemp, Error, TEXT("Failed to load TensorRT plan: %s"), *ModelPathUE);
                    return false;
                }

                TrtRuntime = nvinfer1::createInferRuntime(GTrtLogger);
                if (!TrtRuntime)
                {
                    UE_LOG(LogTemp, Error, TEXT("Failed to create TensorRT runtime"));
                    return false;
                }

                TrtEngine = TrtRuntime->deserializeCudaEngine(PlanData.GetData(), PlanData.Num());
                if (!TrtEngine)
                {
                    UE_LOG(LogTemp, Error, TEXT("Failed to deserialize TensorRT engine"));
                    ReleaseTensorRT();
                    return false;
                }

                TrtContext = TrtEngine->createExecutionContext();
                if (!TrtContext)
                {
                    UE_LOG(LogTemp, Error, TEXT("Failed to create TensorRT execution context"));
                    ReleaseTensorRT();
                    return false;
                }

                const nvinfer1::Dims InputDims = TrtEngine->getTensorShape(kTrtInputName);
                const nvinfer1::Dims OutputDims = TrtEngine->getTensorShape(kTrtOutputName);

                const int64 InputVolume = CalcTrtVolume(InputDims);
                const int64 OutputVolume = CalcTrtVolume(OutputDims);
                if (InputVolume <= 0 || OutputVolume <= 0)
                {
                    UE_LOG(LogTemp, Error, TEXT("Invalid TensorRT tensor shapes (input=%d dims, output=%d dims)"),
                        InputDims.nbDims, OutputDims.nbDims);
                    ReleaseTensorRT();
                    return false;
                }

                if (InputDims.nbDims >= 4 && InputDims.d[2] > 0 && InputDims.d[3] > 0)
                {
                    ModelInputHeight = InputDims.d[2];
                    ModelInputWidth = InputDims.d[3];
                }
                else
                {
                    const int32 ClampedInputSize = FMath::Clamp(OnnxInputSize, 160, 1280);
                    ModelInputWidth = ClampedInputSize;
                    ModelInputHeight = ClampedInputSize;
                }

                TrtInputElements = static_cast<int32>(InputVolume);
                TrtOutputElements = static_cast<int32>(OutputVolume);
                TrtOutputChannels = (OutputDims.nbDims == 3) ? OutputDims.d[1] : 0;
                TrtOutputDetections = (OutputDims.nbDims == 3) ? OutputDims.d[2] : 0;
                TrtInputHost.SetNumUninitialized(TrtInputElements);
                TrtOutputHost.SetNumUninitialized(TrtOutputElements);

                const size_t InputBytes = static_cast<size_t>(TrtInputElements) * sizeof(float);
                const size_t OutputBytes = static_cast<size_t>(TrtOutputElements) * sizeof(float);

                const cudaError_t StreamErr = cudaStreamCreate(&TrtStream);
                if (StreamErr != cudaSuccess)
                {
                    UE_LOG(LogTemp, Error, TEXT("cudaStreamCreate failed: %s"), *FString(cudaGetErrorString(StreamErr)));
                    ReleaseTensorRT();
                    return false;
                }

                const cudaError_t InAllocErr = cudaMalloc(&TrtInputDevice, InputBytes);
                if (InAllocErr != cudaSuccess)
                {
                    UE_LOG(LogTemp, Error, TEXT("cudaMalloc input failed: %s"), *FString(cudaGetErrorString(InAllocErr)));
                    ReleaseTensorRT();
                    return false;
                }

                const cudaError_t OutAllocErr = cudaMalloc(&TrtOutputDevice, OutputBytes);
                if (OutAllocErr != cudaSuccess)
                {
                    UE_LOG(LogTemp, Error, TEXT("cudaMalloc output failed: %s"), *FString(cudaGetErrorString(OutAllocErr)));
                    ReleaseTensorRT();
                    return false;
                }

                UE_LOG(LogTemp, Log, TEXT("TensorRT engine loaded: input=%s, output=%s, elements=%d"),
                    *TrtDimsToString(InputDims), *TrtDimsToString(OutputDims), TrtOutputElements);
                bIsModelLoaded = true;
                return true;
            }

            if (InferenceBackend != EDetectionInferenceBackend::OpenCVDNN)
            {
                UE_LOG(LogTemp, Error, TEXT("Unsupported inference backend value: %d"), static_cast<int32>(InferenceBackend));
                return false;
            }

            FString OpenCvModelPath = ModelPathUE;
            if (OpenCvModelPath.EndsWith(TEXT(".plan")))
            {
                const FString CandidateOnnxPath = FPaths::ChangeExtension(OpenCvModelPath, TEXT("onnx"));
                if (FPlatformFileManager::Get().GetPlatformFile().FileExists(*CandidateOnnxPath))
                {
                    OpenCvModelPath = CandidateOnnxPath;
                }
                else
                {
                    FString BaseName = FPaths::GetBaseFilename(OpenCvModelPath);
                    const FString DirPath = FPaths::GetPath(OpenCvModelPath);
                    if (BaseName.EndsWith(TEXT("_fp16")) || BaseName.EndsWith(TEXT("_fp32")) || BaseName.EndsWith(TEXT("_int8")))
                    {
                        BaseName = BaseName.LeftChop(5);
                    }
                    const FString AltOnnxPath = FPaths::Combine(DirPath, BaseName + TEXT(".onnx"));
                    if (FPlatformFileManager::Get().GetPlatformFile().FileExists(*AltOnnxPath))
                    {
                        OpenCvModelPath = AltOnnxPath;
                    }
                    else
                    {
                        UE_LOG(LogTemp, Error, TEXT("OpenCV DNN cannot load .plan. Tried %s and %s"),
                            *CandidateOnnxPath, *AltOnnxPath);
                        return false;
                    }
                }
            }

            if (OpenCvModelPath.EndsWith(TEXT(".onnx")))
            {
                OpenCVDnnNet = cv::dnn::readNetFromONNX(std::string(TCHAR_TO_UTF8(*OpenCvModelPath)));
                bIsOnnxModel = true;
            }
            else if (OpenCvModelPath.EndsWith(TEXT(".weights")))
            {
                const FString CfgPathUE = FString(CfgPath.c_str());
                if (!FPlatformFileManager::Get().GetPlatformFile().FileExists(*CfgPathUE))
                {
                    UE_LOG(LogTemp, Error, TEXT("Darknet cfg not found: %s"), *CfgPathUE);
                    return false;
                }
                OpenCVDnnNet = cv::dnn::readNetFromDarknet(
                    std::string(TCHAR_TO_UTF8(*CfgPathUE)),
                    std::string(TCHAR_TO_UTF8(*OpenCvModelPath)));
                bIsOnnxModel = false;
            }
            else
            {
                UE_LOG(LogTemp, Error, TEXT("Unsupported OpenCV model format: %s"), *OpenCvModelPath);
                return false;
            }

            if (OpenCVDnnNet.empty())
            {
                UE_LOG(LogTemp, Error, TEXT("OpenCV DNN failed to load model: %s"), *OpenCvModelPath);
                return false;
            }

            try
            {
                if (bOpenCVDNNPreferCUDA)
                {
                    OpenCVDnnNet.setPreferableTarget(
                        bOpenCVDNNUseFP16 ? cv::dnn::DNN_TARGET_CUDA_FP16 : cv::dnn::DNN_TARGET_CUDA);
                    OpenCVDnnNet.setPreferableBackend(cv::dnn::DNN_BACKEND_CUDA);
                }
                else
                {
                    OpenCVDnnNet.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
                    OpenCVDnnNet.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
                }
            }
            catch (const cv::Exception& e)
            {
                UE_LOG(LogTemp, Warning, TEXT("OpenCV DNN preferred backend setup failed (%s). Falling back to CPU."),
                    *FString(e.what()));
                OpenCVDnnNet.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
                OpenCVDnnNet.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
            }

            const int32 ClampedInputSize = FMath::Clamp(OnnxInputSize, 160, 1280);
            ModelInputWidth = ClampedInputSize;
            ModelInputHeight = ClampedInputSize;
            TrtInputElements = 0;
            TrtOutputElements = 0;
            TrtOutputChannels = 0;
            TrtOutputDetections = 0;
            TrtInputHost.Reset();
            TrtOutputHost.Reset();

            UE_LOG(LogTemp, Log, TEXT("OpenCV DNN model loaded: %s"), *OpenCvModelPath);
            bIsModelLoaded = true;
            return true;
	    }
	    catch (const std::exception& e)
	    {
	        UE_LOG(LogTemp, Error, TEXT("Model load failed: %s"), *FString(e.what()));
	        ReleaseTensorRT();
            OpenCVDnnNet = cv::dnn::Net();
	        return false;
	    }
	    catch (...)
	    {
	        UE_LOG(LogTemp, Error, TEXT("Model load failed (unknown exception)"));
	        ReleaseTensorRT();
            OpenCVDnnNet = cv::dnn::Net();
	        return false;
	    }
	}

void UMyActorComponent::ReleaseTensorRT()
{
    if (TrtStream)
    {
        cudaStreamSynchronize(TrtStream);
        cudaStreamDestroy(TrtStream);
        TrtStream = nullptr;
    }

    if (TrtInputDevice)
    {
        cudaFree(TrtInputDevice);
        TrtInputDevice = nullptr;
    }

    if (TrtOutputDevice)
    {
        cudaFree(TrtOutputDevice);
        TrtOutputDevice = nullptr;
    }

    DestroyTrtObject(TrtContext);
    DestroyTrtObject(TrtEngine);
    DestroyTrtObject(TrtRuntime);

    TrtInputHost.Reset();
    TrtOutputHost.Reset();
    TrtInputElements = 0;
    TrtOutputElements = 0;
    TrtOutputChannels = 0;
    TrtOutputDetections = 0;
    bIsModelLoaded = false;
}

bool UMyActorComponent::RunTensorRT()
{
    if (!TrtContext || !TrtInputDevice || !TrtOutputDevice || TrtInputElements <= 0 || TrtOutputElements <= 0)
    {
        return false;
    }

    const size_t InputBytes = static_cast<size_t>(TrtInputElements) * sizeof(float);
    const size_t OutputBytes = static_cast<size_t>(TrtOutputElements) * sizeof(float);

    auto LogCudaError = [](const TCHAR* Label, cudaError_t Err)
    {
        if (Err == cudaSuccess)
        {
            return true;
        }
        UE_LOG(LogTemp, Error, TEXT("%s: %s"), Label, *FString(UTF8_TO_TCHAR(cudaGetErrorString(Err))));
        return false;
    };

    if (!LogCudaError(TEXT("cudaMemcpyAsync H2D failed"), cudaMemcpyAsync(TrtInputDevice, TrtInputHost.GetData(), InputBytes, cudaMemcpyHostToDevice, TrtStream)))
    {
        return false;
    }

    if (!TrtContext->setTensorAddress(kTrtInputName, TrtInputDevice) ||
        !TrtContext->setTensorAddress(kTrtOutputName, TrtOutputDevice))
    {
        UE_LOG(LogTemp, Error, TEXT("TensorRT setTensorAddress failed"));
        return false;
    }

    if (!TrtContext->enqueueV3(TrtStream))
    {
        UE_LOG(LogTemp, Error, TEXT("TensorRT enqueueV3 failed"));
        return false;
    }

    if (!LogCudaError(TEXT("cudaMemcpyAsync D2H failed"), cudaMemcpyAsync(TrtOutputHost.GetData(), TrtOutputDevice, OutputBytes, cudaMemcpyDeviceToHost, TrtStream)))
    {
        return false;
    }

    if (!LogCudaError(TEXT("cudaStreamSynchronize failed"), cudaStreamSynchronize(TrtStream)))
    {
        return false;
    }

    return true;
}

bool UMyActorComponent::RunTensorRTInference_BG(const cv::Mat& ModelInputBGR)
{
    cv::Mat imgRGB;
    cv::cvtColor(ModelInputBGR, imgRGB, cv::COLOR_BGR2RGB);

    cv::Mat imgFloat;
    imgRGB.convertTo(imgFloat, CV_32F, 1.0f / 255.0f);

    const int32 PlaneSize = ModelInputWidth * ModelInputHeight;
    if (TrtInputElements != PlaneSize * 3)
    {
        UE_LOG(LogTemp, Error, TEXT("TensorRT input size mismatch (expected %d, got %d)"),
            PlaneSize * 3, TrtInputElements);
        return false;
    }

    if (TrtInputHost.Num() != TrtInputElements)
    {
        TrtInputHost.SetNumUninitialized(TrtInputElements);
    }

    std::vector<cv::Mat> Channels;
    cv::split(imgFloat, Channels);
    if (Channels.size() != 3)
    {
        UE_LOG(LogTemp, Error, TEXT("Unexpected channel count from preprocessing: %d"), Channels.size());
        return false;
    }

    float* InputPtr = TrtInputHost.GetData();
    for (int32 c = 0; c < 3; ++c)
    {
        const float* Src = Channels[c].ptr<float>();
        FMemory::Memcpy(InputPtr + (c * PlaneSize), Src, PlaneSize * sizeof(float));
    }

    if (!RunTensorRT())
    {
        UE_LOG(LogTemp, Error, TEXT("TensorRT inference failed"));
        return false;
    }

    return true;
}

bool UMyActorComponent::RunOpenCVDNNInference_BG(const cv::Mat& ModelInputBGR)
{
    if (InferenceBackend != EDetectionInferenceBackend::OpenCVDNN)
    {
        UE_LOG(LogTemp, Error, TEXT("RunOpenCVDNNInference_BG called while backend is not OpenCVDNN"));
        return false;
    }

    if (OpenCVDnnNet.empty())
    {
        UE_LOG(LogTemp, Error, TEXT("OpenCV DNN backend selected but net is not initialized"));
        return false;
    }

    cv::Mat Blob = cv::dnn::blobFromImage(
        ModelInputBGR,
        1.0 / 255.0,
        cv::Size(ModelInputWidth, ModelInputHeight),
        cv::Scalar(),
        true,
        false);

    std::vector<cv::Mat> Outputs;
    auto ForwardOnce = [&]() -> void
    {
        Outputs.clear();
        OpenCVDnnNet.setInput(Blob);
        const std::vector<cv::String> OutNames = OpenCVDnnNet.getUnconnectedOutLayersNames();
        if (OutNames.empty())
        {
            Outputs.push_back(OpenCVDnnNet.forward());
        }
        else
        {
            OpenCVDnnNet.forward(Outputs, OutNames);
        }
    };

    try
    {
        ForwardOnce();
    }
    catch (const cv::Exception& e)
    {
        const FString Err = FString(e.what());
        const bool bBackendTargetMismatch =
            Err.Contains(TEXT("validateBackendAndTarget")) ||
            Err.Contains(TEXT("preferableBackend"));

        if (!bBackendTargetMismatch)
        {
            UE_LOG(LogTemp, Error, TEXT("OpenCV DNN forward failed: %s"), *Err);
            return false;
        }

        UE_LOG(LogTemp, Warning, TEXT("OpenCV DNN backend/target mismatch (%s). Falling back to CPU."),
            *Err);
        try
        {
            OpenCVDnnNet.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
            OpenCVDnnNet.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
            ForwardOnce();
        }
        catch (const cv::Exception& RetryErr)
        {
            UE_LOG(LogTemp, Error, TEXT("OpenCV DNN CPU fallback failed: %s"), *FString(RetryErr.what()));
            return false;
        }
    }

    cv::Mat ChosenOut;
    int64 BestScore = -1;
    for (const cv::Mat& OutRef : Outputs)
    {
        cv::Mat Out = OutRef;
        if (Out.empty())
        {
            continue;
        }

        int32 NonSingletonA = 0;
        int32 NonSingletonB = 0;
        int32 NonSingletonCount = 0;
        for (int32 d = 0; d < Out.dims; ++d)
        {
            if (Out.size[d] > 1)
            {
                if (NonSingletonCount == 0)
                {
                    NonSingletonA = Out.size[d];
                }
                else if (NonSingletonCount == 1)
                {
                    NonSingletonB = Out.size[d];
                }
                ++NonSingletonCount;
            }
        }

        int64 Score = static_cast<int64>(Out.total());
        if (NonSingletonCount == 2)
        {
            const int32 MinSide = FMath::Min(NonSingletonA, NonSingletonB);
            const int32 MaxSide = FMath::Max(NonSingletonA, NonSingletonB);
            if (MinSide >= 5 && MinSide <= 512 && MaxSide >= 16)
            {
                Score += 1000000000LL;
            }
            if (MinSide == 6)
            {
                Score += 100000000LL;
            }
        }

        if (Score > BestScore)
        {
            BestScore = Score;
            ChosenOut = Out;
        }
    }

    if (ChosenOut.empty())
    {
        UE_LOG(LogTemp, Error, TEXT("OpenCV DNN produced no outputs"));
        return false;
    }

    if (!ChosenOut.isContinuous())
    {
        ChosenOut = ChosenOut.clone();
    }
    if (ChosenOut.type() != CV_32F)
    {
        ChosenOut.convertTo(ChosenOut, CV_32F);
    }

    int32 OutputChannels = 0;
    int32 OutputDetections = 0;
    if (ChosenOut.dims == 2)
    {
        OutputChannels = ChosenOut.rows;
        OutputDetections = ChosenOut.cols;
    }
    else if (ChosenOut.dims == 3 && ChosenOut.size[0] == 1)
    {
        OutputChannels = ChosenOut.size[1];
        OutputDetections = ChosenOut.size[2];
    }
    else
    {
        TArray<int32> NonSingletonDims;
        for (int32 d = 0; d < ChosenOut.dims; ++d)
        {
            if (ChosenOut.size[d] > 1)
            {
                NonSingletonDims.Add(ChosenOut.size[d]);
            }
        }
        if (NonSingletonDims.Num() != 2)
        {
            UE_LOG(LogTemp, Error, TEXT("Unsupported OpenCV output dims: %d"), ChosenOut.dims);
            return false;
        }
        OutputChannels = NonSingletonDims[0];
        OutputDetections = NonSingletonDims[1];
    }

    const int32 TotalValues = OutputChannels * OutputDetections;
    if (TotalValues <= 0 || ChosenOut.total() < static_cast<size_t>(TotalValues))
    {
        UE_LOG(LogTemp, Error, TEXT("OpenCV output size mismatch: channels=%d, dets=%d, total=%lld"),
            OutputChannels, OutputDetections, static_cast<long long>(ChosenOut.total()));
        return false;
    }

    TrtOutputHost.SetNumUninitialized(TotalValues);
    FMemory::Memcpy(TrtOutputHost.GetData(), ChosenOut.ptr<float>(), static_cast<size_t>(TotalValues) * sizeof(float));
    TrtOutputElements = TotalValues;
    TrtOutputChannels = OutputChannels;
    TrtOutputDetections = OutputDetections;

    if (bLogPerf)
    {
        UE_LOG(LogTemp, Log, TEXT("OpenCV output shape: raw=%s resolved=[%d, %d] total=%d"),
            *MatShapeToString(ChosenOut), TrtOutputChannels, TrtOutputDetections, TrtOutputElements);
    }

    return true;
}

TArray<FDetectionResult> UMyActorComponent::ProcessWithOpenCV_BG(const TArray<FColor>& Bitmap, int32 Width, int32 Height, int Threshold, double* OutInferenceMs)
{
    if (OutInferenceMs)
    {
        *OutInferenceMs = -1.0;
    }

    if (!bIsModelLoaded)
    {
        return {};
    }

    // Wrap UE's pixel buffer directly (BGRA8) to avoid per-pixel copies.
    cv::Mat ImageBGRA(Height, Width, CV_8UC4, const_cast<FColor*>(Bitmap.GetData()));
    cv::Mat imgBGR;
    cv::cvtColor(ImageBGRA, imgBGR, cv::COLOR_BGRA2BGR);

    cv::Mat modelInputBGR;
    float InputScale = 1.0f;
    float PadX = 0.0f;
    float PadY = 0.0f;
    if (bUseLetterbox)
    {
        const float RatioW = static_cast<float>(ModelInputWidth) / static_cast<float>(Width);
        const float RatioH = static_cast<float>(ModelInputHeight) / static_cast<float>(Height);
        InputScale = FMath::Min(RatioW, RatioH);

        const int32 NewW = FMath::Clamp(FMath::RoundToInt(static_cast<float>(Width) * InputScale), 1, ModelInputWidth);
        const int32 NewH = FMath::Clamp(FMath::RoundToInt(static_cast<float>(Height) * InputScale), 1, ModelInputHeight);

        cv::Mat resized;
        cv::resize(imgBGR, resized, cv::Size(NewW, NewH), 0.0, 0.0, cv::INTER_LINEAR);

        const int32 PadLeft = (ModelInputWidth - NewW) / 2;
        const int32 PadTop = (ModelInputHeight - NewH) / 2;
        const uint8 PadValue = static_cast<uint8>(FMath::Clamp(LetterboxValue, 0, 255));

        modelInputBGR = cv::Mat(ModelInputHeight, ModelInputWidth, CV_8UC3, cv::Scalar(PadValue, PadValue, PadValue));
        resized.copyTo(modelInputBGR(cv::Rect(PadLeft, PadTop, NewW, NewH)));

        PadX = static_cast<float>(PadLeft);
        PadY = static_cast<float>(PadTop);
    }
    else
    {
        cv::resize(imgBGR, modelInputBGR, cv::Size(ModelInputWidth, ModelInputHeight), 0.0, 0.0, cv::INTER_LINEAR);
    }

    const double T0 = FPlatformTime::Seconds();
    bool bInferOk = false;
    if (InferenceBackend == EDetectionInferenceBackend::TensorRT)
    {
        bInferOk = RunTensorRTInference_BG(modelInputBGR);
    }
    else if (InferenceBackend == EDetectionInferenceBackend::OpenCVDNN)
    {
        bInferOk = RunOpenCVDNNInference_BG(modelInputBGR);
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("Unknown inference backend value: %d"), static_cast<int32>(InferenceBackend));
        return {};
    }

    if (!bInferOk)
    {
        return {};
    }

    const double InferMs = (FPlatformTime::Seconds() - T0) * 1000.0;
    if (OutInferenceMs)
    {
        *OutInferenceMs = InferMs;
    }
    if (bLogPerf)
    {
        const TCHAR* BackendLabel = (InferenceBackend == EDetectionInferenceBackend::TensorRT)
            ? TEXT("TensorRT")
            : TEXT("OpenCV DNN");
        UE_LOG(LogTemp, Log, TEXT("%s forward: %.1f ms"), BackendLabel, InferMs);
    }

    std::vector<cv::Rect> boxes;
    std::vector<float> confidences;
    std::vector<int> class_ids;

    const float ConfThreshold = FMath::Clamp(
        (ConfidenceThreshold > 0.0f) ? ConfidenceThreshold : static_cast<float>(Threshold) / 100.0f,
        0.01f, 0.99f);
    const float IoUThreshold = FMath::Clamp(NmsThreshold, 0.01f, 0.99f);
    const float* OutData = TrtOutputHost.GetData();

    auto CommitMappedBox = [&](const cv::Rect& Box, float Score, int32 ClassId)
    {
        boxes.push_back(Box);
        class_ids.push_back(ClassId);
        confidences.push_back(Score);
    };

    auto MapXYXYToSource = [&](float x1, float y1, float x2, float y2, bool bUndoLetterbox, cv::Rect& OutBox, float& OutOverflow) -> bool
    {
        OutOverflow = 0.0f;
        const float MaxAbsCoord = FMath::Max(
            FMath::Max(FMath::Abs(x1), FMath::Abs(y1)),
            FMath::Max(FMath::Abs(x2), FMath::Abs(y2)));
        if (MaxAbsCoord <= 2.0f)
        {
            x1 *= static_cast<float>(ModelInputWidth);
            x2 *= static_cast<float>(ModelInputWidth);
            y1 *= static_cast<float>(ModelInputHeight);
            y2 *= static_cast<float>(ModelInputHeight);
        }

        const bool bApplyUndo = bUndoLetterbox && bUseLetterbox && (InputScale > KINDA_SMALL_NUMBER);
        float sx1 = x1;
        float sy1 = y1;
        float sx2 = x2;
        float sy2 = y2;
        if (bApplyUndo)
        {
            sx1 = (x1 - PadX) / InputScale;
            sy1 = (y1 - PadY) / InputScale;
            sx2 = (x2 - PadX) / InputScale;
            sy2 = (y2 - PadY) / InputScale;
        }
        else
        {
            sx1 = x1 * static_cast<float>(Width) / static_cast<float>(ModelInputWidth);
            sy1 = y1 * static_cast<float>(Height) / static_cast<float>(ModelInputHeight);
            sx2 = x2 * static_cast<float>(Width) / static_cast<float>(ModelInputWidth);
            sy2 = y2 * static_cast<float>(Height) / static_cast<float>(ModelInputHeight);
        }

        const float MinX = 0.0f;
        const float MaxX = static_cast<float>(Width - 1);
        const float MinY = 0.0f;
        const float MaxY = static_cast<float>(Height - 1);
        const float RawL = FMath::Min(sx1, sx2);
        const float RawT = FMath::Min(sy1, sy2);
        const float RawR = FMath::Max(sx1, sx2);
        const float RawB = FMath::Max(sy1, sy2);

        auto ClampWithOverflow = [&](float V, float MinV, float MaxV) -> float
        {
            if (V < MinV)
            {
                OutOverflow += (MinV - V);
                return MinV;
            }
            if (V > MaxV)
            {
                OutOverflow += (V - MaxV);
                return MaxV;
            }
            return V;
        };

        const float Lf = ClampWithOverflow(RawL, MinX, MaxX);
        const float Tf = ClampWithOverflow(RawT, MinY, MaxY);
        const float Rf = ClampWithOverflow(RawR, MinX, MaxX);
        const float Bf = ClampWithOverflow(RawB, MinY, MaxY);

        const int32 Left = FMath::FloorToInt(Lf);
        const int32 Top = FMath::FloorToInt(Tf);
        const int32 Right = FMath::CeilToInt(Rf);
        const int32 Bottom = FMath::CeilToInt(Bf);
        const int32 BoxW = Right - Left;
        const int32 BoxH = Bottom - Top;
        if (BoxW <= 1 || BoxH <= 1)
        {
            return false;
        }

        OutBox = cv::Rect(Left, Top, BoxW, BoxH);
        return true;
    };

    if (!bIsOnnxModel || !OutData || TrtOutputChannels <= 0 || TrtOutputDetections <= 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("Unexpected model output state, skipping detections"));
    }
    else
    {
        // Normalize output layout to [attrs, dets].
        int32 Attrs = TrtOutputChannels;
        int32 Dets = TrtOutputDetections;
        bool bAttrsMajor = true;
        if (TrtOutputDetections <= 256 && TrtOutputChannels > TrtOutputDetections)
        {
            Attrs = TrtOutputDetections;
            Dets = TrtOutputChannels;
            bAttrsMajor = false;
        }

        auto ReadAttr = [&](int32 Attr, int32 Det) -> float
        {
            return bAttrsMajor ? OutData[(Attr * Dets) + Det] : OutData[(Det * Attrs) + Attr];
        };

        const int32 NumClassesHint = static_cast<int32>(ClassNames.size());

        if (Attrs == 6)
        {
            // End-to-end layout: [x1, y1, x2, y2, score, class_id].
            for (int32 i = 0; i < Dets; ++i)
            {
                const float x1 = ReadAttr(0, i);
                const float y1 = ReadAttr(1, i);
                const float x2 = ReadAttr(2, i);
                const float y2 = ReadAttr(3, i);
                float score = ReadAttr(4, i);
                const float classF = ReadAttr(5, i);
                if (!FMath::IsFinite(score) || !FMath::IsFinite(classF))
                {
                    continue;
                }

                if (score < 0.0f || score > 1.0f)
                {
                    score = Sigmoidf(score);
                }
                if (score < ConfThreshold)
                {
                    continue;
                }

                int32 classId = FMath::RoundToInt(classF);
                if (NumClassesHint > 0)
                {
                    classId = FMath::Clamp(classId, 0, NumClassesHint - 1);
                }
                else
                {
                    classId = FMath::Max(0, classId);
                }

                cv::Rect MappedBox;
                float Overflow = 0.0f;
                if (MapXYXYToSource(x1, y1, x2, y2, bUseLetterbox, MappedBox, Overflow))
                {
                    CommitMappedBox(MappedBox, score, classId);
                }
            }
        }
        else if (Attrs >= 5)
        {
            // Raw layout: [cx, cy, w, h, ...classes] or [cx, cy, w, h, obj, ...classes].
            const bool bHasObjectness = (NumClassesHint > 0 && (Attrs - 5) == NumClassesHint);
            const int32 ClassStart = bHasObjectness ? 5 : 4;
            const int32 NumClasses = Attrs - ClassStart;
            if (NumClasses <= 0)
            {
                UE_LOG(LogTemp, Warning, TEXT("Unsupported raw output shape [%d, %d]"), Attrs, Dets);
            }
            else
            {
                for (int32 i = 0; i < Dets; ++i)
                {
                    const float cx = ReadAttr(0, i);
                    const float cy = ReadAttr(1, i);
                    const float w = ReadAttr(2, i);
                    const float h = ReadAttr(3, i);
                    float obj = bHasObjectness ? ReadAttr(4, i) : 1.0f;
                    if (bHasObjectness && (obj < 0.0f || obj > 1.0f))
                    {
                        obj = Sigmoidf(obj);
                    }

                    float bestScore = 0.0f;
                    int32 bestClass = -1;
                    for (int32 c = 0; c < NumClasses; ++c)
                    {
                        float cls = ReadAttr(ClassStart + c, i);
                        if (cls < 0.0f || cls > 1.0f)
                        {
                            cls = Sigmoidf(cls);
                        }
                        const float score = obj * cls;
                        if (score > bestScore)
                        {
                            bestScore = score;
                            bestClass = c;
                        }
                    }

                    if (bestClass < 0 || bestScore < ConfThreshold)
                    {
                        continue;
                    }

                    const float x1 = cx - (w * 0.5f);
                    const float y1 = cy - (h * 0.5f);
                    const float x2 = cx + (w * 0.5f);
                    const float y2 = cy + (h * 0.5f);
                    cv::Rect MappedBox;
                    float Overflow = 0.0f;
                    if (MapXYXYToSource(x1, y1, x2, y2, bUseLetterbox, MappedBox, Overflow))
                    {
                        CommitMappedBox(MappedBox, bestScore, bestClass);
                    }
                }
            }
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("Unsupported output shape [%d, %d]"), Attrs, Dets);
        }
    }

    std::vector<int> indices;
    ApplyNms(boxes, confidences, ConfThreshold, IoUThreshold, indices);

    if (InferenceBackend == EDetectionInferenceBackend::OpenCVDNN && indices.empty())
    {
        static int32 OpenCvEmptyLogDecimator = 0;
        if ((OpenCvEmptyLogDecimator++ % 30) == 0)
        {
            float MinV = 0.0f;
            float MaxV = 0.0f;
            if (OutData && TrtOutputElements > 0)
            {
                MinV = OutData[0];
                MaxV = OutData[0];
                const int32 ProbeCount = FMath::Min(TrtOutputElements, 8192);
                for (int32 i = 1; i < ProbeCount; ++i)
                {
                    MinV = FMath::Min(MinV, OutData[i]);
                    MaxV = FMath::Max(MaxV, OutData[i]);
                }
            }

            UE_LOG(LogTemp, Warning, TEXT("OpenCVDNN produced 0 final boxes (raw=%d, shape=[%d,%d], value_range=[%.4f, %.4f], conf=%.2f, nms=%.2f)"),
                static_cast<int32>(boxes.size()), TrtOutputChannels, TrtOutputDetections, MinV, MaxV, ConfThreshold, IoUThreshold);
        }
    }

    TArray<FDetectionResult> Out;
    for (int idx : indices) {
        cv::Rect box = boxes[idx];
        TArray<FVector2D> corners;
        RectToCorners(box, corners);

        FString label = FString::Printf(
            TEXT("%s: %.2f"),
            (class_ids[idx] >= 0 && static_cast<size_t>(class_ids[idx]) < ClassNames.size())
                ? *FString(ClassNames[class_ids[idx]].c_str())
                : TEXT("Unknown"),
            confidences[idx]
        );

        FDetectionResult det;
        det.Corners = corners;
        det.Label = label;
        det.ClassId = class_ids[idx];
        Out.Add(det);
    }
    return Out;
}

// void UMyActorComponent::TestOpenCV(){
//     UE_LOG(LogTemp, Log, TEXT("Testing OpenCV..."));
//     int testDim[3] = {2, 3, 4};
//     cv::Mat testMat(3, testDim, CV_32FC1);
//     UE_LOG(
//         LogTemp, Log,
//         TEXT("dimension = %d, %d, %d"),
//         testMat.size[0], testMat.size[1], testMat.size[2]);
//     UE_LOG(LogTemp, Log, TEXT("Testing Done!"));
// }

// // Called when the game starts
// void UMyActorComponent::BeginPlay()
// {
// 	Super::BeginPlay();
//     TestOpenCV();
//     InitializeSceneCapture();
// 	// ...
// 	GetWorld()->GetTimerManager().SetTimer(
//         TimerHandle_Capture,                    // FTimerHandle (store in your .h)
//         this,
//         &UMyActorComponent::TickCapture,        // function to call
//         0.2f,                                   // interval (seconds)
//         true,                                   // looping
//         0.5f                                    // initial delay (seconds)
//     );
// }

// void UMyActorComponent::TickCapture()
// {
//     CaptureAndProcess(50); // example threshold
// }

// void UMyActorComponent::InitializeSceneCapture()
// {
//     // AActor* OwnerActor = GetOwner();

//     // FVector2D ViewportSize = FVector2D(1, 1);
//     // GEngine->GameViewport->GetViewportSize(ViewportSize);

//     // UCameraComponent* Camera = OwnerActor->FindComponentByClass<UCameraComponent>();

//     // // Create SceneCaptureComponent2D
//     // SceneCaptureComponent = NewObject<USceneCaptureComponent2D>(this);
//     // SceneCaptureComponent->SetupAttachment(Camera);
//     // // UE_LOG(LogTemp, Log, TEXT(GetOwner()->GetParentComponent()));
//     // SceneCaptureComponent->RegisterComponent();

//     // // Create RenderTarget
//     // RenderTarget = NewObject<UTextureRenderTarget2D>();
//     // RenderTarget->InitAutoFormat(ViewportSize.X, ViewportSize.Y); // Set desired resolution
//     // RenderTarget->UpdateResource();

//     // OverlayText = NewObject<UTexture2D>();
//     // // OverlayText->InitAutoFormat(ViewportSize.X, ViewportSize.Y); // Set desired resolution
//     // OverlayText->UpdateResource();

//     // // Configure SceneCaptureComponent
//     // SceneCaptureComponent->TextureTarget = RenderTarget;
//     // SceneCaptureComponent->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
//     // UE_LOG(LogTemp, Log, TEXT("Init Complete!"));
//     std::string FilePath1 = "C:\\Users\\Saranthyr\\Documents\\Unreal Projects\\EagleEye\\Source\\EagleEye\\yolov7.weights";
//     std::string FilePath3 = "C:\\Users\\Saranthyr\\Documents\\Unreal Projects\\EagleEye\\Source\\EagleEye\\yolov7.cfg";
//     std::string FilePath2 = "C:\\Users\\Saranthyr\\Documents\\Unreal Projects\\EagleEye\\Source\\EagleEye\\coco.names";
//     get_class_names(FilePath2);
//     get_yolo_net(FilePath3, FilePath1);
//     // FString VectorString;
//     // // UE_LOG(LogTemp, Warning, TEXT("%s"), );
//     // for (size_t i = 0; i < class_names.size(); i++) {
//     //     VectorString += FString(class_names[i].c_str()) + TEXT(" ");
//     // }
//     // UE_LOG(LogTemp, Warning, TEXT("%s"), *VectorString);

    
// }

// void UMyActorComponent::CaptureAndProcess(int threshold)
// {
//     // if (!RenderTarget)
//     // {
//     //     UE_LOG(LogTemp, Warning, TEXT("RenderTarget not initialized."));
//     //     return;
//     // }

//     // // Capture the current frame
//     // FTextureRenderTargetResource* RenderTargetResource = RenderTarget->GameThread_GetRenderTargetResource();
//     // if (!RenderTargetResource)
//     // {
//     //     UE_LOG(LogTemp, Warning, TEXT("RenderTargetResource not available."));
//     //     return;
//     // }

//     // TArray<FColor> Bitmap;
//     // FReadSurfaceDataFlags ReadPixelFlags(RCM_UNorm);
//     // ReadPixelFlags.SetLinearToGamma(false);

//     // // Read the pixels into the Bitmap array
//     // if (RenderTargetResource->ReadPixels(Bitmap))
//     // {
//     //     int32 Width = RenderTarget->SizeX;
//     //     int32 Height = RenderTarget->SizeY;

//     //     ProcessWithOpenCV(Bitmap, Width, Height, threshold, yolo_net);

//     //     // OverlayText = FOpenCVHelper::TextureFromCvMat(Img);
//     // }
//     // else
//     // {
//     //     UE_LOG(LogTemp, Warning, TEXT("Failed to read pixels from RenderTarget."));
//     // }
//     if (!GEngine || !GEngine->GameViewport) return;

//     FViewport* Viewport = GEngine->GameViewport->Viewport;
//     if (!Viewport) return;

//     TArray<FColor> Bitmap;
//     FIntPoint Size = Viewport->GetSizeXY();

//     if (Size.X <= 0 || Size.Y <= 0) return;

//     if (!Viewport->ReadPixels(Bitmap)) return;

//     if (Bitmap.Num() != Size.X * Size.Y) return;

//     ProcessWithOpenCV(Bitmap, Size.X, Size.Y, threshold, yolo_net);
// }

// void UMyActorComponent::get_yolo_net(const std::string& FilePath1, const std::string& FilePath2) {
//     try
//     {
//         yolo_net = cv::dnn::readNetFromDarknet(FilePath1, FilePath2);

//     }
//     catch(const std::exception& e)
//     {
//         FString str(e.what());
//         UE_LOG(LogTemp, Error, TEXT("%s"), *str);
//     }
    
// }

// TArray<FDetectionResult> UMyActorComponent::ProcessWithOpenCV(const TArray<FColor>& Bitmap, int32 Width, int32 Height, int threshold, cv::dnn::Net& net) {
//     cv::Mat Image(Height, Width, CV_8UC4);
//     TArray<FDetectionResult> results;

//     for (int32 y = 0; y < Height; y++)
//     {
//         for (int32 x = 0; x < Width; x++)
//         {
//             const FColor& Pixel = Bitmap[y * Width + x];
//             Image.at<cv::Vec4b>(y, x) = cv::Vec4b(Pixel.B, Pixel.G, Pixel.R, Pixel.A); // BGRA format
//         }
//     }

//     cv::Mat img;
//     cv::Mat img2;
//     cv::cvtColor(Image, img, cv::COLOR_BGRA2BGR);
//     img2 = cv::dnn::blobFromImage(img, 1/255.f, cv::Size(608, 608), cv::Scalar(), true, false);
//     net.setPreferableBackend(cv::dnn::DNN_BACKEND_CUDA);
//     net.setPreferableTarget(cv::dnn::DNN_TARGET_CUDA);

//     net.setInput(img2);

//     std::vector<std::string> names;
//     if(names.empty()){
//         std::vector<int32_t> out_layers = net.getUnconnectedOutLayers();
//         std::vector<std::string> layers_names = net.getLayerNames();
//         names.resize( out_layers.size() );
//         for( size_t i = 0; i < out_layers.size(); ++i ){
//             names[i] = layers_names[out_layers[i] - 1];
//         }
//     }

//     std::vector<cv::Mat> outputs;

//     try
//     {
//         net.forward(outputs, names);
//     }
//     catch(const std::exception& e)
//     {
//         FString str(e.what());
//         UE_LOG(LogTemp, Error, TEXT("%s"), *str);
//     }
    

//     std::vector<cv::Rect> boxes;
//     std::vector<float> confidences;
//     std::vector<int> class_ids;


//     cv::Mat output = outputs[0];

//     for ( cv::Mat& det : outputs ) {
//         for( int32_t i = 0; i < det.rows; i++) {
//             cv::Mat region = det.row( i );
//             cv::Mat scores = region.colRange( 5, det.cols );
//             cv::Point class_id;
//             double confidence;
//             cv::minMaxLoc( scores, 0, &confidence, 0, &class_id );
//             constexpr float thr = 0.4;
//             if( thr > confidence){
//                 continue;
//             }
//             const int32_t x_center = static_cast<int32_t>( region.at<float>( 0 ) * Width);
//             const int32_t y_center = static_cast<int32_t>( region.at<float>( 1 ) * Height );
//             const int32_t width    = static_cast<int32_t>( region.at<float>( 2 ) * Width );
//             const int32_t height   = static_cast<int32_t>( region.at<float>( 3 ) * Height );
//             const cv::Rect rectangle  = cv::Rect( x_center - ( width / 2 ), y_center - ( height / 2 ), width, height );

//             // Add Class ID, Confidence, Rectangle
//             class_ids.push_back( class_id.x );
//             confidences.push_back( confidence );
//             boxes.push_back( rectangle );
//         }
//     }

//     std::vector<int> indices;
//     cv::dnn::NMSBoxes(boxes, confidences, 0.5, 0.4, indices);

//     for (int idx : indices) {
//         if (idx < boxes.size()) {
//             cv::Rect box = boxes[idx];

//             // Corners
//             TArray<FVector2D> corners;
//             corners.Add(FVector2D(box.x, box.y));
//             corners.Add(FVector2D(box.x + box.width, box.y));
//             corners.Add(FVector2D(box.x + box.width, box.y + box.height));
//             corners.Add(FVector2D(box.x, box.y + box.height));

//             // Label
//             FString label = FString::Printf(
//                 TEXT("%s: %.2f"),
//                 *FString(class_names[class_ids[idx]].c_str()),
//                 confidences[idx]
//             );

//             FDetectionResult detection;
//             detection.Corners = corners;
//             detection.Label   = label;

//             results.Add(detection);
//         }
//     }

//     LastFrameDetections = results;  // store for HUD
//     return results;

//     // return Image;
// }


// // Called every frame
// void UMyActorComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
// {
// 	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);	// ...
// }

// void UMyActorComponent::UpdateDetections(const TArray<FDetectionResult>& NewDetections)
// {
//     LastFrameDetections = NewDetections;
// }
