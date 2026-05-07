// Fill out your copyright notice in the Description page of Project Settings.

#include "WorldGen.h"

#include "AI/BotCharacter.h"
#include "Algo/Unique.h"
#include "Components/BrushComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Kismet/GameplayStatics.h"
#include "NavigationSystem.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "Math/RotationMatrix.h"

namespace
{
	constexpr float kMinStreamingInterval = 0.05f;
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
		TEXT("WorldGen: Crow spawning %s. CrowClass=%s Min=%d Max=%d"),
		bEnableCrowSpawning ? TEXT("enabled") : TEXT("disabled"),
		CrowClass ? *CrowClass->GetName() : TEXT("None"),
		MinCrowsPerSection,
		MaxCrowsPerSection
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

			DestroyCrowsForSection(Pair.Value);
		}
	}

	LoadedSections.Empty();
	FreeMeshSectionIndices.Empty();
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
		return;
	}

	CurrentCenterSection = NewCenter;
	LastStreamingRadius = Radius;

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

	bool bSectionsChanged = false;
	TArray<FIntPoint> SectionsToRemove;
	SectionsToRemove.Reserve(LoadedSections.Num());
	for (const TPair<FIntPoint, FSectionData>& Pair : LoadedSections)
	{
		if (!DesiredSections.Contains(Pair.Key))
		{
			SectionsToRemove.Add(Pair.Key);
		}
	}

	for (const FIntPoint& Section : SectionsToRemove)
	{
		const int32 PreviousLoadedCount = LoadedSections.Num();
		DestroySection(Section);
		bSectionsChanged |= (LoadedSections.Num() != PreviousLoadedCount);
	}

	for (const FIntPoint& Section : DesiredSections)
	{
		if (!LoadedSections.Contains(Section))
		{
			const int32 PreviousLoadedCount = LoadedSections.Num();
			CreateSection(Section);
			bSectionsChanged |= (LoadedSections.Num() != PreviousLoadedCount);
		}
	}

	if (bSectionsChanged && bEnableNavMesh)
	{
		RequestNavRebuild();
	}
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

	FNavigationSystem::OnComponentRegistered(*TerrainMesh);
	FNavigationSystem::UpdateComponentData(*TerrainMesh);

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
	if (bEnableNavMesh && bAutoNavBounds)
	{
		SectionData.NavBoundsVolume = CreateSectionNavBounds(WorldBounds);
	}

	if (bEnableFoliage)
	{
		SpawnFoliageForSection(Section, SectionData);
	}

	if (bEnableCrowSpawning)
	{
		SpawnCrowsForSection(Section, SectionData);
	}

	LoadedSections.Add(Section, MoveTemp(SectionData));

	if (bEnableNavMesh)
	{
		MarkNavDirty(WorldBounds);
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

	DestroyCrowsForSection(*SectionData);

	RemoveFoliageForSection(Section, *SectionData);

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
		Component->SetCollisionEnabled(Config.bEnableCollision ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
		Component->SetCanEverAffectNavigation(false);
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
		const uint32 Seed = HashCombine(TypeSeed, GetTypeHash(Config.SeedOffset));
		FRandomStream Rand(static_cast<int32>(Seed));

		TArray<int32>& InstanceIndices = SectionData.FoliageInstanceIndices[TypeIndex];
		InstanceIndices.Reserve(NumInstances);
		TArray<FTransform> PendingTransforms;
		PendingTransforms.Reserve(NumInstances);

		const bool bCanSnapToSurface = Config.bSnapToGeneratedSurface;
		const float MinAllowedHeight = FMath::Min(Config.MinHeight, Config.MaxHeight);
		const float MaxAllowedHeight = FMath::Max(Config.MinHeight, Config.MaxHeight);
		constexpr int32 MaxPlacementAttemptsPerInstance = 8;

		int32 AddedCount = 0;
		for (int32 InstanceIdx = 0; InstanceIdx < NumInstances; ++InstanceIdx)
		{
			for (int32 Attempt = 0; Attempt < MaxPlacementAttemptsPerInstance; ++Attempt)
			{
				const float LocalX = Rand.FRandRange(0.0f, SectionSizeX);
				const float LocalY = Rand.FRandRange(0.0f, SectionSizeY);
				const FVector2D SampleLocation(BaseX + LocalX, BaseY + LocalY);

				const float HeightSample = GetHeight(SampleLocation);
				const float HxPos = GetHeight(SampleLocation + FVector2D(cellsize, 0.0f));
				const float HxNeg = GetHeight(SampleLocation - FVector2D(cellsize, 0.0f));
				const float HyPos = GetHeight(SampleLocation + FVector2D(0.0f, cellsize));
				const float HyNeg = GetHeight(SampleLocation - FVector2D(0.0f, cellsize));

				const float DzDx = HxPos - HxNeg;
				const float DzDy = HyPos - HyNeg;
				FVector Normal(-DzDx, -DzDy, 2.0f * cellsize);
				Normal.Normalize();

				FVector PlacementLocation(SampleLocation.X, SampleLocation.Y, HeightSample);
				FVector PlacementNormal = Normal;

				if (bCanSnapToSurface)
				{
					FVector SnappedLocation;
					FVector SnappedNormal;
					if (SampleGeneratedSurfaceAt(Section, SampleLocation, SnappedLocation, SnappedNormal))
					{
						PlacementLocation = SnappedLocation + (SnappedNormal * Config.SurfaceOffset);
						PlacementNormal = SnappedNormal;
					}
				}

				const float DotUp = FMath::Clamp(FMath::Abs(FVector::DotProduct(PlacementNormal, FVector::UpVector)), 0.0f, 1.0f);
				const float SlopeDeg = FMath::RadiansToDegrees(FMath::Acos(DotUp));
				if (SlopeDeg > Config.MaxSlopeDeg || PlacementLocation.Z < MinAllowedHeight || PlacementLocation.Z > MaxAllowedHeight)
				{
					continue;
				}

				FRotator Rotation = Config.bAlignToNormal
					? FRotationMatrix::MakeFromZX(PlacementNormal, FVector::ForwardVector).Rotator()
					: FRotator::ZeroRotator;
				Rotation.Yaw += Rand.FRandRange(0.0f, 360.0f);

				const float ScaleValue = Rand.FRandRange(Config.MinScale, Config.MaxScale);
				PendingTransforms.Emplace(Rotation, PlacementLocation, FVector(ScaleValue));
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

void AWorldGen::SpawnCrowsForSection(const FIntPoint& Section, FSectionData& SectionData)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Warning, TEXT("WorldGen: Cannot spawn crows without a valid world."));
		return;
	}

	if (!CrowClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("WorldGen: Crow spawning enabled, but CrowClass is not assigned."));
		return;
	}

	const int32 MaxCount = FMath::Max(MaxCrowsPerSection, 0);
	if (MaxCount <= 0)
	{
		UE_LOG(
			LogTemp,
			Log,
			TEXT("WorldGen: Crow spawning skipped for section (%d, %d) because MaxCrowsPerSection is 0."),
			Section.X,
			Section.Y
		);
		return;
	}

	const int32 MinCount = FMath::Clamp(MinCrowsPerSection, 1, MaxCount);
	if (xvertcnt < 2 || yvertcnt < 2)
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("WorldGen: Crow spawning skipped for section (%d, %d) because terrain vertex counts are invalid. xvertcnt=%d yvertcnt=%d"),
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
			TEXT("WorldGen: Crow spawning skipped for section (%d, %d) because section size is invalid. Size=(%0.2f, %0.2f)"),
			Section.X,
			Section.Y,
			SectionSizeX,
			SectionSizeY
		);
		return;
	}

	const uint32 SectionSeed = GetTypeHash(Section);
	const uint32 Seed = HashCombine(SectionSeed, GetTypeHash(CrowSeedOffset));
	FRandomStream Rand(static_cast<int32>(Seed));
	const int32 NumCrows = Rand.RandRange(MinCount, MaxCount);
	if (NumCrows <= 0)
	{
		return;
	}

	const float BaseX = Section.X * SectionSizeX;
	const float BaseY = Section.Y * SectionSizeY;
	const float PaddingX = FMath::Clamp(CrowSectionEdgePadding, 0.0f, SectionSizeX * 0.49f);
	const float PaddingY = FMath::Clamp(CrowSectionEdgePadding, 0.0f, SectionSizeY * 0.49f);
	const float MinAltitude = FMath::Min(CrowMinAltitudeAboveTerrain, CrowMaxAltitudeAboveTerrain);
	const float MaxAltitude = FMath::Max(CrowMinAltitudeAboveTerrain, CrowMaxAltitudeAboveTerrain);

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	int32 SpawnedCount = 0;
	SectionData.SpawnedCrows.Reserve(NumCrows);
	for (int32 CrowIndex = 0; CrowIndex < NumCrows; ++CrowIndex)
	{
		const float WorldX = BaseX + Rand.FRandRange(PaddingX, SectionSizeX - PaddingX);
		const float WorldY = BaseY + Rand.FRandRange(PaddingY, SectionSizeY - PaddingY);
		const FVector2D SampleLocation(WorldX, WorldY);
		const float TerrainZ = GetHeight(SampleLocation);
		const float Altitude = Rand.FRandRange(MinAltitude, MaxAltitude);

		const FVector SpawnLocation(WorldX, WorldY, TerrainZ + Altitude);
		const FRotator SpawnRotation(0.0f, Rand.FRandRange(0.0f, 360.0f), 0.0f);

		ABotCharacter* SpawnedCrow = World->SpawnActor<ABotCharacter>(CrowClass, SpawnLocation, SpawnRotation, SpawnParams);
		if (SpawnedCrow)
		{
			const FName SectionTag(*FString::Printf(TEXT("CrowSection_%d_%d"), Section.X, Section.Y));
			SpawnedCrow->Tags.AddUnique(SectionTag);

			SectionData.SpawnedCrows.Add(SpawnedCrow);
			++SpawnedCount;

			if (bDebugCrowSpawning)
			{
				const FVector TerrainLocation(WorldX, WorldY, TerrainZ);
				const FString DebugText = FString::Printf(
					TEXT("Crow %d/%d\nSection (%d,%d)\nAlt %.0f"),
					CrowIndex + 1,
					NumCrows,
					Section.X,
					Section.Y,
					Altitude
				);

				DrawDebugSphere(
					World,
					SpawnLocation,
					CrowDebugSphereRadius,
					16,
					FColor::Cyan,
					false,
					CrowDebugDrawDuration,
					0,
					4.0f
				);
				DrawDebugLine(
					World,
					TerrainLocation,
					SpawnLocation,
					FColor::Yellow,
					false,
					CrowDebugDrawDuration,
					0,
					2.0f
				);
				DrawDebugString(
					World,
					SpawnLocation + FVector(0.0f, 0.0f, CrowDebugSphereRadius + 60.0f),
					DebugText,
					SpawnedCrow,
					FColor::White,
					CrowDebugDrawDuration,
					true,
					1.0f
				);

				UE_LOG(
					LogTemp,
					Log,
					TEXT("WorldGen: Spawned crow %s in section (%d, %d) at %s terrainZ=%0.2f altitude=%0.2f tag=%s"),
					*SpawnedCrow->GetName(),
					Section.X,
					Section.Y,
					*SpawnLocation.ToCompactString(),
					TerrainZ,
					Altitude,
					*SectionTag.ToString()
				);
			}
		}
		else
		{
			UE_LOG(
				LogTemp,
				Warning,
				TEXT("WorldGen: Failed to spawn crow %d/%d in section (%d, %d) at %s using class %s."),
				CrowIndex + 1,
				NumCrows,
				Section.X,
				Section.Y,
				*SpawnLocation.ToCompactString(),
				*CrowClass->GetName()
			);
		}
	}

	UE_LOG(
		LogTemp,
		Log,
		TEXT("WorldGen: Spawned %d/%d crows in section (%d, %d)"),
		SpawnedCount,
		NumCrows,
		Section.X,
		Section.Y
	);
}

void AWorldGen::DestroyCrowsForSection(FSectionData& SectionData)
{
	for (TWeakObjectPtr<ABotCharacter>& CrowPtr : SectionData.SpawnedCrows)
	{
		if (ABotCharacter* Crow = CrowPtr.Get())
		{
			if (bDebugCrowSpawning)
			{
				UE_LOG(LogTemp, Log, TEXT("WorldGen: Destroying streamed crow %s at %s"), *Crow->GetName(), *Crow->GetActorLocation().ToCompactString());
			}

			Crow->Destroy();
		}
	}

	SectionData.SpawnedCrows.Reset();
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

	ANavMeshBoundsVolume* TemplateVolume = ResolveNavBoundsTemplate();
	if (!TemplateVolume)
	{
		if (!bLoggedMissingNavBoundsTemplate)
		{
			bLoggedMissingNavBoundsTemplate = true;
			UE_LOG(
				LogTemp,
				Warning,
				TEXT("WorldGen: Cannot create runtime nav bounds without a template NavMeshBoundsVolume. Place one NavMeshBoundsVolume in the level.")
			);
		}
		return nullptr;
	}

	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.Template = TemplateVolume;
	Params.ObjectFlags |= RF_Transient;
	ANavMeshBoundsVolume* BoundsVolume = World->SpawnActor<ANavMeshBoundsVolume>(TemplateVolume->GetClass(), FTransform::Identity, Params);
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
	BoundsVolume->SetActorScale3D(Scale);

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

	if (TerrainMesh && bTerrainNavRegistered)
	{
		TerrainMesh->UpdateBounds();
		FNavigationSystem::OnComponentRegistered(*TerrainMesh);
		FNavigationSystem::UpdateComponentData(*TerrainMesh);
	}

	if (UNavigationSystemV1* NavSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
	{
		NavSystem->Build();
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
		NavSystem->AddDirtyArea(Bounds, ENavigationDirtyFlag::All);
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
