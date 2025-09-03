// Copyright Epic Games, Inc. All Rights Reserved.

#include "EagleEyeGameMode.h"
#include "EagleEyeCharacter.h"
#include "UObject/ConstructorHelpers.h"

AEagleEyeGameMode::AEagleEyeGameMode()
{
	// set default pawn class to our Blueprinted character
	static ConstructorHelpers::FClassFinder<APawn> PlayerPawnBPClass(TEXT("/Game/ThirdPerson/Blueprints/BP_ThirdPersonCharacter"));
	if (PlayerPawnBPClass.Class != NULL)
	{
		DefaultPawnClass = PlayerPawnBPClass.Class;
	}
}
