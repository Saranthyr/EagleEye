#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DetectionModelHostActor.generated.h"

class UMyActorComponent;
class USceneComponent;

UCLASS()
class EAGLEEYE_API ADetectionModelHostActor : public AActor
{
    GENERATED_BODY()

public:
    ADetectionModelHostActor();

    UFUNCTION(BlueprintPure, Category="Detection|Shared Model")
    UMyActorComponent* GetModelHostComponent() const { return ModelHostComponent; }

    UFUNCTION(BlueprintPure, Category="Detection|Shared Model")
    int32 GetMaxActiveModelUsers() const { return MaxActiveModelUsers; }

    UFUNCTION(BlueprintPure, Category="Detection|Shared Model")
    int32 GetMaxQueuedModelFrames() const { return MaxQueuedModelFrames; }

protected:
    virtual void PostInitializeComponents() override;
    virtual void BeginPlay() override;

private:
    bool bPreloadModelOnBeginPlay = true;

    int32 MaxActiveModelUsers = 2;

    int32 MaxQueuedModelFrames = 2;

    float FrameSourceFPS = 8.f;

    int32 FrameSourceWidth = 640;

    int32 FrameSourceHeight = 640;

    float MaxModelUserDistanceToPlayer = 8000.f;

    bool bStaggerInitialFrameSourceCapture = true;

    float MaxInitialFrameSourceCaptureDelay = 0.75f;

    UPROPERTY(VisibleAnywhere, Category="Components", meta=(AllowPrivateAccess="true"))
    TObjectPtr<USceneComponent> SceneRoot;

    UPROPERTY(VisibleAnywhere, Category="Detection|Shared Model", meta=(AllowPrivateAccess="true"))
    TObjectPtr<UMyActorComponent> ModelHostComponent;
};
