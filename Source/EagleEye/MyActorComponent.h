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
#include "MyActorComponent.generated.h"



UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent), Blueprintable )
class EAGLEEYE_API UMyActorComponent : public USceneComponent
{
	GENERATED_BODY()

public:	
	// Sets default values for this component's properties
	UMyActorComponent();
	
    void TestOpenCV();

protected:
	// Called when the game starts
	virtual void BeginPlay() override;

public:
    UFUNCTION(BlueprintCallable, Category="Camera Processing")
    void CaptureAndProcess(int threshold);
    
    UPROPERTY(BlueprintReadOnly)
    UTexture2D* OverlayText;

private:
    UPROPERTY()
    USceneCaptureComponent2D* SceneCaptureComponent;

    UPROPERTY()
    UTextureRenderTarget2D* RenderTarget;

    cv::dnn::Net yolo_net;

    std::vector<std::string> class_names;

    void InitializeSceneCapture();
    
    void get_class_names(const std::string& FilePath);

    void get_yolo_net(const std::string& FilePath1, const std::string& FilePath2);

    cv::Mat ProcessWithOpenCV(const TArray<FColor>& Bitmap, int32 Width, int32 Height, int threshold, cv::dnn::Net& net);


public:	
	// Called every frame
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	
};
