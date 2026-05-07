#pragma once

#include "CoreMinimal.h"
#include "DetectionResult.h"
#include "Subsystems/WorldSubsystem.h"
#include <atomic>
#include "CrowVisionSubsystem.generated.h"

class UMyActorComponent;

UCLASS()
class EAGLEEYE_API UCrowVisionSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Deinitialize() override;

    void RegisterModelHost(UMyActorComponent* Component);
    void UnregisterModelHost(UMyActorComponent* Component);

    void SubmitFrame(
        UMyActorComponent* Requester,
        TArray<FColor>&& Pixels,
        int32 Width,
        int32 Height,
        int32 Threshold);

private:
    struct FQueuedFrame
    {
        TWeakObjectPtr<UMyActorComponent> Requester;
        TArray<FColor> Pixels;
        int32 Width = 0;
        int32 Height = 0;
        int32 Threshold = 0;
        double SubmitTimeSeconds = 0.0;
    };

    void StartWorker();
    void StopWorker();
    UMyActorComponent* ResolveModelHost();

    FCriticalSection QueueMutex;
    TSharedPtr<FQueuedFrame> LatestFrame;

    FCriticalSection HostsMutex;
    TArray<TWeakObjectPtr<UMyActorComponent>> ModelHosts;

    TFuture<void> WorkerFuture;
    std::atomic<bool> bWorkerRunning{false};
};
