// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Components/SceneCaptureComponent2D.h"
#include <opencv2/opencv.hpp>
#include "CameraComp.generated.h"


UCLASS( ClassGroup=(Custom), meta=(BlueprintSpaw0nableComponent), Blueprintable)
class EAGLEEYE_API UCameraComp : public UActorComponent
{
	GENERATED_BODY()

public:	
	// Sets default values for this component's properties
	UCameraComp();

protected:
	// Called when the game starts
	virtual void BeginPlay() override;

//public:	
//	// Called every frame
//	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

// public:
// 	void CaptureAndProcess();

private:
	UPROPERTY()
	USceneCaptureComponent2D* SceneCaptureComponent;

	UPROPERTY()
	UTextureRenderTarget2D* RenderTarget;

	// virtual void InitializeSceneCapture();
	// void ProcessWithOpenCV(const TArray<FColor>& Bitmap, int32 Width, int32 Height);
		
};
