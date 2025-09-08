// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "MyActorComponent.h"
#include "MyHUD.generated.h"

/**
 * 
 */
UCLASS()
class EAGLEEYE_API AMyHUD : public AHUD
{
	GENERATED_BODY()
public:
	virtual void DrawHUD() override;	
	
	UPROPERTY()
    class UMyActorComponent* ActorComponentRef;
};
