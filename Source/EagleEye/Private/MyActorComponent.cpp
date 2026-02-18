// Fill out your copyright notice in the Description page of Project Settings.

#include "MyActorComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "HAL/PlatformProcess.h"
#include "Async/Async.h"
#include "RHI.h"
#include "Misc/ScopeLock.h"
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
}

UMyActorComponent::UMyActorComponent()
{
	PrimaryComponentTick.bCanEverTick = true;

	// ...
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

    #if PLATFORM_WINDOWS
        UMyActorComponent::NamesPath = "C:\\Users\\Saranthyr\\Documents\\Unreal Projects\\EagleEye\\Source\\EagleEye\\coco.names";
        UMyActorComponent::WeightsPath = "C:\\Users\\Saranthyr\\Documents\\Unreal Projects\\EagleEye\\Source\\EagleEye\\yolov7.weights";
        UMyActorComponent::CfgPath = "C:\\Users\\Saranthyr\\Documents\\Unreal Projects\\EagleEye\\Source\\EagleEye\\yolov7.cfg";
    #elif PLATFORM_LINUX
        UMyActorComponent::NamesPath = "/home/saranthyr/Documents/Unreal Projects/EagleEye/Source/EagleEye/coco.names";
        UMyActorComponent::WeightsPath = "/home/saranthyr/Documents/Unreal Projects/EagleEye/Source/EagleEye/yolo26x_fp16.plan";
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
    StartWorker();

    const float ClampedCaptureFPS = FMath::Clamp(CaptureFPS, 1.0f, 120.0f);
    const float CaptureInterval = 1.0f / ClampedCaptureFPS;
    GetWorld()->GetTimerManager().SetTimer(
        TimerHandle_Capture, this, &UMyActorComponent::TickCapture,
        CaptureInterval, true, 0.0f
    );
}

void UMyActorComponent::EndPlay(const EEndPlayReason::Type EndPlayReason) {
    StopWorker();
    Super::EndPlay(EndPlayReason);
}

void UMyActorComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    // Pull latest detections every frame to minimize overlay latency.
    CopyResultsFromWorker();
}

void UMyActorComponent::TickCapture() {
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

void UMyActorComponent::CaptureAndEnqueue(int Threshold)
{
    TArray<FColor> Pixels;
    int32 SourceWidth = 0;
    int32 SourceHeight = 0;
    if (!CaptureViewportToPixels(Pixels, SourceWidth, SourceHeight))
    {
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
}

void UMyActorComponent::StartWorker()
{
    if (bWorkerRunning.load()) return;
    bWorkerRunning.store(true);

    WorkerFuture = Async(EAsyncExecution::Thread, [this]()
    {
        try
        {
            if (!LoadYOLO())
            {
                bWorkerRunning.store(false);
                return;
            }

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
                try
                {
                    Det = ProcessWithOpenCV_BG(Frame->Pixels, Frame->Width, Frame->Height, Frame->Threshold);
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

                {
                    FScopeLock Lock(&ResultsMutex);
                    ResultsShared = MoveTemp(Det);
                    ResultsSourceWidth = Frame->Width;
                    ResultsSourceHeight = Frame->Height;
                    ++ResultsSequence;
                }
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

	bool UMyActorComponent::LoadYOLO()
	{
	    try
	    {
	        cv::setUseOptimized(true);
	        cv::setNumThreads(FPlatformMisc::NumberOfCoresIncludingHyperthreads());

	        ReleaseTensorRT();

	        const FString ModelPathUE = FString(WeightsPath.c_str());
	        if (!FPlatformFileManager::Get().GetPlatformFile().FileExists(*ModelPathUE))
	        {
	            UE_LOG(LogTemp, Error, TEXT("TensorRT plan not found: %s"), *ModelPathUE);
	            return false;
	        }

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
	    catch (const std::exception& e)
	    {
	        UE_LOG(LogTemp, Error, TEXT("TensorRT load failed: %s"), *FString(e.what()));
	        ReleaseTensorRT();
	        return false;
	    }
	    catch (...)
	    {
	        UE_LOG(LogTemp, Error, TEXT("TensorRT load failed (unknown exception)"));
	        ReleaseTensorRT();
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

TArray<FDetectionResult> UMyActorComponent::ProcessWithOpenCV_BG(const TArray<FColor>& Bitmap, int32 Width, int32 Height, int Threshold)
{
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

    cv::Mat imgRGB;
    cv::cvtColor(modelInputBGR, imgRGB, cv::COLOR_BGR2RGB);

    cv::Mat imgFloat;
    imgRGB.convertTo(imgFloat, CV_32F, 1.0f / 255.0f);

    const int32 PlaneSize = ModelInputWidth * ModelInputHeight;
    if (TrtInputElements != PlaneSize * 3)
    {
        UE_LOG(LogTemp, Error, TEXT("TensorRT input size mismatch (expected %d, got %d)"),
            PlaneSize * 3, TrtInputElements);
        return {};
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
        return {};
    }

    float* InputPtr = TrtInputHost.GetData();
    for (int32 c = 0; c < 3; ++c)
    {
        const float* Src = Channels[c].ptr<float>();
        FMemory::Memcpy(InputPtr + (c * PlaneSize), Src, PlaneSize * sizeof(float));
    }

    const double T0 = FPlatformTime::Seconds();
    if (!RunTensorRT())
    {
        UE_LOG(LogTemp, Error, TEXT("TensorRT inference failed"));
        return {};
    }
    const double InferMs = (FPlatformTime::Seconds() - T0) * 1000.0;
    if (bLogPerf)
    {
        UE_LOG(LogTemp, Log, TEXT("TensorRT forward: %.1f ms"), InferMs);
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

    auto AddXYWH = [&](float cx, float cy, float w, float h, float Score, int ClassId)
    {
        const float x1 = cx - (w * 0.5f);
        const float y1 = cy - (h * 0.5f);
        const float x2 = cx + (w * 0.5f);
        const float y2 = cy + (h * 0.5f);
        cv::Rect MappedBox;
        float Overflow = 0.0f;
        if (MapXYXYToSource(x1, y1, x2, y2, bUseLetterbox, MappedBox, Overflow))
        {
            CommitMappedBox(MappedBox, Score, ClassId);
        }
    };

    if (!bIsOnnxModel || !OutData || TrtOutputChannels <= 0 || TrtOutputDetections <= 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("Unexpected TensorRT output state, skipping detections"));
    }
    else if (TrtOutputDetections == 6 || TrtOutputChannels == 6)
    {
        // End-to-end exports with NMS usually emit [1, N, 6] or [1, 6, N]:
        // [x1, y1, x2, y2, score, class].
        const bool bNBy6 = (TrtOutputDetections == 6);
        const int32 Dets = bNBy6 ? TrtOutputChannels : TrtOutputDetections;
        struct FMappedDet
        {
            cv::Rect Box;
            float Score = 0.0f;
            int32 ClassId = -1;
        };
        std::vector<FMappedDet> UndoLetterboxDetections;
        std::vector<FMappedDet> DirectScaleDetections;
        float UndoOverflow = 0.0f;
        float DirectOverflow = 0.0f;

        for (int32 i = 0; i < Dets; ++i)
        {
            const float x1 = bNBy6 ? OutData[(i * 6) + 0] : OutData[(0 * Dets) + i];
            const float y1 = bNBy6 ? OutData[(i * 6) + 1] : OutData[(1 * Dets) + i];
            const float x2 = bNBy6 ? OutData[(i * 6) + 2] : OutData[(2 * Dets) + i];
            const float y2 = bNBy6 ? OutData[(i * 6) + 3] : OutData[(3 * Dets) + i];
            const float score = bNBy6 ? OutData[(i * 6) + 4] : OutData[(4 * Dets) + i];
            const float classF = bNBy6 ? OutData[(i * 6) + 5] : OutData[(5 * Dets) + i];

            if (score < ConfThreshold)
            {
                continue;
            }

            const int32 classId = FMath::Max(0, FMath::RoundToInt(classF));
            cv::Rect UndoBox;
            cv::Rect DirectBox;
            float BoxUndoOverflow = 0.0f;
            float BoxDirectOverflow = 0.0f;

            const bool bHasUndo = MapXYXYToSource(x1, y1, x2, y2, true, UndoBox, BoxUndoOverflow);
            const bool bHasDirect = MapXYXYToSource(x1, y1, x2, y2, false, DirectBox, BoxDirectOverflow);
            UndoOverflow += BoxUndoOverflow;
            DirectOverflow += BoxDirectOverflow;

            if (bHasUndo)
            {
                UndoLetterboxDetections.push_back({ UndoBox, score, classId });
            }
            if (bHasDirect)
            {
                DirectScaleDetections.push_back({ DirectBox, score, classId });
            }
        }

        bool bUseUndoMapping = bUseLetterbox;
        if (bUseLetterbox && (PadX > 0.5f || PadY > 0.5f))
        {
            const bool bDirectClearlyBetter = !DirectScaleDetections.empty() &&
                (UndoLetterboxDetections.empty() || (DirectOverflow + 1e-3f) < (UndoOverflow * 0.85f));
            if (bDirectClearlyBetter)
            {
                bUseUndoMapping = false;
            }
        }

        const std::vector<FMappedDet>& Selected = bUseUndoMapping ? UndoLetterboxDetections : DirectScaleDetections;
        if (!Selected.empty())
        {
            for (const FMappedDet& Det : Selected)
            {
                CommitMappedBox(Det.Box, Det.Score, Det.ClassId);
            }
        }
        else
        {
            const std::vector<FMappedDet>& Fallback = bUseUndoMapping ? DirectScaleDetections : UndoLetterboxDetections;
            for (const FMappedDet& Det : Fallback)
            {
                CommitMappedBox(Det.Box, Det.Score, Det.ClassId);
            }
        }
    }
    else
    {
        // Raw YOLO layout, commonly [1, attrs, dets] or [1, dets, attrs].
        int32 Attrs = TrtOutputChannels;
        int32 Dets = TrtOutputDetections;
        bool bAttrsMajor = true;

        if (TrtOutputDetections <= 256 && TrtOutputChannels > TrtOutputDetections)
        {
            Attrs = TrtOutputDetections;
            Dets = TrtOutputChannels;
            bAttrsMajor = false;
        }

        if (Attrs < 5 || Dets <= 0)
        {
            UE_LOG(LogTemp, Warning, TEXT("Unsupported raw output shape [%d, %d]"), TrtOutputChannels, TrtOutputDetections);
        }
        else
        {
            const int32 ClassCount = static_cast<int32>(ClassNames.size());
            const bool bHasObjectness = (ClassCount > 0 && (Attrs - 5) == ClassCount);
            const int32 ClassStart = bHasObjectness ? 5 : 4;
            const int32 NumClasses = Attrs - ClassStart;

            auto ReadAttr = [&](int32 Attr, int32 Det) -> float
            {
                return bAttrsMajor ? OutData[(Attr * Dets) + Det] : OutData[(Det * Attrs) + Attr];
            };

            if (NumClasses <= 0)
            {
                UE_LOG(LogTemp, Warning, TEXT("Raw output has no classes (attrs=%d)"), Attrs);
            }
            else
            {
                for (int32 i = 0; i < Dets; ++i)
                {
                    const float cx = ReadAttr(0, i);
                    const float cy = ReadAttr(1, i);
                    const float w = ReadAttr(2, i);
                    const float h = ReadAttr(3, i);
                    const float obj = bHasObjectness ? ReadAttr(4, i) : 1.0f;

                    float bestScore = 0.0f;
                    int32 bestClass = -1;
                    for (int32 c = 0; c < NumClasses; ++c)
                    {
                        const float cls = ReadAttr(ClassStart + c, i);
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

                    AddXYWH(cx, cy, w, h, bestScore, bestClass);
                }
            }
        }
    }

    std::vector<int> indices;
    ApplyNms(boxes, confidences, ConfThreshold, IoUThreshold, indices);

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
