// Copyright Epic Games, Inc. All Rights Reserved.

#include "EagleEyeGameMode.h"
#include "AI/DetectionModelHostActor.h"
#include "EagleEyeCharacter.h"
#include "MyHUD.h"
#include "Engine/World.h"
#include "UObject/ConstructorHelpers.h"

AEagleEyeGameMode::AEagleEyeGameMode()
{
	// set default pawn class to our Blueprinted character
	static ConstructorHelpers::FClassFinder<APawn> PlayerPawnBPClass(TEXT("/Game/ThirdPerson/Blueprints/BP_ThirdPersonCharacter"));
	if (PlayerPawnBPClass.Class != NULL)
	{
		DefaultPawnClass = PlayerPawnBPClass.Class;
	}

	HUDClass = AMyHUD::StaticClass();
	DetectionModelHostClass = ADetectionModelHostActor::StaticClass();
}

void AEagleEyeGameMode::BeginPlay()
{
	Super::BeginPlay();

	if (!DetectionModelHostClass || DetectionModelHost)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
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
