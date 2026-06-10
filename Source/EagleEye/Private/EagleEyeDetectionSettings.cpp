#include "EagleEyeDetectionSettings.h"

#include "EagleEyeModelDiscovery.h"
#include "HAL/FileManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"

namespace
{
    bool bRuntimeDetectionConfigLoaded = false;
}

FString UEagleEyeDetectionSettings::GetRuntimeConfigFilename()
{
    return FConfigCacheIni::NormalizeConfigIniPath(
        FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Config"), TEXT("DetectionSettings.ini")));
}

bool UEagleEyeDetectionSettings::LoadRuntimeConfig(bool bForceReload)
{
    if (bRuntimeDetectionConfigLoaded && !bForceReload)
    {
        return false;
    }

    bRuntimeDetectionConfigLoaded = true;

    const FString RuntimeConfigFilename = GetRuntimeConfigFilename();
    if (!IFileManager::Get().FileExists(*RuntimeConfigFilename))
    {
        return false;
    }

    UEagleEyeDetectionSettings* Settings = GetMutableDefault<UEagleEyeDetectionSettings>();
    if (!Settings)
    {
        return false;
    }

    Settings->LoadConfig(Settings->GetClass(), *RuntimeConfigFilename);
    UE_LOG(LogTemp, Log, TEXT("Detection runtime config loaded: %s"), *RuntimeConfigFilename);
    return true;
}

bool UEagleEyeDetectionSettings::SaveRuntimeConfig()
{
    UEagleEyeDetectionSettings* Settings = GetMutableDefault<UEagleEyeDetectionSettings>();
    if (!Settings)
    {
        return false;
    }

    const FString RuntimeConfigFilename = GetRuntimeConfigFilename();
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(RuntimeConfigFilename), true);

    Settings->SaveConfig(CPF_Config, *RuntimeConfigFilename);
    if (GConfig)
    {
        GConfig->Flush(false, RuntimeConfigFilename);
    }

    const bool bSaved = IFileManager::Get().FileExists(*RuntimeConfigFilename);
    if (bSaved)
    {
        bRuntimeDetectionConfigLoaded = true;
        UE_LOG(LogTemp, Log, TEXT("Detection runtime config saved: %s"), *RuntimeConfigFilename);
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("Detection runtime config save failed: %s"), *RuntimeConfigFilename);
    }

    return bSaved;
}

TArray<FString> UEagleEyeDetectionSettings::GetAvailableModelNames() const
{
    TArray<FString> ModelNames = EagleEyeModelDiscovery::GetAvailableModelNames();
    if (!ModelPathOverride.IsEmpty())
    {
        ModelNames.AddUnique(EagleEyeModelDiscovery::NormalizeModelSelection(ModelPathOverride));
        ModelNames.Sort();
    }
    return ModelNames;
}
