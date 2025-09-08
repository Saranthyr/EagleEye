// Fill out your copyright notice in the Description page of Project Settings.


#include "MyHUD.h"
#include "Engine/Canvas.h"
#include "MyActorComponent.h"   // required to use LastFrameDetections
#include "GameFramework/PlayerController.h"

void AMyHUD::DrawHUD()
{
    Super::DrawHUD();

    APlayerController* PC = GetOwningPlayerController();
    if (!PC) return;

    APawn* Pawn = PC->GetPawn();
    if (!Pawn) return;

    UMyActorComponent* Comp = Pawn->FindComponentByClass<UMyActorComponent>();
    if (!Comp) return;

    for (const FDetectionResult& Det : Comp->LastFrameDetections)
    {
        // Draw lines between corners
        for (int32 i = 0; i < Det.Corners.Num(); i++)
        {
            FVector2D Start = Det.Corners[i];
            FVector2D End   = Det.Corners[(i+1) % Det.Corners.Num()];

            DrawLine(Start.X, Start.Y, End.X, End.Y, FLinearColor::Red);
        }

        // Draw label
        if (Det.Corners.Num() > 0)
        {
            DrawText(Det.Label, FLinearColor::Yellow, Det.Corners[0].X, Det.Corners[0].Y, nullptr, 1.0f, false);
        }
    }
}