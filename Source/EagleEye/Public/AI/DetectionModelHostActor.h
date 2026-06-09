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
    virtual void BeginPlay() override;

private:
    UPROPERTY(EditAnywhere, Category="Detection|Shared Model", meta=(AllowPrivateAccess="true"))
    bool bPreloadModelOnBeginPlay = true;

    UPROPERTY(EditAnywhere, Category="Detection|Shared Model", meta=(ClampMin="1", AllowPrivateAccess="true"))
    int32 MaxActiveModelUsers = 2;

    UPROPERTY(EditAnywhere, Category="Detection|Shared Model", meta=(ClampMin="1", ClampMax="120", AllowPrivateAccess="true"))
    int32 MaxQueuedModelFrames = 2;

    UPROPERTY(VisibleAnywhere, Category="Components", meta=(AllowPrivateAccess="true"))
    TObjectPtr<USceneComponent> SceneRoot;

    UPROPERTY(VisibleAnywhere, Category="Detection|Shared Model", meta=(AllowPrivateAccess="true"))
    TObjectPtr<UMyActorComponent> ModelHostComponent;
};
