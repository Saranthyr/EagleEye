// Fill out your copyright notice in the Description page of Project Settings.

#include <string>
#include <fstream>
#include <vector>
#include "MyActorComponent.h"

// Sets default values for this component's properties
UMyActorComponent::UMyActorComponent()
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = true;

	// ...
}

void UMyActorComponent::TestOpenCV(){
    UE_LOG(LogTemp, Log, TEXT("Testing OpenCV..."));
    int testDim[3] = {2, 3, 4};
    cv::Mat testMat(3, testDim, CV_32FC1);
    UE_LOG(
        LogTemp, Log,
        TEXT("dimension = %d, %d, %d"),
        testMat.size[0], testMat.size[1], testMat.size[2]);
    UE_LOG(LogTemp, Log, TEXT("Testing Done!"));
}

// Called when the game starts
void UMyActorComponent::BeginPlay()
{
	Super::BeginPlay();
    TestOpenCV();
    InitializeSceneCapture();
	// ...
	
}

void UMyActorComponent::InitializeSceneCapture()
{
    AActor* OwnerActor = GetOwner();

    FVector2D ViewportSize = FVector2D(1, 1);
    GEngine->GameViewport->GetViewportSize(ViewportSize);

    UCameraComponent* Camera = OwnerActor->FindComponentByClass<UCameraComponent>();

    // Create SceneCaptureComponent2D
    SceneCaptureComponent = NewObject<USceneCaptureComponent2D>(this);
    SceneCaptureComponent->SetupAttachment(Camera);
    // UE_LOG(LogTemp, Log, TEXT(GetOwner()->GetParentComponent()));
    SceneCaptureComponent->RegisterComponent();

    // Create RenderTarget
    RenderTarget = NewObject<UTextureRenderTarget2D>();
    RenderTarget->InitAutoFormat(ViewportSize.X, ViewportSize.Y); // Set desired resolution
    RenderTarget->UpdateResource();

    OverlayText = NewObject<UTexture2D>();
    // OverlayText->InitAutoFormat(ViewportSize.X, ViewportSize.Y); // Set desired resolution
    OverlayText->UpdateResource();

    // Configure SceneCaptureComponent
    SceneCaptureComponent->TextureTarget = RenderTarget;
    SceneCaptureComponent->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
    // UE_LOG(LogTemp, Log, TEXT("Init Complete!"));
    std::string FilePath1 = "C:\\Users\\Saranthyr\\Documents\\Unreal Projects\\EagleEye\\Source\\EagleEye\\yolov7.weights";
    std::string FilePath3 = "C:\\Users\\Saranthyr\\Documents\\Unreal Projects\\EagleEye\\Source\\EagleEye\\yolov7.cfg";
    std::string FilePath2 = "C:\\Users\\Saranthyr\\Documents\\Unreal Projects\\EagleEye\\Source\\EagleEye\\coco.names";
    get_class_names(FilePath2);
    get_yolo_net(FilePath3, FilePath1);
    // FString VectorString;
    // // UE_LOG(LogTemp, Warning, TEXT("%s"), );
    // for (size_t i = 0; i < class_names.size(); i++) {
    //     VectorString += FString(class_names[i].c_str()) + TEXT(" ");
    // }
    // UE_LOG(LogTemp, Warning, TEXT("%s"), *VectorString);

    
}

void UMyActorComponent::CaptureAndProcess(int threshold)
{
    if (!RenderTarget)
    {
        UE_LOG(LogTemp, Warning, TEXT("RenderTarget not initialized."));
        return;
    }

    // Capture the current frame
    FTextureRenderTargetResource* RenderTargetResource = RenderTarget->GameThread_GetRenderTargetResource();
    if (!RenderTargetResource)
    {
        UE_LOG(LogTemp, Warning, TEXT("RenderTargetResource not available."));
        return;
    }

    TArray<FColor> Bitmap;
    FReadSurfaceDataFlags ReadPixelFlags(RCM_UNorm);
    ReadPixelFlags.SetLinearToGamma(false);

    // Read the pixels into the Bitmap array
    if (RenderTargetResource->ReadPixels(Bitmap))
    {
        int32 Width = RenderTarget->SizeX;
        int32 Height = RenderTarget->SizeY;

        cv::Mat Img = ProcessWithOpenCV(Bitmap, Width, Height, threshold, yolo_net);

        OverlayText = FOpenCVHelper::TextureFromCvMat(Img);
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("Failed to read pixels from RenderTarget."));
    }
}

void UMyActorComponent::get_class_names(const std::string& FilePath) {
    std::ifstream file(FilePath);
    std::string line;
    while (std::getline(file, line)) {
        class_names.push_back(line);
    }
}

void UMyActorComponent::get_yolo_net(const std::string& FilePath1, const std::string& FilePath2) {
    try
    {
        yolo_net = cv::dnn::readNetFromDarknet(FilePath1, FilePath2);

    }
    catch(const std::exception& e)
    {
        FString str(e.what());
        UE_LOG(LogTemp, Error, TEXT("%s"), *str);
    }
    
}

cv::Mat UMyActorComponent::ProcessWithOpenCV(const TArray<FColor>& Bitmap, int32 Width, int32 Height, int threshold, cv::dnn::Net& net) {
    cv::Mat Image(Height, Width, CV_8UC4);

    for (int32 y = 0; y < Height; y++)
    {
        for (int32 x = 0; x < Width; x++)
        {
            const FColor& Pixel = Bitmap[y * Width + x];
            Image.at<cv::Vec4b>(y, x) = cv::Vec4b(Pixel.B, Pixel.G, Pixel.R, Pixel.A); // BGRA format
        }
    }

    cv::Mat img;
    cv::Mat img2;
    cv::cvtColor(Image, img, cv::COLOR_BGRA2BGR);
    img2 = cv::dnn::blobFromImage(img, 1/255.f, cv::Size(608, 608), cv::Scalar(), true, false);
    net.setPreferableBackend(cv::dnn::DNN_BACKEND_CUDA);
    net.setPreferableTarget(cv::dnn::DNN_TARGET_CUDA);
    // FString res = FString::Printf(TEXT("Blob Shape: %dD ["), img2.dims);
    // for (int i = 0; i < img2.dims; i++) {
    //     res += FString::Printf(TEXT("%d%s"), img2.size[i], (i < img2.dims - 1 ? TEXT(", ") : TEXT("")));
    // }
    // res += TEXT("]");
    // UE_LOG(LogTemp, Error, TEXT("%s"), *res);
//     std::vector<std::string> layerIds = net.getLayerNames();
// UE_LOG(LogTemp, Warning, TEXT("Total layers in the network: %d"), static_cast<int>(layerIds.size()));
    // try {
    // }
    // catch(const std::exception& e)
    // {
    //     FString str(e.what());
    //     UE_LOG(LogTemp, Error, TEXT("%s"), *str);
    // }
    net.setInput(img2);

    std::vector<std::string> names;
    if(names.empty()){
        std::vector<int32_t> out_layers = net.getUnconnectedOutLayers();
        std::vector<std::string> layers_names = net.getLayerNames();
        names.resize( out_layers.size() );
        for( size_t i = 0; i < out_layers.size(); ++i ){
            names[i] = layers_names[out_layers[i] - 1];
        }
    }

    // FString VectorString;
    // // UE_LOG(LogTemp, Warning, TEXT("%s"), );
    // for (size_t i = 0; i < names.size(); i++) {
    //     VectorString += FString(names[i].c_str()) + TEXT(" ");
    // }
    // UE_LOG(LogTemp, Warning, TEXT("%s"), *VectorString);
    std::vector<cv::Mat> outputs;

    // std::vector<cv::Mat> outputs;
    try
    {
        // std::vector<std::string> outputNames = net.getUnconnectedOutLayersNames(); 
        net.forward(outputs, names);
    }
    catch(const std::exception& e)
    {
        FString str(e.what());
        UE_LOG(LogTemp, Error, TEXT("%s"), *str);
    }
    

    std::vector<cv::Rect> boxes;
    std::vector<float> confidences;
    std::vector<int> class_ids;

    // UE_LOG(LogTemp, Warning, TEXT("%d"), outputs.size());

    cv::Mat output = outputs[0];

    for ( cv::Mat& det : outputs ) {
        for( int32_t i = 0; i < det.rows; i++) {
            cv::Mat region = det.row( i );
            cv::Mat scores = region.colRange( 5, det.cols );
            cv::Point class_id;
            double confidence;
            cv::minMaxLoc( scores, 0, &confidence, 0, &class_id );
            constexpr float thr = 0.4;
            if( thr > confidence){
                continue;
            }
            const int32_t x_center = static_cast<int32_t>( region.at<float>( 0 ) * Width);
            const int32_t y_center = static_cast<int32_t>( region.at<float>( 1 ) * Height );
            const int32_t width    = static_cast<int32_t>( region.at<float>( 2 ) * Width );
            const int32_t height   = static_cast<int32_t>( region.at<float>( 3 ) * Height );
            const cv::Rect rectangle  = cv::Rect( x_center - ( width / 2 ), y_center - ( height / 2 ), width, height );

            // Add Class ID, Confidence, Rectangle
            class_ids.push_back( class_id.x );
            confidences.push_back( confidence );
            boxes.push_back( rectangle );
        }
    }

    std::vector<int> indices;
    cv::dnn::NMSBoxes(boxes, confidences, 0.5, 0.4, indices);

    for (int idx : indices) {
        if (idx < boxes.size()) {
            // std::cout << "Box setup:" << std::endl;
            cv::Rect box = boxes[idx];
            // std::cout << "label setup:" << std::endl;
            std::string label = class_names[class_ids[idx]] + ": " + std::to_string(confidences[idx]);
            // std::cout << "rect setup:" << std::endl;
            cv::rectangle(Image, box, cv::Scalar(0, 0, 0), 2);
            // std::cout << "text setup:" << std::endl;
            cv::putText(Image, label, cv::Point(box.x, box.y + 20), cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 0, 0), 2);
        }
    }

    // for (int i = 0; i < output.rows; i++) {
    //     cv::Mat detection = output.row(i);

    //     float cx = detection.at<float>(0);
    //     float cy = detection.at<float>(1);
    //     float w = detection.at<float>(2);
    //     float h = detection.at<float>(3);
        
    //     cv::Mat scores = detection.colRange(4, detection.cols);
    //     cv::Point class_id;
    //     double confidence;
    //     cv::minMaxLoc(scores, 0, &confidence, 0, &class_id);

    //     if (confidence > 0.5) {
    //         int x = static_cast<int>((cx - w / 2) * (Width / 640.0));
    //         int y = static_cast<int>((cy - h / 2) * (Height / 640.0));
    //         w = static_cast<int>(w * (Width / 640.0));
    //         h = static_cast<int>(h * (Height / 640.0));
            
    //         boxes.push_back(cv::Rect(x, y, w, h));
    //         confidences.push_back(static_cast<float>(confidence));
    //         class_ids.push_back(class_id.x);
    //     }
    // }
    
    // std::vector<int> indices;
    // cv::dnn::NMSBoxes(boxes, confidences, 0.5, 0.4, indices);
    
    // for (int idx : indices) {
    //     if (idx < boxes.size()) {
    //         cv::Rect box = boxes[idx];
    //         std::string label = class_names[class_ids[idx]] + ": " + std::to_string(confidences[idx]);
    //         cv::rectangle(img, box, cv::Scalar(0, 0, 0), 2);
    //         cv::putText(img, label, cv::Point(box.x, box.y + 20), cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 0, 0), 2);
    //     }
    // }
    
    cv::imwrite("C:\\Users\\Saranthyr\\Documents\\Unreal Projects\\EagleEye\\Saved\\ProcessedOutput.png", img);
    return Image;
}


// cv::Mat UMyActorComponent::ProcessWithOpenCV(const TArray<FColor>& Bitmap, int32 Width, int32 Height, int threshold)
// {
//     // Convert TArray<FColor> to cv::Mat
//     cv::Mat Image(Height, Width, CV_8UC4);

//     for (int32 y = 0; y < Height; y++)
//     {
//         for (int32 x = 0; x < Width; x++)
//         {
//             const FColor& Pixel = Bitmap[y * Width + x];
//             Image.at<cv::Vec4b>(y, x) = cv::Vec4b(Pixel.B, Pixel.G, Pixel.R, Pixel.A); // BGRA format
//         }
//     }

//     cv::Mat Grayscale;
//     cv::Mat Contours;
//     cv::cvtColor(Image, Grayscale, cv::COLOR_BGR2GRAY);
//     cv::Canny(Grayscale, Contours, threshold, 255);
//     cv::Mat alpha = cv::Mat::zeros(Contours.size(), CV_8UC1);
//     alpha.setTo(255, Contours);

//     std::vector<cv::Mat> channels;

//     cv::split(Image, channels);
//     // channels.push_back(alpha);
//     cv::Mat ContoursBGRA;
//     // cv::merge(channels, ContoursBGRA);

//     std::vector<std::vector<cv::Point>> contours;
//     std::vector<cv::Vec4i> hierarchy;
//     cv::findContours(Contours, contours, hierarchy, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_NONE);
//     std::string FilePath = "C:\\Users\\Saranthyr\\Documents\\Unreal Projects\\EagleEye\\Saved\\ProcessedOutput.png";
    
    
//     // cv::imwrite(FilePath, Image);

//     for (size_t i = 0; i < contours.size(); i++) {
//         // Approximate the contour
//         std::vector<cv::Point> approx;
//         cv::approxPolyDP(contours[i], approx, 0.01 * cv::arcLength(contours[i], true), true);
//         cv::drawContours(Contours, contours, static_cast<int>(i), cv::Scalar(255, 255, 255), 5);

//     }
//     return Contours;
// }

//     std::string FilePath2 = "C:\\Users\\Saranthyr\\Documents\\Unreal Projects\\EagleEye\\Saved\\ProcessedOutputCnt.png";

//     cv::imwrite(FilePath2, Contours);
// }
    // cv::Mat Grayscale;
    // cv::Mat Contours;
    // cv::cvtColor(Image, Grayscale, cv::COLOR_BGR2GRAY);
    // cv::Canny(Grayscale, Contours, 85, 255);
    // std::vector<std::vector<cv::Point>> contours;
    // std::vector<cv::Vec4i> hierarchy;
    // cv::findContours(Contours, contours, hierarchy, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_NONE);

    // for (size_t i = 0; i < contours.size(); i++) {
    //     // Approximate the contour
    //     std::vector<cv::Point> approx;
    //     cv::approxPolyDP(contours[i], approx, 0.01 * cv::arcLength(contours[i], true), true);

    //     if (approx.size() == 5) {
    //         cv::drawContours(Image, contours, static_cast<int>(i), cv::Scalar(255, 255, 255), -1);
    //     } else if (approx.size() == 3) {
    //         cv::drawContours(Image, contours, static_cast<int>(i), cv::Scalar(0, 255, 0), -1);
    //     } else if (approx.size() == 4) {
    //         cv::drawContours(Image, contours, static_cast<int>(i), cv::Scalar(0, 0, 255), -1);
    //     } else if (approx.size() == 9) {
    //         cv::drawContours(Image, contours, static_cast<int>(i), cv::Scalar(255, 255, 0), -1);
    //     } else if (approx.size() > 15) {
    //         cv::drawContours(Image, contours, static_cast<int>(i), cv::Scalar(0, 255, 255), -1);
    //     }
    // }

// }



// Called every frame
void UMyActorComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);	// ...
}
