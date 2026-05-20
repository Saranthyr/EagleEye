#include "AI/BotCharacter.h"

#include "AI/BotDamageProjectile.h"
#include "AI/BotAIController.h"
#include "Components/SphereComponent.h"
#include "EagleEyeCharacter.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "MyActorComponent.h"

ABotCharacter::ABotCharacter()
{
    PrimaryActorTick.bCanEverTick = true;

    AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
    AIControllerClass = ABotAIController::StaticClass();
    DetectionComponent = CreateDefaultSubobject<UMyActorComponent>(TEXT("DetectionComponent"));
    DetectionComponent->SetupAttachment(RootComponent);
    DetectionComponent->SetUseOwnerCameraCapture(true);
    DetectionComponent->SetCaptureFPS(8.f);
    DetectionComponent->SetCaptureResolution(416, 416);
    DetectionComponent->SetMaxOwnerCameraCaptureDistance(8000.f);
    DetectionComponent->SetMaxActiveOwnerCameraCaptures(2);
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

    ApplyDetectionRecordingSettings();

    ApplyBotMovementSettings();

    if (CloseDamageHitbox && !CloseDamageHitbox->OnComponentBeginOverlap.IsAlreadyBound(this, &ABotCharacter::HandleCloseDamageOverlap))
    {
        CloseDamageHitbox->OnComponentBeginOverlap.AddDynamic(this, &ABotCharacter::HandleCloseDamageOverlap);
    }
    SetCloseDamageHitboxEnabled(bEnableCloseHitboxDamage);
}

void ABotCharacter::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    TickCloseDamageHitbox();
}

void ABotCharacter::ApplyBotMovementSettings()
{
    UCharacterMovementComponent* MoveComp = GetCharacterMovement();
    if (!MoveComp)
    {
        return;
    }

    MoveComp->bOrientRotationToMovement = bOrientRotationToMovement;
    MoveComp->RotationRate = MovementRotationRate;

    if (LocomotionMode == EBotLocomotionMode::Flying)
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

    MoveComp->DefaultLandMovementMode = MOVE_Walking;
    MoveComp->GravityScale = WalkingGravityScale;
    MoveComp->MaxWalkSpeed = WalkingMaxSpeed;
    MoveComp->BrakingDecelerationWalking = WalkingBrakingDeceleration;

    if (HasActorBegunPlay())
    {
        MoveComp->SetMovementMode(MOVE_Walking);
    }
}

void ABotCharacter::ApplyDetectionRecordingSettings()
{
    if (!DetectionComponent)
    {
        return;
    }

    if (bOverrideBotViewportRecordingCaptureSettings)
    {
        DetectionComponent->SetCaptureFPS(BotViewportRecordingFPS);
        DetectionComponent->SetCaptureResolution(BotViewportRecordingWidth, BotViewportRecordingHeight);
    }

    DetectionComponent->SetRecordOwnerCameraCaptureVideo(bRecordBotViewportVideo);
    DetectionComponent->SetRecordOwnerCameraWhenDetectionSkipped(bRecordBotViewportEvenWhenDetectionSkipped);
    DetectionComponent->SetOwnerCameraVideoOutputPath(BotViewportVideoOutputPath);
    DetectionComponent->SetOwnerCameraVideoEncoderPath(BotViewportVideoEncoderPath);
    DetectionComponent->SetMaxQueuedOwnerCameraVideoFrames(MaxQueuedBotViewportVideoFrames);
    DetectionComponent->SetApplyOwnerCameraVideoGammaCorrection(bApplyBotViewportVideoGammaCorrection);

    UE_LOG(LogTemp, Log, TEXT("Bot viewport recording %s for %s (record_when_detection_skipped=%s, output=%s)"),
        bRecordBotViewportVideo ? TEXT("enabled") : TEXT("disabled"),
        *GetNameSafe(this),
        bRecordBotViewportEvenWhenDetectionSkipped ? TEXT("true") : TEXT("false"),
        BotViewportVideoOutputPath.IsEmpty() ? TEXT("<default>") : *BotViewportVideoOutputPath);
}

void ABotCharacter::StartBotViewportRecording()
{
    bRecordBotViewportVideo = true;
    ApplyDetectionRecordingSettings();
}

void ABotCharacter::StartBotViewportRecordingWithSettings(float FPS, int32 Width, int32 Height)
{
    bRecordBotViewportVideo = true;
    bRecordBotViewportEvenWhenDetectionSkipped = true;
    bOverrideBotViewportRecordingCaptureSettings = true;
    BotViewportRecordingFPS = FMath::Clamp(FPS, 1.0f, 120.0f);
    BotViewportRecordingWidth = FMath::Clamp(Width, 160, 1920);
    BotViewportRecordingHeight = FMath::Clamp(Height, 160, 1080);
    ApplyDetectionRecordingSettings();
}

void ABotCharacter::StopBotViewportRecording()
{
    bRecordBotViewportVideo = false;
    ApplyDetectionRecordingSettings();
}

bool ABotCharacter::TryThrowProjectileAtActor(AActor* TargetActor)
{
    if (!bEnableProjectileAttack || !IsValidDamageTarget(TargetActor))
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
    bCloseDamageHitboxEnabled = bEnabled;

    if (!CloseDamageHitbox)
    {
        return;
    }

    CloseDamageHitbox->SetCollisionEnabled(bEnabled ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
    CloseDamageHitbox->SetGenerateOverlapEvents(bEnabled);
}

void ABotCharacter::TickCloseDamageHitbox()
{
    if (!bCloseDamageHitboxEnabled || !CloseDamageHitbox)
    {
        return;
    }

    TArray<AActor*> OverlappingActors;
    CloseDamageHitbox->GetOverlappingActors(OverlappingActors, AEagleEyeCharacter::StaticClass());
    for (AActor* OverlappingActor : OverlappingActors)
    {
        TryApplyCloseDamage(OverlappingActor);
    }
}

bool ABotCharacter::TryApplyCloseDamage(AActor* TargetActor)
{
    if (!bCloseDamageHitboxEnabled || !IsValidDamageTarget(TargetActor) || CloseDamage <= 0.f)
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
    if (!IsValid(TargetActor) || TargetActor == this)
    {
        return false;
    }

    const APawn* Pawn = Cast<APawn>(TargetActor);
    return TargetActor->IsA<AEagleEyeCharacter>() || (Pawn && Pawn->IsPlayerControlled());
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
