#pragma once

#include "CoreMinimal.h"
#include "DetectionInferenceTypes.generated.h"

UENUM(BlueprintType)
enum class EDetectionInferenceBackend : uint8
{
    Auto UMETA(DisplayName="Auto"),
    TensorRT UMETA(DisplayName="TensorRT"),
    ONNXRuntime UMETA(DisplayName="ONNX Runtime"),
    OpenCVDNN UMETA(DisplayName="OpenCV DNN")
};

UENUM(BlueprintType)
enum class EOnnxRuntimeExecutionProvider : uint8
{
    Auto UMETA(DisplayName="Auto"),
    DirectML UMETA(DisplayName="DirectML"),
    MIGraphX UMETA(DisplayName="MIGraphX"),
    CPU UMETA(DisplayName="CPU")
};
