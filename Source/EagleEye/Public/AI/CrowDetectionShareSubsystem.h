#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "CrowDetectionShareSubsystem.generated.h"

USTRUCT()
struct FCrowSharedTargetDetection
{
    GENERATED_BODY()

    UPROPERTY()
    TWeakObjectPtr<AActor> Reporter;

    UPROPERTY()
    FVector TargetLocation = FVector::ZeroVector;

    UPROPERTY()
    float Confidence = 0.f;

    UPROPERTY()
    int32 ClassId = -1;

    UPROPERTY()
    FString ClassLabel;

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

    void PublishTargetDetection(AActor* Reporter, const FVector& TargetLocation, float Confidence, int32 ClassId, const FString& ClassLabel);

    bool GetBestRecentTargetDetection(
        const AActor* Requester,
        float MaxAgeSeconds,
        float MaxReporterDistance,
        const TArray<int32>& AcceptedClassIds,
        const TArray<FName>& AcceptedClassLabels,
        FVector& OutTargetLocation,
        float& OutConfidence,
        int32& OutClassId,
        FString& OutClassLabel) const;

private:
    UPROPERTY()
    TArray<TWeakObjectPtr<AActor>> RegisteredDetectors;

    UPROPERTY()
    TArray<FCrowSharedTargetDetection> RecentTargetDetections;

    APawn* GetPlayerPawn() const;
};
