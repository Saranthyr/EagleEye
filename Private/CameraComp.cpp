// Fill out your copyright notice in the Description page of Project Settings.


#include "CameraComp.h"
#include "Engine/TextureRenderTarget2D.h"
// #include "Kismet/GameplayStatics.h"
// #include "Engine/World.h"
#include <opencv2/opencv.hpp>


// Sets default values for this component's properties
UCameraComp::UCameraComp()
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = false;

	// ...
}


// Called when the game starts
void UCameraComp::BeginPlay()
{
	Super::BeginPlay();
	// InitializeSceneCapture();

	// ...
	
}

// void UCameraComp::InitializeSceneCapture()
// {
//     // Create SceneCaptureComponent2D
//     SceneCaptureComponent = NewObject<USceneCaptureComponent2D>(this);
//     SceneCaptureComponent->SetupAttachment(GetOwner()->GetRootComponent());
//     SceneCaptureComponent->RegisterComponent();

    // // Create RenderTarget
    // RenderTarget = NewObject<UTextureRenderTarget2D>();
    // RenderTarget->InitAutoFormat(1920, 1080); // Set desired resolution
    // RenderTarget->UpdateResource();

    // // Configure SceneCaptureComponent
    // SceneCaptureComponent->TextureTarget = RenderTarget;
    // SceneCaptureComponent->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
// }

// void UCameraComp::CaptureAndProcess()
// {
//     if (!RenderTarget)
//     {
//         UE_LOG(LogTemp, Warning, TEXT("RenderTarget not initialized."));
//         return;
//     }

//     // Capture the current frame
//     FTextureRenderTargetResource* RenderTargetResource = RenderTarget->GameThread_GetRenderTargetResource();
//     if (!RenderTargetResource)
//     {
//         UE_LOG(LogTemp, Warning, TEXT("RenderTargetResource not available."));
//         return;
//     }

//     TArray<FColor> Bitmap;
//     FReadSurfaceDataFlags ReadPixelFlags(RCM_UNorm);
//     ReadPixelFlags.SetLinearToGamma(false);

//     // Read the pixels into the Bitmap array
//     if (RenderTargetResource->ReadPixels(Bitmap))
//     {
//         int32 Width = RenderTarget->SizeX;
//         int32 Height = RenderTarget->SizeY;

//         ProcessWithOpenCV(Bitmap, Width, Height);
//     }
//     else
//     {
//         UE_LOG(LogTemp, Warning, TEXT("Failed to read pixels from RenderTarget."));
//     }
// }

// void UCameraComp::ProcessWithOpenCV(const TArray<FColor>& Bitmap, int32 Width, int32 Height)
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

//     // Process the image with OpenCV (example: convert to grayscale)
//     cv::Mat Grayscale;
//     cv::cvtColor(Image, Grayscale, cv::COLOR_BGRA2GRAY);

//     // Save the processed image (optional)
//     cv::imwrite("ProcessedOutput.png", Grayscale);

//     UE_LOG(LogTemp, Log, TEXT("Image processed and saved as ProcessedOutput.png"));
// }


// Called every frame
//void UCameraComp::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
//{
//	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
//  
//	// ...
//}

