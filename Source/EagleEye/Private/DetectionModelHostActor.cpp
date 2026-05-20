#include "AI/DetectionModelHostActor.h"

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
