// Fill out your copyright notice in the Description page of Project Settings.

#include "MyActorComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "AI/CrowDetectionShareSubsystem.h"
#include "AI/CrowVisionSubsystem.h"
#include "EagleEyeDetectionSettings.h"
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
#include <array>
#if WITH_TENSORRT
#include <NvInfer.h>
#include <NvInferVersion.h>
#endif
#if WITH_ONNXRUNTIME_DML
#include <onnxruntime/core/providers/dml/dml_provider_factory.h>
#endif
#if WITH_ONNXRUNTIME_MIGRAPHX
#include <onnxruntime/core/providers/migraphx/migraphx_provider_factory.h>
#endif

namespace
{
#if WITH_TENSORRT
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
#endif

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

    void AddUniqueNormalizedDirectory(TArray<FString>& Directories, const FString& Directory)
    {
        if (Directory.IsEmpty())
        {
            return;
        }

        FString Normalized = FPaths::ConvertRelativePathToFull(Directory);
        FPaths::NormalizeDirectoryName(Normalized);
        Directories.AddUnique(Normalized);
    }

    TArray<FString> GetRuntimeModelSearchDirectories()
    {
        TArray<FString> Directories;

        AddUniqueNormalizedDirectory(Directories, FPaths::Combine(FPlatformProcess::BaseDir(), TEXT("Models")));
        AddUniqueNormalizedDirectory(Directories, FPaths::Combine(FPaths::LaunchDir(), TEXT("Models")));
        AddUniqueNormalizedDirectory(Directories, FPaths::Combine(FPaths::ProjectDir(), TEXT("Models")));
        AddUniqueNormalizedDirectory(Directories, FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Models")));

        // Editor/development fallback. Packaged builds receive these files through RuntimeDependencies.
        AddUniqueNormalizedDirectory(Directories, FPaths::Combine(FPaths::ProjectDir(), TEXT("Source"), TEXT("EagleEye")));

        return Directories;
    }

    bool RuntimeFileExists(const FString& Path)
    {
        return !Path.IsEmpty() && FPlatformFileManager::Get().GetPlatformFile().FileExists(*Path);
    }

    bool IsDetectionMetricLoggingEnabled()
    {
        const UEagleEyeDetectionSettings* Settings = GetDefault<UEagleEyeDetectionSettings>();
        return !Settings || Settings->bEnableDetectionMetricLogs;
    }

    FString NormalizeRuntimeFilePath(const FString& Path)
    {
        FString Normalized = FPaths::ConvertRelativePathToFull(Path);
        FPaths::NormalizeFilename(Normalized);
        return Normalized;
    }

    FString ResolveRuntimeFilePath(const FString& RequestedPath)
    {
        if (RequestedPath.IsEmpty())
        {
            return FString();
        }

        if (!FPaths::IsRelative(RequestedPath))
        {
            return NormalizeRuntimeFilePath(RequestedPath);
        }

        const FString DirectPath = NormalizeRuntimeFilePath(RequestedPath);
        if (RuntimeFileExists(DirectPath))
        {
            return DirectPath;
        }

        const FString CleanRequestedPath = RequestedPath.Replace(TEXT("\\"), TEXT("/"));
        for (const FString& Directory : GetRuntimeModelSearchDirectories())
        {
            const FString CandidatePath = NormalizeRuntimeFilePath(FPaths::Combine(Directory, CleanRequestedPath));
            if (RuntimeFileExists(CandidatePath))
            {
                return CandidatePath;
            }
        }

        const FString FileName = FPaths::GetCleanFilename(CleanRequestedPath);
        if (!FileName.Equals(CleanRequestedPath, ESearchCase::IgnoreCase))
        {
            for (const FString& Directory : GetRuntimeModelSearchDirectories())
            {
                const FString CandidatePath = NormalizeRuntimeFilePath(FPaths::Combine(Directory, FileName));
                if (RuntimeFileExists(CandidatePath))
                {
                    return CandidatePath;
                }
            }
        }

        return DirectPath;
    }

    std::string ToUtf8Path(const FString& Path)
    {
        return std::string(TCHAR_TO_UTF8(*Path));
    }

    const TCHAR* BackendToString(EDetectionInferenceBackend Backend)
    {
        switch (Backend)
        {
        case EDetectionInferenceBackend::Auto:
            return TEXT("Auto");
        case EDetectionInferenceBackend::TensorRT:
            return TEXT("TensorRT");
        case EDetectionInferenceBackend::ONNXRuntime:
            return TEXT("ONNX Runtime");
        case EDetectionInferenceBackend::OpenCVDNN:
            return TEXT("OpenCV DNN");
        default:
            return TEXT("Unknown");
        }
    }

    const TCHAR* OnnxProviderToString(EOnnxRuntimeExecutionProvider Provider)
    {
        switch (Provider)
        {
        case EOnnxRuntimeExecutionProvider::Auto:
            return TEXT("Auto");
        case EOnnxRuntimeExecutionProvider::DirectML:
            return TEXT("DirectML");
        case EOnnxRuntimeExecutionProvider::MIGraphX:
            return TEXT("MIGraphX");
        case EOnnxRuntimeExecutionProvider::CPU:
            return TEXT("CPU");
        default:
            return TEXT("Unknown");
        }
    }

    bool IsOnnxRuntimeProviderCompiled(EOnnxRuntimeExecutionProvider Provider)
    {
        switch (Provider)
        {
        case EOnnxRuntimeExecutionProvider::CPU:
            return true;
        case EOnnxRuntimeExecutionProvider::DirectML:
#if WITH_ONNXRUNTIME_DML
            return true;
#else
            return false;
#endif
        case EOnnxRuntimeExecutionProvider::MIGraphX:
#if WITH_ONNXRUNTIME_MIGRAPHX
            return true;
#else
            return false;
#endif
        default:
            return false;
        }
    }

    enum class EDetectedGpuVendor : uint8
    {
        Nvidia,
        AMD,
        Other
    };

    enum class EDetectedOperatingSystem : uint8
    {
        Windows,
        Linux,
        Mac,
        Other
    };

    EDetectedGpuVendor DetectGpuVendor(const FString& AdapterName)
    {
        if (AdapterName.Contains(TEXT("NVIDIA"), ESearchCase::IgnoreCase) ||
            AdapterName.Contains(TEXT("GeForce"), ESearchCase::IgnoreCase) ||
            AdapterName.Contains(TEXT("RTX"), ESearchCase::IgnoreCase) ||
            AdapterName.Contains(TEXT("Quadro"), ESearchCase::IgnoreCase) ||
            AdapterName.Contains(TEXT("Tesla"), ESearchCase::IgnoreCase))
        {
            return EDetectedGpuVendor::Nvidia;
        }

        if (AdapterName.Contains(TEXT("AMD"), ESearchCase::IgnoreCase) ||
            AdapterName.Contains(TEXT("Radeon"), ESearchCase::IgnoreCase) ||
            AdapterName.Contains(TEXT("Advanced Micro Devices"), ESearchCase::IgnoreCase))
        {
            return EDetectedGpuVendor::AMD;
        }

        return EDetectedGpuVendor::Other;
    }

    EDetectedOperatingSystem DetectOperatingSystem()
    {
#if PLATFORM_WINDOWS
        return EDetectedOperatingSystem::Windows;
#elif PLATFORM_LINUX
        return EDetectedOperatingSystem::Linux;
#elif PLATFORM_MAC
        return EDetectedOperatingSystem::Mac;
#else
        return EDetectedOperatingSystem::Other;
#endif
    }

    const TCHAR* GpuVendorToString(EDetectedGpuVendor Vendor)
    {
        switch (Vendor)
        {
        case EDetectedGpuVendor::Nvidia:
            return TEXT("NVIDIA");
        case EDetectedGpuVendor::AMD:
            return TEXT("AMD");
        case EDetectedGpuVendor::Other:
        default:
            return TEXT("Other");
        }
    }

    const TCHAR* OperatingSystemToString(EDetectedOperatingSystem OperatingSystem)
    {
        switch (OperatingSystem)
        {
        case EDetectedOperatingSystem::Windows:
            return TEXT("Windows");
        case EDetectedOperatingSystem::Linux:
            return TEXT("Linux");
        case EDetectedOperatingSystem::Mac:
            return TEXT("macOS");
        case EDetectedOperatingSystem::Other:
        default:
            return TEXT("Other");
        }
    }

    constexpr double FfmpegStopTimeoutSeconds = 3.0;
    constexpr double FfmpegFastShutdownTimeoutSeconds = 1.0;
    constexpr double FfmpegTerminateTimeoutSeconds = 0.5;
    TSet<FString> ResetFovDetectionMetricsTablesThisRun;

    FString EscapeCsvField(const FString& Value)
    {
        if (!Value.Contains(TEXT(",")) && !Value.Contains(TEXT("\"")) && !Value.Contains(TEXT("\n")) && !Value.Contains(TEXT("\r")))
        {
            return Value;
        }

        FString Escaped = Value;
        Escaped.ReplaceInline(TEXT("\""), TEXT("\"\""));
        return FString::Printf(TEXT("\"%s\""), *Escaped);
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

    if (OwnerCaptureRenderTarget &&
        (OwnerCaptureRenderTarget->SizeX != CaptureWidth || OwnerCaptureRenderTarget->SizeY != CaptureHeight))
    {
        PendingOwnerCameraReadback.Reset();
        PendingOwnerCameraReadbackWidth = 0;
        PendingOwnerCameraReadbackHeight = 0;
        PendingOwnerCameraReadbackSubmitTimeSeconds = 0.0;

        OwnerCaptureRenderTarget->ResizeTarget(CaptureWidth, CaptureHeight);
        OwnerCaptureRenderTarget->UpdateResourceImmediate(true);
    }
}

void UMyActorComponent::SetCaptureFPS(float InCaptureFPS)
{
    CaptureFPS = FMath::Clamp(InCaptureFPS, 1.0f, 120.0f);

    UWorld* World = GetWorld();
    if (!World || !TimerHandle_Capture.IsValid())
    {
        return;
    }

    const float CaptureInterval = 1.0f / CaptureFPS;
    World->GetTimerManager().SetTimer(
        TimerHandle_Capture,
        this,
        &UMyActorComponent::TickCapture,
        CaptureInterval,
        true,
        CaptureInterval);
}

void UMyActorComponent::SetRecordOwnerCameraCaptureVideo(bool bEnabled)
{
    if (bRecordOwnerCameraCaptureVideo == bEnabled)
    {
        return;
    }

    bRecordOwnerCameraCaptureVideo = bEnabled;

    if (!bRecordOwnerCameraCaptureVideo)
    {
        CloseOwnerCameraVideoWriter();
    }
}

void UMyActorComponent::SetLogFovDetectionMetrics(bool bEnabled)
{
    bLogFovDetectionMetrics = bEnabled;
    if (bLogFovDetectionMetrics)
    {
        InitFovDetectionMetricsTable();
    }
}

bool UMyActorComponent::ShouldLogFrameTimings() const
{
    const UEagleEyeDetectionSettings* Settings = GetDefault<UEagleEyeDetectionSettings>();
    return bLogFrameTimings && (!Settings || Settings->bEnableDetectionPerformanceLogs);
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
    if (ShouldLogFrameTimings())
    {
        UE_LOG(LogTemp, Log, TEXT("Frame timing log enabled: %s"), *ResolvedFrameTimeCsvPath);
    }
}

void UMyActorComponent::InitFovDetectionMetricsTable()
{
    if (!bLogFovDetectionMetrics || bFovDetectionMetricsTableInitialized)
    {
        return;
    }

    ResolvedFovDetectionMetricsCsvPath = FovDetectionMetricsCsvPath;
    if (ResolvedFovDetectionMetricsCsvPath.IsEmpty())
    {
        ResolvedFovDetectionMetricsCsvPath = FPaths::Combine(
            FPaths::ProjectSavedDir(),
            TEXT("Profiling"),
            TEXT("FovDetectionMetrics.csv"));
    }

    const FString Directory = FPaths::GetPath(ResolvedFovDetectionMetricsCsvPath);
    if (!Directory.IsEmpty())
    {
        IFileManager::Get().MakeDirectory(*Directory, true);
    }

    if (bResetFovDetectionMetricsTableOnBeginPlay &&
        !ResetFovDetectionMetricsTablesThisRun.Contains(ResolvedFovDetectionMetricsCsvPath) &&
        IFileManager::Get().FileExists(*ResolvedFovDetectionMetricsCsvPath))
    {
        IFileManager::Get().Delete(*ResolvedFovDetectionMetricsCsvPath, false, true, true);
    }
    ResetFovDetectionMetricsTablesThisRun.Add(ResolvedFovDetectionMetricsCsvPath);

    if (!IFileManager::Get().FileExists(*ResolvedFovDetectionMetricsCsvPath))
    {
        const FString Header = TEXT("timestamp,world_seconds,owner,source,status,outcome,expected_in_fov,actual_person_detected,distance,horizontal_angle,vertical_angle,expected_pixel_x,expected_pixel_y,source_width,source_height,detection_count,tp,tn,fp,fn,unavailable\n");
        FFileHelper::SaveStringToFile(
            Header,
            *ResolvedFovDetectionMetricsCsvPath,
            FFileHelper::EEncodingOptions::AutoDetect,
            &IFileManager::Get());
    }

    bFovDetectionMetricsTableInitialized = true;
    if (IsDetectionMetricLoggingEnabled())
    {
        UE_LOG(LogTemp, Log, TEXT("FOV detection metrics table enabled: %s"), *ResolvedFovDetectionMetricsCsvPath);
    }
}

void UMyActorComponent::AppendFovDetectionMetricsTableRow(
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
    int32 DetectionCount)
{
    if (!bLogFovDetectionMetrics)
    {
        return;
    }

    InitFovDetectionMetricsTable();
    if (!bFovDetectionMetricsTableInitialized)
    {
        return;
    }

    const FString Line = FString::Printf(
        TEXT("%s,%.4f,%s,%s,%s,%s,%s,%s,%.3f,%.3f,%.3f,%.3f,%.3f,%d,%d,%d,%d,%d,%d,%d,%d\n"),
        *FDateTime::Now().ToIso8601(),
        GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f,
        *EscapeCsvField(GetNameSafe(GetOwner())),
        *EscapeCsvField(FString(SourceLabel)),
        *EscapeCsvField(Status),
        *EscapeCsvField(Outcome),
        bExpectedInFov ? TEXT("true") : TEXT("false"),
        bActualPersonDetected ? TEXT("true") : TEXT("false"),
        Distance,
        HorizontalAngleDegrees,
        VerticalAngleDegrees,
        ExpectedPixel.X,
        ExpectedPixel.Y,
        SourceWidth,
        SourceHeight,
        DetectionCount,
        FovDetectionTruePositiveCount,
        FovDetectionTrueNegativeCount,
        FovDetectionFalsePositiveCount,
        FovDetectionFalseNegativeCount,
        FovDetectionUnavailableSampleCount);

    FFileHelper::SaveStringToFile(
        Line,
        *ResolvedFovDetectionMetricsCsvPath,
        FFileHelper::EEncodingOptions::AutoDetect,
        &IFileManager::Get(),
        FILEWRITE_Append);
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

    const FString BackendLabel = BackendToString(EffectiveInferenceBackend);
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

    ApplyProjectDetectionSettings();

    const bool bNeedsModelInitialization = !bUseFovOnlyPersonDetection && (!bUseSharedVisionModel || bSharedVisionModelHost);
    if (bNeedsModelInitialization)
    {
        const EDetectedOperatingSystem DetectedOS = DetectOperatingSystem();
        const EDetectedGpuVendor DetectedGpu = DetectGpuVendor(GRHIAdapterName);
        UE_LOG(LogTemp, Log, TEXT("Inference backend requested: %s (ONNX provider: %s, OS: %s, GPU: %s, RHI adapter: %s, host=%s)"),
            BackendToString(InferenceBackend),
            OnnxProviderToString(OnnxRuntimeExecutionProvider),
            OperatingSystemToString(DetectedOS),
            GpuVendorToString(DetectedGpu),
            *GRHIAdapterName,
            bSharedVisionModelHost ? TEXT("true") : TEXT("false"));

        FString ResolvedNamesPath = ResolveRuntimeFilePath(TEXT("coco.names"));
        FString ResolvedModelPath = ResolveRuntimeFilePath(TEXT("yolo26x.plan"));
        FString ResolvedCfgPath = ResolveRuntimeFilePath(TEXT("yolov7.cfg"));

        if (!ModelPathOverride.IsEmpty())
        {
            ResolvedModelPath = ResolveRuntimeFilePath(ModelPathOverride);
        }
        if (!DarknetCfgPathOverride.IsEmpty())
        {
            ResolvedCfgPath = ResolveRuntimeFilePath(DarknetCfgPathOverride);
        }
        if (!NamesPathOverride.IsEmpty())
        {
            ResolvedNamesPath = ResolveRuntimeFilePath(NamesPathOverride);
        }

        NamesPath = ToUtf8Path(ResolvedNamesPath);
        WeightsPath = ToUtf8Path(ResolvedModelPath);
        CfgPath = ToUtf8Path(ResolvedCfgPath);

        UE_LOG(LogTemp, Log, TEXT("Detection model path: %s"), *ResolvedModelPath);
        UE_LOG(LogTemp, Log, TEXT("Detection names path: %s"), *ResolvedNamesPath);

        get_class_names();
        InitFrameTimingLog();
        if (!bUseSharedVisionModel && !bSharedVisionModelHost)
        {
            StartWorker();
        }
    }

    InitFovDetectionMetricsTable();

    if (bSharedVisionModelHost)
    {
        if (UWorld* World = GetWorld())
        {
            if (UCrowVisionSubsystem* VisionSubsystem = World->GetSubsystem<UCrowVisionSubsystem>())
            {
                VisionSubsystem->RegisterModelHost(this);
            }
        }
        return;
    }

    if (bUseOwnerCameraCapture)
    {
        if (UWorld* World = GetWorld())
        {
            if (UCrowDetectionShareSubsystem* ShareSubsystem = World->GetSubsystem<UCrowDetectionShareSubsystem>())
            {
                ShareSubsystem->RegisterDetector(GetOwner());
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
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(TimerHandle_Capture);
    }

    CloseOwnerCameraVideoWriter(false);

    if (bSharedVisionModelHost)
    {
        if (UWorld* World = GetWorld())
        {
            if (UCrowVisionSubsystem* VisionSubsystem = World->GetSubsystem<UCrowVisionSubsystem>())
            {
                VisionSubsystem->UnregisterModelHost(this);
            }
        }
        StopWorker();
        Super::EndPlay(EndPlayReason);
        return;
    }

    if (bUseOwnerCameraCapture)
    {
        if (UWorld* World = GetWorld())
        {
            if (UCrowDetectionShareSubsystem* ShareSubsystem = World->GetSubsystem<UCrowDetectionShareSubsystem>())
            {
                ShareSubsystem->UnregisterDetector(GetOwner());
            }
        }
    }

    if (!bUseSharedVisionModel && !bSharedVisionModelHost)
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

    if (bUseFovOnlyPersonDetection || bUseSharedVisionModel || bSharedVisionModelHost)
    {
        return;
    }

    // Pull latest detections every frame to minimize overlay latency.
    CopyResultsFromWorker();
}

void UMyActorComponent::TickCapture() {
    if (bUseFovOnlyPersonDetection)
    {
        PublishFovOnlyDetectionFrame();
        return;
    }

    const bool bSkipOwnerCameraDetection = bUseOwnerCameraCapture && ShouldSkipOwnerCameraCapture();
    if (bSkipOwnerCameraDetection && !ShouldRecordOwnerCameraVideo())
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

    CaptureAndEnqueue(!bSkipOwnerCameraDetection);
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

    const int32 DesiredCaptureWidth = FMath::Max(160, CaptureWidth);
    const int32 DesiredCaptureHeight = FMath::Max(160, CaptureHeight);

    if (!OwnerCaptureRenderTarget)
    {
        OwnerCaptureRenderTarget = NewObject<UTextureRenderTarget2D>(this, TEXT("OwnerCameraDetectionRenderTarget"));
        OwnerCaptureRenderTarget->RenderTargetFormat = RTF_RGBA8;
        OwnerCaptureRenderTarget->ClearColor = FLinearColor::Black;
        OwnerCaptureRenderTarget->InitAutoFormat(DesiredCaptureWidth, DesiredCaptureHeight);
        OwnerCaptureRenderTarget->UpdateResourceImmediate(true);
    }
    else if (OwnerCaptureRenderTarget->SizeX != DesiredCaptureWidth || OwnerCaptureRenderTarget->SizeY != DesiredCaptureHeight)
    {
        PendingOwnerCameraReadback.Reset();
        PendingOwnerCameraReadbackWidth = 0;
        PendingOwnerCameraReadbackHeight = 0;
        PendingOwnerCameraReadbackSubmitTimeSeconds = 0.0;
        OwnerCaptureRenderTarget->ResizeTarget(DesiredCaptureWidth, DesiredCaptureHeight);
        OwnerCaptureRenderTarget->UpdateResourceImmediate(true);
    }

    constexpr ETextureRenderTargetFormat DesiredDepthRenderTargetFormat = RTF_RGBA16f;
    if (!OwnerDepthRenderTarget)
    {
        OwnerDepthRenderTarget = NewObject<UTextureRenderTarget2D>(this, TEXT("OwnerCameraDepthRenderTarget"));
        OwnerDepthRenderTarget->RenderTargetFormat = DesiredDepthRenderTargetFormat;
        OwnerDepthRenderTarget->ClearColor = FLinearColor::Black;
        OwnerDepthRenderTarget->InitAutoFormat(DesiredCaptureWidth, DesiredCaptureHeight);
        OwnerDepthRenderTarget->UpdateResourceImmediate(true);
    }
    else if (OwnerDepthRenderTarget->RenderTargetFormat != DesiredDepthRenderTargetFormat ||
        OwnerDepthRenderTarget->SizeX != DesiredCaptureWidth ||
        OwnerDepthRenderTarget->SizeY != DesiredCaptureHeight)
    {
        PendingOwnerCameraReadbackDepth.Reset();
        PendingOwnerCameraReadbackDepthWidth = 0;
        PendingOwnerCameraReadbackDepthHeight = 0;
        ClearLastFrameSceneDepth();
        OwnerDepthRenderTarget->RenderTargetFormat = DesiredDepthRenderTargetFormat;
        OwnerDepthRenderTarget->InitAutoFormat(DesiredCaptureWidth, DesiredCaptureHeight);
        OwnerDepthRenderTarget->UpdateResourceImmediate(true);
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

    if (!OwnerDepthSceneCapture)
    {
        OwnerDepthSceneCapture = NewObject<UScreenCaptureComponent>(Owner, TEXT("OwnerCameraDepthCapture"));
        OwnerDepthSceneCapture->SetupAttachment(OwnerCamera);
        OwnerDepthSceneCapture->RegisterComponent();
        OwnerDepthSceneCapture->SetRelativeTransform(FTransform::Identity);
        OwnerDepthSceneCapture->TextureTarget = OwnerDepthRenderTarget;
        OwnerDepthSceneCapture->bCaptureEveryFrame = false;
        OwnerDepthSceneCapture->bCaptureOnMovement = false;
        OwnerDepthSceneCapture->CaptureSource = ESceneCaptureSource::SCS_SceneDepth;
    }

    OwnerSceneCapture->FOVAngle = OwnerCamera->FieldOfView;
    OwnerSceneCapture->SetWorldLocationAndRotation(OwnerCamera->GetComponentLocation(), OwnerCamera->GetComponentRotation());
    OwnerSceneCapture->TextureTarget = OwnerCaptureRenderTarget;
    OwnerSceneCapture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
    OwnerDepthSceneCapture->FOVAngle = OwnerCamera->FieldOfView;
    OwnerDepthSceneCapture->SetWorldLocationAndRotation(OwnerCamera->GetComponentLocation(), OwnerCamera->GetComponentRotation());
    OwnerDepthSceneCapture->TextureTarget = OwnerDepthRenderTarget;
    OwnerDepthSceneCapture->CaptureSource = ESceneCaptureSource::SCS_SceneDepth;
    ConfigureOwnerCaptureVisibility(Owner);
    return true;
}

void UMyActorComponent::ConfigureOwnerCaptureVisibility(AActor* Owner)
{
    if (!Owner)
    {
        return;
    }

    if (bHideOwnerFromOwnerCameraCapture)
    {
        if (OwnerSceneCapture)
        {
            OwnerSceneCapture->HiddenActors.AddUnique(Owner);
        }
        if (OwnerDepthSceneCapture)
        {
            OwnerDepthSceneCapture->HiddenActors.AddUnique(Owner);
        }
    }
    else
    {
        if (OwnerSceneCapture)
        {
            OwnerSceneCapture->HiddenActors.Remove(Owner);
        }
        if (OwnerDepthSceneCapture)
        {
            OwnerDepthSceneCapture->HiddenActors.Remove(Owner);
        }
    }
}

void UMyActorComponent::ApplyProjectDetectionSettings()
{
    const UEagleEyeDetectionSettings* Settings = GetDefault<UEagleEyeDetectionSettings>();
    if (!Settings)
    {
        return;
    }

    InferenceBackend = Settings->InferenceBackend;
    OnnxRuntimeExecutionProvider = Settings->OnnxRuntimeExecutionProvider;
    ModelPathOverride = Settings->ModelPathOverride;
    DarknetCfgPathOverride = Settings->DarknetCfgPathOverride;
    NamesPathOverride = Settings->NamesPathOverride;
    bOpenCVDNNPreferCUDA = Settings->bOpenCVDNNPreferCUDA;
    bOpenCVDNNUseFP16 = Settings->bOpenCVDNNUseFP16;
    OnnxInputSize = FMath::Clamp(Settings->OnnxInputSize, 160, 1280);
    bUseLetterbox = Settings->bUseLetterbox;
    LetterboxValue = FMath::Clamp(Settings->LetterboxValue, 0, 255);
    ConfidenceThreshold = FMath::Clamp(Settings->ConfidenceThreshold, 0.01f, 0.99f);
    NmsThreshold = FMath::Clamp(Settings->NmsThreshold, 0.01f, 0.99f);
    MaxActiveOwnerCameraCaptures = FMath::Max(0, Settings->MaxActiveSharedDetectionBots);
    MaxOwnerCameraCaptureDistance = FMath::Max(0.f, Settings->SharedDetectionMaxBotDistance);
    bRecordFrameTimes = Settings->bRecordFrameTimes;
    FrameTimeCsvPath = Settings->FrameTimeCsvPath;
    bResetFrameTimeLogOnBeginPlay = Settings->bResetFrameTimeLogOnBeginPlay;
    FrameTimeFlushInterval = FMath::Clamp(Settings->FrameTimeFlushInterval, 1, 600);
}

FString UMyActorComponent::ResolveOwnerCameraVideoPath() const
{
    if (!OwnerCameraVideoOutputPath.IsEmpty())
    {
        FString NormalizedPath = NormalizeRuntimeFilePath(OwnerCameraVideoOutputPath);
        if (FPaths::GetExtension(NormalizedPath).IsEmpty())
        {
            NormalizedPath += TEXT(".ts");
        }
        return NormalizedPath;
    }

    FString OwnerName = GetNameSafe(GetOwner());
    const TCHAR* InvalidChars[] = { TEXT(" "), TEXT(":"), TEXT("/"), TEXT("\\"), TEXT("."), TEXT("'"), TEXT("\"") };
    for (const TCHAR* InvalidChar : InvalidChars)
    {
        OwnerName.ReplaceInline(InvalidChar, TEXT("_"));
    }

    const FString Timestamp = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
    return FPaths::Combine(
        FPaths::VideoCaptureDir(),
        FString::Printf(TEXT("BotViewport_%s_%s.ts"), *OwnerName, *Timestamp));
}

bool UMyActorComponent::EnsureOwnerCameraVideoWriter(int32 Width, int32 Height)
{
    if (Width <= 0 || Height <= 0 || bOwnerCameraVideoWriterFailed)
    {
        return false;
    }

    if (OwnerCameraVideoProcess.IsValid() && OwnerCameraVideoPipeWrite &&
        OwnerCameraVideoWidth == Width && OwnerCameraVideoHeight == Height)
    {
        return true;
    }

    FinalizeOwnerCameraVideoWriter(!bOwnerCameraVideoFastShutdown.load());

    ActiveOwnerCameraVideoPath = ResolveOwnerCameraVideoPath();
    if (ActiveOwnerCameraVideoPath.IsEmpty())
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to resolve bot viewport video path."));
        bOwnerCameraVideoWriterFailed = true;
        return false;
    }

    const FString OutputDirectory = FPaths::GetPath(ActiveOwnerCameraVideoPath);
    if (!OutputDirectory.IsEmpty() && !IFileManager::Get().MakeDirectory(*OutputDirectory, true))
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to create bot viewport video directory: %s"), *OutputDirectory);
        ActiveOwnerCameraVideoPath.Reset();
        bOwnerCameraVideoWriterFailed = true;
        return false;
    }

    void* ChildStdInReadPipe = nullptr;
    void* LocalStdInWritePipe = nullptr;
    if (!FPlatformProcess::CreatePipe(ChildStdInReadPipe, LocalStdInWritePipe, true))
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to create FFmpeg stdin pipe for bot viewport recording."));
        ActiveOwnerCameraVideoPath.Reset();
        bOwnerCameraVideoWriterFailed = true;
        return false;
    }

    OwnerCameraVideoWidth = Width;
    OwnerCameraVideoHeight = Height;
    OwnerCameraVideoFPS = FMath::Clamp(FMath::RoundToInt(CaptureFPS), 1, 120);
    OwnerCameraVideoFrameCount = 0;

    const FString FfmpegArguments = FString::Printf(
        TEXT("-y -nostats -loglevel error -f rawvideo -pix_fmt bgra -s %dx%d -r %d -i - -an -c:v libx264 -preset ultrafast -tune zerolatency -g %d -keyint_min %d -sc_threshold 0 -pix_fmt yuv420p -f mpegts \"%s\""),
        Width,
        Height,
        OwnerCameraVideoFPS,
        OwnerCameraVideoFPS,
        OwnerCameraVideoFPS,
        *ActiveOwnerCameraVideoPath);

    uint32 ProcessId = 0;
    OwnerCameraVideoProcess = FPlatformProcess::CreateProc(
        *OwnerCameraVideoEncoderPath,
        *FfmpegArguments,
        false,
        true,
        true,
        &ProcessId,
        0,
        nullptr,
        nullptr,
        ChildStdInReadPipe,
        nullptr);

    FPlatformProcess::ClosePipe(ChildStdInReadPipe, nullptr);
    ChildStdInReadPipe = nullptr;

    if (!OwnerCameraVideoProcess.IsValid())
    {
        FPlatformProcess::ClosePipe(nullptr, LocalStdInWritePipe);
        UE_LOG(LogTemp, Error,
            TEXT("Failed to start FFmpeg for bot viewport recording. Install ffmpeg or set OwnerCameraVideoEncoderPath. Command: %s %s"),
            *OwnerCameraVideoEncoderPath,
            *FfmpegArguments);
        ActiveOwnerCameraVideoPath.Reset();
        bOwnerCameraVideoWriterFailed = true;
        return false;
    }

    OwnerCameraVideoPipeWrite = LocalStdInWritePipe;

    UE_LOG(LogTemp, Log, TEXT("Recording bot viewport video through FFmpeg: %s (%dx%d @ %d fps)"),
        *ActiveOwnerCameraVideoPath,
        Width,
        Height,
        OwnerCameraVideoFPS);
    return true;
}

void UMyActorComponent::RecordOwnerCameraVideoFrame(const TArray<FColor>& Pixels, int32 Width, int32 Height)
{
    if (!bRecordOwnerCameraCaptureVideo || !bUseOwnerCameraCapture || Pixels.Num() != Width * Height)
    {
        return;
    }

    if (PendingOwnerCameraVideoFrames.load() >= FMath::Max(1, MaxQueuedOwnerCameraVideoFrames))
    {
        ++DroppedOwnerCameraVideoFrames;
        return;
    }

    StartOwnerCameraVideoWorker();

    TSharedPtr<FOwnerCameraVideoFrame> Frame = MakeShared<FOwnerCameraVideoFrame>();
    Frame->Pixels = Pixels;
    Frame->Width = Width;
    Frame->Height = Height;
    PendingOwnerCameraVideoFrames.fetch_add(1);
    OwnerCameraVideoQueue.Enqueue(Frame);
}

void UMyActorComponent::StartOwnerCameraVideoWorker()
{
    if (bOwnerCameraVideoWorkerRunning.load())
    {
        return;
    }

    bOwnerCameraVideoWorkerRunning.store(true);
    OwnerCameraVideoFuture = Async(EAsyncExecution::Thread, [this]()
    {
        OwnerCameraVideoWorkerLoop();
    });
}

void UMyActorComponent::OwnerCameraVideoWorkerLoop()
{
    while (bOwnerCameraVideoWorkerRunning.load() || PendingOwnerCameraVideoFrames.load() > 0)
    {
        TSharedPtr<FOwnerCameraVideoFrame> Frame;
        if (!OwnerCameraVideoQueue.Dequeue(Frame))
        {
            FPlatformProcess::Sleep(0.002f);
            continue;
        }

        PendingOwnerCameraVideoFrames.fetch_sub(1);
        if (Frame.IsValid())
        {
            if (!bOwnerCameraVideoWorkerRunning.load())
            {
                ++DroppedOwnerCameraVideoFrames;
                continue;
            }

            WriteOwnerCameraVideoFrameSync(Frame->Pixels, Frame->Width, Frame->Height);
        }
    }

    FinalizeOwnerCameraVideoWriter(!bOwnerCameraVideoFastShutdown.load());
}

void UMyActorComponent::WriteOwnerCameraVideoFrameSync(const TArray<FColor>& Pixels, int32 Width, int32 Height)
{
    if (Pixels.Num() != Width * Height)
    {
        return;
    }

    if (!EnsureOwnerCameraVideoWriter(Width, Height))
    {
        return;
    }

    TArray<uint8> RawFrame;
    RawFrame.SetNumUninitialized(Width * Height * 4);

    uint8* Dest = RawFrame.GetData();
    for (int32 Y = 0; Y < Height; ++Y)
    {
        const FColor* Source = Pixels.GetData() + (Y * Width);
        for (int32 X = 0; X < Width; ++X)
        {
            FColor VideoColor = Source[X];
            if (bApplyOwnerCameraVideoGammaCorrection)
            {
                const FLinearColor LinearColor(
                    static_cast<float>(Source[X].R) / 255.0f,
                    static_cast<float>(Source[X].G) / 255.0f,
                    static_cast<float>(Source[X].B) / 255.0f,
                    static_cast<float>(Source[X].A) / 255.0f);
                VideoColor = LinearColor.ToFColorSRGB();
            }

            *Dest++ = VideoColor.B;
            *Dest++ = VideoColor.G;
            *Dest++ = VideoColor.R;
            *Dest++ = VideoColor.A;
        }
    }

    int32 BytesWrittenTotal = 0;
    while (BytesWrittenTotal < RawFrame.Num())
    {
        int32 BytesWritten = 0;
        const int32 BytesRemaining = RawFrame.Num() - BytesWrittenTotal;
        if (!FPlatformProcess::WritePipe(
            OwnerCameraVideoPipeWrite,
            RawFrame.GetData() + BytesWrittenTotal,
            BytesRemaining,
            &BytesWritten) || BytesWritten <= 0)
        {
            UE_LOG(LogTemp, Error, TEXT("Failed to write bot viewport frame to FFmpeg stdin."));
            bOwnerCameraVideoWriterFailed = true;
            return;
        }

        BytesWrittenTotal += BytesWritten;
    }

    ++OwnerCameraVideoFrameCount;
}

void UMyActorComponent::CloseOwnerCameraVideoWriter(bool bWaitForEncoder)
{
    bOwnerCameraVideoFastShutdown.store(!bWaitForEncoder);
    bOwnerCameraVideoWorkerRunning.store(false);

    int32 DroppedOnClose = 0;
    TSharedPtr<FOwnerCameraVideoFrame> DroppedFrame;
    while (OwnerCameraVideoQueue.Dequeue(DroppedFrame))
    {
        ++DroppedOnClose;
    }
    if (DroppedOnClose > 0)
    {
        DroppedOwnerCameraVideoFrames += DroppedOnClose;
    }
    PendingOwnerCameraVideoFrames.store(0);

    if (OwnerCameraVideoFuture.IsValid())
    {
        OwnerCameraVideoFuture.Wait();
    }
    else
    {
        FinalizeOwnerCameraVideoWriter(bWaitForEncoder);
    }

    if (DroppedOwnerCameraVideoFrames > 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("Dropped %d bot viewport recording frames because the queue was full or recording stopped."),
            DroppedOwnerCameraVideoFrames);
        DroppedOwnerCameraVideoFrames = 0;
    }
}

void UMyActorComponent::FinalizeOwnerCameraVideoWriter(bool bWaitForEncoder)
{
    if (OwnerCameraVideoPipeWrite)
    {
        FPlatformProcess::ClosePipe(nullptr, OwnerCameraVideoPipeWrite);
        OwnerCameraVideoPipeWrite = nullptr;
    }

    if (OwnerCameraVideoProcess.IsValid())
    {
        const double ShutdownTimeoutSeconds = bWaitForEncoder
            ? FfmpegStopTimeoutSeconds
            : FfmpegFastShutdownTimeoutSeconds;
        const double WaitStart = FPlatformTime::Seconds();
        while (FPlatformProcess::IsProcRunning(OwnerCameraVideoProcess) &&
            (FPlatformTime::Seconds() - WaitStart) < ShutdownTimeoutSeconds)
        {
            FPlatformProcess::Sleep(0.02f);
        }

        if (FPlatformProcess::IsProcRunning(OwnerCameraVideoProcess))
        {
            UE_LOG(LogTemp, Warning, TEXT("FFmpeg did not finish within %.1f seconds; terminating bot viewport recording process."),
                ShutdownTimeoutSeconds);
            FPlatformProcess::TerminateProc(OwnerCameraVideoProcess, false);

            const double TerminateWaitStart = FPlatformTime::Seconds();
            while (FPlatformProcess::IsProcRunning(OwnerCameraVideoProcess) &&
                (FPlatformTime::Seconds() - TerminateWaitStart) < FfmpegTerminateTimeoutSeconds)
            {
                FPlatformProcess::Sleep(0.02f);
            }
        }

        if (FPlatformProcess::IsProcRunning(OwnerCameraVideoProcess))
        {
            UE_LOG(LogTemp, Warning, TEXT("FFmpeg process is still running after terminate request; releasing handle without blocking PIE shutdown."));
            OwnerCameraVideoProcess.Reset();
        }
        else
        {
            FPlatformProcess::CloseProc(OwnerCameraVideoProcess);
            OwnerCameraVideoProcess.Reset();
        }
    }

    if (!ActiveOwnerCameraVideoPath.IsEmpty() && OwnerCameraVideoFrameCount > 0)
    {
        UE_LOG(LogTemp, Log, TEXT("Finished bot viewport video: %s (%d frames)"),
            *ActiveOwnerCameraVideoPath,
            OwnerCameraVideoFrameCount);
    }

    ActiveOwnerCameraVideoPath.Reset();
    OwnerCameraVideoWidth = 0;
    OwnerCameraVideoHeight = 0;
    OwnerCameraVideoFPS = 0;
    OwnerCameraVideoFrameCount = 0;
    bOwnerCameraVideoFastShutdown.store(false);
    bOwnerCameraVideoWriterFailed = false;
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

bool UMyActorComponent::ShouldRecordOwnerCameraVideo() const
{
    return bRecordOwnerCameraCaptureVideo && bUseOwnerCameraCapture &&
        (!ShouldSkipOwnerCameraCapture() || bRecordOwnerCameraWhenDetectionSkipped);
}

bool UMyActorComponent::HasPersonDetection(const TArray<FDetectionResult>& Detections) const
{
    for (const FDetectionResult& Detection : Detections)
    {
        if (Detection.ClassId == 0 || Detection.Label.StartsWith(TEXT("person"), ESearchCase::IgnoreCase))
        {
            return true;
        }
    }

    return false;
}

bool UMyActorComponent::EvaluatePlayerInOwnerCameraFov(
    int32 SourceWidth,
    int32 SourceHeight,
    FVector2D& OutPixel,
    float& OutDistance,
    float& OutHorizontalAngleDegrees,
    float& OutVerticalAngleDegrees,
    bool& bOutHasEvaluation) const
{
    bOutHasEvaluation = false;
    OutPixel = FVector2D::ZeroVector;
    OutDistance = 0.f;
    OutHorizontalAngleDegrees = 0.f;
    OutVerticalAngleDegrees = 0.f;

    const AActor* Owner = GetOwner();
    const UCameraComponent* OwnerCamera = Owner ? Owner->FindComponentByClass<UCameraComponent>() : nullptr;
    const UWorld* World = GetWorld();
    const APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
    const APawn* PlayerPawn = PlayerController ? PlayerController->GetPawn() : nullptr;
    if (!Owner || !OwnerCamera || !PlayerPawn || PlayerPawn == Owner)
    {
        return false;
    }

    const FVector CameraLocation = OwnerCamera->GetComponentLocation();
    const FVector TargetLocation = PlayerPawn->GetPawnViewLocation();
    const FVector ToTarget = TargetLocation - CameraLocation;
    OutDistance = ToTarget.Size();
    if (OutDistance <= KINDA_SMALL_NUMBER)
    {
        bOutHasEvaluation = true;
        OutPixel = FVector2D(SourceWidth * 0.5f, SourceHeight * 0.5f);
        return true;
    }

    const FVector Direction = ToTarget / OutDistance;
    const FTransform CameraTransform = OwnerCamera->GetComponentTransform();
    const float ForwardAmount = FVector::DotProduct(Direction, CameraTransform.GetUnitAxis(EAxis::X));
    const float RightAmount = FVector::DotProduct(Direction, CameraTransform.GetUnitAxis(EAxis::Y));
    const float UpAmount = FVector::DotProduct(Direction, CameraTransform.GetUnitAxis(EAxis::Z));

    bOutHasEvaluation = true;
    if (ForwardAmount <= KINDA_SMALL_NUMBER)
    {
        OutHorizontalAngleDegrees = RightAmount >= 0.f ? 180.f : -180.f;
        OutVerticalAngleDegrees = UpAmount >= 0.f ? 90.f : -90.f;
        return false;
    }

    const float SafeWidth = FMath::Max(static_cast<float>(SourceWidth), 1.f);
    const float SafeHeight = FMath::Max(static_cast<float>(SourceHeight), 1.f);
    const float Aspect = SafeWidth / SafeHeight;
    const float HalfHorizontalFovRad = FMath::DegreesToRadians(OwnerCamera->FieldOfView) * 0.5f;
    const float HalfVerticalFovRad = FMath::Atan(FMath::Tan(HalfHorizontalFovRad) / FMath::Max(Aspect, KINDA_SMALL_NUMBER));
    const float HorizontalAngleRad = FMath::Atan2(RightAmount, ForwardAmount);
    const float VerticalAngleRad = FMath::Atan2(UpAmount, ForwardAmount);

    OutHorizontalAngleDegrees = FMath::RadiansToDegrees(HorizontalAngleRad);
    OutVerticalAngleDegrees = FMath::RadiansToDegrees(VerticalAngleRad);

    const bool bWithinDistance = FovOnlyDetectionMaxDistance <= 0.f || OutDistance <= FovOnlyDetectionMaxDistance;
    const bool bWithinFov =
        FMath::Abs(HorizontalAngleRad) <= HalfHorizontalFovRad &&
        FMath::Abs(VerticalAngleRad) <= HalfVerticalFovRad;

    const float NdcX = FMath::Tan(HorizontalAngleRad) / FMath::Max(FMath::Tan(HalfHorizontalFovRad), KINDA_SMALL_NUMBER);
    const float NdcY = FMath::Tan(VerticalAngleRad) / FMath::Max(FMath::Tan(HalfVerticalFovRad), KINDA_SMALL_NUMBER);
    OutPixel.X = (NdcX + 1.f) * 0.5f * SafeWidth;
    OutPixel.Y = (1.f - NdcY) * 0.5f * SafeHeight;

    return bWithinDistance && bWithinFov;
}

bool UMyActorComponent::BuildFovOnlyPersonDetection(
    TArray<FDetectionResult>& OutDetections,
    int32& OutSourceWidth,
    int32& OutSourceHeight,
    float& OutDistance,
    float& OutHorizontalAngleDegrees,
    float& OutVerticalAngleDegrees)
{
    OutDetections.Reset();
    OutSourceWidth = FMath::Max(CaptureWidth, 1);
    OutSourceHeight = FMath::Max(CaptureHeight, 1);

    FVector2D TargetPixel = FVector2D::ZeroVector;
    bool bHasEvaluation = false;
    const bool bPlayerInFov = EvaluatePlayerInOwnerCameraFov(
        OutSourceWidth,
        OutSourceHeight,
        TargetPixel,
        OutDistance,
        OutHorizontalAngleDegrees,
        OutVerticalAngleDegrees,
        bHasEvaluation);
    if (!bHasEvaluation || !bPlayerInFov)
    {
        return false;
    }

    const float HalfBoxSize = FMath::Clamp(
        static_cast<float>(FovOnlySyntheticBoxSizePixels),
        1.f,
        static_cast<float>(FMath::Max(FMath::Min(OutSourceWidth, OutSourceHeight), 1))) * 0.5f;

    const float MinX = FMath::Clamp(TargetPixel.X - HalfBoxSize, 0.f, static_cast<float>(OutSourceWidth - 1));
    const float MinY = FMath::Clamp(TargetPixel.Y - HalfBoxSize, 0.f, static_cast<float>(OutSourceHeight - 1));
    const float MaxX = FMath::Clamp(TargetPixel.X + HalfBoxSize, 0.f, static_cast<float>(OutSourceWidth - 1));
    const float MaxY = FMath::Clamp(TargetPixel.Y + HalfBoxSize, 0.f, static_cast<float>(OutSourceHeight - 1));

    FDetectionResult Detection;
    Detection.ClassId = 0;
    Detection.Confidence = 1.f;
    Detection.Label = TEXT("person: 1.00 (fov)");
    Detection.Corners.Add(FVector2D(MinX, MinY));
    Detection.Corners.Add(FVector2D(MaxX, MinY));
    Detection.Corners.Add(FVector2D(MaxX, MaxY));
    Detection.Corners.Add(FVector2D(MinX, MaxY));
    OutDetections.Add(MoveTemp(Detection));
    return true;
}

void UMyActorComponent::LogFovDetectionMetricSample(
    const TCHAR* SourceLabel,
    const TArray<FDetectionResult>& Detections,
    int32 SourceWidth,
    int32 SourceHeight)
{
    if (!bLogFovDetectionMetrics)
    {
        return;
    }

    FVector2D ExpectedPixel = FVector2D::ZeroVector;
    float Distance = 0.f;
    float HorizontalAngleDegrees = 0.f;
    float VerticalAngleDegrees = 0.f;
    bool bHasEvaluation = false;
    const bool bExpectedInFov = EvaluatePlayerInOwnerCameraFov(
        SourceWidth,
        SourceHeight,
        ExpectedPixel,
        Distance,
        HorizontalAngleDegrees,
        VerticalAngleDegrees,
        bHasEvaluation);
    if (!bHasEvaluation)
    {
        ++FovDetectionUnavailableSampleCount;
        AppendFovDetectionMetricsTableRow(
            SourceLabel,
            TEXT("unavailable"),
            TEXT("unavailable"),
            false,
            HasPersonDetection(Detections),
            Distance,
            HorizontalAngleDegrees,
            VerticalAngleDegrees,
            ExpectedPixel,
            SourceWidth,
            SourceHeight,
            Detections.Num());
        if (IsDetectionMetricLoggingEnabled())
        {
            UE_LOG(LogTemp, Warning, TEXT("FovDetectionMetric[%s]: unavailable owner=%s source=%dx%d detections=%d unavailable=%d"),
                SourceLabel,
                *GetNameSafe(GetOwner()),
                SourceWidth,
                SourceHeight,
                Detections.Num(),
                FovDetectionUnavailableSampleCount);
        }
        return;
    }

    const bool bActualPersonDetected = HasPersonDetection(Detections);
    const TCHAR* Outcome = TEXT("true_negative");
    const TCHAR* Status = TEXT("success");
    if (bExpectedInFov && bActualPersonDetected)
    {
        ++FovDetectionTruePositiveCount;
        Outcome = TEXT("true_positive");
    }
    else if (!bExpectedInFov && !bActualPersonDetected)
    {
        ++FovDetectionTrueNegativeCount;
        Outcome = TEXT("true_negative");
    }
    else if (!bExpectedInFov && bActualPersonDetected)
    {
        ++FovDetectionFalsePositiveCount;
        Outcome = TEXT("false_positive");
        Status = TEXT("fault");
    }
    else
    {
        ++FovDetectionFalseNegativeCount;
        Outcome = TEXT("false_negative");
        Status = TEXT("fault");
    }

    AppendFovDetectionMetricsTableRow(
        SourceLabel,
        Status,
        Outcome,
        bExpectedInFov,
        bActualPersonDetected,
        Distance,
        HorizontalAngleDegrees,
        VerticalAngleDegrees,
        ExpectedPixel,
        SourceWidth,
        SourceHeight,
        Detections.Num());

    if (IsDetectionMetricLoggingEnabled())
    {
        UE_LOG(LogTemp, Log, TEXT("FovDetectionMetric[%s]: %s outcome=%s owner=%s expected_in_fov=%s actual_person=%s distance=%.1f h_angle=%.1f v_angle=%.1f expected_pixel=%s source=%dx%d detections=%d tp=%d tn=%d fp=%d fn=%d"),
            SourceLabel,
            Status,
            Outcome,
            *GetNameSafe(GetOwner()),
            bExpectedInFov ? TEXT("true") : TEXT("false"),
            bActualPersonDetected ? TEXT("true") : TEXT("false"),
            Distance,
            HorizontalAngleDegrees,
            VerticalAngleDegrees,
            *ExpectedPixel.ToString(),
            SourceWidth,
            SourceHeight,
            Detections.Num(),
            FovDetectionTruePositiveCount,
            FovDetectionTrueNegativeCount,
            FovDetectionFalsePositiveCount,
            FovDetectionFalseNegativeCount);
    }
}

void UMyActorComponent::PublishFovOnlyDetectionFrame()
{
    TArray<FDetectionResult> Detections;
    int32 SourceWidth = 0;
    int32 SourceHeight = 0;
    float Distance = 0.f;
    float HorizontalAngleDegrees = 0.f;
    float VerticalAngleDegrees = 0.f;
    BuildFovOnlyPersonDetection(
        Detections,
        SourceWidth,
        SourceHeight,
        Distance,
        HorizontalAngleDegrees,
        VerticalAngleDegrees);

    LastFrameDetections = MoveTemp(Detections);
    LastFrameSourceWidth = SourceWidth;
    LastFrameSourceHeight = SourceHeight;
    ++LastFrameSequence;
    LastFrameTimeSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;

    LogFovDetectionMetricSample(TEXT("fov_only"), LastFrameDetections, SourceWidth, SourceHeight);

    if (ShouldLogFrameTimings())
    {
        UE_LOG(LogTemp, Log, TEXT("FovOnlyDetection[%s]: detections=%d size=%dx%d seq=%d distance=%.1f h_angle=%.1f v_angle=%.1f"),
            *GetNameSafe(GetOwner()),
            LastFrameDetections.Num(),
            SourceWidth,
            SourceHeight,
            LastFrameSequence,
            Distance,
            HorizontalAngleDegrees,
            VerticalAngleDegrees);
    }
}

void UMyActorComponent::ClearPublishedResults()
{
    {
        FScopeLock ResultsLock(&ResultsMutex);
        ResultsShared.Reset();
        ResultsSourceWidth = 0;
        ResultsSourceHeight = 0;
        ++ResultsSequence;
        ResultsTimeSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
    }

    LastFrameDetections.Reset();
    LastFrameSourceWidth = 0;
    LastFrameSourceHeight = 0;
    ++LastFrameSequence;
    LastFrameTimeSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
    ClearLastFrameSceneDepth();
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

    if (ShouldLogFrameTimings())
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

bool UMyActorComponent::CaptureOwnerSceneDepthToBuffer(TArray<float>& OutDepth, int32& OutWidth, int32& OutHeight)
{
    OutDepth.Reset();
    OutWidth = 0;
    OutHeight = 0;

    if (!EnsureOwnerCameraCapture() || !OwnerDepthSceneCapture || !OwnerDepthRenderTarget)
    {
        return false;
    }

    const double DepthCaptureStartSeconds = FPlatformTime::Seconds();
    OwnerDepthSceneCapture->CaptureScene();

    FTextureRenderTargetResource* Resource = OwnerDepthRenderTarget->GameThread_GetRenderTargetResource();
    if (!Resource)
    {
        return false;
    }

    TArray<FFloat16Color> DepthPixels;
    FReadSurfaceDataFlags ReadFlags(RCM_MinMax);
    ReadFlags.SetLinearToGamma(false);
    if (!Resource->ReadFloat16Pixels(DepthPixels, ReadFlags))
    {
        return false;
    }

    const int32 DepthWidth = OwnerDepthRenderTarget->SizeX;
    const int32 DepthHeight = OwnerDepthRenderTarget->SizeY;
    if (DepthPixels.Num() != DepthWidth * DepthHeight)
    {
        return false;
    }

    OutDepth.SetNumUninitialized(DepthPixels.Num());
    for (int32 Index = 0; Index < DepthPixels.Num(); ++Index)
    {
        const FFloat16Color& DepthPixel = DepthPixels[Index];
        OutDepth[Index] = DepthPixel.R.GetFloat();
    }

    OutWidth = DepthWidth;
    OutHeight = DepthHeight;

    if (ShouldLogFrameTimings())
    {
        UE_LOG(LogTemp, Log, TEXT("SceneDepthCapture[%s]: total=%.2fms size=%dx%d"),
            *GetNameSafe(GetOwner()),
            (FPlatformTime::Seconds() - DepthCaptureStartSeconds) * 1000.0,
            OutWidth,
            OutHeight);
    }

    return true;
}

void UMyActorComponent::PublishOwnerSceneDepth(TArray<float>&& Depth, int32 Width, int32 Height)
{
    if (Depth.Num() != Width * Height || Width <= 0 || Height <= 0)
    {
        ClearLastFrameSceneDepth();
        return;
    }

    LastFrameSceneDepth = MoveTemp(Depth);
    LastFrameSceneDepthWidth = Width;
    LastFrameSceneDepthHeight = Height;
    LastFrameSceneDepthTimeSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
}

void UMyActorComponent::ClearLastFrameSceneDepth()
{
    LastFrameSceneDepth.Reset();
    LastFrameSceneDepthWidth = 0;
    LastFrameSceneDepthHeight = 0;
    LastFrameSceneDepthTimeSeconds = -FLT_MAX;
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

    PublishOwnerSceneDepth(
        MoveTemp(PendingOwnerCameraReadbackDepth),
        PendingOwnerCameraReadbackDepthWidth,
        PendingOwnerCameraReadbackDepthHeight);

    if (ShouldLogFrameTimings())
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
    PendingOwnerCameraReadbackDepth.Reset();
    PendingOwnerCameraReadbackDepthWidth = 0;
    PendingOwnerCameraReadbackDepthHeight = 0;
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

    TArray<float> CapturedDepth;
    int32 CapturedDepthWidth = 0;
    int32 CapturedDepthHeight = 0;
    CaptureOwnerSceneDepthToBuffer(CapturedDepth, CapturedDepthWidth, CapturedDepthHeight);

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
    PendingOwnerCameraReadbackDepth = MoveTemp(CapturedDepth);
    PendingOwnerCameraReadbackDepthWidth = CapturedDepthWidth;
    PendingOwnerCameraReadbackDepthHeight = CapturedDepthHeight;
    FRHIGPUTextureReadback* Readback = PendingOwnerCameraReadback.Get();
    const FIntVector ReadbackSize(PendingOwnerCameraReadbackWidth, PendingOwnerCameraReadbackHeight, 1);

    ENQUEUE_RENDER_COMMAND(EnqueueCrowOwnerCameraReadback)(
        [Readback, TextureRHI, ReadbackSize](FRHICommandListImmediate& RHICmdList)
        {
            Readback->EnqueueCopy(RHICmdList, TextureRHI, FIntVector::ZeroValue, 0, ReadbackSize);
        });

    if (ShouldLogFrameTimings())
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

void UMyActorComponent::CaptureAndEnqueue(bool bSubmitDetection)
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

    if (bUseOwnerCameraCapture && !bUseAsyncOwnerCameraReadback)
    {
        TArray<float> CapturedDepth;
        int32 CapturedDepthWidth = 0;
        int32 CapturedDepthHeight = 0;
        if (CaptureOwnerSceneDepthToBuffer(CapturedDepth, CapturedDepthWidth, CapturedDepthHeight))
        {
            PublishOwnerSceneDepth(MoveTemp(CapturedDepth), CapturedDepthWidth, CapturedDepthHeight);
        }
        else
        {
            ClearLastFrameSceneDepth();
        }
    }
    else if (!bUseOwnerCameraCapture)
    {
        ClearLastFrameSceneDepth();
    }

    RecordOwnerCameraVideoFrame(Pixels, SourceWidth, SourceHeight);

    if (!bSubmitDetection)
    {
        ClearPublishedResults();
        return;
    }

    if (bUseSharedVisionModel)
    {
        if (UWorld* World = GetWorld())
        {
            if (UCrowVisionSubsystem* VisionSubsystem = World->GetSubsystem<UCrowVisionSubsystem>())
            {
                VisionSubsystem->SubmitFrame(this, MoveTemp(Pixels), SourceWidth, SourceHeight);
                if (ShouldLogFrameTimings())
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

    {
        FScopeLock Lock(&FrameMutex);
        LatestFrame = Frame;
    }

    if (ShouldLogFrameTimings())
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
                        Det = ProcessWithOpenCV_BG(Frame->Pixels, Frame->Width, Frame->Height, &FrameInferMs);
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
                    ResultsTimeSeconds = 0.f;
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
        ResultsTimeSeconds = -FLT_MAX;
    }
    LastFrameDetections.Reset();
    LastFrameSourceWidth = 0;
    LastFrameSourceHeight = 0;
    LastFrameSequence = 0;
    LastFrameTimeSeconds = -FLT_MAX;
    ReleaseTensorRT();
    ReleaseOnnxRuntime();
    OpenCVDnnNet = cv::dnn::Net();
}

void UMyActorComponent::CopyResultsFromWorker()
{
    TArray<FDetectionResult> LocalResults;
    int32 SourceWidth = 0;
    int32 SourceHeight = 0;
    int32 Sequence = 0;
    float ResultTimeSeconds = -FLT_MAX;
    {
        FScopeLock Lock(&ResultsMutex);
        LocalResults = ResultsShared;
        SourceWidth = ResultsSourceWidth;
        SourceHeight = ResultsSourceHeight;
        Sequence = ResultsSequence;
        ResultTimeSeconds = ResultsTimeSeconds;
    }

    if (SourceWidth <= 0 || SourceHeight <= 0)
    {
        LastFrameDetections.Reset();
        LastFrameSourceWidth = 0;
        LastFrameSourceHeight = 0;
        LastFrameSequence = 0;
        LastFrameTimeSeconds = -FLT_MAX;
        return;
    }

    if (Sequence == LastFrameSequence)
    {
        return;
    }

    LastFrameDetections = MoveTemp(LocalResults);
    LastFrameSourceWidth = SourceWidth;
    LastFrameSourceHeight = SourceHeight;
    LastFrameSequence = Sequence;
    LastFrameTimeSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : ResultTimeSeconds;

    LogFovDetectionMetricSample(TEXT("model_local"), LastFrameDetections, SourceWidth, SourceHeight);
}

TArray<FDetectionResult> UMyActorComponent::ProcessSharedVisionFrame(
    const TArray<FColor>& Bitmap,
    int32 Width,
    int32 Height,
    double* OutInferenceMs)
{
    if (!bIsModelLoaded && !LoadYOLO())
    {
        return {};
    }

    const double StartSeconds = FPlatformTime::Seconds();
    TArray<FDetectionResult> Detections = ProcessWithOpenCV_BG(Bitmap, Width, Height, OutInferenceMs);

    if (ShouldLogFrameTimings())
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
        LastFrameTimeSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
        return;
    }

    LastFrameDetections = MoveTemp(Detections);
    LastFrameSourceWidth = SourceWidth;
    LastFrameSourceHeight = SourceHeight;
    ++LastFrameSequence;
    LastFrameTimeSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;

    LogFovDetectionMetricSample(TEXT("model_shared"), LastFrameDetections, SourceWidth, SourceHeight);

    if (ShouldLogFrameTimings())
    {
        UE_LOG(LogTemp, Log, TEXT("SharedResult[%s]: detections=%d size=%dx%d seq=%d"),
            *GetNameSafe(GetOwner()),
            LastFrameDetections.Num(),
            SourceWidth,
            SourceHeight,
            LastFrameSequence);
    }
}

void UMyActorComponent::ResetInferenceOutputState()
{
    TrtInputHost.Reset();
    TrtOutputHost.Reset();
    TrtInputElements = 0;
    TrtOutputElements = 0;
    TrtOutputChannels = 0;
    TrtOutputDetections = 0;
}

EDetectionInferenceBackend UMyActorComponent::DetectAutomaticInferenceBackend(const FString& ModelPathUE) const
{
#if WITH_TENSORRT
    if (ModelPathUE.EndsWith(TEXT(".plan"), ESearchCase::IgnoreCase))
    {
        return EDetectionInferenceBackend::TensorRT;
    }
#endif

#if WITH_ONNXRUNTIME
    if (ModelPathUE.EndsWith(TEXT(".onnx"), ESearchCase::IgnoreCase))
    {
        return EDetectionInferenceBackend::ONNXRuntime;
    }
#endif

    if (ModelPathUE.EndsWith(TEXT(".onnx"), ESearchCase::IgnoreCase))
    {
        return EDetectionInferenceBackend::OpenCVDNN;
    }

    const EDetectedOperatingSystem DetectedOS = DetectOperatingSystem();
    const EDetectedGpuVendor DetectedGpu = DetectGpuVendor(GRHIAdapterName);

#if PLATFORM_LINUX && WITH_TENSORRT
    if (DetectedOS == EDetectedOperatingSystem::Linux && DetectedGpu == EDetectedGpuVendor::Nvidia)
    {
        return EDetectionInferenceBackend::TensorRT;
    }
#endif

#if WITH_ONNXRUNTIME
#if PLATFORM_WINDOWS && WITH_ONNXRUNTIME_DML
    if (DetectedOS == EDetectedOperatingSystem::Windows && DetectedGpu == EDetectedGpuVendor::AMD)
    {
        return EDetectionInferenceBackend::ONNXRuntime;
    }
#elif PLATFORM_LINUX && WITH_ONNXRUNTIME_MIGRAPHX
    if (DetectedOS == EDetectedOperatingSystem::Linux && DetectedGpu == EDetectedGpuVendor::AMD)
    {
        return EDetectionInferenceBackend::ONNXRuntime;
    }
#endif
#endif

    return EDetectionInferenceBackend::OpenCVDNN;
}

EDetectionInferenceBackend UMyActorComponent::ResolveEffectiveInferenceBackend(const FString& ModelPathUE) const
{
    EDetectionInferenceBackend RequestedBackend = InferenceBackend;
    if (RequestedBackend == EDetectionInferenceBackend::Auto)
    {
        RequestedBackend = DetectAutomaticInferenceBackend(ModelPathUE);
    }

    if (RequestedBackend == EDetectionInferenceBackend::TensorRT)
    {
#if WITH_TENSORRT
        return EDetectionInferenceBackend::TensorRT;
#else
        UE_LOG(LogTemp, Warning, TEXT("TensorRT backend requested but this build was compiled without TensorRT. Falling back."));
        RequestedBackend = EDetectionInferenceBackend::Auto;
#endif
    }

    if (RequestedBackend == EDetectionInferenceBackend::ONNXRuntime)
    {
#if WITH_ONNXRUNTIME
        return EDetectionInferenceBackend::ONNXRuntime;
#else
        UE_LOG(LogTemp, Warning, TEXT("ONNX Runtime backend requested but this build was compiled without ONNX Runtime. Falling back to OpenCV DNN."));
        return EDetectionInferenceBackend::OpenCVDNN;
#endif
    }

    if (RequestedBackend == EDetectionInferenceBackend::Auto)
    {
        return DetectAutomaticInferenceBackend(ModelPathUE);
    }

    return EDetectionInferenceBackend::OpenCVDNN;
}

EOnnxRuntimeExecutionProvider UMyActorComponent::ResolveEffectiveOnnxRuntimeProvider() const
{
    if (OnnxRuntimeExecutionProvider != EOnnxRuntimeExecutionProvider::Auto)
    {
        return OnnxRuntimeExecutionProvider;
    }

    const EDetectedOperatingSystem DetectedOS = DetectOperatingSystem();
    const EDetectedGpuVendor DetectedGpu = DetectGpuVendor(GRHIAdapterName);

#if PLATFORM_WINDOWS
    if (DetectedOS == EDetectedOperatingSystem::Windows && DetectedGpu == EDetectedGpuVendor::AMD)
    {
#if WITH_ONNXRUNTIME_DML
        return EOnnxRuntimeExecutionProvider::DirectML;
#else
        UE_LOG(LogTemp, Warning, TEXT("AMD adapter detected, but ONNX Runtime DirectML provider is not compiled into this build. Using CPU provider."));
#endif
    }
#endif

#if PLATFORM_LINUX
    if (DetectedOS == EDetectedOperatingSystem::Linux && DetectedGpu == EDetectedGpuVendor::AMD)
    {
#if WITH_ONNXRUNTIME_MIGRAPHX
        return EOnnxRuntimeExecutionProvider::MIGraphX;
#else
        UE_LOG(LogTemp, Warning, TEXT("AMD adapter detected, but ONNX Runtime MIGraphX provider is not compiled into this build. Using CPU provider."));
#endif
    }
#endif

    return EOnnxRuntimeExecutionProvider::CPU;
}

FString UMyActorComponent::ResolveModelPathForBackend(const FString& ModelPathUE, EDetectionInferenceBackend Backend) const
{
    if (Backend == EDetectionInferenceBackend::TensorRT &&
        ModelPathUE.EndsWith(TEXT(".onnx"), ESearchCase::IgnoreCase))
    {
        const FString CandidatePlanPath = FPaths::ChangeExtension(ModelPathUE, TEXT("plan"));
        if (RuntimeFileExists(CandidatePlanPath))
        {
            return CandidatePlanPath;
        }
    }

    if ((Backend == EDetectionInferenceBackend::ONNXRuntime || Backend == EDetectionInferenceBackend::OpenCVDNN) &&
        ModelPathUE.EndsWith(TEXT(".plan"), ESearchCase::IgnoreCase))
    {
        const FString CandidateOnnxPath = FPaths::ChangeExtension(ModelPathUE, TEXT("onnx"));
        if (RuntimeFileExists(CandidateOnnxPath))
        {
            return CandidateOnnxPath;
        }

        FString BaseName = FPaths::GetBaseFilename(ModelPathUE);
        const FString DirPath = FPaths::GetPath(ModelPathUE);
        if (BaseName.EndsWith(TEXT("_fp16")) || BaseName.EndsWith(TEXT("_fp32")) || BaseName.EndsWith(TEXT("_int8")))
        {
            BaseName = BaseName.LeftChop(5);
        }

        const FString AltOnnxPath = FPaths::Combine(DirPath, BaseName + TEXT(".onnx"));
        if (RuntimeFileExists(AltOnnxPath))
        {
            return AltOnnxPath;
        }
    }

    return ModelPathUE;
}

	bool UMyActorComponent::LoadYOLO()
	{
	    try
	    {
	        cv::setUseOptimized(true);
	        cv::setNumThreads(FPlatformMisc::NumberOfCoresIncludingHyperthreads());

	        ReleaseTensorRT();
            ReleaseOnnxRuntime();
	        OpenCVDnnNet = cv::dnn::Net();

	        FString ModelPathUE = FString(WeightsPath.c_str());
	        if (!FPlatformFileManager::Get().GetPlatformFile().FileExists(*ModelPathUE))
	        {
	            UE_LOG(LogTemp, Error, TEXT("Model file not found: %s"), *ModelPathUE);
	            return false;
	        }

            EffectiveInferenceBackend = ResolveEffectiveInferenceBackend(ModelPathUE);
            ModelPathUE = ResolveModelPathForBackend(ModelPathUE, EffectiveInferenceBackend);
            const EDetectedOperatingSystem DetectedOS = DetectOperatingSystem();
            const EDetectedGpuVendor DetectedGpu = DetectGpuVendor(GRHIAdapterName);
            UE_LOG(LogTemp, Log, TEXT("LoadYOLO selected backend: %s (requested=%s, ONNX provider=%s, OS=%s, GPU=%s, adapter=%s, model=%s)"),
                BackendToString(EffectiveInferenceBackend),
                BackendToString(InferenceBackend),
                OnnxProviderToString(OnnxRuntimeExecutionProvider),
                OperatingSystemToString(DetectedOS),
                GpuVendorToString(DetectedGpu),
                *GRHIAdapterName,
                *ModelPathUE);

            if (!FPlatformFileManager::Get().GetPlatformFile().FileExists(*ModelPathUE))
            {
                UE_LOG(LogTemp, Error, TEXT("Resolved model file not found: %s"), *ModelPathUE);
                return false;
            }

            if (EffectiveInferenceBackend == EDetectionInferenceBackend::TensorRT)
            {
#if WITH_TENSORRT
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
#else
                UE_LOG(LogTemp, Error, TEXT("TensorRT backend selected but this build was compiled without TensorRT."));
                return false;
#endif
            }

            if (EffectiveInferenceBackend == EDetectionInferenceBackend::ONNXRuntime)
            {
                return LoadOnnxRuntime(ModelPathUE);
            }

            if (EffectiveInferenceBackend != EDetectionInferenceBackend::OpenCVDNN)
            {
                UE_LOG(LogTemp, Error, TEXT("Unsupported inference backend value: %d"), static_cast<int32>(EffectiveInferenceBackend));
                return false;
            }

            FString OpenCvModelPath = ResolveModelPathForBackend(ModelPathUE, EDetectionInferenceBackend::OpenCVDNN);
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
            ResetInferenceOutputState();

            UE_LOG(LogTemp, Log, TEXT("OpenCV DNN model loaded: %s"), *OpenCvModelPath);
            bIsModelLoaded = true;
            return true;
	    }
	    catch (const std::exception& e)
	    {
	        UE_LOG(LogTemp, Error, TEXT("Model load failed: %s"), *FString(e.what()));
	        ReleaseTensorRT();
            ReleaseOnnxRuntime();
            OpenCVDnnNet = cv::dnn::Net();
	        return false;
	    }
	    catch (...)
	    {
	        UE_LOG(LogTemp, Error, TEXT("Model load failed (unknown exception)"));
	        ReleaseTensorRT();
            ReleaseOnnxRuntime();
            OpenCVDnnNet = cv::dnn::Net();
	        return false;
	    }
	}

void UMyActorComponent::ReleaseTensorRT()
{
#if WITH_TENSORRT
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
#endif

    ResetInferenceOutputState();
    bIsModelLoaded = false;
}

void UMyActorComponent::ReleaseOnnxRuntime()
{
#if WITH_ONNXRUNTIME
    OnnxRuntimeSession.Reset();
    OnnxRuntimeSessionOptions.Reset();
    OnnxRuntimeEnv.Reset();
    OnnxRuntimeInputHost.Reset();
    OnnxRuntimeInputShape.Reset();
    OnnxRuntimeInputName.clear();
    OnnxRuntimeOutputNames.Reset();
    EffectiveOnnxRuntimeExecutionProvider = EOnnxRuntimeExecutionProvider::CPU;
    bOnnxRuntimeUsingGpuProvider = false;
#endif
    ResetInferenceOutputState();
    bIsModelLoaded = false;
}

bool UMyActorComponent::RunTensorRT()
{
#if WITH_TENSORRT
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
#else
    UE_LOG(LogTemp, Error, TEXT("TensorRT inference requested but this build was compiled without TensorRT."));
    return false;
#endif
}

bool UMyActorComponent::RunTensorRTInference_BG(const cv::Mat& ModelInputBGR)
{
#if WITH_TENSORRT
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
#else
    UE_LOG(LogTemp, Error, TEXT("TensorRT inference requested but this build was compiled without TensorRT."));
    return false;
#endif
}

bool UMyActorComponent::LoadOnnxRuntime(const FString& ModelPathUE)
{
#if WITH_ONNXRUNTIME
    if (!ModelPathUE.EndsWith(TEXT(".onnx"), ESearchCase::IgnoreCase))
    {
        UE_LOG(LogTemp, Error, TEXT("ONNX Runtime requires an .onnx model file, got: %s"), *ModelPathUE);
        return false;
    }

    try
    {
        OnnxRuntimeEnv = MakeUnique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "EagleEye");
        OnnxRuntimeSessionOptions = MakeUnique<Ort::SessionOptions>();
        OnnxRuntimeSessionOptions->SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        OnnxRuntimeSessionOptions->SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
        OnnxRuntimeSessionOptions->DisableMemPattern();

        EffectiveOnnxRuntimeExecutionProvider = ResolveEffectiveOnnxRuntimeProvider();
        if (!IsOnnxRuntimeProviderCompiled(EffectiveOnnxRuntimeExecutionProvider))
        {
            UE_LOG(LogTemp, Error, TEXT("ONNX Runtime provider requested but not compiled into this build: %s"),
                OnnxProviderToString(EffectiveOnnxRuntimeExecutionProvider));
            ReleaseOnnxRuntime();
            return false;
        }

        bool bProviderConfigured = false;
        const bool bProviderAuto = OnnxRuntimeExecutionProvider == EOnnxRuntimeExecutionProvider::Auto;

#if WITH_ONNXRUNTIME_DML
        if (EffectiveOnnxRuntimeExecutionProvider == EOnnxRuntimeExecutionProvider::DirectML)
        {
            if (OrtStatus* Status = OrtSessionOptionsAppendExecutionProvider_DML(*OnnxRuntimeSessionOptions, 0))
            {
                const FString ErrorMessage = UTF8_TO_TCHAR(Ort::GetApi().GetErrorMessage(Status));
                Ort::GetApi().ReleaseStatus(Status);
                if (!bProviderAuto)
                {
                    UE_LOG(LogTemp, Error, TEXT("Failed to configure ONNX Runtime DirectML provider: %s"), *ErrorMessage);
                    return false;
                }
                UE_LOG(LogTemp, Warning, TEXT("ONNX Runtime DirectML provider unavailable, falling back to CPU: %s"), *ErrorMessage);
                EffectiveOnnxRuntimeExecutionProvider = EOnnxRuntimeExecutionProvider::CPU;
            }
            else
            {
                bProviderConfigured = true;
                bOnnxRuntimeUsingGpuProvider = true;
                UE_LOG(LogTemp, Log, TEXT("ONNX Runtime provider configured: DirectML"));
            }
        }
#endif

#if WITH_ONNXRUNTIME_MIGRAPHX
        if (!bProviderConfigured && EffectiveOnnxRuntimeExecutionProvider == EOnnxRuntimeExecutionProvider::MIGraphX)
        {
            if (OrtStatus* Status = OrtSessionOptionsAppendExecutionProvider_MIGraphX(*OnnxRuntimeSessionOptions, 0))
            {
                const FString ErrorMessage = UTF8_TO_TCHAR(Ort::GetApi().GetErrorMessage(Status));
                Ort::GetApi().ReleaseStatus(Status);
                if (!bProviderAuto)
                {
                    UE_LOG(LogTemp, Error, TEXT("Failed to configure ONNX Runtime MIGraphX provider: %s"), *ErrorMessage);
                    return false;
                }
                UE_LOG(LogTemp, Warning, TEXT("ONNX Runtime MIGraphX provider unavailable, falling back to CPU: %s"), *ErrorMessage);
                EffectiveOnnxRuntimeExecutionProvider = EOnnxRuntimeExecutionProvider::CPU;
            }
            else
            {
                bProviderConfigured = true;
                bOnnxRuntimeUsingGpuProvider = true;
                UE_LOG(LogTemp, Log, TEXT("ONNX Runtime provider configured: MIGraphX"));
            }
        }
#endif

        if (!bProviderConfigured)
        {
            EffectiveOnnxRuntimeExecutionProvider = EOnnxRuntimeExecutionProvider::CPU;
            bOnnxRuntimeUsingGpuProvider = false;
            UE_LOG(LogTemp, Log, TEXT("ONNX Runtime provider configured: CPU"));
        }

        OnnxRuntimeSession = MakeUnique<Ort::Session>(*OnnxRuntimeEnv, *ModelPathUE, *OnnxRuntimeSessionOptions);

        Ort::AllocatorWithDefaultOptions Allocator;
        if (OnnxRuntimeSession->GetInputCount() <= 0 || OnnxRuntimeSession->GetOutputCount() <= 0)
        {
            UE_LOG(LogTemp, Error, TEXT("ONNX Runtime model has no inputs or outputs: %s"), *ModelPathUE);
            ReleaseOnnxRuntime();
            return false;
        }

        Ort::AllocatedStringPtr InputName = OnnxRuntimeSession->GetInputNameAllocated(0, Allocator);
        OnnxRuntimeInputName = InputName.get();

        OnnxRuntimeOutputNames.Reset();
        const size_t OutputCount = OnnxRuntimeSession->GetOutputCount();
        for (size_t OutputIndex = 0; OutputIndex < OutputCount; ++OutputIndex)
        {
            Ort::AllocatedStringPtr OutputName = OnnxRuntimeSession->GetOutputNameAllocated(OutputIndex, Allocator);
            OnnxRuntimeOutputNames.Add(std::string(OutputName.get()));
        }

        const Ort::TypeInfo InputTypeInfo = OnnxRuntimeSession->GetInputTypeInfo(0);
        const Ort::TensorTypeAndShapeInfo InputTensorInfo = InputTypeInfo.GetTensorTypeAndShapeInfo();
        std::vector<int64_t> InputShape = InputTensorInfo.GetShape();
        if (InputShape.size() == 4)
        {
            if (InputShape[2] > 0)
            {
                ModelInputHeight = static_cast<int32>(InputShape[2]);
            }
            if (InputShape[3] > 0)
            {
                ModelInputWidth = static_cast<int32>(InputShape[3]);
            }
        }
        else
        {
            const int32 ClampedInputSize = FMath::Clamp(OnnxInputSize, 160, 1280);
            ModelInputWidth = ClampedInputSize;
            ModelInputHeight = ClampedInputSize;
        }

        ModelInputWidth = FMath::Clamp(ModelInputWidth, 160, 1280);
        ModelInputHeight = FMath::Clamp(ModelInputHeight, 160, 1280);
        OnnxRuntimeInputShape.Reset();
        OnnxRuntimeInputShape.Add(1);
        OnnxRuntimeInputShape.Add(3);
        OnnxRuntimeInputShape.Add(ModelInputHeight);
        OnnxRuntimeInputShape.Add(ModelInputWidth);
        TrtInputElements = ModelInputWidth * ModelInputHeight * 3;
        OnnxRuntimeInputHost.SetNumUninitialized(TrtInputElements);
        TrtOutputHost.Reset();
        TrtOutputElements = 0;
        TrtOutputChannels = 0;
        TrtOutputDetections = 0;
        bIsOnnxModel = true;
        bIsModelLoaded = true;

        UE_LOG(LogTemp, Log, TEXT("ONNX Runtime model loaded: %s (input=%dx%d, provider=%s%s)"),
            *ModelPathUE,
            ModelInputWidth,
            ModelInputHeight,
            OnnxProviderToString(EffectiveOnnxRuntimeExecutionProvider),
            bOnnxRuntimeUsingGpuProvider ? TEXT(", GPU") : TEXT(", CPU"));
        return true;
    }
    catch (const Ort::Exception& e)
    {
        UE_LOG(LogTemp, Error, TEXT("ONNX Runtime model load failed: %s"), *FString(e.what()));
        ReleaseOnnxRuntime();
        return false;
    }
#else
    UE_LOG(LogTemp, Error, TEXT("ONNX Runtime backend selected but this build was compiled without ONNX Runtime."));
    return false;
#endif
}

bool UMyActorComponent::RunOnnxRuntimeInference_BG(const cv::Mat& ModelInputBGR)
{
#if WITH_ONNXRUNTIME
    if (!OnnxRuntimeSession || OnnxRuntimeInputName.empty() || OnnxRuntimeOutputNames.Num() == 0 || TrtInputElements <= 0)
    {
        UE_LOG(LogTemp, Error, TEXT("ONNX Runtime backend selected but session is not initialized"));
        return false;
    }

    cv::Mat imgRGB;
    cv::cvtColor(ModelInputBGR, imgRGB, cv::COLOR_BGR2RGB);

    cv::Mat imgFloat;
    imgRGB.convertTo(imgFloat, CV_32F, 1.0f / 255.0f);

    const int32 PlaneSize = ModelInputWidth * ModelInputHeight;
    if (OnnxRuntimeInputHost.Num() != PlaneSize * 3)
    {
        OnnxRuntimeInputHost.SetNumUninitialized(PlaneSize * 3);
        TrtInputElements = PlaneSize * 3;
    }

    std::vector<cv::Mat> Channels;
    cv::split(imgFloat, Channels);
    if (Channels.size() != 3)
    {
        UE_LOG(LogTemp, Error, TEXT("Unexpected channel count from preprocessing: %d"), Channels.size());
        return false;
    }

    float* InputPtr = OnnxRuntimeInputHost.GetData();
    for (int32 c = 0; c < 3; ++c)
    {
        const float* Src = Channels[c].ptr<float>();
        FMemory::Memcpy(InputPtr + (c * PlaneSize), Src, PlaneSize * sizeof(float));
    }

    try
    {
        Ort::MemoryInfo MemoryInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        Ort::Value InputTensor = Ort::Value::CreateTensor<float>(
            MemoryInfo,
            OnnxRuntimeInputHost.GetData(),
            OnnxRuntimeInputHost.Num(),
            OnnxRuntimeInputShape.GetData(),
            OnnxRuntimeInputShape.Num());

        const char* InputNames[] = { OnnxRuntimeInputName.c_str() };
        TArray<const char*> OutputNamePtrs;
        OutputNamePtrs.Reserve(OnnxRuntimeOutputNames.Num());
        for (const std::string& OutputName : OnnxRuntimeOutputNames)
        {
            OutputNamePtrs.Add(OutputName.c_str());
        }

        std::vector<Ort::Value> Outputs = OnnxRuntimeSession->Run(
            Ort::RunOptions{ nullptr },
            InputNames,
            &InputTensor,
            1,
            OutputNamePtrs.GetData(),
            OutputNamePtrs.Num());

        Ort::Value* ChosenOutput = nullptr;
        int64 BestElementCount = -1;
        for (Ort::Value& Output : Outputs)
        {
            if (!Output.IsTensor())
            {
                continue;
            }

            Ort::TensorTypeAndShapeInfo TensorInfo = Output.GetTensorTypeAndShapeInfo();
            if (TensorInfo.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT)
            {
                continue;
            }

            const int64 ElementCount = static_cast<int64>(TensorInfo.GetElementCount());
            if (ElementCount > BestElementCount)
            {
                BestElementCount = ElementCount;
                ChosenOutput = &Output;
            }
        }

        if (!ChosenOutput || BestElementCount <= 0)
        {
            UE_LOG(LogTemp, Error, TEXT("ONNX Runtime produced no usable float tensor output"));
            return false;
        }

        Ort::TensorTypeAndShapeInfo OutputInfo = ChosenOutput->GetTensorTypeAndShapeInfo();
        const std::vector<int64_t> OutputShape = OutputInfo.GetShape();
        TrtOutputHost.SetNumUninitialized(static_cast<int32>(BestElementCount));
        FMemory::Memcpy(
            TrtOutputHost.GetData(),
            ChosenOutput->GetTensorMutableData<float>(),
            static_cast<size_t>(BestElementCount) * sizeof(float));
        TrtOutputElements = static_cast<int32>(BestElementCount);

        TArray<int32> NonSingletonDims;
        for (int64_t Dim : OutputShape)
        {
            if (Dim > 1 && Dim <= TNumericLimits<int32>::Max())
            {
                NonSingletonDims.Add(static_cast<int32>(Dim));
            }
        }

        if (NonSingletonDims.Num() >= 2)
        {
            TrtOutputChannels = NonSingletonDims[NonSingletonDims.Num() - 2];
            TrtOutputDetections = NonSingletonDims[NonSingletonDims.Num() - 1];
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("Unsupported ONNX Runtime output rank"));
            return false;
        }

        return true;
    }
    catch (const Ort::Exception& e)
    {
        UE_LOG(LogTemp, Error, TEXT("ONNX Runtime inference failed: %s"), *FString(e.what()));
        return false;
    }
#else
    UE_LOG(LogTemp, Error, TEXT("ONNX Runtime inference requested but this build was compiled without ONNX Runtime."));
    return false;
#endif
}

bool UMyActorComponent::RunOpenCVDNNInference_BG(const cv::Mat& ModelInputBGR)
{
    if (EffectiveInferenceBackend != EDetectionInferenceBackend::OpenCVDNN)
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

TArray<FDetectionResult> UMyActorComponent::ProcessWithOpenCV_BG(const TArray<FColor>& Bitmap, int32 Width, int32 Height, double* OutInferenceMs)
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
    if (EffectiveInferenceBackend == EDetectionInferenceBackend::TensorRT)
    {
        bInferOk = RunTensorRTInference_BG(modelInputBGR);
    }
    else if (EffectiveInferenceBackend == EDetectionInferenceBackend::ONNXRuntime)
    {
        bInferOk = RunOnnxRuntimeInference_BG(modelInputBGR);
    }
    else if (EffectiveInferenceBackend == EDetectionInferenceBackend::OpenCVDNN)
    {
        bInferOk = RunOpenCVDNNInference_BG(modelInputBGR);
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("Unknown inference backend value: %d"), static_cast<int32>(EffectiveInferenceBackend));
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
        UE_LOG(LogTemp, Log, TEXT("%s forward: %.1f ms"), BackendToString(EffectiveInferenceBackend), InferMs);
    }

    std::vector<cv::Rect> boxes;
    std::vector<float> confidences;
    std::vector<int> class_ids;

    const float ConfThreshold = FMath::Clamp(ConfidenceThreshold, 0.01f, 0.99f);
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

    if (EffectiveInferenceBackend == EDetectionInferenceBackend::OpenCVDNN && indices.empty())
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
        det.Confidence = confidences[idx];
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
