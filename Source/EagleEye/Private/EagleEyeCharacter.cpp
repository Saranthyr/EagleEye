// Copyright Epic Games, Inc. All Rights Reserved.

#include "EagleEyeCharacter.h"
#include "AI/BotCharacter.h"
#include "AI/BotDamageProjectile.h"
#include "Engine/LocalPlayer.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SphereComponent.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerController.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputCoreTypes.h"
#include "InputActionValue.h"
#include "Kismet/GameplayStatics.h"
#include "MyHUD.h"

DEFINE_LOG_CATEGORY(LogTemplateCharacter);

namespace
{
    AMyHUD* GetExistingDetectionSettingsHud(const AEagleEyeCharacter* Character)
    {
        const APlayerController* PlayerController = Character ? Cast<APlayerController>(Character->GetController()) : nullptr;
        return PlayerController ? Cast<AMyHUD>(PlayerController->GetHUD()) : nullptr;
    }

    AMyHUD* EnsureDetectionSettingsHud(AEagleEyeCharacter* Character)
    {
        APlayerController* PlayerController = Character ? Cast<APlayerController>(Character->GetController()) : nullptr;
        if (!PlayerController)
        {
            UE_LOG(LogTemplateCharacter, Warning, TEXT("Detection settings menu needs a player controller."));
            return nullptr;
        }

        if (AMyHUD* ExistingHud = Cast<AMyHUD>(PlayerController->GetHUD()))
        {
            UE_LOG(LogTemplateCharacter, Log, TEXT("Detection settings menu using existing AMyHUD: %s"),
                *GetNameSafe(ExistingHud));
            return ExistingHud;
        }

        UE_LOG(LogTemplateCharacter, Log, TEXT("Detection settings menu replacing HUD %s with AMyHUD."),
            *GetNameSafe(PlayerController->GetHUD()));
        PlayerController->ClientSetHUD(AMyHUD::StaticClass());

        AMyHUD* NewHud = Cast<AMyHUD>(PlayerController->GetHUD());
        if (!NewHud)
        {
            UE_LOG(LogTemplateCharacter, Warning, TEXT("Detection settings menu could not create AMyHUD. Current HUD: %s"),
                *GetNameSafe(PlayerController->GetHUD()));
        }
        else
        {
            UE_LOG(LogTemplateCharacter, Log, TEXT("Detection settings menu created AMyHUD: %s"),
                *GetNameSafe(NewHud));
        }
        return NewHud;
    }
}

//////////////////////////////////////////////////////////////////////////
// AEagleEyeCharacter

AEagleEyeCharacter::AEagleEyeCharacter()
{
	PrimaryActorTick.bCanEverTick = true;

	// Set size for collision capsule
	GetCapsuleComponent()->InitCapsuleSize(42.f, 96.0f);
		
	// Don't rotate when the controller rotates. Let that just affect the camera.
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	// Configure character movement
	GetCharacterMovement()->bOrientRotationToMovement = true; // Character moves in the direction of input...	
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 500.0f, 0.0f); // ...at this rotation rate

	// Note: For faster iteration times these variables, and many more, can be tweaked in the Character Blueprint
	// instead of recompiling to adjust them
	GetCharacterMovement()->JumpZVelocity = 700.f;
	GetCharacterMovement()->AirControl = 0.35f;
	GetCharacterMovement()->MaxWalkSpeed = 500.f;
	GetCharacterMovement()->MinAnalogWalkSpeed = 20.f;
	GetCharacterMovement()->BrakingDecelerationWalking = 2000.f;
	GetCharacterMovement()->BrakingDecelerationFalling = 1500.0f;

	// Create a camera boom (pulls in towards the player if there is a collision)
	// CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	// CameraBoom->SetupAttachment(RootComponent);
	// CameraBoom->TargetArmLength = 400.0f; // The camera follows at this distance behind the character	
	// CameraBoom->bUsePawnControlRotation = true; // Rotate the arm based on the controller

	// Create a follow camera
	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera")); // Attach the camera to the end of the boom and let the boom adjust to match the controller orientation
	FollowCamera->bUsePawnControlRotation = true; // Camera does not rotate relative to arm
	FollowCamera->SetFieldOfView(CameraFieldOfView);
	FollowCamera->SetupAttachment(GetMesh(), "head");

	MeleeHitbox = CreateDefaultSubobject<USphereComponent>(TEXT("MeleeHitbox"));
	MeleeHitbox->SetupAttachment(FollowCamera);
	MeleeHitbox->InitSphereRadius(110.f);
	MeleeHitbox->SetRelativeLocation(FVector(140.f, 0.f, 0.f));
	MeleeHitbox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MeleeHitbox->SetCollisionObjectType(ECC_WorldDynamic);
	MeleeHitbox->SetCollisionResponseToAllChannels(ECR_Ignore);
	MeleeHitbox->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	MeleeHitbox->SetGenerateOverlapEvents(false);

	// Note: The skeletal mesh and anim blueprint references on the Mesh component (inherited from Character)
	// are set in the derived blueprint asset named ThirdPersonCharacter (to avoid direct content references in C++)
}

void AEagleEyeCharacter::BeginPlay()
{
	// Call the base class  
	Super::BeginPlay();

	if (MeleeHitbox && !MeleeHitbox->OnComponentBeginOverlap.IsAlreadyBound(this, &AEagleEyeCharacter::HandleMeleeHitboxOverlap))
	{
		MeleeHitbox->OnComponentBeginOverlap.AddDynamic(this, &AEagleEyeCharacter::HandleMeleeHitboxOverlap);
	}
	SetMeleeHitboxEnabled(false);

	BotKillCount = 0;
	bRestartAfterDeathRequested = false;
	ResetHealth();

	if (APlayerController* PlayerController = Cast<APlayerController>(GetController()))
	{
		if (PlayerController->PlayerCameraManager)
		{
			PlayerController->PlayerCameraManager->ViewPitchMin = CameraPitchMin;
			PlayerController->PlayerCameraManager->ViewPitchMax = CameraPitchMax;
		}
	}
}

void AEagleEyeCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (bIsDead)
	{
		TryRestartAfterDeathInput();
		return;
	}

	ApplyRealisticCamera(DeltaSeconds);
	TickMeleeHitbox();
}

void AEagleEyeCharacter::Landed(const FHitResult& Hit)
{
	Super::Landed(Hit);

	if (bEnableRealisticCamera && LastFallingSpeed > 300.f)
	{
		const float LandingImpulse = FMath::Clamp((LastFallingSpeed - 300.f) * LandingImpulseScale, 0.f, MaxLandingImpulse);
		AddCameraImpulse(FVector(0.f, 0.f, -LandingImpulse), FRotator(LandingImpulse * 0.55f, 0.f, 0.f));
	}

	LastFallingSpeed = 0.f;
}

float AEagleEyeCharacter::TakeDamage(
	float DamageAmount,
	FDamageEvent const& DamageEvent,
	AController* EventInstigator,
	AActor* DamageCauser)
{
	const float DamageAppliedByParent = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
	const float DamageToApply = DamageAppliedByParent > 0.f ? DamageAppliedByParent : DamageAmount;
	return ApplyHealthDamage(DamageToApply, EventInstigator, DamageCauser);
}

float AEagleEyeCharacter::ApplyHealthDamage(float DamageAmount, AController* EventInstigator, AActor* DamageCauser)
{
	if (bIsDead || DamageAmount <= 0.f)
	{
		return 0.f;
	}

	const float PreviousHealth = CurrentHealth;
	CurrentHealth = FMath::Clamp(CurrentHealth - DamageAmount, 0.f, MaxHealth);
	const float AppliedDamage = PreviousHealth - CurrentHealth;

	if (AppliedDamage <= 0.f)
	{
		return 0.f;
	}

	OnHealthChanged.Broadcast(CurrentHealth, MaxHealth);

	if (bEnableRealisticCamera)
	{
		const float DamageImpulse = FMath::Clamp(AppliedDamage * DamageImpulseScale, 0.f, MaxDamageImpulse);
		if (DamageImpulse > 0.f)
		{
			const FVector DamageDirection = DamageCauser
				? (DamageCauser->GetActorLocation() - GetActorLocation()).GetSafeNormal()
				: -GetActorForwardVector();
			const FVector LocalDamageDirection = GetActorTransform().InverseTransformVectorNoScale(DamageDirection);
			const float SideImpulse = FMath::Clamp(LocalDamageDirection.Y, -1.f, 1.f);

			AddCameraImpulse(
				FVector(-DamageImpulse * 0.45f, -SideImpulse * DamageImpulse * 0.25f, DamageImpulse * 0.12f),
				FRotator(-DamageImpulse, SideImpulse * DamageImpulse * 0.35f, -SideImpulse * DamageImpulse));
		}
	}

	if (CurrentHealth <= 0.f)
	{
		HandleDeath(EventInstigator, DamageCauser);
	}

	return AppliedDamage;
}

float AEagleEyeCharacter::Heal(float HealAmount)
{
	if (bIsDead || HealAmount <= 0.f)
	{
		return 0.f;
	}

	const float PreviousHealth = CurrentHealth;
	CurrentHealth = FMath::Clamp(CurrentHealth + HealAmount, 0.f, MaxHealth);
	const float AppliedHeal = CurrentHealth - PreviousHealth;
	if (AppliedHeal > 0.f)
	{
		OnHealthChanged.Broadcast(CurrentHealth, MaxHealth);
	}

	return AppliedHeal;
}

void AEagleEyeCharacter::ResetHealth()
{
	MaxHealth = FMath::Max(1.f, MaxHealth);
	CurrentHealth = MaxHealth;
	bIsDead = false;
	bRestartAfterDeathRequested = false;
	DeathTimeSeconds = -TNumericLimits<float>::Max();

	DisableDeathRagdoll();

	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveComp->SetMovementMode(MOVE_Walking);
	}

	OnHealthChanged.Broadcast(CurrentHealth, MaxHealth);
}

void AEagleEyeCharacter::RegisterBotKill(ABotCharacter* KilledBot)
{
	if (!IsValid(KilledBot))
	{
		return;
	}

	++BotKillCount;
	UE_LOG(LogTemplateCharacter, Log, TEXT("%s killed bot %s. BotKillCount=%d"),
		*GetNameSafe(this),
		*GetNameSafe(KilledBot),
		BotKillCount);
}

void AEagleEyeCharacter::RestartAfterDeath()
{
	if (!bIsDead || bRestartAfterDeathRequested)
	{
		return;
	}

	const FString LevelName = UGameplayStatics::GetCurrentLevelName(this, true);
	if (LevelName.IsEmpty())
	{
		return;
	}

	bRestartAfterDeathRequested = true;
	UGameplayStatics::OpenLevel(this, FName(*LevelName));
}

bool AEagleEyeCharacter::TryMeleeAttack()
{
	if (!bEnableMeleeAttack || bIsDead || MeleeDamage <= 0.f)
	{
		return false;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	const float CurrentTime = World->GetTimeSeconds();
	if (CurrentTime - LastMeleeAttackTime < MeleeCooldownSeconds)
	{
		return false;
	}

	LastMeleeAttackTime = CurrentTime;
	LastMeleeDamageActor.Reset();
	MeleeHitboxDisableTime = CurrentTime + FMath::Max(0.01f, MeleeActiveSeconds);
	SetMeleeHitboxEnabled(true);
	TickMeleeHitbox();
	return true;
}

bool AEagleEyeCharacter::TryProjectileAttack()
{
	if (!bEnableProjectileAttack || bIsDead || ProjectileDamage <= 0.f)
	{
		return false;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	const float CurrentTime = World->GetTimeSeconds();
	if (CurrentTime - LastProjectileAttackTime < ProjectileAttackCooldownSeconds)
	{
		return false;
	}

	TSubclassOf<ABotDamageProjectile> ClassToSpawn = ProjectileClass;
	if (!ClassToSpawn)
	{
		ClassToSpawn = ABotDamageProjectile::StaticClass();
	}

	FVector ViewLocation = GetActorLocation();
	FRotator ViewRotation = GetActorRotation();
	if (FollowCamera)
	{
		ViewLocation = FollowCamera->GetComponentLocation();
		ViewRotation = FollowCamera->GetComponentRotation();
	}
	else if (Controller)
	{
		ViewRotation = Controller->GetControlRotation();
	}

	const FVector AimDirection = ViewRotation.Vector().GetSafeNormal();
	if (AimDirection.IsNearlyZero())
	{
		return false;
	}

	const FVector SpawnLocation = ViewLocation + ViewRotation.RotateVector(ProjectileSpawnOffset);
	const FVector AimLocation = ViewLocation + AimDirection * FMath::Max(1.f, ProjectileAttackRange);
	const FVector FireDirection = (AimLocation - SpawnLocation).GetSafeNormal();
	if (FireDirection.IsNearlyZero())
	{
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;
	SpawnParams.Instigator = this;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	ABotDamageProjectile* Projectile = World->SpawnActor<ABotDamageProjectile>(
		ClassToSpawn,
		SpawnLocation,
		FireDirection.Rotation(),
		SpawnParams);
	if (!Projectile)
	{
		return false;
	}

	Projectile->SetDamage(ProjectileDamage);
	Projectile->SetProjectileSpeed(ProjectileSpeed);
	Projectile->FireInDirection(FireDirection);
	LastProjectileAttackTime = CurrentTime;
	return true;
}

void AEagleEyeCharacter::SetMeleeHitboxEnabled(bool bEnabled)
{
	bMeleeHitboxEnabled = bEnabled;

	if (!MeleeHitbox)
	{
		return;
	}

	MeleeHitbox->SetCollisionEnabled(bEnabled ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
	MeleeHitbox->SetGenerateOverlapEvents(bEnabled);
}

void AEagleEyeCharacter::HandleDeath(AController* EventInstigator, AActor* DamageCauser)
{
	if (bIsDead)
	{
		return;
	}

	bIsDead = true;
	DeathTimeSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	bRestartAfterDeathRequested = false;
	SetMeleeHitboxEnabled(false);

	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveComp->DisableMovement();
	}

	EnableDeathRagdoll();

	OnPlayerDied.Broadcast();
	K2_OnDeath(EventInstigator, DamageCauser);

	UE_LOG(LogTemplateCharacter, Log, TEXT("%s died. DamageCauser=%s"),
		*GetNameSafe(this),
		*GetNameSafe(DamageCauser));
}

void AEagleEyeCharacter::EnableDeathRagdoll()
{
	if (!bEnableRagdollOnDeath)
	{
		return;
	}

	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	if (USkeletalMeshComponent* CharacterMesh = GetMesh())
	{
		CharacterMesh->SetCollisionProfileName(TEXT("Ragdoll"));
		CharacterMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		CharacterMesh->SetAllBodiesSimulatePhysics(true);
		CharacterMesh->SetSimulatePhysics(true);
		CharacterMesh->WakeAllRigidBodies();
		CharacterMesh->bBlendPhysics = true;
	}
}

void AEagleEyeCharacter::DisableDeathRagdoll()
{
	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	}

	if (USkeletalMeshComponent* CharacterMesh = GetMesh())
	{
		CharacterMesh->bBlendPhysics = false;
		CharacterMesh->SetSimulatePhysics(false);
		CharacterMesh->SetAllBodiesSimulatePhysics(false);
	}
}

//////////////////////////////////////////////////////////////////////////
// Input

void AEagleEyeCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	// Add Input Mapping Context
	if (APlayerController* PlayerController = Cast<APlayerController>(GetController()))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PlayerController->GetLocalPlayer()))
		{
			Subsystem->AddMappingContext(DefaultMappingContext, 0);
		}
	}
	
	// Set up action bindings
	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent)) {
		
		// Jumping
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Started, this, &ACharacter::Jump);
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Completed, this, &ACharacter::StopJumping);

		// Moving
		EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AEagleEyeCharacter::Move);

		// Looking
		EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &AEagleEyeCharacter::Look);

		if (MeleeAttackAction)
		{
			EnhancedInputComponent->BindAction(MeleeAttackAction, ETriggerEvent::Started, this, &AEagleEyeCharacter::MeleeAttackInput);
		}

		if (ProjectileAttackAction)
		{
			EnhancedInputComponent->BindAction(ProjectileAttackAction, ETriggerEvent::Started, this, &AEagleEyeCharacter::ProjectileAttackInput);
		}

	}
	else
	{
		UE_LOG(LogTemplateCharacter, Error, TEXT("'%s' Failed to find an Enhanced Input component! This template is built to use the Enhanced Input system. If you intend to use the legacy system, then you will need to update this C++ file."), *GetNameSafe(this));
	}
}

void AEagleEyeCharacter::ToggleDetectionSettingsMenuInput()
{
	UE_LOG(LogTemplateCharacter, Log, TEXT("ToggleDetectionSettingsMenuInput called on %s."), *GetNameSafe(this));
	if (AMyHUD* Hud = EnsureDetectionSettingsHud(this))
	{
		Hud->ToggleDetectionSettingsMenu();
	}
}

void AEagleEyeCharacter::DetectionSettingsMenuUpInput()
{
	if (AMyHUD* Hud = EnsureDetectionSettingsHud(this))
	{
		Hud->HandleDetectionSettingsMenuUp();
	}
}

void AEagleEyeCharacter::DetectionSettingsMenuDownInput()
{
	if (AMyHUD* Hud = EnsureDetectionSettingsHud(this))
	{
		Hud->HandleDetectionSettingsMenuDown();
	}
}

void AEagleEyeCharacter::DetectionSettingsMenuLeftInput()
{
	if (AMyHUD* Hud = EnsureDetectionSettingsHud(this))
	{
		Hud->HandleDetectionSettingsMenuLeft();
	}
}

void AEagleEyeCharacter::DetectionSettingsMenuRightInput()
{
	if (AMyHUD* Hud = EnsureDetectionSettingsHud(this))
	{
		Hud->HandleDetectionSettingsMenuRight();
	}
}

void AEagleEyeCharacter::DetectionSettingsMenuConfirmInput()
{
	if (AMyHUD* Hud = EnsureDetectionSettingsHud(this))
	{
		Hud->HandleDetectionSettingsMenuConfirm();
	}
}

void AEagleEyeCharacter::DetectionSettingsMenuCancelInput()
{
	if (AMyHUD* Hud = GetExistingDetectionSettingsHud(this))
	{
		Hud->HandleDetectionSettingsMenuCancel();
	}
}

void AEagleEyeCharacter::Move(const FInputActionValue& Value)
{
	if (bIsDead)
	{
		return;
	}

	if (const AMyHUD* Hud = GetExistingDetectionSettingsHud(this); Hud && Hud->IsDetectionSettingsMenuOpen())
	{
		return;
	}

	// input is a Vector2D
	FVector2D MovementVector = Value.Get<FVector2D>();

	if (Controller != nullptr)
	{
		// find out which way is forward
		const FRotator Rotation = Controller->GetControlRotation();
		const FRotator YawRotation(0, Rotation.Yaw, 0);

		// get forward vector
		const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
	
		// get right vector 
		const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

		// add movement 
		AddMovementInput(ForwardDirection, MovementVector.Y);
		AddMovementInput(RightDirection, MovementVector.X);
	}
}

void AEagleEyeCharacter::Look(const FInputActionValue& Value)
{
	if (bIsDead)
	{
		return;
	}

	if (const AMyHUD* Hud = GetExistingDetectionSettingsHud(this); Hud && Hud->IsDetectionSettingsMenuOpen())
	{
		return;
	}

	// input is a Vector2D
	PendingLookInput += Value.Get<FVector2D>();
}

void AEagleEyeCharacter::ApplyRealisticCamera(float DeltaSeconds)
{
	ApplySmoothedLookInput(DeltaSeconds);

	if (!FollowCamera)
	{
		return;
	}

	FollowCamera->SetFieldOfView(CameraFieldOfView);

	if (!bEnableRealisticCamera)
	{
		FollowCamera->ClearAdditiveOffset();
		return;
	}

	UpdateCameraMotion(DeltaSeconds);
}

void AEagleEyeCharacter::ApplySmoothedLookInput(float DeltaSeconds)
{
	if (bIsDead || !Controller)
	{
		PendingLookInput = FVector2D::ZeroVector;
		SmoothedLookInput = FVector2D::ZeroVector;
		return;
	}

	if (!bEnableRealisticCamera || LookInputSmoothingSpeed <= 0.f || DeltaSeconds <= 0.f)
	{
		SmoothedLookInput = PendingLookInput;
	}
	else
	{
		const float Alpha = FMath::Clamp(1.f - FMath::Exp(-LookInputSmoothingSpeed * DeltaSeconds), 0.f, 1.f);
		SmoothedLookInput = FMath::Lerp(SmoothedLookInput, PendingLookInput, Alpha);
	}

	AddControllerYawInput(SmoothedLookInput.X);
	AddControllerPitchInput(SmoothedLookInput.Y);

	FRotator ControlRotation = Controller->GetControlRotation();
	ControlRotation.Pitch = FMath::Clamp(FRotator::NormalizeAxis(ControlRotation.Pitch), CameraPitchMin, CameraPitchMax);
	Controller->SetControlRotation(ControlRotation);

	PendingLookInput = FVector2D::ZeroVector;
}

void AEagleEyeCharacter::UpdateCameraMotion(float DeltaSeconds)
{
	if (!FollowCamera)
	{
		return;
	}

	const UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	const FVector Velocity = GetVelocity();
	const FVector HorizontalVelocity(Velocity.X, Velocity.Y, 0.f);
	const float MaxMoveSpeed = MoveComp ? FMath::Max(1.f, MoveComp->GetMaxSpeed()) : 500.f;
	const float SpeedAlpha = FMath::Clamp(HorizontalVelocity.Size() / MaxMoveSpeed, 0.f, 1.f);
	const bool bGrounded = MoveComp && MoveComp->IsMovingOnGround();

	if (MoveComp && MoveComp->IsFalling())
	{
		LastFallingSpeed = FMath::Max(LastFallingSpeed, FMath::Abs(Velocity.Z));
	}

	const float BobFrequency = bGrounded
		? FMath::Lerp(IdleBobFrequency, WalkBobFrequency, SpeedAlpha)
		: IdleBobFrequency;
	CameraBobPhase = FMath::Fmod(CameraBobPhase + DeltaSeconds * BobFrequency * 2.f * PI, 2.f * PI);

	const float MoveBobScale = bGrounded ? SpeedAlpha : 0.f;
	const float IdleBobScale = bGrounded ? 1.f - SpeedAlpha : 0.f;
	FVector TargetLocation = FVector::ZeroVector;
	TargetLocation.Y = FMath::Sin(CameraBobPhase) * BobHorizontalAmplitude * MoveBobScale;
	TargetLocation.Z =
		FMath::Sin(CameraBobPhase * 2.f) * BobVerticalAmplitude * MoveBobScale +
		FMath::Sin(CameraBobPhase) * IdleBobAmplitude * IdleBobScale;

	const FVector LocalVelocity = GetActorTransform().InverseTransformVectorNoScale(Velocity);
	const float StrafeAlpha = FMath::Clamp(LocalVelocity.Y / MaxMoveSpeed, -1.f, 1.f);

	FRotator TargetRotation = FRotator::ZeroRotator;
	TargetRotation.Pitch = FMath::Clamp(-SmoothedLookInput.Y * LookSwayPitchAmount, -2.f, 2.f);
	TargetRotation.Yaw = FMath::Clamp(SmoothedLookInput.X * LookSwayYawAmount, -1.5f, 1.5f);
	TargetRotation.Roll = FMath::Clamp(
		-SmoothedLookInput.X * LookSwayRollAmount - StrafeAlpha * MoveSwayRollAmount,
		-4.f,
		4.f);

	CameraMotionLocation = FMath::VInterpTo(CameraMotionLocation, TargetLocation, DeltaSeconds, CameraMotionInterpSpeed);
	CameraMotionRotation = FMath::RInterpTo(CameraMotionRotation, TargetRotation, DeltaSeconds, CameraMotionInterpSpeed);
	CameraImpulseLocation = FMath::VInterpTo(CameraImpulseLocation, FVector::ZeroVector, DeltaSeconds, CameraImpulseReturnSpeed);
	CameraImpulseRotation = FMath::RInterpTo(CameraImpulseRotation, FRotator::ZeroRotator, DeltaSeconds, CameraImpulseReturnSpeed);

	const FVector TotalLocation = CameraMotionLocation + CameraImpulseLocation;
	const FRotator TotalRotation = CameraMotionRotation + CameraImpulseRotation;

	FollowCamera->ClearAdditiveOffset();
	FollowCamera->AddAdditiveOffset(FTransform(TotalRotation.Quaternion(), TotalLocation), 0.f);
}

void AEagleEyeCharacter::AddCameraImpulse(const FVector& LocationImpulse, const FRotator& RotationImpulse)
{
	CameraImpulseLocation += LocationImpulse;
	CameraImpulseRotation += RotationImpulse;
}

void AEagleEyeCharacter::MeleeAttackInput()
{
	TryMeleeAttack();
}

void AEagleEyeCharacter::ProjectileAttackInput()
{
	TryProjectileAttack();
}

void AEagleEyeCharacter::TryRestartAfterDeathInput()
{
	if (!bIsDead || bRestartAfterDeathRequested)
	{
		return;
	}

	const UWorld* World = GetWorld();
	if (!World || World->GetTimeSeconds() - DeathTimeSeconds < RestartInputDelayAfterDeathSeconds)
	{
		return;
	}

	const APlayerController* PlayerController = Cast<APlayerController>(GetController());
	if (PlayerController && PlayerController->WasInputKeyJustPressed(EKeys::AnyKey))
	{
		RestartAfterDeath();
	}
}

void AEagleEyeCharacter::TickMeleeHitbox()
{
	if (!bMeleeHitboxEnabled || !MeleeHitbox)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (World && World->GetTimeSeconds() >= MeleeHitboxDisableTime)
	{
		SetMeleeHitboxEnabled(false);
		return;
	}

	TArray<AActor*> OverlappingActors;
	MeleeHitbox->GetOverlappingActors(OverlappingActors, ABotCharacter::StaticClass());
	for (AActor* OverlappingActor : OverlappingActors)
	{
		TryApplyMeleeDamage(OverlappingActor);
	}
}

bool AEagleEyeCharacter::TryApplyMeleeDamage(AActor* TargetActor)
{
	if (!bMeleeHitboxEnabled || !IsValidPlayerDamageTarget(TargetActor) || MeleeDamage <= 0.f)
	{
		return false;
	}

	if (LastMeleeDamageActor.Get() == TargetActor)
	{
		return false;
	}

	const float AppliedDamage = UGameplayStatics::ApplyDamage(
		TargetActor,
		MeleeDamage,
		GetController(),
		this,
		nullptr);

	if (AppliedDamage <= 0.f)
	{
		return false;
	}

	LastMeleeDamageActor = TargetActor;
	return true;
}

bool AEagleEyeCharacter::IsValidPlayerDamageTarget(const AActor* TargetActor) const
{
	if (!IsValid(TargetActor) || TargetActor == this)
	{
		return false;
	}

	return TargetActor->IsA<ABotCharacter>();
}

void AEagleEyeCharacter::HandleMeleeHitboxOverlap(
	UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex,
	bool bFromSweep,
	const FHitResult& SweepResult)
{
	TryApplyMeleeDamage(OtherActor);
}

ABotCharacter* AEagleEyeCharacter::FindNearestBotForRecording() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	ABotCharacter* BestBot = nullptr;
	float BestDistSq = TNumericLimits<float>::Max();
	for (TActorIterator<ABotCharacter> It(World); It; ++It)
	{
		ABotCharacter* Bot = *It;
		if (!IsValid(Bot))
		{
			continue;
		}

		const float DistSq = FVector::DistSquared(GetActorLocation(), Bot->GetActorLocation());
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			BestBot = Bot;
		}
	}

	return BestBot;
}

void AEagleEyeCharacter::StartNearestBotViewportRecording(float FPS, int32 Width, int32 Height)
{
	ABotCharacter* Bot = FindNearestBotForRecording();
	if (!Bot)
	{
		UE_LOG(LogTemplateCharacter, Warning, TEXT("Bot viewport recording: no bot found."));
		return;
	}

	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<ABotCharacter> It(World); It; ++It)
		{
			ABotCharacter* OtherBot = *It;
			if (IsValid(OtherBot) && OtherBot != Bot)
			{
				OtherBot->StopBotViewportRecording();
			}
		}
	}

	Bot->StartBotViewportRecordingWithSettings(FPS, Width, Height);
	UE_LOG(LogTemplateCharacter, Log, TEXT("Bot viewport recording requested for nearest bot %s (%dx%d @ %.2f fps)."),
		*GetNameSafe(Bot),
		FMath::Clamp(Width, 160, 1920),
		FMath::Clamp(Height, 160, 1080),
		FMath::Clamp(FPS, 1.0f, 120.0f));
}

void AEagleEyeCharacter::StopBotViewportRecordings()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	int32 StoppedCount = 0;
	for (TActorIterator<ABotCharacter> It(World); It; ++It)
	{
		ABotCharacter* Bot = *It;
		if (!IsValid(Bot))
		{
			continue;
		}

		Bot->StopBotViewportRecording();
		++StoppedCount;
	}

	UE_LOG(LogTemplateCharacter, Log, TEXT("Bot viewport recording stopped for %d bots."), StoppedCount);
}
