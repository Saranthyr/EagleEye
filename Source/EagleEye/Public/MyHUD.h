// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "MyActorComponent.h"
#include "MyHUD.generated.h"

/**
 * 
 */
UCLASS()
class EAGLEEYE_API AMyHUD : public AHUD
{
	GENERATED_BODY()
public:
	virtual void DrawHUD() override;	
	
	UPROPERTY()
    class UMyActorComponent* ActorComponentRef;

    UPROPERTY(EditAnywhere, Category="Detection|Stabilization")
    bool bEnableUniversalStabilization = true;

    UPROPERTY(EditAnywhere, Category="Detection|Stabilization", meta=(ClampMin="0.00", ClampMax="1.00"))
    float TrackMatchIoU = 0.08f;

    UPROPERTY(EditAnywhere, Category="Detection|Stabilization", meta=(ClampMin="1.0", ClampMax="300.0"))
    float CenterMatchPx = 120.0f;

    UPROPERTY(EditAnywhere, Category="Detection|Stabilization", meta=(ClampMin="0.0", ClampMax="30.0"))
    float PositionDeadzonePx = 4.0f;

    UPROPERTY(EditAnywhere, Category="Detection|Stabilization", meta=(ClampMin="0.0", ClampMax="40.0"))
    float SizeDeadzonePx = 6.0f;

    UPROPERTY(EditAnywhere, Category="Detection|Stabilization", meta=(ClampMin="0.05", ClampMax="1.00"))
    float BaseSmoothingAlpha = 0.14f;

    UPROPERTY(EditAnywhere, Category="Detection|Stabilization", meta=(ClampMin="0.05", ClampMax="1.00"))
    float MaxSmoothingAlpha = 0.55f;

    UPROPERTY(EditAnywhere, Category="Detection|Stabilization", meta=(ClampMin="1.0", ClampMax="300.0"))
    float FastShiftPixels = 80.0f;

    UPROPERTY(EditAnywhere, Category="Detection|Stabilization", meta=(ClampMin="0.05", ClampMax="1.00"))
    float VelocityBlendAlpha = 0.20f;

    UPROPERTY(EditAnywhere, Category="Detection|Stabilization", meta=(ClampMin="0.10", ClampMax="2.00"))
    float DynamicGateByBox = 0.90f;

    UPROPERTY(EditAnywhere, Category="Detection|Stabilization", meta=(ClampMin="20.0", ClampMax="1000.0"))
    float MaxPredictionShiftPx = 140.0f;

    UPROPERTY(EditAnywhere, Category="Detection|Stabilization", meta=(ClampMin="0", ClampMax="20"))
    int32 MaxTrackMissedFrames = 8;

    UPROPERTY(EditAnywhere, Category="Detection|Stabilization", meta=(ClampMin="1", ClampMax="10"))
    int32 MinTrackConfirmedFrames = 2;

private:
    struct FTrackedDetection
    {
        FDetectionResult Det;
        FVector2D Center = FVector2D::ZeroVector;
        FVector2D Size = FVector2D::ZeroVector;
        FVector2D Velocity = FVector2D::ZeroVector;
        int32 ClassId = -1;
        int32 MissedFrames = 0;
        int32 SeenFrames = 0;
    };

    TArray<FTrackedDetection> StableTracks;
    int32 StableSourceWidth = 0;
    int32 StableSourceHeight = 0;
    int32 LastProcessedSequence = 0;

    void UpdateStableTracks(const TArray<FDetectionResult>& NewDetections, int32 NewSourceWidth, int32 NewSourceHeight, int32 FrameSequence, float DeltaSeconds);
    void DrawDetectionList(const TArray<FDetectionResult>& Detections, int32 SourceWidth, int32 SourceHeight, APawn* Pawn);
};
