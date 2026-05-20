#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "BotCharacter.generated.h"

class UBehaviorTree;
class ABotDamageProjectile;
class UMyActorComponent;
class USphereComponent;
class UPrimitiveComponent;

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

    UBehaviorTree* GetBehaviorTreeAsset() const { return BehaviorTreeAsset; }
    EBotLocomotionMode GetBotLocomotionMode() const { return LocomotionMode; }
    bool IsFlyingBot() const { return LocomotionMode == EBotLocomotionMode::Flying; }
    bool IsWalkingBot() const { return LocomotionMode == EBotLocomotionMode::Walking; }

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
    bool ThrowProjectileAtPlayer();

    UFUNCTION(BlueprintCallable, Category="Gameplay|Damage")
    void SetCloseDamageHitboxEnabled(bool bEnabled);

    UFUNCTION(BlueprintPure, Category="Gameplay|Damage")
    bool IsCloseDamageHitboxEnabled() const { return bCloseDamageHitboxEnabled; }

protected:
    void ApplyDetectionRecordingSettings();
    void TickCloseDamageHitbox();
    bool TryApplyCloseDamage(AActor* TargetActor);
    bool IsValidDamageTarget(const AActor* TargetActor) const;

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

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Detection")
    TObjectPtr<UMyActorComponent> DetectionComponent;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Gameplay|Damage")
    TObjectPtr<USphereComponent> CloseDamageHitbox;

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

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Detection|Recording")
    bool bRecordBotViewportVideo = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Detection|Recording")
    bool bRecordBotViewportEvenWhenDetectionSkipped = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Detection|Recording")
    bool bOverrideBotViewportRecordingCaptureSettings = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Detection|Recording", meta=(ClampMin="1.0", ClampMax="120.0", EditCondition="bOverrideBotViewportRecordingCaptureSettings"))
    float BotViewportRecordingFPS = 8.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Detection|Recording", meta=(ClampMin="160", ClampMax="1920", EditCondition="bOverrideBotViewportRecordingCaptureSettings"))
    int32 BotViewportRecordingWidth = 416;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Detection|Recording", meta=(ClampMin="160", ClampMax="1080", EditCondition="bOverrideBotViewportRecordingCaptureSettings"))
    int32 BotViewportRecordingHeight = 416;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Detection|Recording")
    FString BotViewportVideoOutputPath;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Detection|Recording")
    FString BotViewportVideoEncoderPath = TEXT("ffmpeg");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Detection|Recording", meta=(ClampMin="1", ClampMax="120"))
    int32 MaxQueuedBotViewportVideoFrames = 2;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Detection|Recording")
    bool bApplyBotViewportVideoGammaCorrection = true;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="AI")
    TObjectPtr<UBehaviorTree> BehaviorTreeAsset;

private:
    TWeakObjectPtr<AActor> LastCloseDamageActor;
    float LastCloseDamageTime = -TNumericLimits<float>::Max();
    float LastProjectileAttackTime = -TNumericLimits<float>::Max();
    bool bCloseDamageHitboxEnabled = false;
};
