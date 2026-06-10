#include "AI/BotCharacter.h"

#include "AI/BotDamageProjectile.h"
#include "AI/BotAIController.h"
#include "BrainComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SphereComponent.h"
#include "EagleEyeCharacter.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "MyActorComponent.h"

ABotCharacter::ABotCharacter()
{
    PrimaryActorTick.bCanEverTick = true;

    bUseControllerRotationPitch = false;
    bUseControllerRotationYaw = false;
    bUseControllerRotationRoll = false;

    AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
    AIControllerClass = ABotAIController::StaticClass();
    DetectionComponent = CreateDefaultSubobject<UMyActorComponent>(TEXT("DetectionComponent"));
    DetectionComponent->SetupAttachment(RootComponent);
    DetectionComponent->SetUseOwnerCameraCapture(true);
    DetectionComponent->SetUseSharedVisionModel(true);

    CloseDamageHitbox = CreateDefaultSubobject<USphereComponent>(TEXT("CloseDamageHitbox"));
    CloseDamageHitbox->SetupAttachment(RootComponent);
    CloseDamageHitbox->InitSphereRadius(130.f);
    CloseDamageHitbox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    CloseDamageHitbox->SetCollisionObjectType(ECC_WorldDynamic);
    CloseDamageHitbox->SetCollisionResponseToAllChannels(ECR_Ignore);
    CloseDamageHitbox->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
    CloseDamageHitbox->SetGenerateOverlapEvents(true);

    ApplyBotMovementSettings();
}

void ABotCharacter::BeginPlay()
{
    Super::BeginPlay();

    ResetHealth();

    ApplyBotMovementSettings();

    if (DetectionComponent)
    {
        DetectionComponent->SetSharedVisionModelHost(false);
        DetectionComponent->SetUseSharedVisionModel(true);
    }

    if (CloseDamageHitbox && !CloseDamageHitbox->OnComponentBeginOverlap.IsAlreadyBound(this, &ABotCharacter::HandleCloseDamageOverlap))
    {
        CloseDamageHitbox->OnComponentBeginOverlap.AddDynamic(this, &ABotCharacter::HandleCloseDamageOverlap);
    }
    SetCloseDamageHitboxEnabled(bEnableCloseHitboxDamage);
}

void ABotCharacter::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    if (bIsDead)
    {
        return;
    }

    TickCloseDamageHitbox();
}

float ABotCharacter::TakeDamage(
    float DamageAmount,
    FDamageEvent const& DamageEvent,
    AController* EventInstigator,
    AActor* DamageCauser)
{
    const float DamageAppliedByParent = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
    const float DamageToApply = DamageAppliedByParent > 0.f ? DamageAppliedByParent : DamageAmount;
    if (DamageToApply < 0.f)
    {
        return Heal(-DamageToApply);
    }
    return ApplyHealthDamage(DamageToApply, EventInstigator, DamageCauser);
}

bool ABotCharacter::IsFlyingBot() const
{
    return !IsWalkingBot();
}

bool ABotCharacter::IsWalkingBot() const
{
    if (LocomotionMode == EBotLocomotionMode::Walking)
    {
        return true;
    }

    const UCharacterMovementComponent* MoveComp = GetCharacterMovement();
    return MoveComp &&
        (MoveComp->DefaultLandMovementMode == MOVE_Walking ||
            MoveComp->DefaultLandMovementMode == MOVE_NavWalking);
}

void ABotCharacter::ApplyBotMovementSettings()
{
    UCharacterMovementComponent* MoveComp = GetCharacterMovement();
    if (!MoveComp)
    {
        return;
    }

    MoveComp->bOrientRotationToMovement = bOrientRotationToMovement;
    MoveComp->bUseControllerDesiredRotation = false;
    MoveComp->RotationRate = MovementRotationRate;

    const bool bUseWalkingMovement =
        LocomotionMode == EBotLocomotionMode::Walking ||
        (HasActorBegunPlay() &&
            (MoveComp->DefaultLandMovementMode == MOVE_Walking ||
                MoveComp->DefaultLandMovementMode == MOVE_NavWalking));

    if (!bUseWalkingMovement)
    {
        MoveComp->DefaultLandMovementMode = MOVE_Flying;
        MoveComp->GravityScale = FlyingGravityScale;
        MoveComp->MaxFlySpeed = FlyingMaxSpeed;
        MoveComp->BrakingDecelerationFlying = FlyingBrakingDeceleration;

        if (HasActorBegunPlay())
        {
            MoveComp->SetMovementMode(MOVE_Flying);
        }
        return;
    }

    LocomotionMode = EBotLocomotionMode::Walking;
    MoveComp->DefaultLandMovementMode = MOVE_Walking;
    MoveComp->GravityScale = WalkingGravityScale;
    MoveComp->MaxWalkSpeed = WalkingMaxSpeed;
    MoveComp->BrakingDecelerationWalking = WalkingBrakingDeceleration;

    if (HasActorBegunPlay())
    {
        MoveComp->SetMovementMode(MOVE_Walking);
    }
}

void ABotCharacter::StartBotViewportRecording()
{
    if (DetectionComponent)
    {
        DetectionComponent->SetRecordOwnerCameraCaptureVideo(true);
    }
}

void ABotCharacter::StartBotViewportRecordingWithSettings(float FPS, int32 Width, int32 Height)
{
    if (DetectionComponent)
    {
        DetectionComponent->SetCaptureFPS(FPS);
        DetectionComponent->SetCaptureResolution(Width, Height);
        DetectionComponent->SetRecordOwnerCameraWhenDetectionSkipped(true);
        DetectionComponent->SetRecordOwnerCameraCaptureVideo(true);
    }
}

void ABotCharacter::StopBotViewportRecording()
{
    if (DetectionComponent)
    {
        DetectionComponent->SetRecordOwnerCameraCaptureVideo(false);
    }
}

bool ABotCharacter::TryThrowProjectileAtActor(AActor* TargetActor)
{
    if (bIsDead || !bEnableProjectileAttack || !IsValidDamageTarget(TargetActor))
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

    const FVector TargetLocation = TargetActor->GetActorLocation();
    const float DistanceSq = FVector::DistSquared(GetActorLocation(), TargetLocation);
    if (ProjectileAttackRange > 0.f && DistanceSq > FMath::Square(ProjectileAttackRange))
    {
        return false;
    }

    TSubclassOf<ABotDamageProjectile> ClassToSpawn = ProjectileClass;
    if (!ClassToSpawn)
    {
        ClassToSpawn = ABotDamageProjectile::StaticClass();
    }

    const FVector SpawnLocation = GetActorLocation() + GetActorRotation().RotateVector(ProjectileSpawnOffset);
    const FVector AimLocation = TargetLocation + FVector(0.f, 0.f, 40.f);
    const FVector Direction = (AimLocation - SpawnLocation).GetSafeNormal();
    if (Direction.IsNearlyZero())
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
        Direction.Rotation(),
        SpawnParams);
    if (!Projectile)
    {
        return false;
    }

    const bool bHealingProjectile = IsCloseDamageHealing() && TargetActor->IsA<ABotCharacter>();
    Projectile->SetDamage(bHealingProjectile ? -FMath::Abs(ProjectileDamage) : ProjectileDamage);
    Projectile->SetProjectileSpeed(ProjectileSpeed);
    Projectile->FireInDirection(Direction);
    LastProjectileAttackTime = CurrentTime;
    return true;
}

bool ABotCharacter::TryThrowProjectileAtLocation(const FVector& TargetLocation)
{
    if (bIsDead || !bEnableProjectileAttack)
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

    const float DistanceSq = FVector::DistSquared(GetActorLocation(), TargetLocation);
    if (ProjectileAttackRange > 0.f && DistanceSq > FMath::Square(ProjectileAttackRange))
    {
        return false;
    }

    TSubclassOf<ABotDamageProjectile> ClassToSpawn = ProjectileClass;
    if (!ClassToSpawn)
    {
        ClassToSpawn = ABotDamageProjectile::StaticClass();
    }

    const FVector SpawnLocation = GetActorLocation() + GetActorRotation().RotateVector(ProjectileSpawnOffset);
    const FVector Direction = (TargetLocation - SpawnLocation).GetSafeNormal();
    if (Direction.IsNearlyZero())
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
        Direction.Rotation(),
        SpawnParams);
    if (!Projectile)
    {
        return false;
    }

    Projectile->SetDamage(ProjectileDamage);
    Projectile->SetProjectileSpeed(ProjectileSpeed);
    Projectile->FireInDirection(Direction);
    LastProjectileAttackTime = CurrentTime;
    return true;
}

bool ABotCharacter::ThrowProjectileAtPlayer()
{
    APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0);
    return TryThrowProjectileAtActor(PlayerPawn);
}

void ABotCharacter::SetCloseDamageHitboxEnabled(bool bEnabled)
{
    bCloseDamageHitboxEnabled = bEnabled && !bIsDead;

    if (!CloseDamageHitbox)
    {
        return;
    }

    CloseDamageHitbox->SetCollisionEnabled(bCloseDamageHitboxEnabled ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
    CloseDamageHitbox->SetGenerateOverlapEvents(bCloseDamageHitboxEnabled);
}

void ABotCharacter::TickCloseDamageHitbox()
{
    if (bIsDead || !bCloseDamageHitboxEnabled || !CloseDamageHitbox)
    {
        return;
    }

    TArray<AActor*> OverlappingActors;
    CloseDamageHitbox->GetOverlappingActors(
        OverlappingActors,
        IsCloseDamageHealing() ? ABotCharacter::StaticClass() : AEagleEyeCharacter::StaticClass());
    for (AActor* OverlappingActor : OverlappingActors)
    {
        TryApplyCloseDamage(OverlappingActor);
    }
}

bool ABotCharacter::TryApplyCloseDamage(AActor* TargetActor)
{
    if (bIsDead || !bCloseDamageHitboxEnabled || !IsValidDamageTarget(TargetActor) || FMath::IsNearlyZero(CloseDamage))
    {
        return false;
    }

    UWorld* World = GetWorld();
    if (!World)
    {
        return false;
    }

    const float CurrentTime = World->GetTimeSeconds();
    if (LastCloseDamageActor.Get() == TargetActor && CurrentTime - LastCloseDamageTime < CloseDamageCooldownSeconds)
    {
        return false;
    }

    if (CloseDamage < 0.f)
    {
        ABotCharacter* TargetBot = Cast<ABotCharacter>(TargetActor);
        const float AppliedHeal = TargetBot ? TargetBot->Heal(-CloseDamage) : 0.f;
        if (AppliedHeal <= 0.f)
        {
            return false;
        }

        LastCloseDamageActor = TargetActor;
        LastCloseDamageTime = CurrentTime;
        return true;
    }

    const float AppliedDamage = UGameplayStatics::ApplyDamage(
        TargetActor,
        CloseDamage,
        GetController(),
        this,
        nullptr);

    if (AppliedDamage <= 0.f)
    {
        return false;
    }

    LastCloseDamageActor = TargetActor;
    LastCloseDamageTime = CurrentTime;
    return true;
}

bool ABotCharacter::IsValidDamageTarget(const AActor* TargetActor) const
{
    if (bIsDead || !IsValid(TargetActor) || TargetActor == this)
    {
        return false;
    }

    if (IsCloseDamageHealing())
    {
        const ABotCharacter* TargetBot = Cast<ABotCharacter>(TargetActor);
        return TargetBot &&
            !TargetBot->IsDead() &&
            TargetBot->GetCurrentHealth() < TargetBot->GetMaxHealth();
    }

    const APawn* Pawn = Cast<APawn>(TargetActor);
    return TargetActor->IsA<AEagleEyeCharacter>() || (Pawn && Pawn->IsPlayerControlled());
}

float ABotCharacter::ApplyHealthDamage(float DamageAmount, AController* EventInstigator, AActor* DamageCauser)
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

    UE_LOG(LogTemp, Verbose, TEXT("%s took %.1f damage. Health %.1f / %.1f. DamageCauser=%s"),
        *GetNameSafe(this),
        AppliedDamage,
        CurrentHealth,
        MaxHealth,
        *GetNameSafe(DamageCauser));

    if (CurrentHealth <= 0.f)
    {
        HandleDeath(EventInstigator, DamageCauser);
    }

    return AppliedDamage;
}

float ABotCharacter::Heal(float HealAmount)
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

float ABotCharacter::GetHealthPercent() const
{
    return MaxHealth > KINDA_SMALL_NUMBER ? CurrentHealth / MaxHealth : 0.f;
}

bool ABotCharacter::NeedsHealing(float HealthPercentThreshold) const
{
    if (bIsDead || CurrentHealth >= MaxHealth)
    {
        return false;
    }

    return GetHealthPercent() < FMath::Clamp(HealthPercentThreshold, 0.f, 1.f);
}

void ABotCharacter::ResetHealth()
{
    MaxHealth = FMath::Max(1.f, MaxHealth);
    CurrentHealth = MaxHealth;
    bIsDead = false;
    SetLifeSpan(0.f);

    DisableDeathRagdoll();
    SetActorEnableCollision(true);

    if (UCapsuleComponent* Capsule = GetCapsuleComponent())
    {
        Capsule->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    }

    if (CloseDamageHitbox)
    {
        SetCloseDamageHitboxEnabled(bEnableCloseHitboxDamage);
    }

    if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
    {
        MoveComp->SetComponentTickEnabled(true);
        MoveComp->SetMovementMode(LocomotionMode == EBotLocomotionMode::Flying ? MOVE_Flying : MOVE_Walking);
    }

    OnHealthChanged.Broadcast(CurrentHealth, MaxHealth);
}

void ABotCharacter::HandleDeath(AController* EventInstigator, AActor* DamageCauser)
{
    if (bIsDead)
    {
        return;
    }

    bIsDead = true;
    SetCloseDamageHitboxEnabled(false);
    StopBotViewportRecording();

    if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
    {
        MoveComp->StopMovementImmediately();
        MoveComp->DisableMovement();
    }

    if (AController* BotController = GetController())
    {
        BotController->StopMovement();

        if (AAIController* AIController = Cast<AAIController>(BotController))
        {
            if (UBrainComponent* Brain = AIController->GetBrainComponent())
            {
                Brain->StopLogic(TEXT("Bot died"));
            }
        }
    }

    if (bEnableRagdollOnDeath)
    {
        EnableDeathRagdoll();
    }
    else if (bDisableCollisionOnDeath)
    {
        SetActorEnableCollision(false);
    }

    OnBotDied.Broadcast();
    K2_OnDeath(EventInstigator, DamageCauser);

    UE_LOG(LogTemp, Log, TEXT("%s died. DamageCauser=%s"),
        *GetNameSafe(this),
        *GetNameSafe(DamageCauser));

    if (bDestroyOnDeath)
    {
        SetLifeSpan(FMath::Max(0.01f, DestroyDelaySeconds));
    }
}

void ABotCharacter::EnableDeathRagdoll()
{
    if (UCapsuleComponent* Capsule = GetCapsuleComponent())
    {
        Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    }

    USkeletalMeshComponent* CharacterMesh = GetMesh();
    if (!CharacterMesh)
    {
        return;
    }

    if (!bHasCachedMeshCollisionSettings)
    {
        CachedMeshCollisionProfileName = CharacterMesh->GetCollisionProfileName();
        CachedMeshCollisionEnabled = CharacterMesh->GetCollisionEnabled();
        bHasCachedMeshCollisionSettings = true;
    }

    if (!RagdollCollisionProfileName.IsNone())
    {
        CharacterMesh->SetCollisionProfileName(RagdollCollisionProfileName);
    }
    CharacterMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    CharacterMesh->SetAllBodiesSimulatePhysics(true);
    CharacterMesh->SetSimulatePhysics(true);
    CharacterMesh->WakeAllRigidBodies();
    CharacterMesh->bBlendPhysics = true;
}

void ABotCharacter::DisableDeathRagdoll()
{
    USkeletalMeshComponent* CharacterMesh = GetMesh();
    if (!CharacterMesh)
    {
        return;
    }

    CharacterMesh->bBlendPhysics = false;
    CharacterMesh->SetSimulatePhysics(false);
    CharacterMesh->SetAllBodiesSimulatePhysics(false);

    if (bHasCachedMeshCollisionSettings)
    {
        CharacterMesh->SetCollisionProfileName(CachedMeshCollisionProfileName);
        CharacterMesh->SetCollisionEnabled(CachedMeshCollisionEnabled);
    }
}

void ABotCharacter::HandleCloseDamageOverlap(
    UPrimitiveComponent* OverlappedComponent,
    AActor* OtherActor,
    UPrimitiveComponent* OtherComp,
    int32 OtherBodyIndex,
    bool bFromSweep,
    const FHitResult& SweepResult)
{
    TryApplyCloseDamage(OtherActor);
}
