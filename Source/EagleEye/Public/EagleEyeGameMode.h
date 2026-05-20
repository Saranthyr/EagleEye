// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "EagleEyeGameMode.generated.h"

class ADetectionModelHostActor;

UCLASS(minimalapi)
class AEagleEyeGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AEagleEyeGameMode();

protected:
	virtual void BeginPlay() override;

	UPROPERTY(EditDefaultsOnly, Category="Detection|Shared Model")
	TSubclassOf<ADetectionModelHostActor> DetectionModelHostClass;

private:
	UPROPERTY()
	TObjectPtr<ADetectionModelHostActor> DetectionModelHost;
};



