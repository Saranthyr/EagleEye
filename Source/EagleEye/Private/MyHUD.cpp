// Fill out your copyright notice in the Description page of Project Settings.


#include "MyHUD.h"
#include "Engine/Canvas.h"
#include "EagleEyeCharacter.h"
#include "MyActorComponent.h"
#include "GameFramework/PlayerController.h"
#include "Camera/CameraComponent.h"

void AMyHUD::SetDetectionHudEnabled(bool bEnabled)
{
    bDetectionHudEnabled = bEnabled;
}

void AMyHUD::ToggleDetectionHudEnabled()
{
    SetDetectionHudEnabled(!bDetectionHudEnabled);
}

void AMyHUD::DrawPlayerHealth()
{
    if (!bHealthHudEnabled || !Canvas)
    {
        return;
    }

    const APlayerController* PC = GetOwningPlayerController();
    const AEagleEyeCharacter* PlayerCharacter = PC ? Cast<AEagleEyeCharacter>(PC->GetPawn()) : nullptr;
    if (!PlayerCharacter)
    {
        return;
    }

    const float MaxHealth = FMath::Max(1.f, PlayerCharacter->GetMaxHealth());
    const float CurrentHealth = FMath::Clamp(PlayerCharacter->GetCurrentHealth(), 0.f, MaxHealth);
    const float HealthRatio = CurrentHealth / MaxHealth;

    const float BarWidth = FMath::Max(1.f, HealthBarWidth);
    const float BarHeight = FMath::Max(1.f, HealthBarHeight);
    const float X = FMath::Clamp(HealthBarScreenOffset.X, 0.f, FMath::Max(0.f, Canvas->ClipX - BarWidth));
    const float Y = FMath::Clamp(Canvas->ClipY - HealthBarScreenOffset.Y - BarHeight, 0.f, FMath::Max(0.f, Canvas->ClipY - BarHeight));
    const FLinearColor FillColor = HealthRatio <= 0.30f ? HealthBarLowFillColor : HealthBarFillColor;

    DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.65f), X - 2.f, Y - 2.f, BarWidth + 4.f, BarHeight + 4.f);
    DrawRect(FLinearColor(0.08f, 0.08f, 0.08f, 0.9f), X, Y, BarWidth, BarHeight);
    DrawRect(FillColor, X, Y, BarWidth * HealthRatio, BarHeight);

    DrawLine(X, Y, X + BarWidth, Y, FLinearColor::White, 1.f);
    DrawLine(X + BarWidth, Y, X + BarWidth, Y + BarHeight, FLinearColor::White, 1.f);
    DrawLine(X + BarWidth, Y + BarHeight, X, Y + BarHeight, FLinearColor::White, 1.f);
    DrawLine(X, Y + BarHeight, X, Y, FLinearColor::White, 1.f);

    const FString HealthText = FString::Printf(TEXT("HP %.0f / %.0f"), CurrentHealth, MaxHealth);
    DrawText(HealthText, FLinearColor::White, X, Y - 22.f, nullptr, 1.0f, false);
}

void AMyHUD::DrawDetectionList(const TArray<FDetectionResult>& Detections, int32 SourceWidth, int32 SourceHeight, AActor* CameraActor)
{
    if (!Canvas || SourceWidth <= 0 || SourceHeight <= 0)
    {
        return;
    }

    float DrawOriginX = Canvas->OrgX;
    float DrawOriginY = Canvas->OrgY;
    float DrawWidth = Canvas->ClipX;
    float DrawHeight = Canvas->ClipY;

    if (CameraActor)
    {
        if (const UCameraComponent* Camera = CameraActor->FindComponentByClass<UCameraComponent>())
        {
            if (Camera->bConstrainAspectRatio && Camera->AspectRatio > KINDA_SMALL_NUMBER)
            {
                const float ViewAspect = DrawWidth / FMath::Max(DrawHeight, KINDA_SMALL_NUMBER);
                const float TargetAspect = Camera->AspectRatio;
                if (ViewAspect > TargetAspect)
                {
                    const float FittedWidth = DrawHeight * TargetAspect;
                    DrawOriginX += (DrawWidth - FittedWidth) * 0.5f;
                    DrawWidth = FittedWidth;
                }
                else
                {
                    const float FittedHeight = DrawWidth / TargetAspect;
                    DrawOriginY += (DrawHeight - FittedHeight) * 0.5f;
                    DrawHeight = FittedHeight;
                }
            }
        }
    }

    const float ScaleX = DrawWidth / static_cast<float>(SourceWidth);
    const float ScaleY = DrawHeight / static_cast<float>(SourceHeight);

    for (const FDetectionResult& Det : Detections)
    {
        for (int32 i = 0; i < Det.Corners.Num(); ++i)
        {
            FVector2D Start = Det.Corners[i];
            FVector2D End = Det.Corners[(i + 1) % Det.Corners.Num()];
            Start.X = DrawOriginX + (Start.X * ScaleX);
            Start.Y = DrawOriginY + (Start.Y * ScaleY);
            End.X = DrawOriginX + (End.X * ScaleX);
            End.Y = DrawOriginY + (End.Y * ScaleY);
            DrawLine(Start.X, Start.Y, End.X, End.Y, FLinearColor::Red);
        }

        if (Det.Corners.Num() > 0)
        {
            const float LabelX = DrawOriginX + (Det.Corners[0].X * ScaleX);
            const float LabelY = DrawOriginY + (Det.Corners[0].Y * ScaleY);
            DrawText(Det.Label, FLinearColor::Yellow, LabelX, LabelY, nullptr, 1.0f, false);
        }
    }
}

void AMyHUD::DrawHUD()
{
    Super::DrawHUD();

    if (!Canvas)
    {
        return;
    }

    DrawPlayerHealth();

    APlayerController* PC = GetOwningPlayerController();
    if (!PC)
    {
        return;
    }

    if (!bDetectionHudEnabled)
    {
        return;
    }

    AActor* DetectionActor = PC->GetViewTarget();
    if (!IsValid(DetectionActor))
    {
        DetectionActor = PC->GetPawn();
    }

    UMyActorComponent* Comp = DetectionActor ? DetectionActor->FindComponentByClass<UMyActorComponent>() : nullptr;
    if (!Comp)
    {
        if (APawn* Pawn = PC->GetPawn())
        {
            DetectionActor = Pawn;
            Comp = Pawn->FindComponentByClass<UMyActorComponent>();
        }
    }

    if (!Comp)
    {
        return;
    }

    const int32 SourceWidth = Comp->LastFrameSourceWidth;
    const int32 SourceHeight = Comp->LastFrameSourceHeight;
    if (SourceWidth <= 0 || SourceHeight <= 0)
    {
        return;
    }

    DrawDetectionList(Comp->LastFrameDetections, SourceWidth, SourceHeight, DetectionActor);
}
