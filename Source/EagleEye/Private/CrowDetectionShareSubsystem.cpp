#include "AI/CrowDetectionShareSubsystem.h"

#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"

namespace
{
    bool IsAcceptedCrowDetectionClass(
        int32 ClassId,
        const FString& ClassLabel,
        const TArray<int32>& AcceptedClassIds,
        const TArray<FName>& AcceptedClassLabels)
    {
        if (AcceptedClassIds.Num() == 0 && AcceptedClassLabels.Num() == 0)
        {
            return true;
        }

        if (AcceptedClassIds.Contains(ClassId))
        {
            return true;
        }

        for (const FName& AcceptedLabel : AcceptedClassLabels)
        {
            if (!AcceptedLabel.IsNone() &&
                ClassLabel.Equals(AcceptedLabel.ToString(), ESearchCase::IgnoreCase))
            {
                return true;
            }
        }

        return false;
    }
}

void UCrowDetectionShareSubsystem::RegisterDetector(AActor* DetectorOwner)
{
    if (!IsValid(DetectorOwner))
    {
        return;
    }

    RegisteredDetectors.RemoveAll([](const TWeakObjectPtr<AActor>& Entry)
    {
        return !Entry.IsValid();
    });

    if (!RegisteredDetectors.Contains(DetectorOwner))
    {
        RegisteredDetectors.Add(DetectorOwner);
    }
}

void UCrowDetectionShareSubsystem::UnregisterDetector(AActor* DetectorOwner)
{
    RegisteredDetectors.RemoveAll([DetectorOwner](const TWeakObjectPtr<AActor>& Entry)
    {
        return !Entry.IsValid() || Entry.Get() == DetectorOwner;
    });

    RecentTargetDetections.RemoveAll([DetectorOwner](const FCrowSharedTargetDetection& Entry)
    {
        return !Entry.Reporter.IsValid() || Entry.Reporter.Get() == DetectorOwner;
    });
}

bool UCrowDetectionShareSubsystem::ShouldRunDetector(
    AActor* DetectorOwner,
    int32 MaxActiveDetectors,
    float MaxDistanceToPlayer) const
{
    if (!IsValid(DetectorOwner))
    {
        return false;
    }

    if (MaxActiveDetectors <= 0 && MaxDistanceToPlayer <= 0.f)
    {
        return true;
    }

    APawn* PlayerPawn = GetPlayerPawn();
    if (!PlayerPawn)
    {
        return true;
    }

    if (MaxDistanceToPlayer > 0.f &&
        FVector::DistSquared(DetectorOwner->GetActorLocation(), PlayerPawn->GetActorLocation()) > FMath::Square(MaxDistanceToPlayer))
    {
        return false;
    }

    if (MaxActiveDetectors <= 0)
    {
        return true;
    }

    TArray<AActor*> ValidDetectors;
    for (const TWeakObjectPtr<AActor>& Entry : RegisteredDetectors)
    {
        AActor* Detector = Entry.Get();
        if (IsValid(Detector))
        {
            ValidDetectors.Add(Detector);
        }
    }

    ValidDetectors.Sort([PlayerPawn](const AActor& A, const AActor& B)
    {
        const float DistASq = FVector::DistSquared(A.GetActorLocation(), PlayerPawn->GetActorLocation());
        const float DistBSq = FVector::DistSquared(B.GetActorLocation(), PlayerPawn->GetActorLocation());
        if (!FMath::IsNearlyEqual(DistASq, DistBSq))
        {
            return DistASq < DistBSq;
        }

        return A.GetFName().LexicalLess(B.GetFName());
    });

    const int32 ActiveCount = FMath::Min(MaxActiveDetectors, ValidDetectors.Num());
    for (int32 Index = 0; Index < ActiveCount; ++Index)
    {
        if (ValidDetectors[Index] == DetectorOwner)
        {
            return true;
        }
    }

    return false;
}

void UCrowDetectionShareSubsystem::PublishTargetDetection(
    AActor* Reporter,
    const FVector& TargetLocation,
    float Confidence,
    int32 ClassId,
    const FString& ClassLabel)
{
    if (!IsValid(Reporter))
    {
        return;
    }

    const float CurrentTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;

    RecentTargetDetections.RemoveAll([Reporter](const FCrowSharedTargetDetection& Entry)
    {
        return !Entry.Reporter.IsValid() || Entry.Reporter.Get() == Reporter;
    });

    FCrowSharedTargetDetection Detection;
    Detection.Reporter = Reporter;
    Detection.TargetLocation = TargetLocation;
    Detection.Confidence = Confidence;
    Detection.ClassId = ClassId;
    Detection.ClassLabel = ClassLabel;
    Detection.ReportTime = CurrentTime;
    RecentTargetDetections.Add(Detection);
}

bool UCrowDetectionShareSubsystem::GetBestRecentTargetDetection(
    const AActor* Requester,
    float MaxAgeSeconds,
    float MaxReporterDistance,
    const TArray<int32>& AcceptedClassIds,
    const TArray<FName>& AcceptedClassLabels,
    FVector& OutTargetLocation,
    float& OutConfidence,
    int32& OutClassId,
    FString& OutClassLabel) const
{
    if (!Requester)
    {
        return false;
    }

    const float CurrentTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
    bool bFound = false;
    float BestScore = -FLT_MAX;

    for (const FCrowSharedTargetDetection& Detection : RecentTargetDetections)
    {
        AActor* Reporter = Detection.Reporter.Get();
        if (!IsValid(Reporter) || Reporter == Requester)
        {
            continue;
        }

        if (!IsAcceptedCrowDetectionClass(
            Detection.ClassId,
            Detection.ClassLabel,
            AcceptedClassIds,
            AcceptedClassLabels))
        {
            continue;
        }

        const float Age = CurrentTime - Detection.ReportTime;
        if (MaxAgeSeconds > 0.f && Age > MaxAgeSeconds)
        {
            continue;
        }

        const float ReporterDistSq = FVector::DistSquared(Requester->GetActorLocation(), Reporter->GetActorLocation());
        if (MaxReporterDistance > 0.f && ReporterDistSq > FMath::Square(MaxReporterDistance))
        {
            continue;
        }

        const float Score = Detection.Confidence - (Age * 0.25f) - (FMath::Sqrt(ReporterDistSq) * 0.0001f);
        if (!bFound || Score > BestScore)
        {
            OutTargetLocation = Detection.TargetLocation;
            OutConfidence = Detection.Confidence;
            OutClassId = Detection.ClassId;
            OutClassLabel = Detection.ClassLabel;
            BestScore = Score;
            bFound = true;
        }
    }

    return bFound;
}

APawn* UCrowDetectionShareSubsystem::GetPlayerPawn() const
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return nullptr;
    }

    APlayerController* PlayerController = UGameplayStatics::GetPlayerController(World, 0);
    return PlayerController ? PlayerController->GetPawn() : nullptr;
}
