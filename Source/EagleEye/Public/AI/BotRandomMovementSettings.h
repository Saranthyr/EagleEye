#pragma once

#include "CoreMinimal.h"
#include "BotRandomMovementSettings.generated.h"

USTRUCT(BlueprintType)
struct EAGLEEYE_API FBotRandomMovementSettings
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flight")
    bool bEnableRandomFlight = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flight", meta=(ClampMin="0.0"))
    float FlightRadius = 1800.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flight", meta=(ClampMin="0.0"))
    float FlightAcceptanceRadius = 180.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flight")
    float PreferredFlightHeight = 500.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flight", meta=(ClampMin="0.0"))
    float FlightHeightVariance = 250.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flight", meta=(ClampMin="0.0"))
    float DestinationHoldMinSeconds = 0.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flight", meta=(ClampMin="0.0"))
    float DestinationHoldMaxSeconds = 2.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flight")
    FName RandomFlightBlockedByKey = TEXT("HasDetectedTarget");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Walking")
    bool bEnableRandomWalking = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Walking", meta=(ClampMin="0.0"))
    float WalkRadius = 1200.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Walking", meta=(ClampMin="0.0"))
    float WalkAcceptanceRadius = 120.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Walking")
    FVector WalkingNavProjectionExtent = FVector(300.f, 300.f, 500.f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Walking", meta=(ClampMin="0.0"))
    float WalkDestinationHoldMinSeconds = 0.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Walking", meta=(ClampMin="0.0"))
    float WalkDestinationHoldMaxSeconds = 2.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Walking")
    FName RandomWalkingBlockedByKey = TEXT("HasDetectedTarget");
};
