// Fill out your copyright notice in the Description page of Project Settings.


#include "MyHUD.h"
#include "Engine/Canvas.h"
#include "MyActorComponent.h"
#include "GameFramework/PlayerController.h"
#include "Camera/CameraComponent.h"

namespace
{
    struct FDetectionRect
    {
        float MinX = 0.0f;
        float MinY = 0.0f;
        float MaxX = 0.0f;
        float MaxY = 0.0f;
    };

    bool DetectionToRect(const FDetectionResult& Det, FDetectionRect& OutRect)
    {
        if (Det.Corners.Num() == 0)
        {
            return false;
        }

        OutRect.MinX = Det.Corners[0].X;
        OutRect.MinY = Det.Corners[0].Y;
        OutRect.MaxX = Det.Corners[0].X;
        OutRect.MaxY = Det.Corners[0].Y;

        for (const FVector2D& Corner : Det.Corners)
        {
            OutRect.MinX = FMath::Min(OutRect.MinX, Corner.X);
            OutRect.MinY = FMath::Min(OutRect.MinY, Corner.Y);
            OutRect.MaxX = FMath::Max(OutRect.MaxX, Corner.X);
            OutRect.MaxY = FMath::Max(OutRect.MaxY, Corner.Y);
        }

        return (OutRect.MaxX - OutRect.MinX) > 1.0f && (OutRect.MaxY - OutRect.MinY) > 1.0f;
    }

    float RectIoU(const FDetectionRect& A, const FDetectionRect& B)
    {
        const float InterL = FMath::Max(A.MinX, B.MinX);
        const float InterT = FMath::Max(A.MinY, B.MinY);
        const float InterR = FMath::Min(A.MaxX, B.MaxX);
        const float InterB = FMath::Min(A.MaxY, B.MaxY);
        const float InterW = FMath::Max(0.0f, InterR - InterL);
        const float InterH = FMath::Max(0.0f, InterB - InterT);
        const float InterArea = InterW * InterH;
        const float AreaA = (A.MaxX - A.MinX) * (A.MaxY - A.MinY);
        const float AreaB = (B.MaxX - B.MinX) * (B.MaxY - B.MinY);
        const float UnionArea = AreaA + AreaB - InterArea;
        return UnionArea > KINDA_SMALL_NUMBER ? (InterArea / UnionArea) : 0.0f;
    }

    FVector2D RectCenter(const FDetectionRect& R)
    {
        return FVector2D((R.MinX + R.MaxX) * 0.5f, (R.MinY + R.MaxY) * 0.5f);
    }

    FVector2D RectSize(const FDetectionRect& R)
    {
        return FVector2D(FMath::Max(1.0f, R.MaxX - R.MinX), FMath::Max(1.0f, R.MaxY - R.MinY));
    }

    FDetectionRect MakeRectFromCenterSize(const FVector2D& Center, const FVector2D& Size)
    {
        const float HalfW = Size.X * 0.5f;
        const float HalfH = Size.Y * 0.5f;
        FDetectionRect Rect;
        Rect.MinX = Center.X - HalfW;
        Rect.MinY = Center.Y - HalfH;
        Rect.MaxX = Center.X + HalfW;
        Rect.MaxY = Center.Y + HalfH;
        return Rect;
    }

    void RectToCorners(const FDetectionRect& Rect, TArray<FVector2D>& OutCorners)
    {
        OutCorners.Reset();
        OutCorners.Add(FVector2D(Rect.MinX, Rect.MinY));
        OutCorners.Add(FVector2D(Rect.MaxX, Rect.MinY));
        OutCorners.Add(FVector2D(Rect.MaxX, Rect.MaxY));
        OutCorners.Add(FVector2D(Rect.MinX, Rect.MaxY));
    }
}

void AMyHUD::ResetStabilizationState()
{
    StableTracks.Reset();
    StableSourceWidth = 0;
    StableSourceHeight = 0;
    LastProcessedSequence = 0;
}

void AMyHUD::SetDetectionHudEnabled(bool bEnabled)
{
    if (bDetectionHudEnabled == bEnabled)
    {
        return;
    }

    bDetectionHudEnabled = bEnabled;
    if (!bDetectionHudEnabled)
    {
        ResetStabilizationState();
    }
}

void AMyHUD::ToggleDetectionHudEnabled()
{
    SetDetectionHudEnabled(!bDetectionHudEnabled);
}

void AMyHUD::UpdateStableTracks(const TArray<FDetectionResult>& NewDetections, int32 NewSourceWidth, int32 NewSourceHeight, int32 FrameSequence, float DeltaSeconds)
{
    if (NewSourceWidth <= 0 || NewSourceHeight <= 0)
    {
        StableTracks.Reset();
        StableSourceWidth = 0;
        StableSourceHeight = 0;
        LastProcessedSequence = 0;
        return;
    }

    if (StableSourceWidth != NewSourceWidth || StableSourceHeight != NewSourceHeight)
    {
        StableTracks.Reset();
        StableSourceWidth = NewSourceWidth;
        StableSourceHeight = NewSourceHeight;
        LastProcessedSequence = 0;
    }

    if (FrameSequence == 0 || FrameSequence == LastProcessedSequence)
    {
        return;
    }
    LastProcessedSequence = FrameSequence;

    const float Dt = FMath::Clamp(DeltaSeconds, 1.0f / 240.0f, 0.2f);
    const float MaxPredictShift = FMath::Max(20.0f, MaxPredictionShiftPx);
    TArray<FVector2D> PrePredictCenters;
    TArray<FVector2D> PrePredictSizes;
    PrePredictCenters.SetNum(StableTracks.Num());
    PrePredictSizes.SetNum(StableTracks.Num());
    for (int32 TrackIdx = 0; TrackIdx < StableTracks.Num(); ++TrackIdx)
    {
        FTrackedDetection& Track = StableTracks[TrackIdx];
        PrePredictCenters[TrackIdx] = Track.Center;
        PrePredictSizes[TrackIdx] = Track.Size;

        FVector2D PredictDelta = Track.Velocity * Dt;
        const float PredictLen = PredictDelta.Size();
        if (PredictLen > MaxPredictShift)
        {
            PredictDelta *= (MaxPredictShift / PredictLen);
        }
        Track.Center += PredictDelta;
    }

    struct FParsedDetection
    {
        FDetectionResult Det;
        FDetectionRect Rect;
        FVector2D Center = FVector2D::ZeroVector;
        FVector2D Size = FVector2D::ZeroVector;
        int32 ClassId = -1;
    };

    TArray<FParsedDetection> ParsedDetections;
    ParsedDetections.Reserve(NewDetections.Num());
    for (const FDetectionResult& Det : NewDetections)
    {
        FDetectionRect Rect;
        if (!DetectionToRect(Det, Rect))
        {
            continue;
        }

        FParsedDetection Parsed;
        Parsed.Det = Det;
        Parsed.Rect = Rect;
        Parsed.Center = RectCenter(Rect);
        Parsed.Size = RectSize(Rect);
        Parsed.ClassId = Det.ClassId;
        ParsedDetections.Add(MoveTemp(Parsed));
    }

    struct FMatchCandidate
    {
        int32 TrackIdx = INDEX_NONE;
        int32 DetIdx = INDEX_NONE;
        float Score = -1000000.0f;
    };

    const float MinIoU = FMath::Clamp(TrackMatchIoU, 0.0f, 1.0f);
    const float BaseCenterGatePx = FMath::Max(1.0f, CenterMatchPx);
    const float GateByBoxScale = FMath::Clamp(DynamicGateByBox, 0.1f, 2.0f);
    TArray<FMatchCandidate> Candidates;
    Candidates.Reserve(StableTracks.Num() * FMath::Max(1, ParsedDetections.Num()));

    for (int32 TrackIdx = 0; TrackIdx < StableTracks.Num(); ++TrackIdx)
    {
        const FTrackedDetection& Track = StableTracks[TrackIdx];
        const FDetectionRect TrackRect = MakeRectFromCenterSize(Track.Center, Track.Size);
        const float TrackDiag = FMath::Sqrt((Track.Size.X * Track.Size.X) + (Track.Size.Y * Track.Size.Y));
        const float DynamicCenterGate = FMath::Max(BaseCenterGatePx, TrackDiag * GateByBoxScale);

        for (int32 DetIdx = 0; DetIdx < ParsedDetections.Num(); ++DetIdx)
        {
            const FParsedDetection& Parsed = ParsedDetections[DetIdx];
            const float IoU = RectIoU(TrackRect, Parsed.Rect);
            const float CenterDist = FVector2D::Distance(Track.Center, Parsed.Center);
            if (IoU < MinIoU && CenterDist > DynamicCenterGate)
            {
                continue;
            }

            const bool bClassMismatch = (Track.ClassId >= 0 && Parsed.ClassId >= 0 && Track.ClassId != Parsed.ClassId);
            const float DistNorm = FMath::Clamp(CenterDist / DynamicCenterGate, 0.0f, 1.0f);
            FMatchCandidate Candidate;
            Candidate.TrackIdx = TrackIdx;
            Candidate.DetIdx = DetIdx;
            Candidate.Score = IoU + ((1.0f - DistNorm) * 0.30f) - (bClassMismatch ? 0.12f : 0.0f);
            Candidates.Add(Candidate);
        }
    }

    Candidates.Sort([](const FMatchCandidate& A, const FMatchCandidate& B)
    {
        return A.Score > B.Score;
    });

    TArray<bool> TrackMatched;
    TArray<bool> DetMatched;
    TrackMatched.Init(false, StableTracks.Num());
    DetMatched.Init(false, ParsedDetections.Num());

    const float PositionDeadzone = FMath::Max(0.0f, PositionDeadzonePx);
    const float SizeDeadzone = FMath::Max(0.0f, SizeDeadzonePx);
    const float BaseAlpha = FMath::Clamp(BaseSmoothingAlpha, 0.05f, 1.0f);
    const float MaxAlpha = FMath::Clamp(FMath::Max(BaseAlpha, MaxSmoothingAlpha), BaseAlpha, 1.0f);
    const float FastShiftPx = FMath::Max(1.0f, FastShiftPixels);
    const float VelAlpha = FMath::Clamp(VelocityBlendAlpha, 0.05f, 1.0f);

    for (const FMatchCandidate& Candidate : Candidates)
    {
        if (TrackMatched[Candidate.TrackIdx] || DetMatched[Candidate.DetIdx])
        {
            continue;
        }

        FTrackedDetection& Track = StableTracks[Candidate.TrackIdx];
        const FParsedDetection& Parsed = ParsedDetections[Candidate.DetIdx];

        const FVector2D PrevCenter = Track.Center;
        const float CenterShift = FVector2D::Distance(Track.Center, Parsed.Center);
        const float WidthDelta = FMath::Abs(Track.Size.X - Parsed.Size.X);
        const float HeightDelta = FMath::Abs(Track.Size.Y - Parsed.Size.Y);

        const bool bNeedsSmoothing = (CenterShift > PositionDeadzone || WidthDelta > SizeDeadzone || HeightDelta > SizeDeadzone);
        if (bNeedsSmoothing)
        {
            const float ShiftT = FMath::Clamp(CenterShift / FastShiftPx, 0.0f, 1.0f);
            const float Alpha = FMath::Lerp(BaseAlpha, MaxAlpha, ShiftT);
            Track.Center = FMath::Lerp(Track.Center, Parsed.Center, Alpha);
            Track.Size.X = FMath::Max(1.0f, FMath::Lerp(Track.Size.X, Parsed.Size.X, Alpha));
            Track.Size.Y = FMath::Max(1.0f, FMath::Lerp(Track.Size.Y, Parsed.Size.Y, Alpha));
            const FVector2D MeasuredVelocity = (Track.Center - PrevCenter) / Dt;
            Track.Velocity = FMath::Lerp(Track.Velocity, MeasuredVelocity, VelAlpha);
        }
        else
        {
            Track.Center = PrePredictCenters[Candidate.TrackIdx];
            Track.Size = PrePredictSizes[Candidate.TrackIdx];
            Track.Velocity *= 0.5f;
        }

        const int32 PreviousClassId = Track.ClassId;
        const FString PreviousLabel = Track.Det.Label;
        Track.Det = Parsed.Det;
        if (PreviousClassId >= 0 && Parsed.ClassId >= 0 && PreviousClassId != Parsed.ClassId && Track.SeenFrames >= 3)
        {
            Track.Det.ClassId = PreviousClassId;
            Track.Det.Label = PreviousLabel;
            Track.ClassId = PreviousClassId;
        }
        else
        {
            Track.ClassId = Parsed.ClassId;
        }
        RectToCorners(MakeRectFromCenterSize(Track.Center, Track.Size), Track.Det.Corners);
        Track.MissedFrames = 0;
        Track.SeenFrames += 1;
        TrackMatched[Candidate.TrackIdx] = true;
        DetMatched[Candidate.DetIdx] = true;
    }

    for (int32 TrackIdx = 0; TrackIdx < StableTracks.Num(); ++TrackIdx)
    {
        if (!TrackMatched[TrackIdx])
        {
            StableTracks[TrackIdx].Velocity *= 0.85f;
            StableTracks[TrackIdx].MissedFrames += 1;
            RectToCorners(
                MakeRectFromCenterSize(StableTracks[TrackIdx].Center, StableTracks[TrackIdx].Size),
                StableTracks[TrackIdx].Det.Corners);
        }
    }

    for (int32 DetIdx = 0; DetIdx < ParsedDetections.Num(); ++DetIdx)
    {
        if (DetMatched[DetIdx])
        {
            continue;
        }

        const FParsedDetection& Parsed = ParsedDetections[DetIdx];
        FTrackedDetection NewTrack;
        NewTrack.Det = Parsed.Det;
        NewTrack.Center = Parsed.Center;
        NewTrack.Size = Parsed.Size;
        NewTrack.Velocity = FVector2D::ZeroVector;
        NewTrack.ClassId = Parsed.ClassId;
        NewTrack.MissedFrames = 0;
        NewTrack.SeenFrames = 1;
        RectToCorners(MakeRectFromCenterSize(NewTrack.Center, NewTrack.Size), NewTrack.Det.Corners);
        StableTracks.Add(MoveTemp(NewTrack));
    }

    const int32 AllowedMissed = FMath::Max(0, MaxTrackMissedFrames);
    StableTracks.RemoveAll([&](const FTrackedDetection& Track)
    {
        return Track.MissedFrames > AllowedMissed;
    });
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

    if (!bEnableUniversalStabilization)
    {
        DrawDetectionList(Comp->LastFrameDetections, SourceWidth, SourceHeight, DetectionActor);
        return;
    }

    UWorld* World = GetWorld();
    const float DeltaSeconds = World ? World->GetDeltaSeconds() : (1.0f / 60.0f);
    UpdateStableTracks(Comp->LastFrameDetections, SourceWidth, SourceHeight, Comp->LastFrameSequence, DeltaSeconds);

    TArray<FDetectionResult> DrawDetections;
    const int32 MinConfirmed = FMath::Max(1, MinTrackConfirmedFrames);
    for (const FTrackedDetection& Track : StableTracks)
    {
        if (Track.SeenFrames >= MinConfirmed)
        {
            DrawDetections.Add(Track.Det);
        }
    }

    DrawDetectionList(DrawDetections, StableSourceWidth, StableSourceHeight, DetectionActor);
}
