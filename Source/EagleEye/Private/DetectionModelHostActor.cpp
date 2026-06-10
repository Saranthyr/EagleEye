#include "AI/DetectionModelHostActor.h"

#include "AI/CrowDetectionShareSubsystem.h"
#include "AI/CrowVisionSubsystem.h"
#include "Components/SceneComponent.h"
#include "EagleEyeDetectionSettings.h"
#include "MyActorComponent.h"

ADetectionModelHostActor::ADetectionModelHostActor()
{
    PrimaryActorTick.bCanEverTick = false;
    SetActorHiddenInGame(true);

    SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
    RootComponent = SceneRoot;
}

void ADetectionModelHostActor::PostInitializeComponents()
{
    Super::PostInitializeComponents();

    if (!ModelHostComponent)
    {
        ModelHostComponent = NewObject<UMyActorComponent>(this, TEXT("SharedVisionModelHost"));
        if (ModelHostComponent)
        {
            ModelHostComponent->SetupAttachment(SceneRoot);
            ModelHostComponent->SetSharedVisionModelHost(true);
            AddInstanceComponent(ModelHostComponent);
            ModelHostComponent->RegisterComponent();
        }
    }
}

void ADetectionModelHostActor::BeginPlay()
{
    Super::BeginPlay();

    if (const UEagleEyeDetectionSettings* Settings = GetDefault<UEagleEyeDetectionSettings>())
    {
        bPreloadModelOnBeginPlay = Settings->bPreloadModelHostOnBeginPlay;
        MaxActiveModelUsers = FMath::Max(1, Settings->MaxActiveModelUsers);
        MaxQueuedModelFrames = FMath::Clamp(Settings->MaxQueuedModelFrames, 1, 120);
        FrameSourceFPS = FMath::Clamp(Settings->FrameSourceFPS, 1.f, 120.f);
        FrameSourceWidth = FMath::Clamp(Settings->FrameSourceWidth, 160, 3840);
        FrameSourceHeight = FMath::Clamp(Settings->FrameSourceHeight, 160, 2160);
        MaxModelUserDistanceToPlayer = FMath::Max(0.f, Settings->MaxModelUserDistanceToPlayer);
        bStaggerInitialFrameSourceCapture = Settings->bStaggerInitialFrameSourceCapture;
        MaxInitialFrameSourceCaptureDelay = FMath::Clamp(Settings->MaxInitialFrameSourceCaptureDelay, 0.f, 5.f);
    }

    if (UWorld* World = GetWorld())
    {
        if (UCrowVisionSubsystem* VisionSubsystem = World->GetSubsystem<UCrowVisionSubsystem>())
        {
            VisionSubsystem->ConfigureModelHostLimits(MaxActiveModelUsers, MaxQueuedModelFrames);
            VisionSubsystem->ConfigureFrameSource(
                FrameSourceFPS,
                FrameSourceWidth,
                FrameSourceHeight,
                MaxModelUserDistanceToPlayer,
                bStaggerInitialFrameSourceCapture,
                MaxInitialFrameSourceCaptureDelay);
            UE_LOG(LogTemp, Log, TEXT("Shared vision model host limits: activeUsers=%d queuedFrames=%d fps=%.1f size=%dx%d maxDistance=%.1f"),
                FMath::Max(1, MaxActiveModelUsers),
                FMath::Max(FMath::Max(1, MaxActiveModelUsers), MaxQueuedModelFrames),
                FMath::Clamp(FrameSourceFPS, 1.f, 120.f),
                FMath::Max(160, FrameSourceWidth),
                FMath::Max(160, FrameSourceHeight),
                FMath::Max(0.f, MaxModelUserDistanceToPlayer));
            if (ModelHostComponent)
            {
                VisionSubsystem->RegisterModelHost(ModelHostComponent);
            }
        }

        if (UCrowDetectionShareSubsystem* ShareSubsystem = World->GetSubsystem<UCrowDetectionShareSubsystem>())
        {
            ShareSubsystem->ConfigureDetectorLimits(MaxActiveModelUsers, MaxModelUserDistanceToPlayer);
        }
    }

    if (bPreloadModelOnBeginPlay && ModelHostComponent)
    {
        ModelHostComponent->EnsureModelLoaded();
    }
}
