// Fill out your copyright notice in the Description page of Project Settings.

#include "MyActorComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "HAL/PlatformProcess.h"
#include "Async/Async.h"
#include "Kismet/GameplayStatics.h"

UMyActorComponent::UMyActorComponent()
{
	PrimaryComponentTick.bCanEverTick = false;

	// ...
}

void UMyActorComponent::get_class_names() {
    std::ifstream file(NamesPath);
    std::string line;
    while (std::getline(file, line)) {
        ClassNames.push_back(line);
    }
}

void UMyActorComponent::BeginPlay() {
    Super::BeginPlay();

    UMyActorComponent::NamesPath = "C:\\Users\\Saranthyr\\Documents\\Unreal Projects\\EagleEye\\Source\\EagleEye\\coco.names";
    UMyActorComponent::WeightsPath = "C:\\Users\\Saranthyr\\Documents\\Unreal Projects\\EagleEye\\Source\\EagleEye\\yolov7.weights";
    UMyActorComponent::CfgPath = "C:\\Users\\Saranthyr\\Documents\\Unreal Projects\\EagleEye\\Source\\EagleEye\\yolov7.cfg";

    get_class_names();

    StartWorker();

    GetWorld()->GetTimerManager().SetTimer(
        TimerHandle_Capture, this, &UMyActorComponent::TickCapture,
        1.0f / 30.0f, true, 0.25f
    );

    GetWorld()->GetTimerManager().SetTimer(
        TimerHandle_PullResults, [this]()
        {
            CopyResultsFromWorker();
        },
        0.05f, true, 0.2f
    );
}

void UMyActorComponent::EndPlay(const EEndPlayReason::Type EndPlayReason) {
    StopWorker();
    Super::EndPlay(EndPlayReason);
}

void UMyActorComponent::TickCapture() {
    CaptureAndEnqueue(50);
}

void UMyActorComponent::CaptureAndEnqueue(int Threshold)
{
    if (!GEngine || !GEngine->GameViewport) return;

    FViewport* Viewport = GEngine->GameViewport->Viewport;
    if (!Viewport) return;

    FIntPoint Size = Viewport->GetSizeXY();
    if (Size.X <= 0 || Size.Y <= 0) return;

    TArray<FColor> Pixels;
    if (!Viewport->ReadPixels(Pixels)) return;
    if (Pixels.Num() != Size.X * Size.Y) return;

    {
        TSharedPtr<FFrameData> Dump;
        while (FrameQueue.Dequeue(Dump)) {} 
    }

    TSharedPtr<FFrameData> Frame = MakeShared<FFrameData>();
    Frame->Pixels   = MoveTemp(Pixels); 
    Frame->Width    = Size.X;
    Frame->Height   = Size.Y;
    Frame->Threshold= Threshold;

    FrameQueue.Enqueue(Frame);
}

void UMyActorComponent::StartWorker()
{
    if (bWorkerRunning.load()) return;
    bWorkerRunning.store(true);

    WorkerFuture = Async(EAsyncExecution::Thread, [this]()
{
    if (!LoadYOLO())
    {
        bWorkerRunning.store(false);
        return;
    }

    while (bWorkerRunning.load())
    {
        TSharedPtr<FFrameData> Frame;
        if (!FrameQueue.Dequeue(Frame))
        {
            FPlatformProcess::Sleep(0.001f);
            continue;
        }

        TArray<FDetectionResult> Det = ProcessWithOpenCV_BG(
            Frame->Pixels, Frame->Width, Frame->Height, Frame->Threshold, YoloNet
        );

        {
            FScopeLock Lock(&ResultsMutex);
            ResultsShared = MoveTemp(Det);
        }
    }
});

}

void UMyActorComponent::StopWorker()
{
    if (!bWorkerRunning.load()) return;
    bWorkerRunning.store(false);
    if (WorkerFuture.IsValid())
    {
        WorkerFuture.Wait();
    }
}

void UMyActorComponent::CopyResultsFromWorker()
{
    FScopeLock Lock(&ResultsMutex);
    LastFrameDetections = ResultsShared; 
}

bool UMyActorComponent::LoadYOLO()
{
    try
    {
        YoloNet = cv::dnn::readNetFromDarknet(CfgPath, WeightsPath);
        YoloNet.setPreferableBackend(cv::dnn::DNN_BACKEND_CUDA);
        YoloNet.setPreferableTarget(cv::dnn::DNN_TARGET_CUDA_FP16);

        bIsModelLoaded = true;
        return true;
    }
    catch (const cv::Exception& e)
    {
        UE_LOG(LogTemp, Error, TEXT("YOLO load failed: %s"), *FString(e.what()));
        return false;
    }
}

TArray<FDetectionResult> UMyActorComponent::ProcessWithOpenCV_BG(const TArray<FColor>& Bitmap, int32 Width, int32 Height, int Threshold, cv::dnn::Net& Net)
{
    cv::Mat Image(Height, Width, CV_8UC4);
    for (int32 y = 0; y < Height; y++) {
        const FColor* Row = &Bitmap[y * Width];
        for (int32 x = 0; x < Width; x++) {
            const FColor& P = Row[x];
            Image.at<cv::Vec4b>(y, x) = cv::Vec4b(P.B, P.G, P.R, P.A);
        }
    }
    cv::Mat imgBGR;
    cv::cvtColor(Image, imgBGR, cv::COLOR_BGRA2BGR);
    cv::resize(imgBGR, imgBGR, cv::Size(416, 416));

    cv::Mat blob = cv::dnn::blobFromImage(imgBGR, 1/255.f, cv::Size(416, 416),
                                          cv::Scalar(), true, false);
    Net.setInput(blob);

    std::vector<cv::Mat> outputs;
    Net.forward(outputs, Net.getUnconnectedOutLayersNames());

    std::vector<cv::Rect> boxes;
    std::vector<float> confidences;
    std::vector<int> class_ids;

    for (auto& det : outputs) {
        for (int i = 0; i < det.rows; i++) {
            cv::Mat row = det.row(i);
            cv::Mat scores = row.colRange(5, det.cols);
            cv::Point class_id;
            double confidence;
            cv::minMaxLoc(scores, 0, &confidence, 0, &class_id);
            if (confidence < 0.4) continue;

            int x_center = int(row.at<float>(0) * Width);
            int y_center = int(row.at<float>(1) * Height);
            int w = int(row.at<float>(2) * Width);
            int h = int(row.at<float>(3) * Height);
            boxes.emplace_back(x_center - w/2, y_center - h/2, w, h);
            class_ids.push_back(class_id.x);
            confidences.push_back((float)confidence);
        }
    }

    std::vector<int> indices;
    cv::dnn::NMSBoxes(boxes, confidences, 0.5, 0.4, indices);

    TArray<FDetectionResult> Out;
    for (int idx : indices) {
        cv::Rect box = boxes[idx];
        TArray<FVector2D> corners;
        corners.Add(FVector2D(box.x, box.y));
        corners.Add(FVector2D(box.x + box.width, box.y));
        corners.Add(FVector2D(box.x + box.width, box.y + box.height));
        corners.Add(FVector2D(box.x, box.y + box.height));

        FString label = FString::Printf(
            TEXT("%s: %.2f"),
            *FString(ClassNames[class_ids[idx]].c_str()),
            confidences[idx]
        );

        FDetectionResult det;
        det.Corners = corners;
        det.Label = label;
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
