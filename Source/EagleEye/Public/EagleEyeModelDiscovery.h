#pragma once

#include "CoreMinimal.h"
#include "DetectionInferenceTypes.h"

namespace EagleEyeModelDiscovery
{
    EAGLEEYE_API TArray<FString> GetRuntimeModelSearchDirectories();
    EAGLEEYE_API TArray<FString> GetAvailableModelNames();
    EAGLEEYE_API FString NormalizeModelSelection(const FString& RequestedModel);
    EAGLEEYE_API FString ResolveModelPathForBackend(const FString& RequestedModel, EDetectionInferenceBackend Backend);
}
