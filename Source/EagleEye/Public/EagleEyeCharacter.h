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

	virtual float TakeDamage(
		float DamageAmount,
		struct FDamageEvent const& DamageEvent,
		class AController* EventInstigator,
		AActor* DamageCauser) override;

	UFUNCTION(BlueprintCallable, Exec, Category="Detection|Recording")
	void StartNearestBotViewportRecording(float FPS = 8.0f, int32 Width = 416, int32 Height = 416);

	UFUNCTION(BlueprintCallable, Exec, Category="Detection|Recording")
	void StopBotViewportRecordings();

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

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Gameplay|Health", meta=(ClampMin="1.0"))
	float MaxHealth = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Gameplay|Death")
	bool bEnableRagdollOnDeath = true;

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

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Gameplay|Health")
	float CurrentHealth = 100.f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Gameplay|Health")
	bool bIsDead = false;

	TWeakObjectPtr<AActor> LastMeleeDamageActor;
	float LastMeleeAttackTime = -TNumericLimits<float>::Max();
	float MeleeHitboxDisableTime = -TNumericLimits<float>::Max();
	float LastProjectileAttackTime = -TNumericLimits<float>::Max();
	bool bMeleeHitboxEnabled = false;

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

