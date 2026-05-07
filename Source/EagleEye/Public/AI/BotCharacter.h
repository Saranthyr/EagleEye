#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "BotCharacter.generated.h"

class UBehaviorTree;
class UMyActorComponent;

UCLASS()
class EAGLEEYE_API ABotCharacter : public ACharacter
{
    GENERATED_BODY()

public:
    ABotCharacter();

    virtual void BeginPlay() override;

    UBehaviorTree* GetBehaviorTreeAsset() const { return BehaviorTreeAsset; }

protected:
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Detection")
    TObjectPtr<UMyActorComponent> DetectionComponent;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="AI")
    TObjectPtr<UBehaviorTree> BehaviorTreeAsset;
};
