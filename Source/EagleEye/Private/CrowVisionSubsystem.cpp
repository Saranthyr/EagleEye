#include "AI/CrowVisionSubsystem.h"

#include "AI/DetectionModelHostActor.h"
#include "Async/Async.h"
#include "Engine/World.h"
#include "HAL/PlatformProcess.h"
#include "Misc/ScopeLock.h"
#include "MyActorComponent.h"

void UCrowVisionSubsystem::Deinitialize()
{
    StopWorker();
    Super::Deinitialize();
}

void UCrowVisionSubsystem::RegisterModelHost(UMyActorComponent* Component)
{
    if (!IsValid(Component))
    {
        return;
    }

    {
        FScopeLock Lock(&HostsMutex);
        ModelHosts.RemoveAll([](const TWeakObjectPtr<UMyActorComponent>& Entry)
        {
            return !Entry.IsValid();
        });

        if (!ModelHosts.Contains(Component))
        {
            ModelHosts.Add(Component);
        }
    }

    StartWorker();
}

void UCrowVisionSubsystem::UnregisterModelHost(UMyActorComponent* Component)
{
    {
        FScopeLock Lock(&HostsMutex);
        ModelHosts.RemoveAll([Component](const TWeakObjectPtr<UMyActorComponent>& Entry)
        {
            return !Entry.IsValid() || Entry.Get() == Component;
        });
    }

    {
        FScopeLock Lock(&QueueMutex);
        if (LatestFrame.IsValid() && LatestFrame->Requester.Get() == Component)
        {
            LatestFrame.Reset();
        }
    }
}

void UCrowVisionSubsystem::SubmitFrame(
    UMyActorComponent* Requester,
    TArray<FColor>&& Pixels,
    int32 Width,
    int32 Height)
{
    if (!IsValid(Requester) || Width <= 0 || Height <= 0 || Pixels.Num() != Width * Height)
    {
        return;
    }

    EnsureModelHostActor();
    StartWorker();

    TSharedPtr<FQueuedFrame> Frame = MakeShared<FQueuedFrame>();
    Frame->Requester = Requester;
    Frame->Pixels = MoveTemp(Pixels);
    Frame->Width = Width;
    Frame->Height = Height;
    Frame->SubmitTimeSeconds = FPlatformTime::Seconds();

    {
        FScopeLock Lock(&QueueMutex);
        LatestFrame = Frame;
    }
}

void UCrowVisionSubsystem::StartWorker()
{
    if (bWorkerRunning.load())
    {
        return;
    }

    bWorkerRunning.store(true);
    TWeakObjectPtr<UCrowVisionSubsystem> WeakThis(this);

    WorkerFuture = Async(EAsyncExecution::Thread, [WeakThis]()
    {
        while (WeakThis.IsValid() && WeakThis->bWorkerRunning.load())
        {
            TSharedPtr<FQueuedFrame> Frame;
            {
                FScopeLock Lock(&WeakThis->QueueMutex);
                Frame = WeakThis->LatestFrame;
                WeakThis->LatestFrame.Reset();
            }

            if (!Frame.IsValid())
            {
                FPlatformProcess::Sleep(0.001f);
                continue;
            }

            UMyActorComponent* Requester = Frame->Requester.Get();
            UMyActorComponent* Host = WeakThis->ResolveModelHost();
            if (!IsValid(Requester) || !IsValid(Host))
            {
                continue;
            }

            double InferenceMs = -1.0;
            const double WorkerStartSeconds = FPlatformTime::Seconds();
            const double QueueWaitMs = (WorkerStartSeconds - Frame->SubmitTimeSeconds) * 1000.0;
            TArray<FDetectionResult> Detections = Host->ProcessSharedVisionFrame(
                Frame->Pixels,
                Frame->Width,
                Frame->Height,
                &InferenceMs);
            const double WorkerTotalMs = (FPlatformTime::Seconds() - WorkerStartSeconds) * 1000.0;

            if (Host->ShouldLogFrameTimings() || Requester->ShouldLogFrameTimings())
            {
                UE_LOG(LogTemp, Log, TEXT("SharedVisionFrame: requester=%s host=%s queue=%.2fms worker=%.2fms infer=%.2fms detections=%d size=%dx%d"),
                    *GetNameSafe(Requester->GetOwner()),
                    *GetNameSafe(Host->GetOwner()),
                    QueueWaitMs,
                    WorkerTotalMs,
                    InferenceMs,
                    Detections.Num(),
                    Frame->Width,
                    Frame->Height);
            }

            TWeakObjectPtr<UMyActorComponent> WeakRequester(Requester);
            const int32 Width = Frame->Width;
            const int32 Height = Frame->Height;
            const double SubmitTimeSeconds = Frame->SubmitTimeSeconds;

            AsyncTask(ENamedThreads::GameThread, [WeakRequester, Detections = MoveTemp(Detections), Width, Height, SubmitTimeSeconds]() mutable
            {
                if (WeakRequester.IsValid())
                {
                    if (WeakRequester->ShouldLogFrameTimings())
                    {
                        UE_LOG(LogTemp, Log, TEXT("SharedVisionDelivery[%s]: end_to_end=%.2fms"),
                            *GetNameSafe(WeakRequester->GetOwner()),
                            (FPlatformTime::Seconds() - SubmitTimeSeconds) * 1000.0);
                    }
                    WeakRequester->ConsumeSharedVisionResult(MoveTemp(Detections), Width, Height);
                }
            });
        }
    });
}

void UCrowVisionSubsystem::EnsureModelHostActor()
{
    if (IsValid(ModelHostActor))
    {
        return;
    }

    UWorld* World = GetWorld();
    if (!World || World->bIsTearingDown)
    {
        return;
    }

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    ModelHostActor = World->SpawnActor<ADetectionModelHostActor>(
        ADetectionModelHostActor::StaticClass(),
        FVector::ZeroVector,
        FRotator::ZeroRotator,
        SpawnParams);

    UE_LOG(LogTemp, Log, TEXT("Shared vision model host spawned by CrowVisionSubsystem: %s"), *GetNameSafe(ModelHostActor));
}

void UCrowVisionSubsystem::StopWorker()
{
    bWorkerRunning.store(false);
    {
        FScopeLock Lock(&QueueMutex);
        LatestFrame.Reset();
    }

    if (WorkerFuture.IsValid())
    {
        WorkerFuture.Wait();
    }
}

UMyActorComponent* UCrowVisionSubsystem::ResolveModelHost()
{
    FScopeLock Lock(&HostsMutex);
    ModelHosts.RemoveAll([](const TWeakObjectPtr<UMyActorComponent>& Entry)
    {
        return !Entry.IsValid();
    });

    return ModelHosts.Num() > 0 ? ModelHosts[0].Get() : nullptr;
}
