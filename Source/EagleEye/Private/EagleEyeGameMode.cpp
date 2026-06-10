// Copyright Epic Games, Inc. All Rights Reserved.

#include "EagleEyeGameMode.h"
#include "AI/DetectionModelHostActor.h"
#include "EagleEyeCharacter.h"
#include "MyHUD.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "NavigationSystem.h"

AEagleEyeGameMode::AEagleEyeGameMode()
{
	DefaultPawnClass = AEagleEyeCharacter::StaticClass();

	HUDClass = AMyHUD::StaticClass();
	DetectionModelHostClass = ADetectionModelHostActor::StaticClass();
}

void AEagleEyeGameMode::BeginPlay()
{
	Super::BeginPlay();

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	BuildNavigationOnBeginPlay();

	if (!DetectionModelHostClass || DetectionModelHost)
	{
		return;
	}

	for (TActorIterator<ADetectionModelHostActor> It(World); It; ++It)
	{
		ADetectionModelHostActor* ExistingHost = *It;
		if (IsValid(ExistingHost) && ExistingHost->IsA(DetectionModelHostClass))
		{
			DetectionModelHost = ExistingHost;
			return;
		}
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	DetectionModelHost = World->SpawnActor<ADetectionModelHostActor>(
		DetectionModelHostClass,
		FVector::ZeroVector,
		FRotator::ZeroRotator,
		SpawnParams);
}

void AEagleEyeGameMode::BuildNavigationOnBeginPlay() const
{
	if (!bBuildNavigationOnBeginPlay)
	{
		return;
	}

	UWorld* World = GetWorld();
	UNavigationSystemV1* NavSystem = World ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
	if (!NavSystem)
	{
		UE_LOG(LogTemp, Warning, TEXT("EagleEyeGameMode: Cannot build navmesh on BeginPlay because navigation system is unavailable."));
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("EagleEyeGameMode: Building navmesh on BeginPlay."));
	NavSystem->Build();
}
