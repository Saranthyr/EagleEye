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

    UFUNCTION(BlueprintCallable, Category="Detection|Settings")
    void SetDetectionSettingsMenuOpen(bool bOpen);

    UFUNCTION(BlueprintCallable, Category="Detection|Settings")
    void ToggleDetectionSettingsMenu();

    UFUNCTION(BlueprintPure, Category="Detection|Settings")
    bool IsDetectionSettingsMenuOpen() const { return bDetectionSettingsMenuOpen; }

    UFUNCTION(BlueprintCallable, Category="Detection|Settings")
    bool HandleDetectionSettingsMenuUp();

    UFUNCTION(BlueprintCallable, Category="Detection|Settings")
    bool HandleDetectionSettingsMenuDown();

    UFUNCTION(BlueprintCallable, Category="Detection|Settings")
    bool HandleDetectionSettingsMenuLeft();

    UFUNCTION(BlueprintCallable, Category="Detection|Settings")
    bool HandleDetectionSettingsMenuRight();

    UFUNCTION(BlueprintCallable, Category="Detection|Settings")
    bool HandleDetectionSettingsMenuConfirm();

    UFUNCTION(BlueprintCallable, Category="Detection|Settings")
    bool HandleDetectionSettingsMenuCancel();
	
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
    enum class EDetectionSettingsMenuItem : uint8
    {
        ModelPath,
        Backend,
        OnnxProvider,
        Confidence,
        Nms,
        InputSize,
        Letterbox,
        LetterboxValue,
        NamesPath,
        PathDecisionLogs,
        PathObjectLogs,
        DetectionDebugLogs,
        DetectionPerformanceLogs,
        DetectionMetricLogs,
        ApplyReload,
        Count
    };

    void DrawPlayerHealth();
    void DrawDetectionList(const TArray<FDetectionResult>& Detections, int32 SourceWidth, int32 SourceHeight, AActor* CameraActor);
    void DrawDetectionSettingsMenu();
    void RefreshDetectionSettingsMenu();
    void RebuildDetectionSettingsOptions();
    void AdjustDetectionSettingsValue(int32 Direction);
    void ApplyPendingDetectionSettings();
    FString GetDetectionSettingsItemLabel(int32 ItemIndex) const;
    FString GetDetectionSettingsItemValue(int32 ItemIndex) const;
    void CycleStringOption(const TArray<FString>& Options, FString& Value, int32 Direction) const;

    bool bDetectionSettingsMenuOpen = false;
    int32 SelectedDetectionSettingsItem = 0;
    FString PendingModelPathOverride;
    FString PendingNamesPathOverride;
    FString PendingDarknetCfgPathOverride;
    int32 PendingInferenceBackend = 0;
    int32 PendingOnnxRuntimeExecutionProvider = 0;
    float PendingConfidenceThreshold = 0.25f;
    float PendingNmsThreshold = 0.45f;
    int32 PendingOnnxInputSize = 640;
    bool bPendingUseLetterbox = true;
    int32 PendingLetterboxValue = 114;
    bool bPendingEnablePathfindingDecisionLogs = true;
    bool bPendingEnablePathfindingObjectLogs = false;
    bool bPendingEnableDetectionDebugLogs = true;
    bool bPendingEnableDetectionPerformanceLogs = false;
    bool bPendingEnableDetectionMetricLogs = false;
    TArray<FString> ModelPathOptions;
    TArray<FString> NamesPathOptions;
    FString LastDetectionSettingsMessage;
};
