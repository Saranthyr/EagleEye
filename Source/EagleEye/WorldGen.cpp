// Fill out your copyright notice in the Description page of Project Settings.


#include "WorldGen.h"

// Sets default values
AWorldGen::AWorldGen()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	TerrainMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("TerrainMesh"));

}

// Called when the game starts or when spawned
void AWorldGen::BeginPlay()
{
	Super::BeginPlay();
	
}

// Called every frame
void AWorldGen::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}

void AWorldGen::GenTerrain(const int secidxx, const int secidxy) {
	FVector offset = FVector(secidxx*(xvertcnt-1), secidxy*(yvertcnt-1), 0.f)*cellsize;

	TArray<FVector> verts;
	FVector vert;

	TArray<FVector2D> uvs;
	FVector2D uv;

	TArray<int32> tris;
	TArray<FVector> normals;
	TArray<FProcMeshTangent> tangs;

	for (int32 iVY = -1; iVY <= yvertcnt; iVY++) {
		for (int32 iVX = -1; iVX <= xvertcnt; iVX++) {
			vert.X = iVX*cellsize + offset.X;
			vert.Y = iVY*cellsize + offset.Y;
			vert.Z = GetHeight(FVector2D(vert.X, vert.Y));
			verts.Add(vert);

			uv.X = (iVX+(secidxx*(xvertcnt-1)))*cellsize/100;
			uv.Y = (iVY+(secidxy*(yvertcnt-1)))*cellsize/100;
			uvs.Add(uv);
		}
	}

	for (int32 iTY = 0; iTY <= yvertcnt; iTY++) {
		for (int32 iTX = 0; iTX <= xvertcnt; iTX++) {
			tris.Add(iTX+iTY*(xvertcnt+2));
			tris.Add(iTX+(iTY+1)*(xvertcnt+2));
			tris.Add(iTX+iTY*(xvertcnt+2)+1);
			tris.Add(iTX+(iTY+1)*(xvertcnt+2));
			tris.Add(iTX+(iTY+1)*(xvertcnt+2)+1);
			tris.Add(iTX+iTY*(xvertcnt+2)+1);
		}
	}

	UKismetProceduralMeshLibrary::CalculateTangentsForMesh(verts, tris, uvs, normals, tangs);

	TArray<FVector> subverts;
	TArray<FVector2D> subuvs;
	TArray<int32> subtris;
	TArray<FVector> subnormals;
	TArray<FProcMeshTangent> subtangs;

	int vertidx = 0;

	for (int32 iVY = -1; iVY <= yvertcnt; iVY++){
		for (int32 iVX = -1; iVX <= xvertcnt; iVX++) {
			if (-1<iVY && iVY < yvertcnt && -1 <iVX && iVX < xvertcnt) {
				subverts.Add(verts[vertidx]);
				subuvs.Add(uvs[vertidx]);
				subnormals.Add(normals[vertidx]);
				subtangs.Add(tangs[vertidx]);
			}
			vertidx++;
		}
	}

	for (int32 iTY = 0; iTY <= yvertcnt-2; iTY++){
		for (int32 iTX = 0; iTX <= xvertcnt-2; iTX++) {
			subtris.Add(iTX+iTY*xvertcnt);
			subtris.Add(iTX+iTY*xvertcnt + xvertcnt);
			subtris.Add(iTX+iTY*xvertcnt+1);
			subtris.Add(iTX+iTY*xvertcnt+xvertcnt);
			subtris.Add(iTX+iTY*xvertcnt+xvertcnt+1);
			subtris.Add(iTX+iTY*xvertcnt+1);
		}
	}

	TerrainMesh->CreateMeshSection(meshsecidx, subverts, subtris, subnormals, subuvs, TArray<FColor>(), subtangs, true);
	if (TerrMat) {
		TerrainMesh->SetMaterial(meshsecidx, TerrMat);
	}
	meshsecidx++;
}

float AWorldGen::GetHeight(const FVector2D loc){
	float calc_height = {
		PerlinNoise(loc, .00001f, FVector2D(.1f))+
		PerlinNoise(loc, .0001f, FVector2D(.2f))+
		PerlinNoise(loc, .001f, FVector2D(.3f))+
		PerlinNoise(loc, .01f, FVector2D(.4f))
	};
	return calc_height <= height ? calc_height : height;
}

float AWorldGen::PerlinNoise(const FVector2D loc, const float scale, const FVector2D offset) {
	return FMath::PerlinNoise2D(loc*scale + FVector2D(.1f, .1f) + offset) * height;
}
