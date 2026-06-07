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

protected:
    virtual void BeginPlay() override;

private:
    UPROPERTY(EditAnywhere, Category="Detection|Shared Model", meta=(AllowPrivateAccess="true"))
    bool bPreloadModelOnBeginPlay = true;

    UPROPERTY(VisibleAnywhere, Category="Components", meta=(AllowPrivateAccess="true"))
    TObjectPtr<USceneComponent> SceneRoot;

    UPROPERTY(VisibleAnywhere, Category="Detection|Shared Model", meta=(AllowPrivateAccess="true"))
    TObjectPtr<UMyActorComponent> ModelHostComponent;
};
