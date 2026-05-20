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

    UFUNCTION(BlueprintCallable, Category="Detection|HUD")
    void SetDetectionHudEnabled(bool bEnabled);

    UFUNCTION(BlueprintCallable, Category="Detection|HUD")
    void ToggleDetectionHudEnabled();

    UFUNCTION(BlueprintPure, Category="Detection|HUD")
    bool IsDetectionHudEnabled() const { return bDetectionHudEnabled; }
	
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Detection|HUD")
    bool bDetectionHudEnabled = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Gameplay|HUD")
    bool bHealthHudEnabled = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Gameplay|HUD", meta=(ClampMin="1.0"))
    float HealthBarWidth = 260.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Gameplay|HUD", meta=(ClampMin="1.0"))
    float HealthBarHeight = 22.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Gameplay|HUD")
    FVector2D HealthBarScreenOffset = FVector2D(32.f, 32.f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Gameplay|HUD")
    FLinearColor HealthBarFillColor = FLinearColor(0.05f, 0.85f, 0.22f, 1.f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Gameplay|HUD")
    FLinearColor HealthBarLowFillColor = FLinearColor(0.95f, 0.08f, 0.04f, 1.f);

private:
    void DrawPlayerHealth();
    void DrawDetectionList(const TArray<FDetectionResult>& Detections, int32 SourceWidth, int32 SourceHeight, AActor* CameraActor);
};
