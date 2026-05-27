#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "BotCharacter.generated.h"

class UBehaviorTree;
class ABotDamageProjectile;
class UMyActorComponent;
class USphereComponent;
class UPrimitiveComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FBotHealthChangedSignature, float, CurrentHealth, float, MaxHealth);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FBotDiedSignature);

UENUM(BlueprintType)
enum class EBotLocomotionMode : uint8
{
    Walking UMETA(DisplayName="Walking"),
    Flying UMETA(DisplayName="Flying")
};

UCLASS()
class EAGLEEYE_API ABotCharacter : public ACharacter
{
    GENERATED_BODY()

public:
    ABotCharacter();

    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual float TakeDamage(
        float DamageAmount,
        struct FDamageEvent const& DamageEvent,
        AController* EventInstigator,
        AActor* DamageCauser) override;

    UBehaviorTree* GetBehaviorTreeAsset() const { return BehaviorTreeAsset; }
    EBotLocomotionMode GetBotLocomotionMode() const { return LocomotionMode; }
    bool IsFlyingBot() const;
    bool IsWalkingBot() const;

    UFUNCTION(BlueprintCallable, Category="AI|Movement")
    void ApplyBotMovementSettings();

    UFUNCTION(BlueprintCallable, Exec, Category="Detection|Recording")
    void StartBotViewportRecording();

    UFUNCTION(BlueprintCallable, Category="Detection|Recording")
    void StartBotViewportRecordingWithSettings(float FPS, int32 Width, int32 Height);

    UFUNCTION(BlueprintCallable, Exec, Category="Detection|Recording")
    void StopBotViewportRecording();

    UFUNCTION(BlueprintCallable, Category="Gameplay|Damage")
    bool TryThrowProjectileAtActor(AActor* TargetActor);

    UFUNCTION(BlueprintCallable, Category="Gameplay|Damage")
    bool TryThrowProjectileAtLocation(const FVector& TargetLocation);

    UFUNCTION(BlueprintCallable, Category="Gameplay|Damage")
    bool ThrowProjectileAtPlayer();

    UFUNCTION(BlueprintCallable, Category="Gameplay|Damage")
    void SetCloseDamageHitboxEnabled(bool bEnabled);

    UFUNCTION(BlueprintPure, Category="Gameplay|Damage")
    bool IsCloseDamageHitboxEnabled() const { return bCloseDamageHitboxEnabled; }

    UFUNCTION(BlueprintCallable, Category="Gameplay|Health")
    float ApplyHealthDamage(float DamageAmount, AController* EventInstigator = nullptr, AActor* DamageCauser = nullptr);

    UFUNCTION(BlueprintCallable, Category="Gameplay|Health")
    float Heal(float HealAmount);

    UFUNCTION(BlueprintCallable, Category="Gameplay|Health")
    void ResetHealth();

    UFUNCTION(BlueprintPure, Category="Gameplay|Health")
    float GetCurrentHealth() const { return CurrentHealth; }

    UFUNCTION(BlueprintPure, Category="Gameplay|Health")
    float GetMaxHealth() const { return MaxHealth; }

    UFUNCTION(BlueprintPure, Category="Gameplay|Health")
    bool IsDead() const { return bIsDead; }

    UPROPERTY(BlueprintAssignable, Category="Gameplay|Health")
    FBotHealthChangedSignature OnHealthChanged;

    UPROPERTY(BlueprintAssignable, Category="Gameplay|Health")
    FBotDiedSignature OnBotDied;

protected:
    void TickCloseDamageHitbox();
    bool TryApplyCloseDamage(AActor* TargetActor);
    bool IsValidDamageTarget(const AActor* TargetActor) const;
    void HandleDeath(AController* EventInstigator, AActor* DamageCauser);
    void EnableDeathRagdoll();
    void DisableDeathRagdoll();

    UFUNCTION(BlueprintImplementableEvent, Category="Gameplay|Health")
    void K2_OnDeath(AController* EventInstigator, AActor* DamageCauser);

    UFUNCTION()
    void HandleCloseDamageOverlap(
        UPrimitiveComponent* OverlappedComponent,
        AActor* OtherActor,
        UPrimitiveComponent* OtherComp,
        int32 OtherBodyIndex,
        bool bFromSweep,
        const FHitResult& SweepResult);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Movement")
    EBotLocomotionMode LocomotionMode = EBotLocomotionMode::Flying;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Movement")
    bool bOrientRotationToMovement = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Movement")
    FRotator MovementRotationRate = FRotator(240.f, 360.f, 240.f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Movement|Walking", meta=(ClampMin="0.0"))
    float WalkingMaxSpeed = 600.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Movement|Walking", meta=(ClampMin="0.0"))
    float WalkingGravityScale = 1.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Movement|Walking", meta=(ClampMin="0.0"))
    float WalkingBrakingDeceleration = 2048.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Movement|Flying", meta=(ClampMin="0.0"))
    float FlyingMaxSpeed = 900.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Movement|Flying", meta=(ClampMin="0.0"))
    float FlyingGravityScale = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Movement|Flying", meta=(ClampMin="0.0"))
    float FlyingBrakingDeceleration = 1200.f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Detection", meta=(AllowPrivateAccess="true"))
    TObjectPtr<UMyActorComponent> DetectionComponent;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Gameplay|Damage")
    TObjectPtr<USphereComponent> CloseDamageHitbox;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Gameplay|Health", meta=(ClampMin="1.0"))
    float MaxHealth = 50.f;

    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Gameplay|Health")
    float CurrentHealth = 50.f;

    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Gameplay|Health")
    bool bIsDead = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Gameplay|Death")
    bool bDestroyOnDeath = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Gameplay|Death", meta=(ClampMin="0.0", EditCondition="bDestroyOnDeath"))
    float DestroyDelaySeconds = 1.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Gameplay|Death")
    bool bDisableCollisionOnDeath = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Gameplay|Death")
    bool bEnableRagdollOnDeath = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Gameplay|Death", meta=(EditCondition="bEnableRagdollOnDeath"))
    FName RagdollCollisionProfileName = TEXT("Ragdoll");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Gameplay|Damage|Close")
    bool bEnableCloseHitboxDamage = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Gameplay|Damage|Close", meta=(ClampMin="0.0"))
    float CloseDamage = 20.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Gameplay|Damage|Close", meta=(ClampMin="0.0"))
    float CloseDamageCooldownSeconds = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Gameplay|Damage|Projectile")
    bool bEnableProjectileAttack = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Gameplay|Damage|Projectile")
    TSubclassOf<ABotDamageProjectile> ProjectileClass;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Gameplay|Damage|Projectile", meta=(ClampMin="0.0"))
    float ProjectileDamage = 15.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Gameplay|Damage|Projectile", meta=(ClampMin="0.0"))
    float ProjectileSpeed = 1400.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Gameplay|Damage|Projectile", meta=(ClampMin="0.0"))
    float ProjectileAttackRange = 1500.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Gameplay|Damage|Projectile", meta=(ClampMin="0.0"))
    float ProjectileAttackCooldownSeconds = 2.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Gameplay|Damage|Projectile")
    FVector ProjectileSpawnOffset = FVector(80.f, 0.f, 40.f);

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="AI")
    TObjectPtr<UBehaviorTree> BehaviorTreeAsset;

private:
    TWeakObjectPtr<AActor> LastCloseDamageActor;
    float LastCloseDamageTime = -TNumericLimits<float>::Max();
    float LastProjectileAttackTime = -TNumericLimits<float>::Max();
    bool bCloseDamageHitboxEnabled = false;
    FName CachedMeshCollisionProfileName = NAME_None;
    ECollisionEnabled::Type CachedMeshCollisionEnabled = ECollisionEnabled::NoCollision;
    bool bHasCachedMeshCollisionSettings = false;
};
