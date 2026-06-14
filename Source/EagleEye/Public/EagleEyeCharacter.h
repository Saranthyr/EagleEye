// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Logging/LogMacros.h"
#include "EagleEyeCharacter.generated.h"

class USpringArmComponent;
class UCameraComponent;
class UInputMappingContext;
class UInputAction;
class USphereComponent;
class UPrimitiveComponent;
class ABotCharacter;
class ABotDamageProjectile;
struct FInputActionValue;

DECLARE_LOG_CATEGORY_EXTERN(LogTemplateCharacter, Log, All);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FPlayerHealthChangedSignature, float, CurrentHealth, float, MaxHealth);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FPlayerDiedSignature);

UCLASS(config=Game)
class AEagleEyeCharacter : public ACharacter
{
	GENERATED_BODY()

	/** Camera boom positioning the camera behind the character */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Camera, meta = (AllowPrivateAccess = "true"))
	USpringArmComponent* CameraBoom;

	/** Follow camera */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Camera, meta = (AllowPrivateAccess = "true"))
	UCameraComponent* FollowCamera;
	
	/** MappingContext */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	UInputMappingContext* DefaultMappingContext;

	/** Jump Input Action */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	UInputAction* JumpAction;

	/** Move Input Action */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	UInputAction* MoveAction;

	/** Look Input Action */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	UInputAction* LookAction;

	/** Melee attack Input Action */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	UInputAction* MeleeAttackAction;

	/** Projectile attack Input Action */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	UInputAction* ProjectileAttackAction;

public:
	AEagleEyeCharacter();

	virtual void Tick(float DeltaSeconds) override;
	virtual void Landed(const FHitResult& Hit) override;

	virtual float TakeDamage(
		float DamageAmount,
		struct FDamageEvent const& DamageEvent,
		class AController* EventInstigator,
		AActor* DamageCauser) override;

	UFUNCTION(BlueprintCallable, Exec, Category="Detection|Recording")
	void StartNearestBotViewportRecording(float FPS = 8.0f, int32 Width = 640, int32 Height = 640);

	UFUNCTION(BlueprintCallable, Exec, Category="Detection|Recording")
	void StopBotViewportRecordings();

	UFUNCTION(BlueprintCallable, Exec, Category="Detection|Settings")
	void ToggleDetectionSettingsMenuInput();

	UFUNCTION(BlueprintCallable, Category="Detection|Settings")
	void DetectionSettingsMenuUpInput();

	UFUNCTION(BlueprintCallable, Category="Detection|Settings")
	void DetectionSettingsMenuDownInput();

	UFUNCTION(BlueprintCallable, Category="Detection|Settings")
	void DetectionSettingsMenuLeftInput();

	UFUNCTION(BlueprintCallable, Category="Detection|Settings")
	void DetectionSettingsMenuRightInput();

	UFUNCTION(BlueprintCallable, Category="Detection|Settings")
	void DetectionSettingsMenuConfirmInput();

	UFUNCTION(BlueprintCallable, Category="Detection|Settings")
	void DetectionSettingsMenuCancelInput();

	UFUNCTION(BlueprintCallable, Category="Gameplay|Health")
	float ApplyHealthDamage(float DamageAmount, AController* EventInstigator = nullptr, AActor* DamageCauser = nullptr);

	UFUNCTION(BlueprintCallable, Category="Gameplay|Health")
	float Heal(float HealAmount);

	UFUNCTION(BlueprintCallable, Category="Gameplay|Health")
	void ResetHealth();

	UFUNCTION(BlueprintCallable, Category="Gameplay|Attack")
	bool TryMeleeAttack();

	UFUNCTION(BlueprintCallable, Category="Gameplay|Attack")
	bool TryProjectileAttack();

	UFUNCTION(BlueprintCallable, Category="Gameplay|Attack")
	void SetMeleeHitboxEnabled(bool bEnabled);

	UFUNCTION(BlueprintPure, Category="Gameplay|Health")
	float GetCurrentHealth() const { return CurrentHealth; }

	UFUNCTION(BlueprintPure, Category="Gameplay|Health")
	float GetMaxHealth() const { return MaxHealth; }

	UFUNCTION(BlueprintPure, Category="Gameplay|Health")
	bool IsDead() const { return bIsDead; }

	UFUNCTION(BlueprintPure, Category="Gameplay|Kills")
	int32 GetBotKillCount() const { return BotKillCount; }

	UFUNCTION(BlueprintCallable, Category="Gameplay|Kills")
	void RegisterBotKill(ABotCharacter* KilledBot);

	UFUNCTION(BlueprintCallable, Category="Gameplay|Death")
	void RestartAfterDeath();

	UPROPERTY(BlueprintAssignable, Category="Gameplay|Health")
	FPlayerHealthChangedSignature OnHealthChanged;

	UPROPERTY(BlueprintAssignable, Category="Gameplay|Health")
	FPlayerDiedSignature OnPlayerDied;
	

protected:

	/** Called for movement input */
	void Move(const FInputActionValue& Value);

	/** Called for looking input */
	void Look(const FInputActionValue& Value);
	void MeleeAttackInput();
	void ProjectileAttackInput();
	void ApplyRealisticCamera(float DeltaSeconds);
	void ApplySmoothedLookInput(float DeltaSeconds);
	void UpdateCameraMotion(float DeltaSeconds);
	void AddCameraImpulse(const FVector& LocationImpulse, const FRotator& RotationImpulse);

	ABotCharacter* FindNearestBotForRecording() const;
	void TickMeleeHitbox();
	bool TryApplyMeleeDamage(AActor* TargetActor);
	bool IsValidPlayerDamageTarget(const AActor* TargetActor) const;

	UFUNCTION()
	void HandleMeleeHitboxOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex,
		bool bFromSweep,
		const FHitResult& SweepResult);

	UFUNCTION(BlueprintImplementableEvent, Category="Gameplay|Health")
	void K2_OnDeath(AController* EventInstigator, AActor* DamageCauser);

	void HandleDeath(AController* EventInstigator, AActor* DamageCauser);

	void EnableDeathRagdoll();

	void DisableDeathRagdoll();

	void TryRestartAfterDeathInput();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Gameplay|Health", meta=(ClampMin="1.0"))
	float MaxHealth = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Gameplay|Death")
	bool bEnableRagdollOnDeath = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Gameplay|Death", meta=(ClampMin="0.0"))
	float RestartInputDelayAfterDeathSeconds = 0.25f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Gameplay|Attack")
	TObjectPtr<USphereComponent> MeleeHitbox;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Gameplay|Attack|Melee")
	bool bEnableMeleeAttack = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Gameplay|Attack|Melee", meta=(ClampMin="0.0"))
	float MeleeDamage = 25.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Gameplay|Attack|Melee", meta=(ClampMin="0.0"))
	float MeleeCooldownSeconds = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Gameplay|Attack|Melee", meta=(ClampMin="0.01"))
	float MeleeActiveSeconds = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Gameplay|Attack|Projectile")
	bool bEnableProjectileAttack = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Gameplay|Attack|Projectile")
	TSubclassOf<ABotDamageProjectile> ProjectileClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Gameplay|Attack|Projectile", meta=(ClampMin="0.0"))
	float ProjectileDamage = 15.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Gameplay|Attack|Projectile", meta=(ClampMin="0.0"))
	float ProjectileSpeed = 1800.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Gameplay|Attack|Projectile", meta=(ClampMin="0.0"))
	float ProjectileAttackRange = 3000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Gameplay|Attack|Projectile", meta=(ClampMin="0.0"))
	float ProjectileAttackCooldownSeconds = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Gameplay|Attack|Projectile")
	FVector ProjectileSpawnOffset = FVector(80.f, 0.f, -10.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Camera|Realistic")
	bool bEnableRealisticCamera = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Camera|Realistic", meta=(ClampMin="5.0", ClampMax="170.0"))
	float CameraFieldOfView = 85.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Camera|Realistic", meta=(ClampMin="-89.0", ClampMax="0.0"))
	float CameraPitchMin = -75.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Camera|Realistic", meta=(ClampMin="0.0", ClampMax="89.0"))
	float CameraPitchMax = 75.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Camera|Realistic", meta=(ClampMin="0.0"))
	float LookInputSmoothingSpeed = 28.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Camera|Realistic", meta=(ClampMin="0.0"))
	float WalkBobFrequency = 1.75f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Camera|Realistic", meta=(ClampMin="0.0"))
	float IdleBobFrequency = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Camera|Realistic", meta=(ClampMin="0.0"))
	float BobVerticalAmplitude = 1.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Camera|Realistic", meta=(ClampMin="0.0"))
	float BobHorizontalAmplitude = 0.75f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Camera|Realistic", meta=(ClampMin="0.0"))
	float IdleBobAmplitude = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Camera|Realistic", meta=(ClampMin="0.0"))
	float LookSwayPitchAmount = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Camera|Realistic", meta=(ClampMin="0.0"))
	float LookSwayYawAmount = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Camera|Realistic", meta=(ClampMin="0.0"))
	float LookSwayRollAmount = 1.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Camera|Realistic", meta=(ClampMin="0.0"))
	float MoveSwayRollAmount = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Camera|Realistic", meta=(ClampMin="0.0"))
	float CameraMotionInterpSpeed = 12.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Camera|Realistic", meta=(ClampMin="0.0"))
	float CameraImpulseReturnSpeed = 9.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Camera|Realistic", meta=(ClampMin="0.0"))
	float LandingImpulseScale = 0.006f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Camera|Realistic", meta=(ClampMin="0.0"))
	float MaxLandingImpulse = 5.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Camera|Realistic", meta=(ClampMin="0.0"))
	float DamageImpulseScale = 0.035f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Camera|Realistic", meta=(ClampMin="0.0"))
	float MaxDamageImpulse = 4.f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Gameplay|Health")
	float CurrentHealth = 100.f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Gameplay|Health")
	bool bIsDead = false;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Gameplay|Kills")
	int32 BotKillCount = 0;

	TWeakObjectPtr<AActor> LastMeleeDamageActor;
	float LastMeleeAttackTime = -TNumericLimits<float>::Max();
	float MeleeHitboxDisableTime = -TNumericLimits<float>::Max();
	float LastProjectileAttackTime = -TNumericLimits<float>::Max();
	float DeathTimeSeconds = -TNumericLimits<float>::Max();
	float CameraBobPhase = 0.f;
	float LastFallingSpeed = 0.f;
	FVector2D PendingLookInput = FVector2D::ZeroVector;
	FVector2D SmoothedLookInput = FVector2D::ZeroVector;
	FVector CameraMotionLocation = FVector::ZeroVector;
	FVector CameraImpulseLocation = FVector::ZeroVector;
	FRotator CameraMotionRotation = FRotator::ZeroRotator;
	FRotator CameraImpulseRotation = FRotator::ZeroRotator;
	bool bMeleeHitboxEnabled = false;
	bool bRestartAfterDeathRequested = false;

protected:
	// APawn interface
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;
	
	// To add mapping context
	virtual void BeginPlay();

public:
	/** Returns CameraBoom subobject **/
	FORCEINLINE class USpringArmComponent* GetCameraBoom() const { return CameraBoom; }
	/** Returns FollowCamera subobject **/
	FORCEINLINE class UCameraComponent* GetFollowCamera() const { return FollowCamera; }
};

