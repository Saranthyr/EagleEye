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

