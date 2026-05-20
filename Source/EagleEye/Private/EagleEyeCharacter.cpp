// Copyright Epic Games, Inc. All Rights Reserved.

#include "EagleEyeCharacter.h"
#include "AI/BotCharacter.h"
#include "Engine/LocalPlayer.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/Controller.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputActionValue.h"

DEFINE_LOG_CATEGORY(LogTemplateCharacter);

//////////////////////////////////////////////////////////////////////////
// AEagleEyeCharacter

AEagleEyeCharacter::AEagleEyeCharacter()
{
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
	FollowCamera->SetupAttachment(GetMesh(), "head");

	// Note: The skeletal mesh and anim blueprint references on the Mesh component (inherited from Character) 
	// are set in the derived blueprint asset named ThirdPersonCharacter (to avoid direct content references in C++)
}

void AEagleEyeCharacter::BeginPlay()
{
	// Call the base class  
	Super::BeginPlay();

	ResetHealth();
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

	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveComp->SetMovementMode(MOVE_Walking);
	}

	OnHealthChanged.Broadcast(CurrentHealth, MaxHealth);
}

void AEagleEyeCharacter::HandleDeath(AController* EventInstigator, AActor* DamageCauser)
{
	if (bIsDead)
	{
		return;
	}

	bIsDead = true;

	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveComp->DisableMovement();
	}

	if (APlayerController* PlayerController = Cast<APlayerController>(GetController()))
	{
		DisableInput(PlayerController);
	}

	OnPlayerDied.Broadcast();
	K2_OnDeath(EventInstigator, DamageCauser);

	UE_LOG(LogTemplateCharacter, Log, TEXT("%s died. DamageCauser=%s"),
		*GetNameSafe(this),
		*GetNameSafe(DamageCauser));
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

	}
	else
	{
		UE_LOG(LogTemplateCharacter, Error, TEXT("'%s' Failed to find an Enhanced Input component! This template is built to use the Enhanced Input system. If you intend to use the legacy system, then you will need to update this C++ file."), *GetNameSafe(this));
	}
}

void AEagleEyeCharacter::Move(const FInputActionValue& Value)
{
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
	// input is a Vector2D
	FVector2D LookAxisVector = Value.Get<FVector2D>();

	if (Controller != nullptr)
	{
		// add yaw and pitch input to controller
		AddControllerYawInput(LookAxisVector.X);
		AddControllerPitchInput(LookAxisVector.Y);
	}
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
