#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "CrowDetectionShareSubsystem.generated.h"

USTRUCT()
struct FCrowSharedPersonDetection
{
    GENERATED_BODY()

    UPROPERTY()
    TWeakObjectPtr<AActor> Reporter;

    UPROPERTY()
    FVector TargetLocation = FVector::ZeroVector;

    UPROPERTY()
    float Confidence = 0.f;

    UPROPERTY()
    float ReportTime = 0.f;
};

UCLASS()
class EAGLEEYE_API UCrowDetectionShareSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    void RegisterDetector(AActor* DetectorOwner);
    void UnregisterDetector(AActor* DetectorOwner);

    bool ShouldRunDetector(AActor* DetectorOwner, int32 MaxActiveDetectors, float MaxDistanceToPlayer) const;

    void PublishPersonDetection(AActor* Reporter, const FVector& TargetLocation, float Confidence);

    bool GetBestRecentPersonDetection(
        const AActor* Requester,
        float MaxAgeSeconds,
        float MaxReporterDistance,
        FVector& OutTargetLocation,
        float& OutConfidence) const;

private:
    UPROPERTY()
    TArray<TWeakObjectPtr<AActor>> RegisteredDetectors;

    UPROPERTY()
    TArray<FCrowSharedPersonDetection> RecentPersonDetections;

    APawn* GetPlayerPawn() const;
};
