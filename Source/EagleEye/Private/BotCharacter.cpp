#include "AI/BotCharacter.h"

#include "AI/BotAIController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "MyActorComponent.h"

ABotCharacter::ABotCharacter()
{
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

    UCharacterMovementComponent* MoveComp = GetCharacterMovement();
    MoveComp->DefaultLandMovementMode = MOVE_Flying;
    MoveComp->GravityScale = 0.f;
    MoveComp->MaxFlySpeed = 900.f;
    MoveComp->BrakingDecelerationFlying = 1200.f;
    MoveComp->bOrientRotationToMovement = false;
    MoveComp->RotationRate = FRotator(240.f, 360.f, 240.f);
}

void ABotCharacter::BeginPlay()
{
    Super::BeginPlay();

    GetCharacterMovement()->SetMovementMode(MOVE_Flying);
}
