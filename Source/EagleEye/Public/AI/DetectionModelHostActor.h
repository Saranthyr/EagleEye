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

private:
    UPROPERTY(VisibleAnywhere, Category="Components", meta=(AllowPrivateAccess="true"))
    TObjectPtr<USceneComponent> SceneRoot;

    UPROPERTY(VisibleAnywhere, Category="Detection|Shared Model", meta=(AllowPrivateAccess="true"))
    TObjectPtr<UMyActorComponent> ModelHostComponent;
};
