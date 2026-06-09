#pragma once

#include "CoreMinimal.h"
#include "DetectionResult.h"
#include "Subsystems/WorldSubsystem.h"
#include <atomic>
#include "CrowVisionSubsystem.generated.h"

class UMyActorComponent;
class ADetectionModelHostActor;

UCLASS()
class EAGLEEYE_API UCrowVisionSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void OnWorldBeginPlay(UWorld& InWorld) override;
    virtual void Deinitialize() override;

    void RegisterModelHost(UMyActorComponent* Component);
    void UnregisterModelHost(UMyActorComponent* Component);
    void ConfigureModelHostLimits(int32 MaxActiveUsers, int32 MaxQueuedFrames);

    void SubmitFrame(
        UMyActorComponent* Requester,
        TArray<FColor>&& Pixels,
        int32 Width,
        int32 Height);

    bool IsShuttingDown() const { return bIsShuttingDown.load(); }

private:
    struct FQueuedFrame
    {
        TWeakObjectPtr<UMyActorComponent> Requester;
        TWeakObjectPtr<UCrowVisionSubsystem> SubsystemForDelivery;
        TWeakObjectPtr<UMyActorComponent> ModelHost;
        TArray<FColor> Pixels;
        int32 Width = 0;
        int32 Height = 0;
        double SubmitTimeSeconds = 0.0;
        FString RequesterName;
        bool bRequesterLogFrameTimings = false;
        int32 RequestSerial = 0;
    };

    void StartWorker();
    void StopWorker(bool bShutdownSubsystem);
    void EnsureModelHostActor();
    UMyActorComponent* ResolveModelHost();
    void HandleWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources);
    bool HasActiveRequestForRequesterLocked(const UMyActorComponent* Requester) const;
    int32 CountActiveModelUsersLocked() const;

    FCriticalSection QueueMutex;
    TArray<TSharedPtr<FQueuedFrame>> PendingFrames;
    TSharedPtr<FQueuedFrame> InFlightFrame;
    int32 MaxActiveModelUsers = 2;
    int32 MaxQueuedModelFrames = 2;

    FCriticalSection HostsMutex;
    TArray<TWeakObjectPtr<UMyActorComponent>> ModelHosts;

    UPROPERTY()
    TObjectPtr<ADetectionModelHostActor> ModelHostActor;

    FDelegateHandle WorldCleanupHandle;
    TFuture<void> WorkerFuture;
    std::atomic<bool> bWorkerRunning{false};
    std::atomic<bool> bIsShuttingDown{false};
    std::atomic<int32> DeliveryGeneration{0};
};
