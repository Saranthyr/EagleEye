#pragma once

#include "CoreMinimal.h"
#include "DetectionResult.h"
#include "Subsystems/WorldSubsystem.h"
#include <atomic>
#include <thread>
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
    void ConfigureFrameSource(
        float InCaptureFPS,
        int32 InCaptureWidth,
        int32 InCaptureHeight,
        float InMaxDistanceToPlayer,
        bool bInStaggerInitialCapture,
        float InMaxInitialCaptureDelay);

    void SubmitFrame(
        UMyActorComponent* Requester,
        TArray<FColor>&& Pixels,
        int32 Width,
        int32 Height,
        const FVector2D& DetectionTargetPixel,
        bool bHasDetectionEvaluation,
        bool bExpectedInFov);

    bool IsShuttingDown() const { return bIsShuttingDown.load(); }
    float GetFrameSourceCaptureFPS() const { return FrameSourceCaptureFPS; }
    int32 GetFrameSourceCaptureWidth() const { return FrameSourceCaptureWidth; }
    int32 GetFrameSourceCaptureHeight() const { return FrameSourceCaptureHeight; }
    float GetFrameSourceMaxDistanceToPlayer() const { return FrameSourceMaxDistanceToPlayer; }
    bool ShouldStaggerInitialCapture() const { return bStaggerInitialFrameSourceCapture; }
    float GetMaxInitialCaptureDelay() const { return MaxInitialFrameSourceCaptureDelay; }

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
        FVector2D DetectionTargetPixel = FVector2D::ZeroVector;
        bool bHasDetectionEvaluation = false;
        bool bExpectedInFov = false;
        int32 RequestSerial = 0;
    };

    void StartWorker();
    void StopWorker(bool bShutdownSubsystem);
    void EnsureModelHostActor();
    UMyActorComponent* ResolveModelHost();
    void RequestHostInferenceShutdown();
    void HandleWorldBeginTearDown(UWorld* World);
    void HandleWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources);
    bool HasActiveRequestForRequesterLocked(const UMyActorComponent* Requester) const;
    int32 CountActiveModelUsersLocked() const;
    float GetRequesterDistanceToPlayerSq(const UMyActorComponent* Requester) const;
    int32 FindFarthestPendingFrameIndexLocked() const;
    void SortPendingFramesByRequesterDistanceLocked();

    FCriticalSection QueueMutex;
    TArray<TSharedPtr<FQueuedFrame>> PendingFrames;
    TSharedPtr<FQueuedFrame> InFlightFrame;
    int32 MaxActiveModelUsers = 2;
    int32 MaxQueuedModelFrames = 2;
    float FrameSourceCaptureFPS = 8.f;
    int32 FrameSourceCaptureWidth = 640;
    int32 FrameSourceCaptureHeight = 640;
    float FrameSourceMaxDistanceToPlayer = 8000.f;
    bool bStaggerInitialFrameSourceCapture = true;
    float MaxInitialFrameSourceCaptureDelay = 0.75f;

    FCriticalSection HostsMutex;
    TArray<TWeakObjectPtr<UMyActorComponent>> ModelHosts;

    UPROPERTY()
    TObjectPtr<ADetectionModelHostActor> ModelHostActor;

    FDelegateHandle WorldBeginTearDownHandle;
    FDelegateHandle WorldCleanupHandle;
    std::thread* WorkerThread = nullptr;
    std::atomic<bool> bWorkerRunning{false};
    std::atomic<bool> bIsShuttingDown{false};
    std::atomic<int32> DeliveryGeneration{0};
};
