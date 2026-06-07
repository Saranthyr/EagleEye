#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BotDamageProjectile.generated.h"

class UProjectileMovementComponent;
class UPrimitiveComponent;
class USphereComponent;
class UStaticMeshComponent;

UCLASS(Blueprintable)
class EAGLEEYE_API ABotDamageProjectile : public AActor
{
    GENERATED_BODY()

public:
    ABotDamageProjectile();

    virtual void BeginPlay() override;

    UFUNCTION(BlueprintCallable, Category="Gameplay|Damage")
    void FireInDirection(const FVector& Direction);

    UFUNCTION(BlueprintCallable, Category="Gameplay|Damage")
    void SetDamage(float NewDamage) { Damage = NewDamage; }

    UFUNCTION(BlueprintCallable, Category="Gameplay|Damage")
    void SetProjectileSpeed(float NewSpeed);

    UFUNCTION(BlueprintPure, Category="Gameplay|Damage")
    float GetDamage() const { return Damage; }

    UFUNCTION(BlueprintPure, Category="Gameplay|Damage")
    USphereComponent* GetCollisionComponent() const { return CollisionComponent; }

protected:
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
    TObjectPtr<USphereComponent> CollisionComponent;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
    TObjectPtr<UStaticMeshComponent> MeshComponent;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
    TObjectPtr<UProjectileMovementComponent> ProjectileMovementComponent;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Gameplay|Damage", meta=(DisplayName="Damage (Negative Heals Bots)"))
    float Damage = 15.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Gameplay|Damage", meta=(ClampMin="0.1"))
    float LifeSeconds = 5.f;

private:
    UFUNCTION()
    void HandleProjectileOverlap(
        UPrimitiveComponent* OverlappedComponent,
        AActor* OtherActor,
        UPrimitiveComponent* OtherComp,
        int32 OtherBodyIndex,
        bool bFromSweep,
        const FHitResult& SweepResult);

    UFUNCTION()
    void HandleProjectileHit(
        UPrimitiveComponent* HitComponent,
        AActor* OtherActor,
        UPrimitiveComponent* OtherComp,
        FVector NormalImpulse,
        const FHitResult& Hit);

    bool TryDamageActor(AActor* OtherActor, const FHitResult& Hit);

    bool bHasAppliedDamage = false;
};
