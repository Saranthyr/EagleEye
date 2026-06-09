#include "AI/CrowVisionSubsystem.h"

#include "AI/DetectionModelHostActor.h"
#include "Async/Async.h"
#include "Engine/World.h"
#include "HAL/PlatformProcess.h"
#include "Misc/ScopeLock.h"
#include "MyActorComponent.h"

void UCrowVisionSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
    Super::OnWorldBeginPlay(InWorld);
    bIsShuttingDown.store(false);
    ++DeliveryGeneration;
    if (!WorldCleanupHandle.IsValid())
    {
        WorldCleanupHandle = FWorldDelegates::OnWorldCleanup.AddUObject(this, &UCrowVisionSubsystem::HandleWorldCleanup);
    }
    EnsureModelHostActor();
}

void UCrowVisionSubsystem::Deinitialize()
{
    StopWorker(true);
    if (WorldCleanupHandle.IsValid())
    {
        FWorldDelegates::OnWorldCleanup.Remove(WorldCleanupHandle);
        WorldCleanupHandle.Reset();
    }
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
    bool bRemovedModelHost = false;
    {
        FScopeLock Lock(&HostsMutex);
        const int32 RemovedCount = ModelHosts.RemoveAll([Component](const TWeakObjectPtr<UMyActorComponent>& Entry)
        {
            return !Entry.IsValid() || Entry.Get() == Component;
        });
        bRemovedModelHost = RemovedCount > 0;
    }

    {
        FScopeLock Lock(&QueueMutex);
        PendingFrames.RemoveAll([Component, bRemovedModelHost](const TSharedPtr<FQueuedFrame>& Frame)
        {
            return !Frame.IsValid() ||
                Frame->Requester.Get() == Component ||
                Frame->ModelHost.Get() == Component ||
                bRemovedModelHost;
        });
        if (InFlightFrame.IsValid() &&
            (InFlightFrame->Requester.Get() == Component || InFlightFrame->ModelHost.Get() == Component || bRemovedModelHost))
        {
            InFlightFrame.Reset();
        }
    }

    if (bRemovedModelHost)
    {
        StopWorker(false);
    }
}

void UCrowVisionSubsystem::ConfigureModelHostLimits(int32 MaxActiveUsers, int32 MaxQueuedFrames)
{
    FScopeLock Lock(&QueueMutex);
    MaxActiveModelUsers = FMath::Max(1, MaxActiveUsers);
    MaxQueuedModelFrames = FMath::Max(1, MaxQueuedFrames);
    while (PendingFrames.Num() > MaxQueuedModelFrames)
    {
        PendingFrames.RemoveAt(0);
    }
}

void UCrowVisionSubsystem::SubmitFrame(
    UMyActorComponent* Requester,
    TArray<FColor>&& Pixels,
    int32 Width,
    int32 Height)
{
    if (bIsShuttingDown.load() || !IsValid(Requester) || Width <= 0 || Height <= 0 || Pixels.Num() != Width * Height)
    {
        return;
    }

    EnsureModelHostActor();

    UMyActorComponent* Host = ResolveModelHost();
    if (!IsValid(Host) || Host == Requester)
    {
        return;
    }

    TSharedPtr<FQueuedFrame> Frame = MakeShared<FQueuedFrame>();
    Frame->Requester = Requester;
    Frame->SubsystemForDelivery = this;
    Frame->ModelHost = Host;
    Frame->Pixels = MoveTemp(Pixels);
    Frame->Width = Width;
    Frame->Height = Height;
    Frame->SubmitTimeSeconds = FPlatformTime::Seconds();
    Frame->RequesterName = GetNameSafe(Requester->GetOwner());
    Frame->bRequesterLogFrameTimings = Requester->ShouldLogFrameTimings();

    {
        FScopeLock Lock(&QueueMutex);
        PendingFrames.RemoveAll([](const TSharedPtr<FQueuedFrame>& PendingFrame)
        {
            return !PendingFrame.IsValid() ||
                !PendingFrame->Requester.IsValid() ||
                !PendingFrame->ModelHost.IsValid();
        });

        if (HasActiveRequestForRequesterLocked(Requester) ||
            CountActiveModelUsersLocked() >= MaxActiveModelUsers ||
            PendingFrames.Num() >= MaxQueuedModelFrames)
        {
            return;
        }

        Frame->RequestSerial = Requester->BeginSharedVisionRequest();
        if (Frame->RequestSerial <= 0)
        {
            return;
        }

        PendingFrames.Add(Frame);
    }

    StartWorker();
}

void UCrowVisionSubsystem::StartWorker()
{
    if (bWorkerRunning.load())
    {
        return;
    }
    if (bIsShuttingDown.load())
    {
        return;
    }

    bWorkerRunning.store(true);
    UCrowVisionSubsystem* Subsystem = this;

    WorkerFuture = Async(EAsyncExecution::Thread, [Subsystem]()
    {
        while (Subsystem->bWorkerRunning.load() && !Subsystem->bIsShuttingDown.load())
        {
            TSharedPtr<FQueuedFrame> Frame;
            {
                FScopeLock Lock(&Subsystem->QueueMutex);
                if (Subsystem->PendingFrames.Num() > 0)
                {
                    Frame = Subsystem->PendingFrames[0];
                    Subsystem->PendingFrames.RemoveAt(0);
                    Subsystem->InFlightFrame = Frame;
                }
            }

            if (!Frame.IsValid())
            {
                FPlatformProcess::Sleep(0.001f);
                continue;
            }

            UMyActorComponent* Host = Frame->ModelHost.Get();
            if (Subsystem->bIsShuttingDown.load() || !IsValid(Host))
            {
                FScopeLock Lock(&Subsystem->QueueMutex);
                if (Subsystem->InFlightFrame == Frame)
                {
                    Subsystem->InFlightFrame.Reset();
                }
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

            if (Frame->bRequesterLogFrameTimings)
            {
                UE_LOG(LogTemp, Log, TEXT("SharedVisionFrame: requester=%s queue=%.2fms worker=%.2fms infer=%.2fms detections=%d size=%dx%d"),
                    *Frame->RequesterName,
                    QueueWaitMs,
                    WorkerTotalMs,
                    InferenceMs,
                    Detections.Num(),
                    Frame->Width,
                    Frame->Height);
            }

            TWeakObjectPtr<UMyActorComponent> WeakRequester = Frame->Requester;
            TWeakObjectPtr<UCrowVisionSubsystem> WeakSubsystem = Frame->SubsystemForDelivery;
            const int32 Width = Frame->Width;
            const int32 Height = Frame->Height;
            const double SubmitTimeSeconds = Frame->SubmitTimeSeconds;
            const FString RequesterName = Frame->RequesterName;
            const bool bRequesterLogFrameTimings = Frame->bRequesterLogFrameTimings;
            const int32 RequestSerial = Frame->RequestSerial;
            const int32 Generation = Subsystem->DeliveryGeneration.load();

            AsyncTask(ENamedThreads::GameThread, [WeakSubsystem, WeakRequester, Detections = MoveTemp(Detections), Width, Height, SubmitTimeSeconds, RequesterName, bRequesterLogFrameTimings, RequestSerial, Generation]() mutable
            {
                UCrowVisionSubsystem* DeliverySubsystem = WeakSubsystem.Get();
                if (!DeliverySubsystem || DeliverySubsystem->IsShuttingDown() || Generation != DeliverySubsystem->DeliveryGeneration.load())
                {
                    return;
                }

                UMyActorComponent* Requester = WeakRequester.Get();
                UWorld* World = Requester ? Requester->GetWorld() : nullptr;
                if (!IsValid(Requester) || !World || World->bIsTearingDown)
                {
                    return;
                }

                if (bRequesterLogFrameTimings)
                {
                    UE_LOG(LogTemp, Log, TEXT("SharedVisionDelivery[%s]: end_to_end=%.2fms"),
                        *RequesterName,
                        (FPlatformTime::Seconds() - SubmitTimeSeconds) * 1000.0);
                }
                Requester->ConsumeSharedVisionResult(MoveTemp(Detections), Width, Height, RequestSerial);
            });

            {
                FScopeLock Lock(&Subsystem->QueueMutex);
                if (Subsystem->InFlightFrame == Frame)
                {
                    Subsystem->InFlightFrame.Reset();
                }
            }
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

void UCrowVisionSubsystem::StopWorker(bool bShutdownSubsystem)
{
    bWorkerRunning.store(false);
    ++DeliveryGeneration;
    if (bShutdownSubsystem)
    {
        bIsShuttingDown.store(true);
    }
    {
        FScopeLock Lock(&QueueMutex);
        PendingFrames.Reset();
        InFlightFrame.Reset();
    }

    if (WorkerFuture.IsValid())
    {
        WorkerFuture.Wait();
    }
}

void UCrowVisionSubsystem::HandleWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources)
{
    if (World == GetWorld())
    {
        StopWorker(true);
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

bool UCrowVisionSubsystem::HasActiveRequestForRequesterLocked(const UMyActorComponent* Requester) const
{
    if (!Requester)
    {
        return false;
    }

    if (InFlightFrame.IsValid() && InFlightFrame->Requester.Get() == Requester)
    {
        return true;
    }

    for (const TSharedPtr<FQueuedFrame>& Frame : PendingFrames)
    {
        if (Frame.IsValid() && Frame->Requester.Get() == Requester)
        {
            return true;
        }
    }

    return false;
}

int32 UCrowVisionSubsystem::CountActiveModelUsersLocked() const
{
    TArray<const UMyActorComponent*> ActiveRequesters;
    auto AddRequester = [&ActiveRequesters](const TSharedPtr<FQueuedFrame>& Frame)
    {
        const UMyActorComponent* Requester = Frame.IsValid() ? Frame->Requester.Get() : nullptr;
        if (Requester)
        {
            ActiveRequesters.AddUnique(Requester);
        }
    };

    AddRequester(InFlightFrame);
    for (const TSharedPtr<FQueuedFrame>& Frame : PendingFrames)
    {
        AddRequester(Frame);
    }

    return ActiveRequesters.Num();
}
