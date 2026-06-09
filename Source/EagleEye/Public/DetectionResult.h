#pragma once

#include "CoreMinimal.h"
#include "DetectionResult.generated.h"

USTRUCT(BlueprintType)
struct FDetectionResult {
    GENERATED_BODY()

    UPROPERTY()
    TArray<FVector2D> Corners;   // 4 corners in screen space

    UPROPERTY()
    FString Label;               // "person: 0.92"

    UPROPERTY()
    float Confidence = 0.f;

    int32 ClassId = -1;
};
