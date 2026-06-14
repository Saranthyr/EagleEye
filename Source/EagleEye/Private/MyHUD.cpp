// Fill out your copyright notice in the Description page of Project Settings.


#include "MyHUD.h"
#include "Engine/Canvas.h"
#include "EagleEyeDetectionSettings.h"
#include "EagleEyeModelDiscovery.h"
#include "EagleEyeCharacter.h"
#include "EngineUtils.h"
#include "MyActorComponent.h"
#include "GameFramework/PlayerController.h"
#include "Camera/CameraComponent.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"

namespace
{
    constexpr int32 DetectionSettingsMenuItemCount = 15;
    constexpr float DetectionSettingsMenuToggleCooldownSeconds = 0.30f;
    constexpr float DetectionSettingsMenuNavigationCooldownSeconds = 0.16f;
    constexpr float DetectionSettingsMenuValueCooldownSeconds = 0.14f;
    constexpr float DetectionSettingsMenuConfirmCooldownSeconds = 0.25f;

    FString DetectionBackendToString(int32 Value)
    {
        switch (static_cast<EDetectionInferenceBackend>(Value))
        {
        case EDetectionInferenceBackend::Auto:
            return TEXT("Auto");
        case EDetectionInferenceBackend::TensorRT:
            return TEXT("TensorRT");
        case EDetectionInferenceBackend::ONNXRuntime:
            return TEXT("ONNX Runtime");
        case EDetectionInferenceBackend::OpenCVDNN:
            return TEXT("OpenCV DNN");
        default:
            return TEXT("Unknown");
        }
    }

    FString OnnxProviderToString(int32 Value)
    {
        switch (static_cast<EOnnxRuntimeExecutionProvider>(Value))
        {
        case EOnnxRuntimeExecutionProvider::Auto:
            return TEXT("Auto");
        case EOnnxRuntimeExecutionProvider::DirectML:
            return TEXT("DirectML");
        case EOnnxRuntimeExecutionProvider::MIGraphX:
            return TEXT("MIGraphX");
        case EOnnxRuntimeExecutionProvider::CPU:
            return TEXT("CPU");
        default:
            return TEXT("Unknown");
        }
    }

    void AddUniqueRuntimeFileOption(TArray<FString>& Options, const FString& FilePath)
    {
        if (FilePath.IsEmpty())
        {
            return;
        }

        FString NormalizedPath = FPaths::ConvertRelativePathToFull(FilePath);
        FPaths::NormalizeFilename(NormalizedPath);

        const FString ProjectDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
        const FString SourceModelDir = FPaths::Combine(ProjectDir, TEXT("Source/EagleEye"));
        FString Option = NormalizedPath;
        if (NormalizedPath.StartsWith(SourceModelDir, ESearchCase::IgnoreCase))
        {
            Option = FPaths::GetCleanFilename(NormalizedPath);
        }
        else if (Option.StartsWith(ProjectDir, ESearchCase::IgnoreCase))
        {
            FPaths::MakePathRelativeTo(Option, *ProjectDir);
        }

        Options.AddUnique(Option);
    }

    void FindRuntimeFileOptions(TArray<FString>& Options, const TArray<FString>& Extensions)
    {
        TArray<FString> SearchDirs;
        SearchDirs.Add(FPaths::Combine(FPaths::ProjectDir(), TEXT("Models")));
        SearchDirs.Add(FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Models")));
        SearchDirs.Add(FPaths::Combine(FPaths::ProjectDir(), TEXT("Source"), TEXT("EagleEye")));

        for (const FString& SearchDir : SearchDirs)
        {
            if (!IFileManager::Get().DirectoryExists(*SearchDir))
            {
                continue;
            }

            for (const FString& Extension : Extensions)
            {
                TArray<FString> FoundFiles;
                IFileManager::Get().FindFilesRecursive(
                    FoundFiles,
                    *SearchDir,
                    *FString::Printf(TEXT("*.%s"), *Extension),
                    true,
                    false);

                for (const FString& FoundFile : FoundFiles)
                {
                    AddUniqueRuntimeFileOption(Options, FoundFile);
                }
            }
        }

        Options.Sort();
    }

    FString TrimForHud(const FString& Value, int32 MaxChars)
    {
        if (Value.Len() <= MaxChars)
        {
            return Value;
        }
        return TEXT("...") + Value.Right(FMath::Max(1, MaxChars - 3));
    }
}

void AMyHUD::SetDetectionHudEnabled(bool bEnabled)
{
    bDetectionHudEnabled = bEnabled;
}

void AMyHUD::ToggleDetectionHudEnabled()
{
    SetDetectionHudEnabled(!bDetectionHudEnabled);
}

void AMyHUD::SetDetectionSettingsMenuOpen(bool bOpen)
{
    if (bDetectionSettingsMenuOpen == bOpen)
    {
        return;
    }

    bDetectionSettingsMenuOpen = bOpen;
    UE_LOG(LogTemp, Log, TEXT("Detection settings menu %s on HUD %s."),
        bDetectionSettingsMenuOpen ? TEXT("opened") : TEXT("closed"),
        *GetNameSafe(this));
    if (bDetectionSettingsMenuOpen)
    {
        ResetDetectionSettingsMenuInputTimers();
        RefreshDetectionSettingsMenu();
    }
}

void AMyHUD::ToggleDetectionSettingsMenu()
{
    if (!CanAcceptDetectionSettingsMenuInput(
        LastDetectionSettingsMenuToggleInputTime,
        DetectionSettingsMenuToggleCooldownSeconds))
    {
        return;
    }

    SetDetectionSettingsMenuOpen(!bDetectionSettingsMenuOpen);
}

bool AMyHUD::HandleDetectionSettingsMenuUp()
{
    if (!bDetectionSettingsMenuOpen ||
        !CanAcceptDetectionSettingsMenuInput(
            LastDetectionSettingsNavigationInputTime,
            DetectionSettingsMenuNavigationCooldownSeconds))
    {
        return false;
    }

    SelectedDetectionSettingsItem = (SelectedDetectionSettingsItem + DetectionSettingsMenuItemCount - 1) % DetectionSettingsMenuItemCount;
    return true;
}

bool AMyHUD::HandleDetectionSettingsMenuDown()
{
    if (!bDetectionSettingsMenuOpen ||
        !CanAcceptDetectionSettingsMenuInput(
            LastDetectionSettingsNavigationInputTime,
            DetectionSettingsMenuNavigationCooldownSeconds))
    {
        return false;
    }

    SelectedDetectionSettingsItem = (SelectedDetectionSettingsItem + 1) % DetectionSettingsMenuItemCount;
    return true;
}

bool AMyHUD::HandleDetectionSettingsMenuLeft()
{
    if (!bDetectionSettingsMenuOpen ||
        !CanAcceptDetectionSettingsMenuInput(
            LastDetectionSettingsValueInputTime,
            DetectionSettingsMenuValueCooldownSeconds))
    {
        return false;
    }

    AdjustDetectionSettingsValue(-1);
    return true;
}

bool AMyHUD::HandleDetectionSettingsMenuRight()
{
    if (!bDetectionSettingsMenuOpen ||
        !CanAcceptDetectionSettingsMenuInput(
            LastDetectionSettingsValueInputTime,
            DetectionSettingsMenuValueCooldownSeconds))
    {
        return false;
    }

    AdjustDetectionSettingsValue(1);
    return true;
}

bool AMyHUD::HandleDetectionSettingsMenuConfirm()
{
    if (!bDetectionSettingsMenuOpen ||
        !CanAcceptDetectionSettingsMenuInput(
            LastDetectionSettingsConfirmInputTime,
            DetectionSettingsMenuConfirmCooldownSeconds))
    {
        return false;
    }

    if (SelectedDetectionSettingsItem == static_cast<int32>(EDetectionSettingsMenuItem::ApplyReload))
    {
        ApplyPendingDetectionSettings();
    }
    else
    {
        AdjustDetectionSettingsValue(1);
    }
    return true;
}

bool AMyHUD::HandleDetectionSettingsMenuCancel()
{
    if (!bDetectionSettingsMenuOpen ||
        !CanAcceptDetectionSettingsMenuInput(
            LastDetectionSettingsConfirmInputTime,
            DetectionSettingsMenuConfirmCooldownSeconds))
    {
        return false;
    }

    SetDetectionSettingsMenuOpen(false);
    return true;
}

void AMyHUD::RefreshDetectionSettingsMenu()
{
    RebuildDetectionSettingsOptions();
    UEagleEyeDetectionSettings::LoadRuntimeConfig();

    const UEagleEyeDetectionSettings* Settings = GetDefault<UEagleEyeDetectionSettings>();
    if (!Settings)
    {
        return;
    }

    PendingModelPathOverride = EagleEyeModelDiscovery::NormalizeModelSelection(Settings->ModelPathOverride);
    PendingNamesPathOverride = Settings->NamesPathOverride;
    PendingInferenceBackend = static_cast<int32>(Settings->InferenceBackend);
    PendingOnnxRuntimeExecutionProvider = static_cast<int32>(Settings->OnnxRuntimeExecutionProvider);
    PendingConfidenceThreshold = FMath::Clamp(Settings->ConfidenceThreshold, 0.01f, 0.99f);
    PendingNmsThreshold = FMath::Clamp(Settings->NmsThreshold, 0.01f, 0.99f);
    PendingOnnxInputSize = FMath::Clamp(Settings->OnnxInputSize, 160, 1280);
    bPendingUseLetterbox = Settings->bUseLetterbox;
    PendingLetterboxValue = FMath::Clamp(Settings->LetterboxValue, 0, 255);
    bPendingEnablePathfindingDecisionLogs = Settings->bEnablePathfindingDecisionLogs;
    bPendingEnablePathfindingObjectLogs = Settings->bEnablePathfindingObjectLogs;
    bPendingEnableDetectionDebugLogs = Settings->bEnableDetectionDebugLogs;
    bPendingEnableDetectionPerformanceLogs = Settings->bEnableDetectionPerformanceLogs;
    bPendingEnableDetectionMetricLogs = Settings->bEnableDetectionMetricLogs;
    LastDetectionSettingsMessage = TEXT("Cancel closes. Confirm applies selected action.");

    if (PendingModelPathOverride.IsEmpty() && ModelPathOptions.Num() > 0)
    {
        PendingModelPathOverride = ModelPathOptions[0];
    }
    if (PendingNamesPathOverride.IsEmpty() && NamesPathOptions.Num() > 0)
    {
        PendingNamesPathOverride = NamesPathOptions[0];
    }
}

void AMyHUD::RebuildDetectionSettingsOptions()
{
    ModelPathOptions.Reset();
    NamesPathOptions.Reset();

    ModelPathOptions = EagleEyeModelDiscovery::GetAvailableModelNames();

    TArray<FString> NamesExtensions;
    NamesExtensions.Add(TEXT("names"));
    FindRuntimeFileOptions(NamesPathOptions, NamesExtensions);

    if (!PendingModelPathOverride.IsEmpty())
    {
        ModelPathOptions.AddUnique(PendingModelPathOverride);
    }
    if (!PendingNamesPathOverride.IsEmpty())
    {
        NamesPathOptions.AddUnique(PendingNamesPathOverride);
    }
}

void AMyHUD::CycleStringOption(const TArray<FString>& Options, FString& Value, int32 Direction) const
{
    if (Options.Num() == 0)
    {
        return;
    }

    int32 CurrentIndex = 0;
    for (int32 i = 0; i < Options.Num(); ++i)
    {
        if (Options[i].Equals(Value, ESearchCase::IgnoreCase))
        {
            CurrentIndex = i;
            break;
        }
    }

    const int32 NextIndex = (CurrentIndex + Direction + Options.Num()) % Options.Num();
    Value = Options[NextIndex];
}

bool AMyHUD::CanAcceptDetectionSettingsMenuInput(float& LastInputTime, float MinIntervalSeconds)
{
    const UWorld* World = GetWorld();
    const float Now = World ? World->GetTimeSeconds() : 0.f;
    if (Now - LastInputTime < MinIntervalSeconds)
    {
        return false;
    }

    LastInputTime = Now;
    return true;
}

void AMyHUD::ResetDetectionSettingsMenuInputTimers()
{
    LastDetectionSettingsNavigationInputTime = -1000.f;
    LastDetectionSettingsValueInputTime = -1000.f;
    LastDetectionSettingsConfirmInputTime = -1000.f;
}

void AMyHUD::AdjustDetectionSettingsValue(int32 Direction)
{
    switch (static_cast<EDetectionSettingsMenuItem>(SelectedDetectionSettingsItem))
    {
    case EDetectionSettingsMenuItem::ModelPath:
        CycleStringOption(ModelPathOptions, PendingModelPathOverride, Direction);
        break;
    case EDetectionSettingsMenuItem::Backend:
        PendingInferenceBackend = (PendingInferenceBackend + Direction + 4) % 4;
        break;
    case EDetectionSettingsMenuItem::OnnxProvider:
        PendingOnnxRuntimeExecutionProvider = (PendingOnnxRuntimeExecutionProvider + Direction + 4) % 4;
        break;
    case EDetectionSettingsMenuItem::Confidence:
        PendingConfidenceThreshold = FMath::Clamp(PendingConfidenceThreshold + (0.01f * Direction), 0.01f, 0.99f);
        break;
    case EDetectionSettingsMenuItem::Nms:
        PendingNmsThreshold = FMath::Clamp(PendingNmsThreshold + (0.01f * Direction), 0.01f, 0.99f);
        break;
    case EDetectionSettingsMenuItem::InputSize:
        PendingOnnxInputSize = FMath::Clamp(PendingOnnxInputSize + (32 * Direction), 160, 1280);
        break;
    case EDetectionSettingsMenuItem::Letterbox:
        bPendingUseLetterbox = !bPendingUseLetterbox;
        break;
    case EDetectionSettingsMenuItem::LetterboxValue:
        PendingLetterboxValue = FMath::Clamp(PendingLetterboxValue + Direction, 0, 255);
        break;
    case EDetectionSettingsMenuItem::NamesPath:
        CycleStringOption(NamesPathOptions, PendingNamesPathOverride, Direction);
        break;
    case EDetectionSettingsMenuItem::PathDecisionLogs:
        bPendingEnablePathfindingDecisionLogs = !bPendingEnablePathfindingDecisionLogs;
        break;
    case EDetectionSettingsMenuItem::PathObjectLogs:
        bPendingEnablePathfindingObjectLogs = !bPendingEnablePathfindingObjectLogs;
        break;
    case EDetectionSettingsMenuItem::DetectionDebugLogs:
        bPendingEnableDetectionDebugLogs = !bPendingEnableDetectionDebugLogs;
        break;
    case EDetectionSettingsMenuItem::DetectionPerformanceLogs:
        bPendingEnableDetectionPerformanceLogs = !bPendingEnableDetectionPerformanceLogs;
        break;
    case EDetectionSettingsMenuItem::DetectionMetricLogs:
        bPendingEnableDetectionMetricLogs = !bPendingEnableDetectionMetricLogs;
        break;
    case EDetectionSettingsMenuItem::ApplyReload:
        ApplyPendingDetectionSettings();
        break;
    default:
        break;
    }
}

void AMyHUD::ApplyPendingDetectionSettings()
{
    UEagleEyeDetectionSettings* Settings = GetMutableDefault<UEagleEyeDetectionSettings>();
    if (!Settings)
    {
        LastDetectionSettingsMessage = TEXT("Detection settings unavailable.");
        return;
    }

    Settings->ModelPathOverride = PendingModelPathOverride;
    Settings->NamesPathOverride = PendingNamesPathOverride;
    Settings->InferenceBackend = static_cast<EDetectionInferenceBackend>(PendingInferenceBackend);
    Settings->OnnxRuntimeExecutionProvider = static_cast<EOnnxRuntimeExecutionProvider>(PendingOnnxRuntimeExecutionProvider);
    Settings->ConfidenceThreshold = FMath::Clamp(PendingConfidenceThreshold, 0.01f, 0.99f);
    Settings->NmsThreshold = FMath::Clamp(PendingNmsThreshold, 0.01f, 0.99f);
    Settings->OnnxInputSize = FMath::Clamp(PendingOnnxInputSize, 160, 1280);
    Settings->bUseLetterbox = bPendingUseLetterbox;
    Settings->LetterboxValue = FMath::Clamp(PendingLetterboxValue, 0, 255);
    Settings->bEnablePathfindingDecisionLogs = bPendingEnablePathfindingDecisionLogs;
    Settings->bEnablePathfindingObjectLogs = bPendingEnablePathfindingObjectLogs;
    Settings->bEnableDetectionDebugLogs = bPendingEnableDetectionDebugLogs;
    Settings->bEnableDetectionPerformanceLogs = bPendingEnableDetectionPerformanceLogs;
    Settings->bEnableDetectionMetricLogs = bPendingEnableDetectionMetricLogs;
    const bool bRuntimeConfigSaved = UEagleEyeDetectionSettings::SaveRuntimeConfig();

    int32 ReloadedComponents = 0;
    if (UWorld* World = GetWorld())
    {
        for (TActorIterator<AActor> It(World); It; ++It)
        {
            if (UMyActorComponent* Component = It->FindComponentByClass<UMyActorComponent>())
            {
                Component->ApplyRuntimeDetectionSettingsFromConfig(true);
                ++ReloadedComponents;
            }
        }
    }

    LastDetectionSettingsMessage = FString::Printf(
        TEXT("Saved (%s). Reloaded %d detection component(s). Debug=%s Perf=%s Metrics=%s."),
        bRuntimeConfigSaved ? TEXT("config ok") : TEXT("config write failed"),
        ReloadedComponents,
        Settings->bEnableDetectionDebugLogs ? TEXT("On") : TEXT("Off"),
        Settings->bEnableDetectionPerformanceLogs ? TEXT("On") : TEXT("Off"),
        Settings->bEnableDetectionMetricLogs ? TEXT("On") : TEXT("Off"));
    UE_LOG(LogTemp, Log, TEXT("Detection settings applied: model=%s debug=%s perf=%s metrics=%s pathDecision=%s pathObject=%s config=%s path=%s reloaded=%d"),
        *Settings->ModelPathOverride,
        Settings->bEnableDetectionDebugLogs ? TEXT("true") : TEXT("false"),
        Settings->bEnableDetectionPerformanceLogs ? TEXT("true") : TEXT("false"),
        Settings->bEnableDetectionMetricLogs ? TEXT("true") : TEXT("false"),
        Settings->bEnablePathfindingDecisionLogs ? TEXT("true") : TEXT("false"),
        Settings->bEnablePathfindingObjectLogs ? TEXT("true") : TEXT("false"),
        bRuntimeConfigSaved ? TEXT("updated") : TEXT("failed"),
        *UEagleEyeDetectionSettings::GetRuntimeConfigFilename(),
        ReloadedComponents);
}

FString AMyHUD::GetDetectionSettingsItemLabel(int32 ItemIndex) const
{
    switch (static_cast<EDetectionSettingsMenuItem>(ItemIndex))
    {
    case EDetectionSettingsMenuItem::ModelPath:
        return TEXT("YOLO model");
    case EDetectionSettingsMenuItem::Backend:
        return TEXT("Backend");
    case EDetectionSettingsMenuItem::OnnxProvider:
        return TEXT("ONNX provider");
    case EDetectionSettingsMenuItem::Confidence:
        return TEXT("Confidence");
    case EDetectionSettingsMenuItem::Nms:
        return TEXT("NMS");
    case EDetectionSettingsMenuItem::InputSize:
        return TEXT("Input size");
    case EDetectionSettingsMenuItem::Letterbox:
        return TEXT("Letterbox");
    case EDetectionSettingsMenuItem::LetterboxValue:
        return TEXT("Letterbox value");
    case EDetectionSettingsMenuItem::NamesPath:
        return TEXT("Names file");
    case EDetectionSettingsMenuItem::PathDecisionLogs:
        return TEXT("Path decision logs");
    case EDetectionSettingsMenuItem::PathObjectLogs:
        return TEXT("Path object logs");
    case EDetectionSettingsMenuItem::DetectionDebugLogs:
        return TEXT("Detection debug logs");
    case EDetectionSettingsMenuItem::DetectionPerformanceLogs:
        return TEXT("Detection perf logs");
    case EDetectionSettingsMenuItem::DetectionMetricLogs:
        return TEXT("Detection metric logs");
    case EDetectionSettingsMenuItem::ApplyReload:
        return TEXT("Apply + reload");
    default:
        return TEXT("");
    }
}

FString AMyHUD::GetDetectionSettingsItemValue(int32 ItemIndex) const
{
    switch (static_cast<EDetectionSettingsMenuItem>(ItemIndex))
    {
    case EDetectionSettingsMenuItem::ModelPath:
        return PendingModelPathOverride.IsEmpty() ? TEXT("(none found)") : TrimForHud(PendingModelPathOverride, 46);
    case EDetectionSettingsMenuItem::Backend:
        return DetectionBackendToString(PendingInferenceBackend);
    case EDetectionSettingsMenuItem::OnnxProvider:
        return OnnxProviderToString(PendingOnnxRuntimeExecutionProvider);
    case EDetectionSettingsMenuItem::Confidence:
        return FString::Printf(TEXT("%.2f"), PendingConfidenceThreshold);
    case EDetectionSettingsMenuItem::Nms:
        return FString::Printf(TEXT("%.2f"), PendingNmsThreshold);
    case EDetectionSettingsMenuItem::InputSize:
        return FString::Printf(TEXT("%d"), PendingOnnxInputSize);
    case EDetectionSettingsMenuItem::Letterbox:
        return bPendingUseLetterbox ? TEXT("On") : TEXT("Off");
    case EDetectionSettingsMenuItem::LetterboxValue:
        return FString::Printf(TEXT("%d"), PendingLetterboxValue);
    case EDetectionSettingsMenuItem::NamesPath:
        return PendingNamesPathOverride.IsEmpty() ? TEXT("(none found)") : TrimForHud(PendingNamesPathOverride, 46);
    case EDetectionSettingsMenuItem::PathDecisionLogs:
        return bPendingEnablePathfindingDecisionLogs ? TEXT("On") : TEXT("Off");
    case EDetectionSettingsMenuItem::PathObjectLogs:
        return bPendingEnablePathfindingObjectLogs ? TEXT("On") : TEXT("Off");
    case EDetectionSettingsMenuItem::DetectionDebugLogs:
        return bPendingEnableDetectionDebugLogs ? TEXT("On") : TEXT("Off");
    case EDetectionSettingsMenuItem::DetectionPerformanceLogs:
        return bPendingEnableDetectionPerformanceLogs ? TEXT("On") : TEXT("Off");
    case EDetectionSettingsMenuItem::DetectionMetricLogs:
        return bPendingEnableDetectionMetricLogs ? TEXT("On") : TEXT("Off");
    case EDetectionSettingsMenuItem::ApplyReload:
        return TEXT("Enter");
    default:
        return TEXT("");
    }
}

void AMyHUD::DrawPlayerHealth()
{
    if (!bHealthHudEnabled || !Canvas)
    {
        return;
    }

    const APlayerController* PC = GetOwningPlayerController();
    const AEagleEyeCharacter* PlayerCharacter = PC ? Cast<AEagleEyeCharacter>(PC->GetPawn()) : nullptr;
    if (!PlayerCharacter)
    {
        return;
    }

    const float MaxHealth = FMath::Max(1.f, PlayerCharacter->GetMaxHealth());
    const float CurrentHealth = FMath::Clamp(PlayerCharacter->GetCurrentHealth(), 0.f, MaxHealth);
    const float HealthRatio = CurrentHealth / MaxHealth;

    const float BarWidth = FMath::Max(1.f, HealthBarWidth);
    const float BarHeight = FMath::Max(1.f, HealthBarHeight);
    const float X = FMath::Clamp(HealthBarScreenOffset.X, 0.f, FMath::Max(0.f, Canvas->ClipX - BarWidth));
    const float Y = FMath::Clamp(Canvas->ClipY - HealthBarScreenOffset.Y - BarHeight, 0.f, FMath::Max(0.f, Canvas->ClipY - BarHeight));
    const FLinearColor FillColor = HealthRatio <= 0.30f ? HealthBarLowFillColor : HealthBarFillColor;

    DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.65f), X - 2.f, Y - 2.f, BarWidth + 4.f, BarHeight + 4.f);
    DrawRect(FLinearColor(0.08f, 0.08f, 0.08f, 0.9f), X, Y, BarWidth, BarHeight);
    DrawRect(FillColor, X, Y, BarWidth * HealthRatio, BarHeight);

    DrawLine(X, Y, X + BarWidth, Y, FLinearColor::White, 1.f);
    DrawLine(X + BarWidth, Y, X + BarWidth, Y + BarHeight, FLinearColor::White, 1.f);
    DrawLine(X + BarWidth, Y + BarHeight, X, Y + BarHeight, FLinearColor::White, 1.f);
    DrawLine(X, Y + BarHeight, X, Y, FLinearColor::White, 1.f);

    const FString HealthText = FString::Printf(TEXT("HP %.0f / %.0f"), CurrentHealth, MaxHealth);
    DrawText(HealthText, FLinearColor::White, X, Y - 22.f, nullptr, 1.0f, false);
}

void AMyHUD::DrawBotKillCount()
{
    if (!bKillCountHudEnabled || !Canvas)
    {
        return;
    }

    const APlayerController* PC = GetOwningPlayerController();
    const AEagleEyeCharacter* PlayerCharacter = PC ? Cast<AEagleEyeCharacter>(PC->GetPawn()) : nullptr;
    if (!PlayerCharacter)
    {
        return;
    }

    const FString KillText = FString::Printf(TEXT("Bots killed: %d"), PlayerCharacter->GetBotKillCount());
    const float TextScale = 1.0f;
    const float X = FMath::Clamp(HealthBarScreenOffset.X, 0.f, FMath::Max(0.f, Canvas->ClipX - 180.f));
    const float HealthY = FMath::Clamp(
        Canvas->ClipY - HealthBarScreenOffset.Y - FMath::Max(1.f, HealthBarHeight),
        0.f,
        FMath::Max(0.f, Canvas->ClipY - FMath::Max(1.f, HealthBarHeight)));
    const float Y = FMath::Max(8.f, HealthY - 48.f);

    DrawText(KillText, FLinearColor(0.95f, 0.95f, 0.95f, 1.f), X, Y, nullptr, TextScale, false);
}

void AMyHUD::DrawDeathPrompt()
{
    if (!bDeathPromptHudEnabled || !Canvas)
    {
        return;
    }

    const APlayerController* PC = GetOwningPlayerController();
    const AEagleEyeCharacter* PlayerCharacter = PC ? Cast<AEagleEyeCharacter>(PC->GetPawn()) : nullptr;
    if (!PlayerCharacter || !PlayerCharacter->IsDead())
    {
        return;
    }

    DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.55f), 0.f, 0.f, Canvas->ClipX, Canvas->ClipY);

    const FString TitleText = TEXT("YOU DIED");
    const FString KillText = FString::Printf(TEXT("Bots killed: %d"), PlayerCharacter->GetBotKillCount());
    const FString PromptText = TEXT("Press any button to restart");

    const float CenterX = Canvas->ClipX * 0.5f;
    const float CenterY = Canvas->ClipY * 0.5f;

    DrawText(TitleText, FLinearColor(0.95f, 0.08f, 0.04f, 1.f), CenterX - 92.f, CenterY - 76.f, nullptr, 2.0f, false);
    DrawText(KillText, FLinearColor::White, CenterX - 74.f, CenterY - 18.f, nullptr, 1.15f, false);
    DrawText(PromptText, FLinearColor(0.85f, 0.88f, 0.90f, 1.f), CenterX - 132.f, CenterY + 24.f, nullptr, 1.0f, false);
}

void AMyHUD::DrawDetectionList(const TArray<FDetectionResult>& Detections, int32 SourceWidth, int32 SourceHeight, AActor* CameraActor)
{
    if (!Canvas || SourceWidth <= 0 || SourceHeight <= 0)
    {
        return;
    }

    float DrawOriginX = Canvas->OrgX;
    float DrawOriginY = Canvas->OrgY;
    float DrawWidth = Canvas->ClipX;
    float DrawHeight = Canvas->ClipY;

    if (CameraActor)
    {
        if (const UCameraComponent* Camera = CameraActor->FindComponentByClass<UCameraComponent>())
        {
            if (Camera->bConstrainAspectRatio && Camera->AspectRatio > KINDA_SMALL_NUMBER)
            {
                const float ViewAspect = DrawWidth / FMath::Max(DrawHeight, KINDA_SMALL_NUMBER);
                const float TargetAspect = Camera->AspectRatio;
                if (ViewAspect > TargetAspect)
                {
                    const float FittedWidth = DrawHeight * TargetAspect;
                    DrawOriginX += (DrawWidth - FittedWidth) * 0.5f;
                    DrawWidth = FittedWidth;
                }
                else
                {
                    const float FittedHeight = DrawWidth / TargetAspect;
                    DrawOriginY += (DrawHeight - FittedHeight) * 0.5f;
                    DrawHeight = FittedHeight;
                }
            }
        }
    }

    const float ScaleX = DrawWidth / static_cast<float>(SourceWidth);
    const float ScaleY = DrawHeight / static_cast<float>(SourceHeight);

    for (const FDetectionResult& Det : Detections)
    {
        for (int32 i = 0; i < Det.Corners.Num(); ++i)
        {
            FVector2D Start = Det.Corners[i];
            FVector2D End = Det.Corners[(i + 1) % Det.Corners.Num()];
            Start.X = DrawOriginX + (Start.X * ScaleX);
            Start.Y = DrawOriginY + (Start.Y * ScaleY);
            End.X = DrawOriginX + (End.X * ScaleX);
            End.Y = DrawOriginY + (End.Y * ScaleY);
            DrawLine(Start.X, Start.Y, End.X, End.Y, FLinearColor::Red);
        }

        if (Det.Corners.Num() > 0)
        {
            const float LabelX = DrawOriginX + (Det.Corners[0].X * ScaleX);
            const float LabelY = DrawOriginY + (Det.Corners[0].Y * ScaleY);
            DrawText(Det.Label, FLinearColor::Yellow, LabelX, LabelY, nullptr, 1.0f, false);
        }
    }
}

void AMyHUD::DrawDetectionSettingsMenu()
{
    if (!Canvas)
    {
        return;
    }

    const float PanelWidth = FMath::Min(760.f, FMath::Max(360.f, Canvas->ClipX - 80.f));
    const float RowHeight = Canvas->ClipY < 700.f ? 24.f : 28.f;
    const float PanelHeight = 118.f + RowHeight * DetectionSettingsMenuItemCount;
    const float X = FMath::Max(20.f, (Canvas->ClipX - PanelWidth) * 0.5f);
    const float Y = FMath::Max(20.f, (Canvas->ClipY - PanelHeight) * 0.5f);

    DrawRect(FLinearColor(0.01f, 0.015f, 0.02f, 0.88f), X, Y, PanelWidth, PanelHeight);
    DrawRect(FLinearColor(0.08f, 0.18f, 0.20f, 1.f), X, Y, PanelWidth, 3.f);

    DrawText(TEXT("Detection Settings"), FLinearColor::White, X + 18.f, Y + 16.f, nullptr, 1.35f, false);
    DrawText(TEXT("Cancel close   Up/Down select   Left/Right change   Confirm apply/cycle"),
        FLinearColor(0.78f, 0.82f, 0.84f, 1.f),
        X + 18.f,
        Y + 48.f,
        nullptr,
        0.85f,
        false);

    const float RowsY = Y + 82.f;
    for (int32 i = 0; i < DetectionSettingsMenuItemCount; ++i)
    {
        const bool bSelected = i == SelectedDetectionSettingsItem;
        const float RowY = RowsY + RowHeight * i;
        if (bSelected)
        {
            DrawRect(FLinearColor(0.12f, 0.32f, 0.34f, 0.82f), X + 12.f, RowY - 3.f, PanelWidth - 24.f, RowHeight);
        }

        const FLinearColor LabelColor = bSelected ? FLinearColor::White : FLinearColor(0.80f, 0.84f, 0.86f, 1.f);
        const FLinearColor ValueColor = bSelected ? FLinearColor(0.90f, 1.0f, 0.88f, 1.f) : FLinearColor(0.70f, 0.78f, 0.78f, 1.f);
        DrawText((bSelected ? TEXT("> ") : TEXT("  ")) + GetDetectionSettingsItemLabel(i),
            LabelColor,
            X + 20.f,
            RowY,
            nullptr,
            0.88f,
            false);
        DrawText(GetDetectionSettingsItemValue(i),
            ValueColor,
            X + 260.f,
            RowY,
            nullptr,
            0.88f,
            false);
    }

    if (!LastDetectionSettingsMessage.IsEmpty())
    {
        DrawText(TrimForHud(LastDetectionSettingsMessage, 92),
            FLinearColor(0.82f, 0.88f, 0.90f, 1.f),
            X + 18.f,
            Y + PanelHeight - 28.f,
            nullptr,
            0.82f,
            false);
    }
}

void AMyHUD::DrawHUD()
{
    Super::DrawHUD();

    if (!Canvas)
    {
        return;
    }

    DrawPlayerHealth();
    DrawBotKillCount();
    DrawDeathPrompt();

    APlayerController* PC = GetOwningPlayerController();
    const AEagleEyeCharacter* PlayerCharacter = PC ? Cast<AEagleEyeCharacter>(PC->GetPawn()) : nullptr;
    if (!PC || (PlayerCharacter && PlayerCharacter->IsDead()))
    {
        return;
    }

    if (bDetectionSettingsMenuOpen)
    {
        DrawDetectionSettingsMenu();
        return;
    }

    if (!bDetectionHudEnabled)
    {
        return;
    }

    AActor* DetectionActor = PC->GetViewTarget();
    if (!IsValid(DetectionActor))
    {
        DetectionActor = PC->GetPawn();
    }

    UMyActorComponent* Comp = DetectionActor ? DetectionActor->FindComponentByClass<UMyActorComponent>() : nullptr;
    if (!Comp)
    {
        if (APawn* Pawn = PC->GetPawn())
        {
            DetectionActor = Pawn;
            Comp = Pawn->FindComponentByClass<UMyActorComponent>();
        }
    }

    if (!Comp)
    {
        return;
    }

    const int32 SourceWidth = Comp->LastFrameSourceWidth;
    const int32 SourceHeight = Comp->LastFrameSourceHeight;
    if (SourceWidth <= 0 || SourceHeight <= 0)
    {
        return;
    }

    DrawDetectionList(Comp->LastFrameDetections, SourceWidth, SourceHeight, DetectionActor);
}
