// Fill out your copyright notice in the Description page of Project Settings.

#include "ScreenCaptureComponent.h"

UScreenCaptureComponent::UScreenCaptureComponent()
{
    bCaptureEveryFrame = false;
    bCaptureOnMovement = false;
    CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
    PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_RenderScenePrimitives;

    // Disable expensive post-process features for detector feed.
    ShowFlags.SetTemporalAA(false);
    ShowFlags.SetMotionBlur(false);
    ShowFlags.SetBloom(false);
    ShowFlags.SetFog(false);
}
