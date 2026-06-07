// Fill out your copyright notice in the Description page of Project Settings.

#include "WorldGen.h"

#include "AI/BotCharacter.h"
#include "Algo/Unique.h"
#include "Components/BoxComponent.h"
#include "Components/BrushComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/PrimitiveComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerStart.h"
#include "Kismet/GameplayStatics.h"
#include "NavigationSystem.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "Math/RotationMatrix.h"
#include "PhysicsEngine/BodySetup.h"

namespace
{
	constexpr float kMinStreamingInterval = 0.05f;

	bool StaticMeshHasCollision(const UStaticMesh& Mesh)
	{
		const UBodySetup* BodySetup = Mesh.GetBodySetup();
		return BodySetup &&
			(BodySetup->AggGeom.GetElementCount() > 0 ||
				BodySetup->CollisionTraceFlag == CTF_UseComplexAsSimple);
	}
}

// Sets default values
AWorldGen::AWorldGen()
{
	PrimaryActorTick.bCanEverTick = false;

	TerrainMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("TerrainMesh"));
	RootComponent = TerrainMesh;

	// Nav generation depends on collision/nav data being ready during streaming updates.
	TerrainMesh->bUseAsyncCooking = false;
	TerrainMesh->bUseComplexAsSimpleCollision = true;
	TerrainMesh->SetMobility(EComponentMobility::Static);
	TerrainMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	TerrainMesh->SetCollisionObjectType(ECC_WorldStatic);
	TerrainMesh->SetCollisionResponseToAllChannels(ECR_Block);
	// Defer nav registration until at least one section exists to avoid empty-bounds registration.
	TerrainMesh->SetCanEverAffectNavigation(false);
}

// Called when the game starts or when spawned
void AWorldGen::BeginPlay()
{
	Super::BeginPlay();

	UE_LOG(
		LogTemp,
		Log,
		TEXT("WorldGen: Bot spawning %s. Types=%d"),
		bEnableBotSpawning ? TEXT("enabled") : TEXT("disabled"),
		BotSpawnTypes.Num()
	);

	InitializeFoliageComponents();

	const float Interval = FMath::Max(StreamingUpdateInterval, kMinStreamingInterval);
	GetWorld()->GetTimerManager().SetTimer(StreamingTimerHandle, this, &AWorldGen::UpdateStreaming, Interval, true, 0.0f);

	ForceStreamingUpdate();
}

// Called every frame
void AWorldGen::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

void AWorldGen::GenTerrain(const int secidxx, const int secidxy)
{
	const int32 PreviousLoadedCount = LoadedSections.Num();
	CreateSection(FIntPoint(secidxx, secidxy));

	if (bEnableNavMesh && LoadedSections.Num() != PreviousLoadedCount)
	{
		RequestNavRebuild();
	}
}

void AWorldGen::ForceStreamingUpdate()
{
	UpdateStreamingInternal(true);
}

void AWorldGen::RegenerateAll()
{
	if (TerrainMesh)
	{
		for (TPair<FIntPoint, FSectionData>& Pair : LoadedSections)
		{
			if (Pair.Value.MeshSectionIndex != INDEX_NONE)
			{
				TerrainMesh->ClearMeshSection(Pair.Value.MeshSectionIndex);
			}

			if (bEnableNavMesh)
			{
				DestroySectionNavBounds(Pair.Value);
			}

			DestroyBotsForSection(Pair.Value);
		}
	}

	LoadedSections.Empty();
	FreeMeshSectionIndices.Empty();
	PendingSectionsToCreate.Empty();
	PendingSectionsToDestroy.Empty();
	NextMeshSectionIndex = 0;
	bTerrainNavRegistered = false;

	InitializeFoliageComponents();

	ForceStreamingUpdate();
}

void AWorldGen::UpdateStreaming()
{
	UpdateStreamingInternal(false);
}

void AWorldGen::UpdateStreamingInternal(bool bForce)
{
	if (xvertcnt < 2 || yvertcnt < 2)
	{
		return;
	}

	FVector CenterLocation = GetActorLocation();
	if (AActor* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0))
	{
		CenterLocation = PlayerPawn->GetActorLocation();
	}
	else if (APlayerController* PlayerController = UGameplayStatics::GetPlayerController(this, 0))
	{
		if (PlayerController->GetPawn())
		{
			CenterLocation = PlayerController->GetPawn()->GetActorLocation();
		}
	}

	const FIntPoint NewCenter = GetSectionFromWorld(CenterLocation);
	const int32 Radius = FMath::Max(StreamingRadiusSections, 0);
	if (!bForce && NewCenter == CurrentCenterSection && LastStreamingRadius == Radius)
	{
		if (ProcessPendingStreamingOps())
		{
			RequestNavRebuild();
		}
		return;
	}

	CurrentCenterSection = NewCenter;
	LastStreamingRadius = Radius;
	PendingSectionsToCreate.Reset();
	PendingSectionsToDestroy.Reset();

	TSet<FIntPoint> DesiredSections;
	DesiredSections.Reserve((2 * Radius + 1) * (2 * Radius + 1));

	for (int32 Dy = -Radius; Dy <= Radius; ++Dy)
	{
		for (int32 Dx = -Radius; Dx <= Radius; ++Dx)
		{
			FIntPoint Section(NewCenter.X + Dx, NewCenter.Y + Dy);
			if (!IsSectionInBounds(Section))
			{
				continue;
			}
			DesiredSections.Add(Section);
		}
	}

	PendingSectionsToDestroy.Reserve(LoadedSections.Num());
	for (const TPair<FIntPoint, FSectionData>& Pair : LoadedSections)
	{
		if (!DesiredSections.Contains(Pair.Key))
		{
			PendingSectionsToDestroy.Add(Pair.Key);
		}
	}

	for (const FIntPoint& Section : DesiredSections)
	{
		if (!LoadedSections.Contains(Section))
		{
			PendingSectionsToCreate.Add(Section);
		}
	}

	PendingSectionsToCreate.Sort([NewCenter](const FIntPoint& A, const FIntPoint& B)
	{
		const int32 DistA = FMath::Abs(A.X - NewCenter.X) + FMath::Abs(A.Y - NewCenter.Y);
		const int32 DistB = FMath::Abs(B.X - NewCenter.X) + FMath::Abs(B.Y - NewCenter.Y);
		return DistA < DistB;
	});

	if (ProcessPendingStreamingOps())
	{
		RequestNavRebuild();
	}
}

bool AWorldGen::ProcessPendingStreamingOps()
{
	int32 RemainingOps = FMath::Max(MaxStreamingSectionOpsPerUpdate, 1);
	bool bSectionsChanged = false;

	while (RemainingOps > 0 && PendingSectionsToDestroy.Num() > 0)
	{
		const FIntPoint Section = PendingSectionsToDestroy.Pop(EAllowShrinking::No);
		const int32 PreviousLoadedCount = LoadedSections.Num();
		DestroySection(Section);
		bSectionsChanged |= (LoadedSections.Num() != PreviousLoadedCount);
		--RemainingOps;
	}

	while (RemainingOps > 0 && PendingSectionsToCreate.Num() > 0)
	{
		const FIntPoint Section = PendingSectionsToCreate[0];
		PendingSectionsToCreate.RemoveAt(0, 1, EAllowShrinking::No);
		if (!IsSectionInBounds(Section) || LoadedSections.Contains(Section))
		{
			continue;
		}

		const int32 PreviousLoadedCount = LoadedSections.Num();
		CreateSection(Section);
		bSectionsChanged |= (LoadedSections.Num() != PreviousLoadedCount);
		--RemainingOps;
	}

	return bSectionsChanged;
}

void AWorldGen::CreateSection(const FIntPoint& Section)
{
	if (!IsSectionInBounds(Section) || LoadedSections.Contains(Section))
	{
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("WorldGen: Creating section (%d, %d)"), Section.X, Section.Y);

	FSectionMeshData MeshData;
	FBox Bounds(ForceInit);
	GenerateSectionMeshData(Section, MeshData, Bounds);
	if (MeshData.Verts.Num() == 0 || MeshData.Tris.Num() == 0)
	{
		return;
	}

	const int32 MeshSectionIndex = AcquireMeshSectionIndex();
	TerrainMesh->CreateMeshSection(
		MeshSectionIndex,
		MeshData.Verts,
		MeshData.Tris,
		MeshData.Normals,
		MeshData.UVs,
		TArray<FColor>(),
		MeshData.Tangents,
		true
	);
	TerrainMesh->UpdateBounds();
	TerrainMesh->RecreatePhysicsState();
	const FVector TerrainExtent = TerrainMesh->Bounds.GetBox().GetExtent();
	if (TerrainExtent.X <= KINDA_SMALL_NUMBER || TerrainExtent.Y <= KINDA_SMALL_NUMBER || TerrainExtent.Z <= KINDA_SMALL_NUMBER)
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("WorldGen: TerrainMesh bounds still near zero after section creation. Extent=(%0.2f, %0.2f, %0.2f)"),
			TerrainExtent.X,
			TerrainExtent.Y,
			TerrainExtent.Z
		);
	}
	if (bEnableNavMesh && !bTerrainNavRegistered)
	{
		TerrainMesh->SetCanEverAffectNavigation(true);
		bTerrainNavRegistered = true;
	}

	if (TerrMat)
	{
		TerrainMesh->SetMaterial(MeshSectionIndex, TerrMat);
	}

	const FTransform TerrainTransform = TerrainMesh ? TerrainMesh->GetComponentTransform() : GetActorTransform();
	const FBox WorldBounds = Bounds.TransformBy(TerrainTransform);

	FSectionData SectionData;
	SectionData.MeshSectionIndex = MeshSectionIndex;
	SectionData.Bounds = WorldBounds;
	SectionData.FoliageInstanceIndices.SetNum(FoliageTypes.Num());

	if (bEnableBuildings)
	{
		SpawnBuildingsForSection(Section, SectionData);
	}

	if (bEnableFoliage)
	{
		SpawnFoliageForSection(Section, SectionData);
	}

	if (bEnableNavMesh)
	{
		if (bAutoNavBounds)
		{
			SectionData.NavBoundsVolume = CreateSectionNavBounds(SectionData.Bounds);
		}
	}

	if (bEnableBotSpawning)
	{
		SpawnBotsForSection(Section, SectionData);
	}

	FSectionData& LoadedSectionData = LoadedSections.Add(Section, MoveTemp(SectionData));

	if (bEnableNavMesh)
	{
		UpdateGeneratedNavigationData();
		MarkNavDirty(LoadedSectionData.Bounds);
	}
}

void AWorldGen::DestroySection(const FIntPoint& Section)
{
	FSectionData* SectionData = LoadedSections.Find(Section);
	if (!SectionData)
	{
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("WorldGen: Destroying section (%d, %d)"), Section.X, Section.Y);

	DestroyBotsForSection(*SectionData);

	RemoveFoliageForSection(Section, *SectionData);

	RemoveBuildingsForSection(Section, *SectionData);

	if (TerrainMesh && SectionData->MeshSectionIndex != INDEX_NONE)
	{
		TerrainMesh->ClearMeshSection(SectionData->MeshSectionIndex);
		FreeMeshSectionIndices.Add(SectionData->MeshSectionIndex);
	}

	if (bEnableNavMesh)
	{
		DestroySectionNavBounds(*SectionData);
		MarkNavDirty(SectionData->Bounds);
	}

	LoadedSections.Remove(Section);
}

void AWorldGen::GenerateSectionMeshData(const FIntPoint& Section, FSectionMeshData& Out, FBox& OutBounds)
{
	const int32 VertCountX = xvertcnt;
	const int32 VertCountY = yvertcnt;
	const int32 ExtendedCountX = VertCountX + 2;
	const int32 ExtendedCountY = VertCountY + 2;

	const float SectionSizeX = (VertCountX - 1) * cellsize;
	const float SectionSizeY = (VertCountY - 1) * cellsize;
	const float BaseX = Section.X * SectionSizeX;
	const float BaseY = Section.Y * SectionSizeY;

	TArray<float> HeightMap;
	HeightMap.SetNumUninitialized(ExtendedCountX * ExtendedCountY);

	for (int32 Ey = 0; Ey < ExtendedCountY; ++Ey)
	{
		for (int32 Ex = 0; Ex < ExtendedCountX; ++Ex)
		{
			const float WorldX = BaseX + (Ex - 1) * cellsize;
			const float WorldY = BaseY + (Ey - 1) * cellsize;
			HeightMap[Ex + Ey * ExtendedCountX] = GetHeight(FVector2D(WorldX, WorldY));
		}
	}

	const int32 NumVerts = VertCountX * VertCountY;
	const int32 NumTris = (VertCountX - 1) * (VertCountY - 1) * 2;

	Out.Verts.SetNumUninitialized(NumVerts);
	Out.UVs.SetNumUninitialized(NumVerts);
	Out.Normals.SetNumUninitialized(NumVerts);
	Out.Tangents.SetNumUninitialized(NumVerts);
	Out.Tris.SetNumUninitialized(NumTris * 3);

	OutBounds = FBox(ForceInit);

	for (int32 Vy = 0; Vy < VertCountY; ++Vy)
	{
		for (int32 Vx = 0; Vx < VertCountX; ++Vx)
		{
			const int32 VertIndex = Vx + Vy * VertCountX;
			const int32 HeightIndex = (Vx + 1) + (Vy + 1) * ExtendedCountX;

			const float WorldX = BaseX + Vx * cellsize;
			const float WorldY = BaseY + Vy * cellsize;
			const float WorldZ = HeightMap[HeightIndex];

			const FVector Vertex(WorldX, WorldY, WorldZ);
			Out.Verts[VertIndex] = Vertex;
			OutBounds += Vertex;

			const FVector2D UV((WorldX) / 100.0f, (WorldY) / 100.0f);
			Out.UVs[VertIndex] = UV;

			const float DzDx = HeightMap[(Vx + 2) + (Vy + 1) * ExtendedCountX] - HeightMap[(Vx) + (Vy + 1) * ExtendedCountX];
			const float DzDy = HeightMap[(Vx + 1) + (Vy + 2) * ExtendedCountX] - HeightMap[(Vx + 1) + (Vy) * ExtendedCountX];
			FVector Normal(-DzDx, -DzDy, 2.0f * cellsize);
			Normal.Normalize();
			Out.Normals[VertIndex] = Normal;

			Out.Tangents[VertIndex] = FProcMeshTangent(1.0f, 0.0f, 0.0f);
		}
	}

	int32 TriIndex = 0;
	for (int32 Ty = 0; Ty < VertCountY - 1; ++Ty)
	{
		for (int32 Tx = 0; Tx < VertCountX - 1; ++Tx)
		{
			const int32 I0 = Tx + Ty * VertCountX;
			const int32 I1 = Tx + (Ty + 1) * VertCountX;
			const int32 I2 = Tx + 1 + Ty * VertCountX;
			const int32 I3 = Tx + 1 + (Ty + 1) * VertCountX;

			Out.Tris[TriIndex++] = I0;
			Out.Tris[TriIndex++] = I1;
			Out.Tris[TriIndex++] = I2;

			Out.Tris[TriIndex++] = I1;
			Out.Tris[TriIndex++] = I3;
			Out.Tris[TriIndex++] = I2;
		}
	}

	if (bComputeTangents)
	{
		UKismetProceduralMeshLibrary::CalculateTangentsForMesh(Out.Verts, Out.Tris, Out.UVs, Out.Normals, Out.Tangents);
	}
}

bool AWorldGen::SampleGeneratedSurfaceAt(const FIntPoint& Section, const FVector2D& SampleLocation, FVector& OutLocation, FVector& OutNormal)
{
	if (xvertcnt < 2 || yvertcnt < 2 || cellsize <= KINDA_SMALL_NUMBER)
	{
		return false;
	}

	const float SectionSizeX = (xvertcnt - 1) * cellsize;
	const float SectionSizeY = (yvertcnt - 1) * cellsize;
	if (SectionSizeX <= KINDA_SMALL_NUMBER || SectionSizeY <= KINDA_SMALL_NUMBER)
	{
		return false;
	}

	const float BaseX = Section.X * SectionSizeX;
	const float BaseY = Section.Y * SectionSizeY;
	const float LocalX = FMath::Clamp(SampleLocation.X - BaseX, 0.0f, SectionSizeX - KINDA_SMALL_NUMBER);
	const float LocalY = FMath::Clamp(SampleLocation.Y - BaseY, 0.0f, SectionSizeY - KINDA_SMALL_NUMBER);

	const int32 CellX = FMath::Clamp(FMath::FloorToInt(LocalX / cellsize), 0, xvertcnt - 2);
	const int32 CellY = FMath::Clamp(FMath::FloorToInt(LocalY / cellsize), 0, yvertcnt - 2);

	const float CellBaseX = BaseX + CellX * cellsize;
	const float CellBaseY = BaseY + CellY * cellsize;
	const float Fx = FMath::Clamp((SampleLocation.X - CellBaseX) / cellsize, 0.0f, 1.0f);
	const float Fy = FMath::Clamp((SampleLocation.Y - CellBaseY) / cellsize, 0.0f, 1.0f);

	const FVector P00(CellBaseX, CellBaseY, GetHeight(FVector2D(CellBaseX, CellBaseY)));
	const FVector P10(CellBaseX + cellsize, CellBaseY, GetHeight(FVector2D(CellBaseX + cellsize, CellBaseY)));
	const FVector P01(CellBaseX, CellBaseY + cellsize, GetHeight(FVector2D(CellBaseX, CellBaseY + cellsize)));
	const FVector P11(CellBaseX + cellsize, CellBaseY + cellsize, GetHeight(FVector2D(CellBaseX + cellsize, CellBaseY + cellsize)));

	FVector A;
	FVector B;
	FVector C;
	float SurfaceZ = 0.0f;
	if (Fx + Fy <= 1.0f)
	{
		// Matches mesh triangle (I0, I1, I2).
		A = P00;
		B = P01;
		C = P10;
		SurfaceZ = P00.Z + Fx * (P10.Z - P00.Z) + Fy * (P01.Z - P00.Z);
	}
	else
	{
		// Matches mesh triangle (I1, I3, I2).
		A = P01;
		B = P11;
		C = P10;
		const float OneMinusFx = 1.0f - Fx;
		const float OneMinusFy = 1.0f - Fy;
		SurfaceZ = P11.Z + OneMinusFx * (P01.Z - P11.Z) + OneMinusFy * (P10.Z - P11.Z);
	}

	FVector SurfaceNormal = FVector::CrossProduct(B - A, C - A).GetSafeNormal();
	if (SurfaceNormal.IsNearlyZero())
	{
		SurfaceNormal = FVector::UpVector;
	}
	else if (SurfaceNormal.Z < 0.0f)
	{
		SurfaceNormal *= -1.0f;
	}

	OutLocation = FVector(SampleLocation.X, SampleLocation.Y, SurfaceZ);
	OutNormal = SurfaceNormal;
	return true;
}

bool AWorldGen::BuildSurfacePlacementAt(
	const FIntPoint& Section,
	const FVector2D& SampleLocation,
	const FTransform& TerrainTransform,
	float SurfaceOffset,
	FSurfacePlacement& OutPlacement)
{
	FVector LocalLocation;
	FVector LocalNormal;
	if (!SampleGeneratedSurfaceAt(Section, SampleLocation, LocalLocation, LocalNormal))
	{
		return false;
	}

	if (LocalNormal.IsNearlyZero())
	{
		LocalNormal = FVector::UpVector;
	}

	OutPlacement.LocalNormal = LocalNormal.GetSafeNormal();
	OutPlacement.LocalLocation = LocalLocation + (OutPlacement.LocalNormal * SurfaceOffset);
	OutPlacement.WorldLocation = TerrainTransform.TransformPosition(OutPlacement.LocalLocation);
	OutPlacement.WorldNormal = TerrainTransform.TransformVectorNoScale(OutPlacement.LocalNormal).GetSafeNormal();
	if (OutPlacement.WorldNormal.IsNearlyZero())
	{
		OutPlacement.WorldNormal = FVector::UpVector;
	}

	const float DotUp = FMath::Clamp(FMath::Abs(FVector::DotProduct(OutPlacement.WorldNormal, FVector::UpVector)), 0.0f, 1.0f);
	OutPlacement.SlopeDeg = FMath::RadiansToDegrees(FMath::Acos(DotUp));
	return true;
}

void AWorldGen::SpawnBuildingsForSection(const FIntPoint& Section, FSectionData& SectionData)
{
	if (!bEnableBuildings || BuildingTypes.Num() == 0)
	{
		UE_LOG(LogTemp, Verbose, TEXT("WorldGen: Skipping buildings for section (%d, %d) (disabled or no types)"), Section.X, Section.Y);
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const float SectionSizeX = (xvertcnt - 1) * cellsize;
	const float SectionSizeY = (yvertcnt - 1) * cellsize;
	const float AreaSqM = (SectionSizeX * SectionSizeY) / 10000.0f;
	if (SectionSizeX <= KINDA_SMALL_NUMBER || SectionSizeY <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	const float BaseX = Section.X * SectionSizeX;
	const float BaseY = Section.Y * SectionSizeY;
	const FTransform TerrainTransform = TerrainMesh ? TerrainMesh->GetComponentTransform() : GetActorTransform();
	TArray<FVector> PlayerStartLocations;
	GatherPlayerStartLocations(PlayerStartLocations);

	for (int32 TypeIndex = 0; TypeIndex < BuildingTypes.Num(); ++TypeIndex)
	{
		const FBuildingTypeConfig& Config = BuildingTypes[TypeIndex];
		if (!Config.bEnabled || !Config.BuildingClass)
		{
			UE_LOG(LogTemp, Verbose, TEXT("WorldGen: Building type %d skipped (disabled/class missing)"), TypeIndex);
			continue;
		}

		const int32 MaxCount = FMath::Max(Config.MaxBuildingsPerSection, 0);
		const int32 DensityCount = FMath::RoundToInt(FMath::Max(Config.DensityPerSqM, 0.0f) * AreaSqM);
		const uint32 SectionSeed = GetTypeHash(Section);
		const uint32 TypeSeed = HashCombine(SectionSeed, GetTypeHash(TypeIndex));
		const uint32 GlobalSeed = HashCombine(TypeSeed, GetTypeHash(BuildingSeedOffset));
		const uint32 Seed = HashCombine(GlobalSeed, GetTypeHash(Config.SeedOffset));
		FRandomStream Rand(static_cast<int32>(Seed));
		const int32 NumBuildings = Config.bUseDensityPerSqM
			? FMath::Clamp(DensityCount, 0, MaxCount)
			: Rand.RandRange(FMath::Clamp(Config.MinBuildingsPerSection, 0, MaxCount), MaxCount);
		if (NumBuildings <= 0)
		{
			continue;
		}

		TArray<FGeneratedBuilding> PendingBuildings;
		PendingBuildings.Reserve(NumBuildings);

		const float MinAllowedHeight = FMath::Min(Config.MinHeight, Config.MaxHeight);
		const float MaxAllowedHeight = FMath::Max(Config.MinHeight, Config.MaxHeight);
		constexpr int32 MaxPlacementAttemptsPerInstance = 16;
		FActorSpawnParameters SpawnParams;
		SpawnParams.Owner = this;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		SpawnParams.ObjectFlags |= RF_Transient;

		int32 AddedCount = 0;
		for (int32 BuildingIdx = 0; BuildingIdx < NumBuildings; ++BuildingIdx)
		{
			for (int32 Attempt = 0; Attempt < MaxPlacementAttemptsPerInstance; ++Attempt)
			{
				const float ScaleValue = Rand.FRandRange(
					FMath::Min(Config.MinScale, Config.MaxScale),
					FMath::Max(Config.MinScale, Config.MaxScale));
				const float PlacementRadius = Config.PlacementFootprintRadius * ScaleValue;
				const float PaddingX = FMath::Clamp(Config.SectionEdgePadding + PlacementRadius, 0.0f, SectionSizeX * 0.49f);
				const float PaddingY = FMath::Clamp(Config.SectionEdgePadding + PlacementRadius, 0.0f, SectionSizeY * 0.49f);
				if (SectionSizeX <= PaddingX * 2.0f || SectionSizeY <= PaddingY * 2.0f)
				{
					break;
				}

				const FVector2D SampleLocation(
					BaseX + Rand.FRandRange(PaddingX, SectionSizeX - PaddingX),
					BaseY + Rand.FRandRange(PaddingY, SectionSizeY - PaddingY));

				FSurfacePlacement Placement;
				if (!BuildSurfacePlacementAt(Section, SampleLocation, TerrainTransform, Config.SurfaceOffset, Placement))
				{
					continue;
				}

				if (Placement.SlopeDeg > Config.MaxSlopeDeg ||
					Placement.LocalLocation.Z < MinAllowedHeight ||
					Placement.LocalLocation.Z > MaxAllowedHeight)
				{
					continue;
				}

				if (IsNearAnyPlayerStart(PlayerStartLocations, Placement.WorldLocation, PlayerStartObjectAvoidanceRadius + PlacementRadius))
				{
					continue;
				}

				FRotator Rotation = Config.bAlignToNormal
					? FRotationMatrix::MakeFromZX(Placement.WorldNormal, FVector::ForwardVector).Rotator()
					: FRotator::ZeroRotator;
				Rotation.Yaw += Rand.FRandRange(0.0f, 360.0f);

				const FTransform BuildingTransform(Rotation, Placement.WorldLocation, FVector(ScaleValue));
				AActor* SpawnedBuilding = World->SpawnActor<AActor>(Config.BuildingClass, BuildingTransform, SpawnParams);
				if (!SpawnedBuilding)
				{
					continue;
				}

				SpawnedBuilding->Tags.AddUnique(FName(*FString::Printf(TEXT("BuildingSection_%d_%d"), Section.X, Section.Y)));
				SpawnedBuilding->Tags.AddUnique(FName(*FString::Printf(TEXT("BuildingType_%d"), TypeIndex)));
				if (Config.bAffectNavigation)
				{
					TArray<UPrimitiveComponent*> PrimitiveComponents;
					SpawnedBuilding->GetComponents<UPrimitiveComponent>(PrimitiveComponents);
					for (UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
					{
						if (PrimitiveComponent)
						{
							PrimitiveComponent->SetCanEverAffectNavigation(true);
							PrimitiveComponent->UpdateBounds();
							FNavigationSystem::OnComponentRegistered(*PrimitiveComponent);
							FNavigationSystem::UpdateComponentData(*PrimitiveComponent);
						}
					}
				}

				FBox WorldBounds = SpawnedBuilding->GetComponentsBoundingBox(true);
				if (!WorldBounds.IsValid ||
					WorldBounds.GetExtent().X <= KINDA_SMALL_NUMBER ||
					WorldBounds.GetExtent().Y <= KINDA_SMALL_NUMBER)
				{
					const float FallbackRadius = FMath::Max(PlacementRadius, 50.0f);
					const float FallbackHeight = FMath::Max(Config.FallbackBoundsHeight * ScaleValue, 50.0f);
					WorldBounds = FBox(
						Placement.WorldLocation - FVector(FallbackRadius, FallbackRadius, 0.0f),
						Placement.WorldLocation + FVector(FallbackRadius, FallbackRadius, FallbackHeight));
				}

				const FBox2D Footprint(
					FVector2D(WorldBounds.Min.X, WorldBounds.Min.Y),
					FVector2D(WorldBounds.Max.X, WorldBounds.Max.Y));
				const float ActualPlacementRadius = FVector2D(WorldBounds.GetExtent().X, WorldBounds.GetExtent().Y).Size();
				if (IsNearAnyPlayerStart(PlayerStartLocations, Placement.WorldLocation, PlayerStartObjectAvoidanceRadius + ActualPlacementRadius) ||
					IsFootprintNearAnyPlayerStart(PlayerStartLocations, Footprint, PlayerStartObjectAvoidanceRadius) ||
					IsFootprintBlockedByBuildings(Footprint, SectionData, Config.MinDistanceBetweenBuildings))
				{
					SpawnedBuilding->Destroy();
					continue;
				}

				FSectionData PendingSectionData;
				PendingSectionData.Buildings = PendingBuildings;
				if (IsFootprintBlockedByBuildings(Footprint, PendingSectionData, Config.MinDistanceBetweenBuildings))
				{
					SpawnedBuilding->Destroy();
					continue;
				}

				FGeneratedBuilding Building;
				Building.Footprint = Footprint;
				Building.WorldBounds = WorldBounds;
				Building.FloorZ = Building.WorldBounds.Min.Z;
				Building.TopZ = Building.WorldBounds.Max.Z;
				Building.BotInteriorPadding = FMath::Max(Config.BotInteriorPadding, 0.0f);
				Building.FoliageAvoidancePadding = FMath::Max(Config.FoliageAvoidancePadding, 0.0f);
				Building.Actor = SpawnedBuilding;

				PendingBuildings.Add(Building);
				++AddedCount;
				break;
			}
		}

		if (PendingBuildings.Num() > 0)
		{
			for (FGeneratedBuilding& Building : PendingBuildings)
			{
				SectionData.Bounds += Building.WorldBounds;
				SectionData.Buildings.Add(MoveTemp(Building));
			}
		}

		UE_LOG(LogTemp, Log, TEXT("WorldGen: Building type %d added %d/%d instances in section (%d, %d)"), TypeIndex, AddedCount, NumBuildings, Section.X, Section.Y);
	}
}

void AWorldGen::RemoveBuildingsForSection(const FIntPoint& Section, FSectionData& SectionData)
{
	for (FGeneratedBuilding& Building : SectionData.Buildings)
	{
		if (AActor* BuildingActor = Building.Actor.Get())
		{
			BuildingActor->Destroy();
		}
	}

	SectionData.Buildings.Reset();
}

bool AWorldGen::IsFootprintBlockedByBuildings(const FBox2D& Footprint, const FSectionData& SectionData, float Padding) const
{
	if (!Footprint.bIsValid)
	{
		return true;
	}

	const FVector2D Expansion(FMath::Max(Padding, 0.0f));
	const FBox2D ExpandedFootprint(Footprint.Min - Expansion, Footprint.Max + Expansion);
	for (const FGeneratedBuilding& Building : SectionData.Buildings)
	{
		if (Building.Footprint.bIsValid && ExpandedFootprint.Intersect(Building.Footprint))
		{
			return true;
		}
	}

	return false;
}

bool AWorldGen::IsFoliageLocationBlockedByBuildings(const FVector& WorldLocation, float Radius, const FSectionData& SectionData) const
{
	const FVector2D Location2D(WorldLocation.X, WorldLocation.Y);
	for (const FGeneratedBuilding& Building : SectionData.Buildings)
	{
		if (!Building.Footprint.bIsValid)
		{
			continue;
		}

		const FVector2D BuildingExpansion(FMath::Max(Radius + Building.FoliageAvoidancePadding, 0.0f));
		const FBox2D ExpandedFootprint(Building.Footprint.Min - BuildingExpansion, Building.Footprint.Max + BuildingExpansion);
		if (ExpandedFootprint.IsInside(Location2D))
		{
			return true;
		}
	}

	return false;
}

bool AWorldGen::TryBuildBotSpawnInsideBuilding(
	const FSectionData& SectionData,
	const FBotSpawnTypeConfig& Config,
	const ABotCharacter* BotDefaults,
	FRandomStream& Rand,
	FVector& OutSpawnLocation) const
{
	if (!bAllowBotSpawnsInsideBuildings ||
		SectionData.Buildings.Num() == 0 ||
		Rand.FRand() > FMath::Clamp(BuildingInteriorBotSpawnChance, 0.0f, 1.0f))
	{
		return false;
	}

	const bool bSpawnAsWalkingBot = BotDefaults && BotDefaults->IsWalkingBot();
	if (!bSpawnAsWalkingBot)
	{
		return false;
	}

	const float CapsuleHalfHeight = BotDefaults && BotDefaults->GetCapsuleComponent()
		? BotDefaults->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()
		: 88.0f;
	const float CapsuleRadius = BotDefaults && BotDefaults->GetCapsuleComponent()
		? BotDefaults->GetCapsuleComponent()->GetScaledCapsuleRadius()
		: 34.0f;
	const float Clearance = FMath::Max(Config.GroundSpawnClearance, 0.0f);

	for (int32 Attempt = 0; Attempt < 8; ++Attempt)
	{
		const FGeneratedBuilding& Building = SectionData.Buildings[Rand.RandRange(0, SectionData.Buildings.Num() - 1)];
		if (!Building.Footprint.bIsValid)
		{
			continue;
		}

		const float InteriorPadding = Building.BotInteriorPadding + CapsuleRadius + FMath::Max(Config.SpawnCollisionExtraClearance, 0.0f);
		const float MinX = Building.Footprint.Min.X + InteriorPadding;
		const float MaxX = Building.Footprint.Max.X - InteriorPadding;
		const float MinY = Building.Footprint.Min.Y + InteriorPadding;
		const float MaxY = Building.Footprint.Max.Y - InteriorPadding;
		if (MaxX <= MinX || MaxY <= MinY)
		{
			continue;
		}

		OutSpawnLocation = FVector(
			Rand.FRandRange(MinX, MaxX),
			Rand.FRandRange(MinY, MaxY),
			Building.FloorZ + CapsuleHalfHeight + Clearance);
		return true;
	}

	return false;
}

void AWorldGen::InitializeFoliageComponents()
{
	for (UHierarchicalInstancedStaticMeshComponent* Component : FoliageComponents)
	{
		if (Component)
		{
			Component->DestroyComponent();
		}
	}
	FoliageComponents.Empty();

	if (!bEnableFoliage)
	{
		UE_LOG(LogTemp, Log, TEXT("WorldGen: Foliage disabled (bEnableFoliage=false)"));
		return;
	}

	if (FoliageTypes.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("WorldGen: No foliage types configured."));
		return;
	}

	for (const FFoliageTypeConfig& Config : FoliageTypes)
	{
		if (!Config.Mesh)
		{
			UE_LOG(LogTemp, Warning, TEXT("WorldGen: Foliage type missing mesh; skipping component"));
			FoliageComponents.Add(nullptr);
			continue;
		}

		UHierarchicalInstancedStaticMeshComponent* Component = NewObject<UHierarchicalInstancedStaticMeshComponent>(this);
		Component->SetupAttachment(RootComponent);
		Component->SetStaticMesh(Config.Mesh);
		if (Config.MaterialOverride)
		{
			const int32 NumMaterials = FMath::Max(1, Component->GetNumMaterials());
			for (int32 MaterialIndex = 0; MaterialIndex < NumMaterials; ++MaterialIndex)
			{
				Component->SetMaterial(MaterialIndex, Config.MaterialOverride);
			}
		}
		Component->SetMobility(EComponentMobility::Movable);
		if (Config.bEnableCollision)
		{
			if (Config.bUseComplexCollisionAsSimple)
			{
				Config.Mesh->CreateBodySetup();
				if (UBodySetup* BodySetup = Config.Mesh->GetBodySetup())
				{
					BodySetup->CollisionTraceFlag = CTF_UseComplexAsSimple;
					BodySetup->CreatePhysicsMeshes();
				}
			}

			Component->SetCollisionProfileName(TEXT("BlockAll"));
			Component->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			Component->SetCollisionObjectType(ECC_WorldStatic);
			Component->SetCollisionResponseToAllChannels(ECR_Block);
			Component->SetGenerateOverlapEvents(false);

			if (!StaticMeshHasCollision(*Config.Mesh))
			{
				UE_LOG(
					LogTemp,
					Warning,
					TEXT("WorldGen: Foliage mesh %s has no collision data. Enable bUseComplexCollisionAsSimple or add collision to the mesh."),
					*Config.Mesh->GetName()
				);
			}
		}
		else
		{
			Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Component->SetCollisionResponseToAllChannels(ECR_Ignore);
		}
		Component->SetCanEverAffectNavigation(Config.bEnableCollision);
		AddInstanceComponent(Component);
		Component->RegisterComponent();

		FoliageComponents.Add(Component);
		UE_LOG(LogTemp, Log, TEXT("WorldGen: Foliage component created for mesh %s"), *Config.Mesh->GetName());
	}
}

void AWorldGen::SpawnFoliageForSection(const FIntPoint& Section, FSectionData& SectionData)
{
	if (!bEnableFoliage || FoliageComponents.Num() == 0)
	{
		UE_LOG(LogTemp, Verbose, TEXT("WorldGen: Skipping foliage for section (%d, %d) (disabled or no components)"), Section.X, Section.Y);
		return;
	}

	const float SectionSizeX = (xvertcnt - 1) * cellsize;
	const float SectionSizeY = (yvertcnt - 1) * cellsize;
	const float AreaSqM = (SectionSizeX * SectionSizeY) / 10000.0f;
	const float BaseX = Section.X * SectionSizeX;
	const float BaseY = Section.Y * SectionSizeY;
	const FTransform TerrainTransform = TerrainMesh ? TerrainMesh->GetComponentTransform() : GetActorTransform();
	TArray<FVector> PlayerStartLocations;
	GatherPlayerStartLocations(PlayerStartLocations);

	for (int32 TypeIndex = 0; TypeIndex < FoliageTypes.Num(); ++TypeIndex)
	{
		const FFoliageTypeConfig& Config = FoliageTypes[TypeIndex];
		UHierarchicalInstancedStaticMeshComponent* Component = FoliageComponents.IsValidIndex(TypeIndex) ? FoliageComponents[TypeIndex] : nullptr;
		if (!Component || !Config.Mesh || Config.DensityPerSqM <= 0.0f)
		{
			UE_LOG(LogTemp, Verbose, TEXT("WorldGen: Foliage type %d skipped (component/mesh missing or density <= 0)"), TypeIndex);
			continue;
		}

		const int32 NumInstances = FMath::RoundToInt(Config.DensityPerSqM * AreaSqM);
		if (NumInstances <= 0)
		{
			UE_LOG(LogTemp, Verbose, TEXT("WorldGen: Foliage type %d has 0 instances for section (%d, %d)"), TypeIndex, Section.X, Section.Y);
			continue;
		}

		const uint32 SectionSeed = GetTypeHash(Section);
		const uint32 TypeSeed = HashCombine(SectionSeed, GetTypeHash(TypeIndex));
		const uint32 GlobalSeed = HashCombine(TypeSeed, GetTypeHash(FoliageSeedOffset));
		const uint32 Seed = HashCombine(GlobalSeed, GetTypeHash(Config.SeedOffset));
		FRandomStream Rand(static_cast<int32>(Seed));

		TArray<int32>& InstanceIndices = SectionData.FoliageInstanceIndices[TypeIndex];
		InstanceIndices.Reserve(NumInstances);
		TArray<FTransform> PendingTransforms;
		PendingTransforms.Reserve(NumInstances);

		const float MinAllowedHeight = FMath::Min(Config.MinHeight, Config.MaxHeight);
		const float MaxAllowedHeight = FMath::Max(Config.MinHeight, Config.MaxHeight);
		const FBoxSphereBounds MeshBounds = Config.Mesh ? Config.Mesh->GetBounds() : FBoxSphereBounds(EForceInit::ForceInit);
		const float MeshFootprintRadius = FVector2D(MeshBounds.BoxExtent.X, MeshBounds.BoxExtent.Y).Size();
		constexpr int32 MaxPlacementAttemptsPerInstance = 8;

		int32 AddedCount = 0;
		for (int32 InstanceIdx = 0; InstanceIdx < NumInstances; ++InstanceIdx)
		{
			for (int32 Attempt = 0; Attempt < MaxPlacementAttemptsPerInstance; ++Attempt)
			{
				const float LocalX = Rand.FRandRange(0.0f, SectionSizeX);
				const float LocalY = Rand.FRandRange(0.0f, SectionSizeY);
				const FVector2D SampleLocation(BaseX + LocalX, BaseY + LocalY);

				FSurfacePlacement Placement;
				if (!BuildSurfacePlacementAt(
					Section,
					SampleLocation,
					TerrainTransform,
					Config.SurfaceOffset,
					Placement))
				{
					continue;
				}

				if (Placement.SlopeDeg > Config.MaxSlopeDeg ||
					Placement.LocalLocation.Z < MinAllowedHeight ||
					Placement.LocalLocation.Z > MaxAllowedHeight)
				{
					continue;
				}

				const float ScaleValue = Rand.FRandRange(Config.MinScale, Config.MaxScale);
				if (Config.bEnableCollision &&
					IsNearAnyPlayerStart(
						PlayerStartLocations,
						Placement.WorldLocation,
						PlayerStartObjectAvoidanceRadius + MeshFootprintRadius * ScaleValue))
				{
					continue;
				}
				if (IsFoliageLocationBlockedByBuildings(Placement.WorldLocation, MeshFootprintRadius * ScaleValue, SectionData))
				{
					continue;
				}

				FRotator Rotation = Config.bAlignToNormal
					? FRotationMatrix::MakeFromZX(Placement.WorldNormal, FVector::ForwardVector).Rotator()
					: FRotator::ZeroRotator;
				Rotation.Yaw += Rand.FRandRange(0.0f, 360.0f);

				PendingTransforms.Emplace(Rotation, Placement.WorldLocation, FVector(ScaleValue));
				++AddedCount;
				break;
			}
		}

		if (PendingTransforms.Num() > 0)
		{
			InstanceIndices = Component->AddInstances(
				PendingTransforms,
				true,
				false,
				false
			);

			if (InstanceIndices.Num() != PendingTransforms.Num())
			{
				UE_LOG(
					LogTemp,
					Warning,
					TEXT("WorldGen: Foliage type %d added %d transforms but received %d instance indices in section (%d, %d)"),
					TypeIndex,
					PendingTransforms.Num(),
					InstanceIndices.Num(),
					Section.X,
					Section.Y
				);
			}
		}

		UE_LOG(LogTemp, Log, TEXT("WorldGen: Foliage type %d added %d/%d instances in section (%d, %d)"), TypeIndex, AddedCount, NumInstances, Section.X, Section.Y);
		if (AddedCount == 0)
		{
			UE_LOG(
				LogTemp,
				Warning,
				TEXT("WorldGen: Foliage type %d added no instances. Density=%0.4f MaxSlope=%0.2f HeightRange=[%0.2f,%0.2f]"),
				TypeIndex,
				Config.DensityPerSqM,
				Config.MaxSlopeDeg,
				MinAllowedHeight,
				MaxAllowedHeight
			);
		}
	}
}

void AWorldGen::RemoveFoliageForSection(const FIntPoint& Section, FSectionData& SectionData)
{
	if (FoliageComponents.Num() == 0)
	{
		return;
	}

	const int32 TypeCount = FMath::Max(FoliageComponents.Num(), SectionData.FoliageInstanceIndices.Num());
	for (int32 TypeIndex = 0; TypeIndex < TypeCount; ++TypeIndex)
	{
		UHierarchicalInstancedStaticMeshComponent* Component = FoliageComponents.IsValidIndex(TypeIndex) ? FoliageComponents[TypeIndex] : nullptr;
		if (!Component || !SectionData.FoliageInstanceIndices.IsValidIndex(TypeIndex))
		{
			continue;
		}

		TArray<int32>& IndicesToRemove = SectionData.FoliageInstanceIndices[TypeIndex];
		if (IndicesToRemove.Num() == 0)
		{
			continue;
		}

		IndicesToRemove.Sort();
		IndicesToRemove.SetNum(Algo::Unique(IndicesToRemove));

		for (int32 Index = IndicesToRemove.Num() - 1; Index >= 0; --Index)
		{
			const int32 InstanceIndex = IndicesToRemove[Index];
			const int32 LastInstanceIndex = Component->GetInstanceCount() - 1;
			if (InstanceIndex < 0 || InstanceIndex > LastInstanceIndex)
			{
				UE_LOG(
					LogTemp,
					Warning,
					TEXT("WorldGen: Skipping invalid foliage instance index %d for type %d"),
					InstanceIndex,
					TypeIndex
				);
				continue;
			}

			if (Component->RemoveInstance(InstanceIndex))
			{
				if (InstanceIndex != LastInstanceIndex)
				{
					UpdateFoliageIndexAfterSwapRemoval(TypeIndex, LastInstanceIndex, InstanceIndex, Section);
				}
			}
		}

		IndicesToRemove.Reset();
	}
}

void AWorldGen::UpdateFoliageIndexAfterSwapRemoval(int32 TypeIndex, int32 OldIndex, int32 NewIndex, const FIntPoint& RemovedSection)
{
	if (OldIndex == NewIndex)
	{
		return;
	}

	for (TPair<FIntPoint, FSectionData>& Pair : LoadedSections)
	{
		if (Pair.Key == RemovedSection)
		{
			continue;
		}

		if (!Pair.Value.FoliageInstanceIndices.IsValidIndex(TypeIndex))
		{
			continue;
		}

		TArray<int32>& Indices = Pair.Value.FoliageInstanceIndices[TypeIndex];
		for (int32& Index : Indices)
		{
			if (Index == OldIndex)
			{
				Index = NewIndex;
				return;
			}
		}
	}

	UE_LOG(
		LogTemp,
		Verbose,
		TEXT("WorldGen: Foliage swap update did not find moved instance. Type=%d OldIndex=%d NewIndex=%d"),
		TypeIndex,
		OldIndex,
		NewIndex
	);
}

void AWorldGen::SpawnBotsForSection(const FIntPoint& Section, FSectionData& SectionData)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Warning, TEXT("WorldGen: Cannot spawn bots without a valid world."));
		return;
	}

	if (xvertcnt < 2 || yvertcnt < 2)
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("WorldGen: Bot spawning skipped for section (%d, %d) because terrain vertex counts are invalid. xvertcnt=%d yvertcnt=%d"),
			Section.X,
			Section.Y,
			xvertcnt,
			yvertcnt
		);
		return;
	}

	const float SectionSizeX = (xvertcnt - 1) * cellsize;
	const float SectionSizeY = (yvertcnt - 1) * cellsize;
	if (SectionSizeX <= KINDA_SMALL_NUMBER || SectionSizeY <= KINDA_SMALL_NUMBER)
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("WorldGen: Bot spawning skipped for section (%d, %d) because section size is invalid. Size=(%0.2f, %0.2f)"),
			Section.X,
			Section.Y,
			SectionSizeX,
			SectionSizeY
		);
		return;
	}

	if (BotSpawnTypes.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("WorldGen: Bot spawning enabled, but BotSpawnTypes is empty."));
		return;
	}

	for (int32 TypeIndex = 0; TypeIndex < BotSpawnTypes.Num(); ++TypeIndex)
	{
		SpawnBotTypeForSection(
			Section,
			SectionData,
			BotSpawnTypes[TypeIndex],
			TypeIndex,
			FString::Printf(TEXT("bot type %d"), TypeIndex),
			SectionSizeX,
			SectionSizeY);
	}
}

void AWorldGen::SpawnBotTypeForSection(
	const FIntPoint& Section,
	FSectionData& SectionData,
	const FBotSpawnTypeConfig& Config,
	int32 TypeIndex,
	const FString& DebugName,
	float SectionSizeX,
	float SectionSizeY)
{
	if (!Config.bEnabled)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (!Config.BotClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("WorldGen: %s spawning enabled, but BotClass is not assigned."), *DebugName);
		return;
	}
	const ABotCharacter* BotDefaults = Config.BotClass->GetDefaultObject<ABotCharacter>();
	const bool bSpawnAsWalkingBot = BotDefaults && BotDefaults->IsWalkingBot();

	const int32 MaxCount = FMath::Max(Config.MaxBotsPerSection, 0);
	if (MaxCount <= 0)
	{
		UE_LOG(
			LogTemp,
			Log,
			TEXT("WorldGen: %s spawning skipped for section (%d, %d) because MaxBotsPerSection is 0."),
			*DebugName,
			Section.X,
			Section.Y
		);
		return;
	}

	const int32 MinCount = FMath::Clamp(Config.MinBotsPerSection, 1, MaxCount);
	const float AreaSqM = (SectionSizeX * SectionSizeY) / 10000.0f;
	const int32 DensityCount = FMath::RoundToInt(FMath::Max(Config.DensityPerSqM, 0.0f) * AreaSqM);

	const uint32 SectionSeed = GetTypeHash(Section);
	const uint32 TypeSeed = HashCombine(SectionSeed, GetTypeHash(TypeIndex));
	const uint32 Seed = HashCombine(TypeSeed, GetTypeHash(Config.SeedOffset));
	FRandomStream Rand(static_cast<int32>(Seed));
	const int32 NumBots = Config.bUseDensityPerSqM
		? FMath::Clamp(DensityCount, 0, MaxCount)
		: Rand.RandRange(MinCount, MaxCount);
	if (NumBots <= 0)
	{
		return;
	}

	const float BaseX = Section.X * SectionSizeX;
	const float BaseY = Section.Y * SectionSizeY;
	const float PaddingX = FMath::Clamp(Config.SectionEdgePadding, 0.0f, SectionSizeX * 0.49f);
	const float PaddingY = FMath::Clamp(Config.SectionEdgePadding, 0.0f, SectionSizeY * 0.49f);
	const float MinAltitude = FMath::Min(Config.MinAltitudeAboveTerrain, Config.MaxAltitudeAboveTerrain);
	const float MaxAltitude = FMath::Max(Config.MinAltitudeAboveTerrain, Config.MaxAltitudeAboveTerrain);
	const FTransform TerrainTransform = TerrainMesh ? TerrainMesh->GetComponentTransform() : GetActorTransform();
	TArray<FVector> PlayerStartLocations;
	GatherPlayerStartLocations(PlayerStartLocations);
	const float BotCollisionRadius = BotDefaults && BotDefaults->GetCapsuleComponent()
		? BotDefaults->GetCapsuleComponent()->GetScaledCapsuleRadius()
		: 0.0f;

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	int32 SpawnedCount = 0;
	SectionData.SpawnedBots.Reserve(SectionData.SpawnedBots.Num() + NumBots);
	for (int32 BotIndex = 0; BotIndex < NumBots; ++BotIndex)
	{
		const float CapsuleHalfHeight = bSpawnAsWalkingBot && BotDefaults && BotDefaults->GetCapsuleComponent()
			? BotDefaults->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()
			: 0.f;

		FVector SpawnLocation = FVector::ZeroVector;
		FVector GroundLocation = FVector::ZeroVector;
		float Altitude = 0.0f;
		const bool bSpawnedInsideBuilding = TryBuildBotSpawnInsideBuilding(SectionData, Config, BotDefaults, Rand, SpawnLocation);
		if (bSpawnedInsideBuilding)
		{
			GroundLocation = FVector(SpawnLocation.X, SpawnLocation.Y, SpawnLocation.Z - CapsuleHalfHeight - Config.GroundSpawnClearance);
			Altitude = SpawnLocation.Z - GroundLocation.Z;
		}
		else
		{
			const float WorldX = BaseX + Rand.FRandRange(PaddingX, SectionSizeX - PaddingX);
			const float WorldY = BaseY + Rand.FRandRange(PaddingY, SectionSizeY - PaddingY);
			const FVector2D SampleLocation(WorldX, WorldY);
			FSurfacePlacement Placement;
			if (!BuildSurfacePlacementAt(Section, SampleLocation, TerrainTransform, 0.0f, Placement))
			{
				continue;
			}

			Altitude = bSpawnAsWalkingBot
				? CapsuleHalfHeight + Config.GroundSpawnClearance
				: Rand.FRandRange(MinAltitude, MaxAltitude);
			GroundLocation = Placement.WorldLocation;
			SpawnLocation = GroundLocation + FVector(0.0f, 0.0f, Altitude);
		}

		if (IsNearAnyPlayerStart(PlayerStartLocations, SpawnLocation, PlayerStartObjectAvoidanceRadius + BotCollisionRadius))
		{
			continue;
		}

		const FRotator SpawnRotation(0.0f, Rand.FRandRange(0.0f, 360.0f), 0.0f);
		if (Config.bCheckSpawnCollision &&
			IsBotSpawnLocationBlocked(SpawnLocation, SpawnRotation, BotDefaults, Config.SpawnCollisionExtraClearance))
		{
			if (bDebugBotSpawning)
			{
				const float DebugClearance = FMath::Max(Config.SpawnCollisionExtraClearance, 0.0f);
				DrawDebugCapsule(
					World,
					SpawnLocation + FVector(0.0f, 0.0f, DebugClearance),
					BotDefaults && BotDefaults->GetCapsuleComponent()
						? BotDefaults->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + DebugClearance
						: 88.0f + DebugClearance,
					BotDefaults && BotDefaults->GetCapsuleComponent()
						? BotDefaults->GetCapsuleComponent()->GetScaledCapsuleRadius() + DebugClearance
						: 34.0f + DebugClearance,
					SpawnRotation.Quaternion(),
					FColor::Red,
					false,
					BotDebugDrawDuration,
					0,
					2.0f
				);
			}
			continue;
		}

		ABotCharacter* SpawnedBot = World->SpawnActor<ABotCharacter>(Config.BotClass, SpawnLocation, SpawnRotation, SpawnParams);
		if (SpawnedBot)
		{
			const FName SectionTag(*FString::Printf(TEXT("BotSection_%d_%d"), Section.X, Section.Y));
			const FName TypeTag(*FString::Printf(TEXT("BotType_%d"), TypeIndex));
			SpawnedBot->Tags.AddUnique(SectionTag);
			SpawnedBot->Tags.AddUnique(TypeTag);

			SectionData.SpawnedBots.Add(SpawnedBot);
			++SpawnedCount;

			if (bDebugBotSpawning)
			{
				const FString DebugText = FString::Printf(
					TEXT("%s %d/%d\nSection (%d,%d)\nAlt %.0f"),
					*DebugName,
					BotIndex + 1,
					NumBots,
					Section.X,
					Section.Y,
					Altitude
				);

				DrawDebugSphere(
					World,
					SpawnLocation,
					BotDebugSphereRadius,
					16,
					FColor::Cyan,
					false,
					BotDebugDrawDuration,
					0,
					4.0f
				);
				DrawDebugLine(
					World,
					GroundLocation,
					SpawnLocation,
					FColor::Yellow,
					false,
					BotDebugDrawDuration,
					0,
					2.0f
				);
				DrawDebugString(
					World,
					SpawnLocation + FVector(0.0f, 0.0f, BotDebugSphereRadius + 60.0f),
					DebugText,
					SpawnedBot,
					FColor::White,
					BotDebugDrawDuration,
					true,
					1.0f
				);

				UE_LOG(
					LogTemp,
					Log,
					TEXT("WorldGen: Spawned %s %s in section (%d, %d) at %s terrainZ=%0.2f altitude=%0.2f tag=%s class=%s"),
					*DebugName,
					*SpawnedBot->GetName(),
					Section.X,
					Section.Y,
					*SpawnLocation.ToCompactString(),
					GroundLocation.Z,
					Altitude,
					*SectionTag.ToString(),
					*Config.BotClass->GetName()
				);
			}
		}
		else
		{
			UE_LOG(
				LogTemp,
				Warning,
				TEXT("WorldGen: Failed to spawn %s %d/%d in section (%d, %d) at %s using class %s."),
				*DebugName,
				BotIndex + 1,
				NumBots,
				Section.X,
				Section.Y,
				*SpawnLocation.ToCompactString(),
				*Config.BotClass->GetName()
			);
		}
	}

	UE_LOG(
		LogTemp,
		Log,
		TEXT("WorldGen: Spawned %d/%d %s bots in section (%d, %d)"),
		SpawnedCount,
		NumBots,
		*DebugName,
		Section.X,
		Section.Y
	);
}

void AWorldGen::DestroyBotsForSection(FSectionData& SectionData)
{
	for (TWeakObjectPtr<ABotCharacter>& BotPtr : SectionData.SpawnedBots)
	{
		if (ABotCharacter* Bot = BotPtr.Get())
		{
			if (bDebugBotSpawning)
			{
				UE_LOG(LogTemp, Log, TEXT("WorldGen: Destroying streamed bot %s at %s"), *Bot->GetName(), *Bot->GetActorLocation().ToCompactString());
			}

			Bot->Destroy();
		}
	}

	SectionData.SpawnedBots.Reset();
}

void AWorldGen::GatherPlayerStartLocations(TArray<FVector>& OutLocations) const
{
	OutLocations.Reset();

	TArray<AActor*> PlayerStarts;
	UGameplayStatics::GetAllActorsOfClass(this, APlayerStart::StaticClass(), PlayerStarts);
	OutLocations.Reserve(PlayerStarts.Num() + 1);
	for (const AActor* PlayerStart : PlayerStarts)
	{
		if (PlayerStart)
		{
			OutLocations.Add(PlayerStart->GetActorLocation());
		}
	}

	if (const AActor* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0))
	{
		OutLocations.AddUnique(PlayerPawn->GetActorLocation());
	}
	else if (const APlayerController* PlayerController = UGameplayStatics::GetPlayerController(this, 0))
	{
		if (const APawn* Pawn = PlayerController->GetPawn())
		{
			OutLocations.AddUnique(Pawn->GetActorLocation());
		}
	}
}

bool AWorldGen::IsNearAnyPlayerStart(const TArray<FVector>& PlayerStartLocations, const FVector& WorldLocation, float ClearanceRadius) const
{
	if (PlayerStartLocations.Num() == 0 || ClearanceRadius <= 0.0f)
	{
		return false;
	}

	const float ClearanceRadiusSq = FMath::Square(ClearanceRadius);
	for (const FVector& PlayerStartLocation : PlayerStartLocations)
	{
		if (FVector::DistSquared2D(PlayerStartLocation, WorldLocation) <= ClearanceRadiusSq)
		{
			return true;
		}
	}

	return false;
}

bool AWorldGen::IsFootprintNearAnyPlayerStart(const TArray<FVector>& PlayerStartLocations, const FBox2D& Footprint, float ClearanceRadius) const
{
	if (!Footprint.bIsValid || PlayerStartLocations.Num() == 0)
	{
		return false;
	}

	const FVector2D Expansion(FMath::Max(ClearanceRadius, 0.0f));
	const FBox2D ExpandedFootprint(Footprint.Min - Expansion, Footprint.Max + Expansion);
	for (const FVector& PlayerStartLocation : PlayerStartLocations)
	{
		if (ExpandedFootprint.IsInside(FVector2D(PlayerStartLocation.X, PlayerStartLocation.Y)))
		{
			return true;
		}
	}

	return false;
}

bool AWorldGen::IsBotSpawnLocationBlocked(
	const FVector& SpawnLocation,
	const FRotator& SpawnRotation,
	const ABotCharacter* BotDefaults,
	float ExtraClearance) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	float CapsuleRadius = 34.0f;
	float CapsuleHalfHeight = 88.0f;
	if (BotDefaults && BotDefaults->GetCapsuleComponent())
	{
		const UCapsuleComponent* Capsule = BotDefaults->GetCapsuleComponent();
		CapsuleRadius = Capsule->GetScaledCapsuleRadius();
		CapsuleHalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	}

	const float Clearance = FMath::Max(ExtraClearance, 0.0f);
	CapsuleRadius += Clearance;
	CapsuleHalfHeight = FMath::Max(CapsuleHalfHeight + Clearance, CapsuleRadius);
	const FVector QueryLocation = SpawnLocation + FVector(0.0f, 0.0f, Clearance);

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(WorldGenBotSpawnCollision), false, this);
	QueryParams.bFindInitialOverlaps = true;

	const FCollisionShape CollisionShape = FCollisionShape::MakeCapsule(CapsuleRadius, CapsuleHalfHeight);
	return World->OverlapBlockingTestByChannel(
		QueryLocation,
		SpawnRotation.Quaternion(),
		ECC_Pawn,
		CollisionShape,
		QueryParams);
}

ANavMeshBoundsVolume* AWorldGen::ResolveNavBoundsTemplate()
{
	if (ANavMeshBoundsVolume* CachedTemplate = NavBoundsTemplateVolume.Get())
	{
		return CachedTemplate;
	}

	TArray<AActor*> ExistingBoundsVolumes;
	UGameplayStatics::GetAllActorsOfClass(this, ANavMeshBoundsVolume::StaticClass(), ExistingBoundsVolumes);
	for (AActor* Actor : ExistingBoundsVolumes)
	{
		ANavMeshBoundsVolume* Candidate = Cast<ANavMeshBoundsVolume>(Actor);
		if (!Candidate || Candidate->GetOwner() == this)
		{
			continue;
		}

		const FBox CandidateBounds = Candidate->GetComponentsBoundingBox(true);
		if (!CandidateBounds.IsValid)
		{
			continue;
		}

		const FVector CandidateExtent = CandidateBounds.GetExtent();
		if (CandidateExtent.X <= KINDA_SMALL_NUMBER ||
			CandidateExtent.Y <= KINDA_SMALL_NUMBER ||
			CandidateExtent.Z <= KINDA_SMALL_NUMBER)
		{
			continue;
		}

		NavBoundsTemplateVolume = Candidate;
		return Candidate;
	}

	return nullptr;
}

ANavMeshBoundsVolume* AWorldGen::CreateSectionNavBounds(const FBox& WorldBounds)
{
	if (!bEnableNavMesh || !bAutoNavBounds)
	{
		return nullptr;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.ObjectFlags |= RF_Transient;
	ANavMeshBoundsVolume* TemplateVolume = ResolveNavBoundsTemplate();
	if (TemplateVolume)
	{
		Params.Template = TemplateVolume;
	}

	ANavMeshBoundsVolume* BoundsVolume = World->SpawnActor<ANavMeshBoundsVolume>(
		TemplateVolume ? TemplateVolume->GetClass() : ANavMeshBoundsVolume::StaticClass(),
		FTransform::Identity,
		Params);
	if (!BoundsVolume)
	{
		return nullptr;
	}

	if (UBrushComponent* BrushComponent = BoundsVolume->GetBrushComponent())
	{
		BrushComponent->SetMobility(EComponentMobility::Movable);
	}

	FVector DesiredExtent = WorldBounds.GetExtent();
	DesiredExtent.X = FMath::Max(DesiredExtent.X + cellsize, cellsize);
	DesiredExtent.Y = FMath::Max(DesiredExtent.Y + cellsize, cellsize);
	DesiredExtent.Z = FMath::Max(NavBoundsHeight, 100.0f);
	if (DesiredExtent.X <= KINDA_SMALL_NUMBER || DesiredExtent.Y <= KINDA_SMALL_NUMBER)
	{
		BoundsVolume->Destroy();
		return nullptr;
	}

	const FBox CurrentBox = BoundsVolume->GetComponentsBoundingBox(true);
	FVector CurrentExtent = CurrentBox.GetExtent();
	if (CurrentExtent.X <= KINDA_SMALL_NUMBER)
	{
		CurrentExtent.X = 100.0f;
	}
	if (CurrentExtent.Y <= KINDA_SMALL_NUMBER)
	{
		CurrentExtent.Y = 100.0f;
	}
	if (CurrentExtent.Z <= KINDA_SMALL_NUMBER)
	{
		CurrentExtent.Z = 100.0f;
	}

	const FVector Scale(
		DesiredExtent.X / CurrentExtent.X,
		DesiredExtent.Y / CurrentExtent.Y,
		DesiredExtent.Z / CurrentExtent.Z
	);

	BoundsVolume->SetActorLocation(WorldBounds.GetCenter());
	BoundsVolume->SetActorScale3D(TemplateVolume ? Scale : FVector::OneVector);

	if (!TemplateVolume)
	{
		UBoxComponent* BoundsComponent = NewObject<UBoxComponent>(BoundsVolume, TEXT("RuntimeNavBoundsBox"));
		if (BoundsComponent)
		{
			BoundsComponent->SetupAttachment(BoundsVolume->GetRootComponent());
			BoundsComponent->SetBoxExtent(DesiredExtent);
			BoundsComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			BoundsComponent->SetCanEverAffectNavigation(false);
			BoundsComponent->SetHiddenInGame(true);
			BoundsVolume->AddInstanceComponent(BoundsComponent);
			BoundsComponent->RegisterComponent();
		}
	}

	if (UNavigationSystemV1* NavSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World))
	{
		NavSystem->OnNavigationBoundsUpdated(BoundsVolume);
	}

	return BoundsVolume;
}

void AWorldGen::DestroySectionNavBounds(FSectionData& SectionData)
{
	ANavMeshBoundsVolume* BoundsVolume = SectionData.NavBoundsVolume.Get();
	if (!BoundsVolume)
	{
		return;
	}

	if (UNavigationSystemV1* NavSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
	{
		NavSystem->OnNavigationBoundsUpdated(BoundsVolume);
	}

	BoundsVolume->Destroy();
	SectionData.NavBoundsVolume = nullptr;
}

void AWorldGen::RequestNavRebuild()
{
	if (!bEnableNavMesh)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	World->GetTimerManager().ClearTimer(DeferredNavRebuildHandle);
	World->GetTimerManager().SetTimer(DeferredNavRebuildHandle, this, &AWorldGen::PerformNavRebuild, 0.05f, false);
}

void AWorldGen::PerformNavRebuild()
{
	if (!bEnableNavMesh)
	{
		return;
	}

	UpdateGeneratedNavigationData();
}

void AWorldGen::UpdateGeneratedNavigationData()
{
	if (!bEnableNavMesh)
	{
		return;
	}

	if (TerrainMesh && bTerrainNavRegistered)
	{
		TerrainMesh->UpdateBounds();
		FNavigationSystem::OnComponentRegistered(*TerrainMesh);
		FNavigationSystem::UpdateComponentData(*TerrainMesh);
	}

	for (UHierarchicalInstancedStaticMeshComponent* Component : FoliageComponents)
	{
		if (Component && Component->CanEverAffectNavigation())
		{
			Component->UpdateBounds();
			FNavigationSystem::OnComponentRegistered(*Component);
			FNavigationSystem::UpdateComponentData(*Component);
		}
	}

	for (TPair<FIntPoint, FSectionData>& Pair : LoadedSections)
	{
		for (FGeneratedBuilding& Building : Pair.Value.Buildings)
		{
			AActor* BuildingActor = Building.Actor.Get();
			if (!BuildingActor)
			{
				continue;
			}

			TArray<UPrimitiveComponent*> PrimitiveComponents;
			BuildingActor->GetComponents<UPrimitiveComponent>(PrimitiveComponents);
			for (UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
			{
				if (PrimitiveComponent && PrimitiveComponent->CanEverAffectNavigation())
				{
					PrimitiveComponent->UpdateBounds();
					FNavigationSystem::OnComponentRegistered(*PrimitiveComponent);
					FNavigationSystem::UpdateComponentData(*PrimitiveComponent);
				}
			}
		}
	}
}

void AWorldGen::MarkNavDirty(const FBox& Bounds)
{
	if (!bEnableNavMesh)
	{
		return;
	}

	if (UNavigationSystemV1* NavSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
	{
		NavSystem->AddDirtyArea(Bounds.ExpandBy(FVector(cellsize, cellsize, NavBoundsHeight)), ENavigationDirtyFlag::All);
	}
}

FIntPoint AWorldGen::GetSectionFromWorld(const FVector& Location) const
{
	const float SectionSize = (xvertcnt - 1) * cellsize;
	if (SectionSize <= KINDA_SMALL_NUMBER)
	{
		return FIntPoint(0, 0);
	}

	const int32 SecX = FMath::FloorToInt(Location.X / SectionSize);
	const int32 SecY = FMath::FloorToInt(Location.Y / SectionSize);
	return FIntPoint(SecX, SecY);
}

bool AWorldGen::IsSectionInBounds(const FIntPoint& Section) const
{
	if (!bClampToWorldSize)
	{
		return true;
	}

	if (numsecx <= 0 || numsecy <= 0)
	{
		return true;
	}

	if (!bAllowNegativeSections)
	{
		return Section.X >= 0 && Section.X < numsecx && Section.Y >= 0 && Section.Y < numsecy;
	}

	const int32 HalfX = numsecx / 2;
	const int32 HalfY = numsecy / 2;
	const int32 MinX = CurrentCenterSection.X - HalfX;
	const int32 MaxX = MinX + numsecx - 1;
	const int32 MinY = CurrentCenterSection.Y - HalfY;
	const int32 MaxY = MinY + numsecy - 1;

	return Section.X >= MinX && Section.X <= MaxX && Section.Y >= MinY && Section.Y <= MaxY;
}

int32 AWorldGen::AcquireMeshSectionIndex()
{
	if (FreeMeshSectionIndices.Num() > 0)
	{
		return FreeMeshSectionIndices.Pop(EAllowShrinking::No);
	}

	return NextMeshSectionIndex++;
}

float AWorldGen::GetHeight(const FVector2D loc)
{
	const float calc_height = {
		PerlinNoise(loc, .00001f, FVector2D(.1f)) +
		PerlinNoise(loc, .0001f, FVector2D(.2f)) +
		PerlinNoise(loc, .001f, FVector2D(.3f)) +
		PerlinNoise(loc, .01f, FVector2D(.4f))
	};
	return (calc_height <= height ? calc_height : height);
}

float AWorldGen::PerlinNoise(const FVector2D loc, const float scale, const FVector2D offset)
{
	return FMath::PerlinNoise2D(loc * scale + FVector2D(.1f, .1f) + offset) * height;
}
