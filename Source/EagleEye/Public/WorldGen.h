// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "KismetProceduralMeshLibrary.h"
#include "ProceduralMeshComponent.h"
#include "Math/UnrealMathUtility.h"
#include "TimerManager.h"
#include "WorldGen.generated.h"

class UHierarchicalInstancedStaticMeshComponent;
class ANavMeshBoundsVolume;
class ABotCharacter;
class UStaticMesh;
class UMaterialInterface;

USTRUCT(BlueprintType)
struct FFoliageTypeConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Foliage")
	UStaticMesh* Mesh = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Foliage")
	UMaterialInterface* MaterialOverride = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Foliage")
	float DensityPerSqM = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Foliage")
	float MinScale = 0.8f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Foliage")
	float MaxScale = 1.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Foliage")
	float MaxSlopeDeg = 35.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Foliage")
	float MinHeight = -100000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Foliage")
	float MaxHeight = 100000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Foliage")
	bool bAlignToNormal = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Foliage")
	bool bSnapToGeneratedSurface = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Foliage")
	float SurfaceOffset = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Foliage")
	bool bEnableCollision = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Foliage")
	int32 SeedOffset = 0;
};

USTRUCT(BlueprintType)
struct FBotSpawnTypeConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bot")
	TSubclassOf<ABotCharacter> BotClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bot")
	bool bEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bot", meta=(ClampMin="0"))
	int32 MinBotsPerSection = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bot", meta=(ClampMin="0"))
	int32 MaxBotsPerSection = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bot")
	bool bUseDensityPerSqM = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bot", meta=(ClampMin="0.0", EditCondition="bUseDensityPerSqM"))
	float DensityPerSqM = 0.003f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bot", meta=(ClampMin="0.0"))
	float MinAltitudeAboveTerrain = 800.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bot", meta=(ClampMin="0.0"))
	float MaxAltitudeAboveTerrain = 1600.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bot", meta=(ClampMin="0.0"))
	float SectionEdgePadding = 300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bot")
	int32 SeedOffset = 9173;
};

UCLASS() 
class EAGLEEYE_API AWorldGen : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	AWorldGen();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Terrain")
	int xvertcnt = 25;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Terrain")
	int yvertcnt = 25;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Terrain")
	float cellsize = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="World")
	int numsecx = 5;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="World")
	int numsecy = 5;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="World")
	bool bAllowNegativeSections = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Terrain")
	float height = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Terrain")
	bool bComputeTangents = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Streaming")
	int32 StreamingRadiusSections = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Streaming")
	float StreamingUpdateInterval = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Streaming")
	bool bClampToWorldSize = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Foliage")
	TArray<FFoliageTypeConfig> FoliageTypes;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Foliage")
	bool bEnableFoliage = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bots")
	bool bEnableBotSpawning = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bots")
	TArray<FBotSpawnTypeConfig> BotSpawnTypes;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bots|Debug")
	bool bDebugBotSpawning = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bots|Debug", meta=(ClampMin="0.0"))
	float BotDebugDrawDuration = 15.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bots|Debug", meta=(ClampMin="1.0"))
	float BotDebugSphereRadius = 120.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Navigation")
	bool bEnableNavMesh = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Navigation")
	bool bAutoNavBounds = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Navigation")
	float NavBoundsHeight = 2000.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Terrain")
	UProceduralMeshComponent* TerrainMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Terrain")
	UMaterialInterface* TerrMat = nullptr;

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	UFUNCTION(BlueprintCallable)
	void GenTerrain(const int secidxx, const int secidxy);

	UFUNCTION(BlueprintCallable, Category="WorldGen")
	void ForceStreamingUpdate();

	UFUNCTION(BlueprintCallable, Category="WorldGen")
	void RegenerateAll();

	float GetHeight(const FVector2D loc);
	float PerlinNoise(const FVector2D loc, const float scale, const FVector2D offset);

private:
	struct FSectionMeshData
	{
		TArray<FVector> Verts;
		TArray<int32> Tris;
		TArray<FVector> Normals;
		TArray<FVector2D> UVs;
		TArray<FProcMeshTangent> Tangents;
	};

	struct FSectionData
	{
		int32 MeshSectionIndex = INDEX_NONE;
		FBox Bounds;
		TArray<TArray<int32>> FoliageInstanceIndices;
		TWeakObjectPtr<ANavMeshBoundsVolume> NavBoundsVolume;
		TArray<TWeakObjectPtr<ABotCharacter>> SpawnedBots;
	};

	void UpdateStreaming();
	void UpdateStreamingInternal(bool bForce);
	void CreateSection(const FIntPoint& Section);
	void DestroySection(const FIntPoint& Section);
	void GenerateSectionMeshData(const FIntPoint& Section, FSectionMeshData& Out, FBox& OutBounds);
	bool SampleGeneratedSurfaceAt(const FIntPoint& Section, const FVector2D& SampleLocation, FVector& OutLocation, FVector& OutNormal);
	void InitializeFoliageComponents();
	void SpawnFoliageForSection(const FIntPoint& Section, FSectionData& SectionData);
	void RemoveFoliageForSection(const FIntPoint& Section, FSectionData& SectionData);
	void UpdateFoliageIndexAfterSwapRemoval(int32 TypeIndex, int32 OldIndex, int32 NewIndex, const FIntPoint& RemovedSection);
	void SpawnBotsForSection(const FIntPoint& Section, FSectionData& SectionData);
	void SpawnBotTypeForSection(
		const FIntPoint& Section,
		FSectionData& SectionData,
		const FBotSpawnTypeConfig& Config,
		int32 TypeIndex,
		const FString& DebugName,
		float SectionSizeX,
		float SectionSizeY);
	void DestroyBotsForSection(FSectionData& SectionData);
	ANavMeshBoundsVolume* ResolveNavBoundsTemplate();
	ANavMeshBoundsVolume* CreateSectionNavBounds(const FBox& WorldBounds);
	void DestroySectionNavBounds(FSectionData& SectionData);
	void RequestNavRebuild();
	void PerformNavRebuild();
	void MarkNavDirty(const FBox& Bounds);
	FIntPoint GetSectionFromWorld(const FVector& Location) const;
	bool IsSectionInBounds(const FIntPoint& Section) const;
	int32 AcquireMeshSectionIndex();

	UPROPERTY()
	TArray<UHierarchicalInstancedStaticMeshComponent*> FoliageComponents;

	TMap<FIntPoint, FSectionData> LoadedSections;

	UPROPERTY()
	TArray<int32> FreeMeshSectionIndices;

	UPROPERTY()
	int32 NextMeshSectionIndex = 0;

	UPROPERTY()
	FIntPoint CurrentCenterSection = FIntPoint(0, 0);

	UPROPERTY()
	int32 LastStreamingRadius = -1;

	TWeakObjectPtr<ANavMeshBoundsVolume> NavBoundsTemplateVolume;
	bool bLoggedMissingNavBoundsTemplate = false;
	bool bTerrainNavRegistered = false;

	FTimerHandle StreamingTimerHandle;
	FTimerHandle DeferredNavRebuildHandle;
};
