#include "EagleEyeDetectionSettings.h"

#include "EagleEyeModelDiscovery.h"

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
