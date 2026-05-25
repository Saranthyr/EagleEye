#include "AI/BotDamageProjectile.h"

#include "AI/BotCharacter.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EagleEyeCharacter.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/ConstructorHelpers.h"

ABotDamageProjectile::ABotDamageProjectile()
{
    PrimaryActorTick.bCanEverTick = false;

    CollisionComponent = CreateDefaultSubobject<USphereComponent>(TEXT("CollisionComponent"));
    CollisionComponent->InitSphereRadius(16.f);
    CollisionComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    CollisionComponent->SetCollisionObjectType(ECC_WorldDynamic);
    CollisionComponent->SetCollisionResponseToAllChannels(ECR_Block);
    CollisionComponent->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
    CollisionComponent->SetGenerateOverlapEvents(true);
    CollisionComponent->OnComponentBeginOverlap.AddDynamic(this, &ABotDamageProjectile::HandleProjectileOverlap);
    CollisionComponent->OnComponentHit.AddDynamic(this, &ABotDamageProjectile::HandleProjectileHit);
    RootComponent = CollisionComponent;

    MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComponent"));
    MeshComponent->SetupAttachment(CollisionComponent);
    MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    MeshComponent->SetRelativeScale3D(FVector(0.16f));

    static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMeshFinder(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    if (SphereMeshFinder.Succeeded())
    {
        MeshComponent->SetStaticMesh(SphereMeshFinder.Object);
    }

    ProjectileMovementComponent = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("ProjectileMovementComponent"));
    ProjectileMovementComponent->UpdatedComponent = CollisionComponent;
    ProjectileMovementComponent->InitialSpeed = 1400.f;
    ProjectileMovementComponent->MaxSpeed = 1400.f;
    ProjectileMovementComponent->bRotationFollowsVelocity = true;
    ProjectileMovementComponent->bShouldBounce = false;
    ProjectileMovementComponent->ProjectileGravityScale = 0.15f;
}

void ABotDamageProjectile::BeginPlay()
{
    Super::BeginPlay();

    SetLifeSpan(LifeSeconds);

    if (CollisionComponent)
    {
        CollisionComponent->IgnoreActorWhenMoving(GetOwner(), true);
        CollisionComponent->IgnoreActorWhenMoving(GetInstigator(), true);
    }
}

void ABotDamageProjectile::FireInDirection(const FVector& Direction)
{
    if (!ProjectileMovementComponent)
    {
        return;
    }

    const FVector SafeDirection = Direction.GetSafeNormal();
    ProjectileMovementComponent->Velocity = SafeDirection * ProjectileMovementComponent->InitialSpeed;
}

void ABotDamageProjectile::SetProjectileSpeed(float NewSpeed)
{
    if (!ProjectileMovementComponent)
    {
        return;
    }

    const float ClampedSpeed = FMath::Max(0.f, NewSpeed);
    ProjectileMovementComponent->InitialSpeed = ClampedSpeed;
    ProjectileMovementComponent->MaxSpeed = ClampedSpeed;

    if (!ProjectileMovementComponent->Velocity.IsNearlyZero())
    {
        ProjectileMovementComponent->Velocity = ProjectileMovementComponent->Velocity.GetSafeNormal() * ClampedSpeed;
    }
}

void ABotDamageProjectile::HandleProjectileOverlap(
    UPrimitiveComponent* OverlappedComponent,
    AActor* OtherActor,
    UPrimitiveComponent* OtherComp,
    int32 OtherBodyIndex,
    bool bFromSweep,
    const FHitResult& SweepResult)
{
    TryDamageActor(OtherActor, SweepResult);
}

void ABotDamageProjectile::HandleProjectileHit(
    UPrimitiveComponent* HitComponent,
    AActor* OtherActor,
    UPrimitiveComponent* OtherComp,
    FVector NormalImpulse,
    const FHitResult& Hit)
{
    if (!TryDamageActor(OtherActor, Hit) && OtherActor != GetOwner() && OtherActor != GetInstigator())
    {
        Destroy();
    }
}

bool ABotDamageProjectile::TryDamageActor(AActor* OtherActor, const FHitResult& Hit)
{
    if (bHasAppliedDamage || !IsValid(OtherActor) || OtherActor == this || OtherActor == GetOwner() || OtherActor == GetInstigator())
    {
        return false;
    }

    const APawn* OtherPawn = Cast<APawn>(OtherActor);
    const bool bIsPlayerTarget = OtherActor->IsA<AEagleEyeCharacter>() || (OtherPawn && OtherPawn->IsPlayerControlled());
    const bool bIsBotTarget = OtherActor->IsA<ABotCharacter>();
    const bool bOwnerIsBot = GetOwner() && GetOwner()->IsA<ABotCharacter>();
    const bool bOwnerIsPlayer = GetOwner() && GetOwner()->IsA<AEagleEyeCharacter>();
    if ((bOwnerIsBot && !bIsPlayerTarget) || (bOwnerIsPlayer && !bIsBotTarget) || (!bOwnerIsBot && !bOwnerIsPlayer && !bIsPlayerTarget && !bIsBotTarget))
    {
        return false;
    }

    const FVector ShotDirection = ProjectileMovementComponent
        ? ProjectileMovementComponent->Velocity.GetSafeNormal()
        : GetActorForwardVector();
    UGameplayStatics::ApplyPointDamage(
        OtherActor,
        Damage,
        ShotDirection,
        Hit,
        GetInstigatorController(),
        this,
        nullptr);

    bHasAppliedDamage = true;
    Destroy();
    return true;
}
