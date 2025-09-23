// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "KismetProceduralMeshLibrary.h"
#include "ProceduralMeshComponent.h"
#include "Math/UnrealMathUtility.h"
#include "WorldGen.generated.h"

UCLASS() 
class EAGLEEYE_API AWorldGen : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	AWorldGen();

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int xvertcnt = 25;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int yvertcnt = 25;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float cellsize = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int numsecx = 5;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int numsecy = 5;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float height = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int meshsecidx = 0;

	UPROPERTY(BlueprintReadOnly)
	UProceduralMeshComponent* TerrainMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	UMaterialInterface* TerrMat = nullptr;

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	UFUNCTION(BlueprintCallable)
	void GenTerrain(const int secidxx, const int secidxy);

	float GetHeight(const FVector2D loc);
	float PerlinNoise(const FVector2D loc, const float scale, const FVector2D offset);
};
