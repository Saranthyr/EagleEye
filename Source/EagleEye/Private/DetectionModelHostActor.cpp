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

    if (UWorld* World = GetWorld())
    {
        if (UCrowVisionSubsystem* VisionSubsystem = World->GetSubsystem<UCrowVisionSubsystem>())
        {
            VisionSubsystem->ConfigureModelHostLimits(MaxActiveModelUsers, MaxQueuedModelFrames);
            if (ModelHostComponent)
            {
                VisionSubsystem->RegisterModelHost(ModelHostComponent);
            }
        }
    }

    if (bPreloadModelOnBeginPlay && ModelHostComponent)
    {
        ModelHostComponent->EnsureModelLoaded();
    }
}
