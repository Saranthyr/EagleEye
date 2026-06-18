#include "AI/CrowVisionSubsystem.h"

#include "AI/DetectionModelHostActor.h"
#include "Async/Async.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "HAL/PlatformProcess.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/ScopeLock.h"
#include "MyActorComponent.h"

namespace
{
    constexpr float MaxSharedVisionQueuedFrameAgeSeconds = 0.4f;
}

void UCrowVisionSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
    Super::OnWorldBeginPlay(InWorld);
    bIsShuttingDown.store(false);
    ++DeliveryGeneration;
    if (!WorldBeginTearDownHandle.IsValid())
    {
        WorldBeginTearDownHandle = FWorldDelegates::OnWorldBeginTearDown.AddUObject(this, &UCrowVisionSubsystem::HandleWorldBeginTearDown);
    }
    if (!WorldCleanupHandle.IsValid())
    {
        WorldCleanupHandle = FWorldDelegates::OnWorldCleanup.AddUObject(this, &UCrowVisionSubsystem::HandleWorldCleanup);
    }
    EnsureModelHostActor();
}

void UCrowVisionSubsystem::Deinitialize()
{
    StopWorker(true);
    if (WorldBeginTearDownHandle.IsValid())
    {
        FWorldDelegates::OnWorldBeginTearDown.Remove(WorldBeginTearDownHandle);
        WorldBeginTearDownHandle.Reset();
    }
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
    MaxQueuedModelFrames = FMath::Max(MaxActiveModelUsers, MaxQueuedFrames);
    while (PendingFrames.Num() > MaxQueuedModelFrames)
    {
        PendingFrames.RemoveAt(0);
    }
    SortPendingFramesByRequesterDistanceLocked();
}

void UCrowVisionSubsystem::ConfigureFrameSource(
    float InCaptureFPS,
    int32 InCaptureWidth,
    int32 InCaptureHeight,
    float InMaxDistanceToPlayer,
    bool bInStaggerInitialCapture,
    float InMaxInitialCaptureDelay)
{
    FrameSourceCaptureFPS = FMath::Clamp(InCaptureFPS, 1.f, 120.f);
    FrameSourceCaptureWidth = FMath::Max(160, InCaptureWidth);
    FrameSourceCaptureHeight = FMath::Max(160, InCaptureHeight);
    FrameSourceMaxDistanceToPlayer = FMath::Max(0.f, InMaxDistanceToPlayer);
    bStaggerInitialFrameSourceCapture = bInStaggerInitialCapture;
    MaxInitialFrameSourceCaptureDelay = FMath::Clamp(InMaxInitialCaptureDelay, 0.f, 5.f);
}

void UCrowVisionSubsystem::SubmitFrame(
    UMyActorComponent* Requester,
    TArray<FColor>&& Pixels,
    int32 Width,
    int32 Height,
    float CapturedWorldTimeSeconds,
    const FVector2D& DetectionTargetPixel,
    bool bHasDetectionEvaluation,
    bool bExpectedInFov)
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
    Frame->CapturedWorldTimeSeconds = CapturedWorldTimeSeconds;
    Frame->RequesterName = GetNameSafe(Requester->GetOwner());
    Frame->bRequesterLogFrameTimings = Requester->ShouldLogFrameTimings();
    Frame->DetectionTargetPixel = DetectionTargetPixel;
    Frame->bHasDetectionEvaluation = bHasDetectionEvaluation;
    Frame->bExpectedInFov = bExpectedInFov;

    {
        FScopeLock Lock(&QueueMutex);
        const UWorld* RequestWorld = Requester->GetWorld();
        const float WorldTimeSeconds = RequestWorld ? RequestWorld->GetTimeSeconds() : -FLT_MAX;
        const float MaxQueuedFrameAgeSeconds = FMath::Max(
            MaxSharedVisionQueuedFrameAgeSeconds,
            2.0f / FMath::Clamp(FrameSourceCaptureFPS, 1.f, 120.f));
        if (CapturedWorldTimeSeconds > -FLT_MAX * 0.5f &&
            WorldTimeSeconds > -FLT_MAX * 0.5f &&
            (WorldTimeSeconds - CapturedWorldTimeSeconds) > MaxQueuedFrameAgeSeconds)
        {
            return;
        }

        PendingFrames.RemoveAll([WorldTimeSeconds, MaxQueuedFrameAgeSeconds](const TSharedPtr<FQueuedFrame>& PendingFrame)
        {
            const bool bIsStale = PendingFrame.IsValid() &&
                PendingFrame->CapturedWorldTimeSeconds > -FLT_MAX * 0.5f &&
                WorldTimeSeconds > -FLT_MAX * 0.5f &&
                (WorldTimeSeconds - PendingFrame->CapturedWorldTimeSeconds) > MaxQueuedFrameAgeSeconds;
            return bIsStale ||
                !PendingFrame.IsValid() ||
                !PendingFrame->Requester.IsValid() ||
                !PendingFrame->ModelHost.IsValid();
        });

        if (InFlightFrame.IsValid() && InFlightFrame->Requester.Get() == Requester)
        {
            return;
        }

        PendingFrames.RemoveAll([Requester](const TSharedPtr<FQueuedFrame>& PendingFrame)
        {
            return PendingFrame.IsValid() && PendingFrame->Requester.Get() == Requester;
        });

        if (CountActiveModelUsersLocked() >= MaxActiveModelUsers ||
            PendingFrames.Num() >= MaxQueuedModelFrames)
        {
            const int32 FarthestPendingFrameIndex = FindFarthestPendingFrameIndexLocked();
            if (FarthestPendingFrameIndex == INDEX_NONE)
            {
                return;
            }

            const TSharedPtr<FQueuedFrame>& FarthestPendingFrame = PendingFrames[FarthestPendingFrameIndex];
            const float RequesterDistanceSq = GetRequesterDistanceToPlayerSq(Requester);
            const float FarthestDistanceSq = GetRequesterDistanceToPlayerSq(FarthestPendingFrame.IsValid()
                ? FarthestPendingFrame->Requester.Get()
                : nullptr);
            if (!FMath::IsFinite(RequesterDistanceSq) ||
                (FMath::IsFinite(FarthestDistanceSq) && RequesterDistanceSq >= FarthestDistanceSq))
            {
                return;
            }

            PendingFrames.RemoveAt(FarthestPendingFrameIndex);
        }

        Frame->RequestSerial = Requester->BeginSharedVisionRequest();
        if (Frame->RequestSerial <= 0)
        {
            return;
        }

        PendingFrames.Add(Frame);
        SortPendingFramesByRequesterDistanceLocked();
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

    if (WorkerThread)
    {
        if (WorkerThread->joinable())
        {
            WorkerThread->join();
        }
        delete WorkerThread;
        WorkerThread = nullptr;
    }

    bWorkerRunning.store(true);
    UCrowVisionSubsystem* Subsystem = this;

    WorkerThread = new std::thread([Subsystem]()
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
            const float MaxQueuedFrameAgeSeconds = FMath::Max(
                MaxSharedVisionQueuedFrameAgeSeconds,
                2.0f / FMath::Clamp(Subsystem->FrameSourceCaptureFPS, 1.f, 120.f));
            if (QueueWaitMs > MaxQueuedFrameAgeSeconds * 1000.0)
            {
                if (Frame->bRequesterLogFrameTimings)
                {
                    UE_LOG(LogTemp, Log, TEXT("SharedVisionDrop: requester=%s queue=%.2fms stale queued frame"),
                        *Frame->RequesterName,
                        QueueWaitMs);
                }

                FScopeLock Lock(&Subsystem->QueueMutex);
                if (Subsystem->InFlightFrame == Frame)
                {
                    Subsystem->InFlightFrame.Reset();
                }
                continue;
            }

            TArray<FDetectionResult> Detections = Host->ProcessSharedVisionFrame(
                Frame->Pixels,
                Frame->Width,
                Frame->Height,
                &InferenceMs);
            const double WorkerTotalMs = (FPlatformTime::Seconds() - WorkerStartSeconds) * 1000.0;
            Host->RecordDetectionModelFrameTiming(
                Frame->RequestSerial,
                Frame->Width,
                Frame->Height,
                Detections,
                WorkerTotalMs,
                InferenceMs,
                Frame->DetectionTargetPixel,
                Frame->bHasDetectionEvaluation,
                Frame->bExpectedInFov);

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
            const float CapturedWorldTimeSeconds = Frame->CapturedWorldTimeSeconds;
            const double SubmitTimeSeconds = Frame->SubmitTimeSeconds;
            const FString RequesterName = Frame->RequesterName;
            const bool bRequesterLogFrameTimings = Frame->bRequesterLogFrameTimings;
            const int32 RequestSerial = Frame->RequestSerial;
            const int32 Generation = Subsystem->DeliveryGeneration.load();

            AsyncTask(ENamedThreads::GameThread, [WeakSubsystem, WeakRequester, Detections = MoveTemp(Detections), Width, Height, CapturedWorldTimeSeconds, SubmitTimeSeconds, RequesterName, bRequesterLogFrameTimings, RequestSerial, Generation]() mutable
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
                Requester->ConsumeSharedVisionResult(MoveTemp(Detections), Width, Height, CapturedWorldTimeSeconds, RequestSerial);
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
        RequestHostInferenceShutdown();
    }
    {
        FScopeLock Lock(&QueueMutex);
        PendingFrames.Reset();
        InFlightFrame.Reset();
    }

    if (WorkerThread)
    {
        if (WorkerThread->joinable())
        {
            if (WorkerThread->get_id() == std::this_thread::get_id())
            {
                WorkerThread->detach();
            }
            else
            {
                WorkerThread->join();
            }
        }
        delete WorkerThread;
        WorkerThread = nullptr;
    }
}

void UCrowVisionSubsystem::RequestHostInferenceShutdown()
{
    TArray<TWeakObjectPtr<UMyActorComponent>> HostsSnapshot;
    {
        FScopeLock Lock(&HostsMutex);
        HostsSnapshot = ModelHosts;
    }
    {
        FScopeLock Lock(&QueueMutex);
        if (InFlightFrame.IsValid())
        {
            HostsSnapshot.Add(InFlightFrame->ModelHost);
        }
        for (const TSharedPtr<FQueuedFrame>& PendingFrame : PendingFrames)
        {
            if (PendingFrame.IsValid())
            {
                HostsSnapshot.Add(PendingFrame->ModelHost);
            }
        }
    }

    for (const TWeakObjectPtr<UMyActorComponent>& HostPtr : HostsSnapshot)
    {
        if (UMyActorComponent* Host = HostPtr.Get())
        {
            Host->RequestInferenceShutdown();
        }
    }
}

void UCrowVisionSubsystem::HandleWorldBeginTearDown(UWorld* World)
{
    if (World == GetWorld())
    {
        StopWorker(true);
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

float UCrowVisionSubsystem::GetRequesterDistanceToPlayerSq(const UMyActorComponent* Requester) const
{
    const AActor* RequesterOwner = Requester ? Requester->GetOwner() : nullptr;
    UWorld* World = Requester ? Requester->GetWorld() : GetWorld();
    const APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
    const APawn* PlayerPawn = PlayerController ? PlayerController->GetPawn() : nullptr;
    if (!RequesterOwner || !PlayerPawn)
    {
        return FLT_MAX;
    }

    return FVector::DistSquared(RequesterOwner->GetActorLocation(), PlayerPawn->GetActorLocation());
}

int32 UCrowVisionSubsystem::FindFarthestPendingFrameIndexLocked() const
{
    int32 FarthestIndex = INDEX_NONE;
    float FarthestDistanceSq = -1.f;

    for (int32 Index = 0; Index < PendingFrames.Num(); ++Index)
    {
        const TSharedPtr<FQueuedFrame>& Frame = PendingFrames[Index];
        const UMyActorComponent* Requester = Frame.IsValid() ? Frame->Requester.Get() : nullptr;
        const float DistanceSq = GetRequesterDistanceToPlayerSq(Requester);
        if (FarthestIndex == INDEX_NONE || DistanceSq > FarthestDistanceSq)
        {
            FarthestIndex = Index;
            FarthestDistanceSq = DistanceSq;
        }
    }

    return FarthestIndex;
}

void UCrowVisionSubsystem::SortPendingFramesByRequesterDistanceLocked()
{
    PendingFrames.Sort([this](const TSharedPtr<FQueuedFrame>& A, const TSharedPtr<FQueuedFrame>& B)
    {
        const UMyActorComponent* RequesterA = A.IsValid() ? A->Requester.Get() : nullptr;
        const UMyActorComponent* RequesterB = B.IsValid() ? B->Requester.Get() : nullptr;
        const float DistanceASq = GetRequesterDistanceToPlayerSq(RequesterA);
        const float DistanceBSq = GetRequesterDistanceToPlayerSq(RequesterB);
        if (!FMath::IsNearlyEqual(DistanceASq, DistanceBSq))
        {
            return DistanceASq < DistanceBSq;
        }

        const double SubmitA = A.IsValid() ? A->SubmitTimeSeconds : TNumericLimits<double>::Max();
        const double SubmitB = B.IsValid() ? B->SubmitTimeSeconds : TNumericLimits<double>::Max();
        return SubmitA < SubmitB;
    });
}
