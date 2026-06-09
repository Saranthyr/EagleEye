#include "AI/DetectionModelHostActor.h"

#include "AI/CrowVisionSubsystem.h"
#include "Components/SceneComponent.h"
#include "MyActorComponent.h"

ADetectionModelHostActor::ADetectionModelHostActor()
{
    PrimaryActorTick.bCanEverTick = false;
    SetActorHiddenInGame(true);

    SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
    RootComponent = SceneRoot;

    ModelHostComponent = CreateDefaultSubobject<UMyActorComponent>(TEXT("SharedVisionModelHost"));
    ModelHostComponent->SetupAttachment(SceneRoot);
    ModelHostComponent->SetSharedVisionModelHost(true);
}

void ADetectionModelHostActor::BeginPlay()
{
    Super::BeginPlay();

    if (UWorld* World = GetWorld())
    {
        if (UCrowVisionSubsystem* VisionSubsystem = World->GetSubsystem<UCrowVisionSubsystem>())
        {
            VisionSubsystem->ConfigureModelHostLimits(MaxActiveModelUsers, MaxQueuedModelFrames);
        }
    }

    if (bPreloadModelOnBeginPlay && ModelHostComponent)
    {
        ModelHostComponent->EnsureModelLoaded();
    }
}
