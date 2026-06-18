#include "AI/BTTask_FlyToBlackboardLocation.h"

#include "AIController.h"
#include "AI/BotAIController.h"
#include "AI/BotCharacter.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Bool.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "DrawDebugHelpers.h"
#include "EagleEyeDetectionSettings.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "NavigationPath.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationSystem.h"

namespace
{
    static constexpr int32 MaxWalkingRoutePoints = 8;

    struct FBTTaskFlyToBlackboardLocationMemory
    {
        float LastMovementDecisionLogTime = -FLT_MAX;
        bool bHasTargetSnapshot = false;
        FVector TargetSnapshot = FVector::ZeroVector;
        float LastTargetSnapshotTime = -FLT_MAX;
        bool bHasLastMoveRequest = false;
        FVector LastMoveRequestGoal = FVector::ZeroVector;
        float LastMoveRequestTime = -FLT_MAX;
        EPathFollowingRequestResult::Type LastMoveRequestResult = EPathFollowingRequestResult::Failed;
        bool bLastMoveRequestUsedDetour = false;
        bool bHasWalkingRoute = false;
        FVector WalkingRouteTarget = FVector::ZeroVector;
        FVector WalkingRoutePoints[MaxWalkingRoutePoints] = {};
        int32 WalkingRoutePointCount = 0;
        int32 WalkingRoutePointIndex = 0;
        float BestRoutePointDistance = FLT_MAX;
        float LastRouteProgressTime = -FLT_MAX;
        bool bHoldingBestEffortGoal = false;
        FVector BestEffortHoldTarget = FVector::ZeroVector;
        float LastBestEffortRepathTime = -FLT_MAX;
        float BestMoveRequestDistance = FLT_MAX;
        float LastMoveProgressTime = -FLT_MAX;
        bool bRandomMovementBlocked = false;
    };

    struct FWalkingPathDebugInfo
    {
        bool bHasWorld = false;
        bool bHasNavSystem = false;
        bool bProjectedGoal = false;
        bool bPathCreated = false;
        bool bPathValid = false;
        bool bPathPartial = false;
        bool bSegmentBlocked = false;
        bool bUsedDetour = false;
        bool bUsedReachableGoal = false;
        bool bUsedRouteWaypoint = false;
        bool bRouteStuck = false;
        int32 PathPointCount = 0;
        int32 NextPointIndex = INDEX_NONE;
        int32 ReachableGoalCandidates = 0;
        int32 RoutePointCount = 0;
        int32 RoutePointIndex = INDEX_NONE;
        float RoutePointDistance = -1.f;
        FVector RequestedGoal = FVector::ZeroVector;
        FVector ProjectedGoal = FVector::ZeroVector;
        FVector LocalGoal = FVector::ZeroVector;
        TArray<FVector> PathPoints;
        FVector BlockImpact = FVector::ZeroVector;
        FString BlockActor = TEXT("None");
        FString BlockComponent = TEXT("None");
        FString FailureReason = TEXT("NotRun");
    };

    struct FPathObjectLogEntry
    {
        const UPrimitiveComponent* Component = nullptr;
        float DistanceSquared = 0.f;
        bool bDirectSweepHit = false;
        float SweepHitDistance = -1.f;
    };

    enum class ETargetMoveIntent : uint8
    {
        Hold,
        Approach,
        Retreat
    };

    const TCHAR* LexToString(ETargetMoveIntent Intent)
    {
        switch (Intent)
        {
        case ETargetMoveIntent::Approach:
            return TEXT("Approach");
        case ETargetMoveIntent::Retreat:
            return TEXT("Retreat");
        case ETargetMoveIntent::Hold:
        default:
            return TEXT("Hold");
        }
    }

    const TCHAR* LexToString(EBTLocomotionModeSource Source)
    {
        switch (Source)
        {
        case EBTLocomotionModeSource::ForceWalking:
            return TEXT("ForceWalking");
        case EBTLocomotionModeSource::ForceFlying:
            return TEXT("ForceFlying");
        case EBTLocomotionModeSource::BlackboardBool:
            return TEXT("BlackboardBool");
        case EBTLocomotionModeSource::BotLocomotion:
        default:
            return TEXT("BotLocomotion");
        }
    }

    const TCHAR* LexToString(EBTTargetMovementModeSource Source)
    {
        switch (Source)
        {
        case EBTTargetMovementModeSource::BlackboardBool:
            return TEXT("BlackboardBool");
        case EBTTargetMovementModeSource::TaskSettings:
        default:
            return TEXT("TaskSettings");
        }
    }

    const TCHAR* LexToString(EBTTargetMovementMode Mode)
    {
        switch (Mode)
        {
        case EBTTargetMovementMode::MaintainDistance:
            return TEXT("MaintainDistance");
        case EBTTargetMovementMode::ApproachTarget:
        default:
            return TEXT("ApproachTarget");
        }
    }

    bool IsPathfindingDecisionLoggingEnabled()
    {
        UEagleEyeDetectionSettings::LoadRuntimeConfig();
        const UEagleEyeDetectionSettings* Settings = GetDefault<UEagleEyeDetectionSettings>();
        return !Settings || Settings->bEnablePathfindingDecisionLogs;
    }

    bool IsPathfindingObjectLoggingEnabled()
    {
        UEagleEyeDetectionSettings::LoadRuntimeConfig();
        const UEagleEyeDetectionSettings* Settings = GetDefault<UEagleEyeDetectionSettings>();
        return (!Settings || Settings->bEnablePathfindingDecisionLogs) &&
            (!Settings || Settings->bEnablePathfindingObjectLogs);
    }

    bool IsDetectionAndPathDebugDrawingDisabled()
    {
        UEagleEyeDetectionSettings::LoadRuntimeConfig();
        const UEagleEyeDetectionSettings* Settings = GetDefault<UEagleEyeDetectionSettings>();
        return Settings && Settings->bDisableDetectionAndPathDebugDrawing;
    }

    const TCHAR* LexToString(ECollisionEnabled::Type CollisionEnabled)
    {
        switch (CollisionEnabled)
        {
        case ECollisionEnabled::NoCollision:
            return TEXT("NoCollision");
        case ECollisionEnabled::QueryOnly:
            return TEXT("QueryOnly");
        case ECollisionEnabled::PhysicsOnly:
            return TEXT("PhysicsOnly");
        case ECollisionEnabled::QueryAndPhysics:
            return TEXT("QueryAndPhysics");
        default:
            return TEXT("Unknown");
        }
    }

    const TCHAR* LexToString(ECollisionResponse Response)
    {
        switch (Response)
        {
        case ECR_Ignore:
            return TEXT("Ignore");
        case ECR_Overlap:
            return TEXT("Overlap");
        case ECR_Block:
            return TEXT("Block");
        default:
            return TEXT("Unknown");
        }
    }

    const TCHAR* LexToString(EComponentMobility::Type Mobility)
    {
        switch (Mobility)
        {
        case EComponentMobility::Static:
            return TEXT("Static");
        case EComponentMobility::Stationary:
            return TEXT("Stationary");
        case EComponentMobility::Movable:
            return TEXT("Movable");
        default:
            return TEXT("Unknown");
        }
    }

    const TCHAR* LexToString(ECollisionChannel Channel)
    {
        switch (Channel)
        {
        case ECC_WorldStatic:
            return TEXT("WorldStatic");
        case ECC_WorldDynamic:
            return TEXT("WorldDynamic");
        case ECC_Pawn:
            return TEXT("Pawn");
        case ECC_Visibility:
            return TEXT("Visibility");
        case ECC_Camera:
            return TEXT("Camera");
        case ECC_PhysicsBody:
            return TEXT("PhysicsBody");
        case ECC_Vehicle:
            return TEXT("Vehicle");
        default:
            return TEXT("Other");
        }
    }

    FString DescribePathfindingEffect(const UPrimitiveComponent& Component, bool bDirectSweepHit)
    {
        const bool bCanAffectNav = Component.CanEverAffectNavigation();
        const bool bHasQueryCollision =
            Component.GetCollisionEnabled() == ECollisionEnabled::QueryOnly ||
            Component.GetCollisionEnabled() == ECollisionEnabled::QueryAndPhysics;
        const bool bBlocksPawn = Component.GetCollisionResponseToChannel(ECC_Pawn) == ECR_Block;

        if (bDirectSweepHit)
        {
            return bCanAffectNav ? TEXT("blocksCurrentSegment+navRelevant") : TEXT("blocksCurrentSegment+notNavRelevant");
        }
        if (bCanAffectNav && bHasQueryCollision && bBlocksPawn)
        {
            return TEXT("navGeometryOrObstacle+blocksPawn");
        }
        if (bCanAffectNav && bHasQueryCollision)
        {
            return TEXT("navGeometryOrObstacle");
        }
        if (bCanAffectNav)
        {
            return TEXT("navRelevantButNoQueryCollision");
        }
        if (bHasQueryCollision && bBlocksPawn)
        {
            return TEXT("pawnCollisionOnly");
        }
        if (bHasQueryCollision)
        {
            return TEXT("queryCollisionOnly");
        }
        return TEXT("noPathfindingEffect");
    }

    void AddPathfindingObjectTypes(FCollisionObjectQueryParams& ObjectParams)
    {
        ObjectParams.AddObjectTypesToQuery(ECC_WorldStatic);
        ObjectParams.AddObjectTypesToQuery(ECC_WorldDynamic);
        ObjectParams.AddObjectTypesToQuery(ECC_Pawn);
        ObjectParams.AddObjectTypesToQuery(ECC_PhysicsBody);
        ObjectParams.AddObjectTypesToQuery(ECC_Vehicle);
    }

    void LogPresentPathfindingObjects(
        APawn& ControlledPawn,
        const FVector& From,
        const FVector& To,
        const FWalkingPathDebugInfo& PathDebug,
        float CorridorRadius,
        float VerticalExtent,
        int32 MaxObjectsToLog)
    {
        UWorld* World = ControlledPawn.GetWorld();
        if (!World)
        {
            return;
        }

        const FVector QueryCenter = (From + To) * 0.5f;
        const FVector HalfDelta = (To - From).GetAbs() * 0.5f;
        const FVector QueryExtent(
            HalfDelta.X + FMath::Max(0.f, CorridorRadius),
            HalfDelta.Y + FMath::Max(0.f, CorridorRadius),
            HalfDelta.Z + FMath::Max(0.f, VerticalExtent));
        const FBox QueryBox(QueryCenter - QueryExtent, QueryCenter + QueryExtent);

        float CollisionRadius = 34.f;
        float CollisionHalfHeight = 88.f;
        ControlledPawn.GetSimpleCollisionCylinder(CollisionRadius, CollisionHalfHeight);
        const float SweepRadius = FMath::Max(20.f, CollisionRadius * 0.75f);
        const FVector TraceOffset(0.f, 0.f, FMath::Clamp(CollisionHalfHeight * 0.5f, 40.f, 120.f));

        FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(BTTaskPathfindingObjectLog), false, &ControlledPawn);
        QueryParams.AddIgnoredActor(&ControlledPawn);

        FCollisionObjectQueryParams ObjectParams;
        AddPathfindingObjectTypes(ObjectParams);

        TMap<const UPrimitiveComponent*, float> SweepHitDistances;
        TArray<FHitResult> SweepHits;
        World->SweepMultiByObjectType(
            SweepHits,
            From + TraceOffset,
            To + TraceOffset,
            FQuat::Identity,
            ObjectParams,
            FCollisionShape::MakeSphere(SweepRadius),
            QueryParams);

        for (const FHitResult& Hit : SweepHits)
        {
            const UPrimitiveComponent* HitComponent = Hit.Component.Get();
            if (!HitComponent)
            {
                continue;
            }

            float* ExistingDistance = SweepHitDistances.Find(HitComponent);
            if (!ExistingDistance || Hit.Distance < *ExistingDistance)
            {
                SweepHitDistances.Add(HitComponent, Hit.Distance);
            }
        }

        TArray<FPathObjectLogEntry> Entries;
        for (TActorIterator<AActor> ActorIt(World); ActorIt; ++ActorIt)
        {
            AActor* Actor = *ActorIt;
            if (!Actor || Actor == &ControlledPawn)
            {
                continue;
            }

            TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents;
            Actor->GetComponents(PrimitiveComponents);
            for (UPrimitiveComponent* Component : PrimitiveComponents)
            {
                if (!Component || !Component->IsRegistered())
                {
                    continue;
                }

                const FBox ComponentBox = Component->Bounds.GetBox();
                if (!ComponentBox.IsValid || !QueryBox.Intersect(ComponentBox))
                {
                    continue;
                }

                FPathObjectLogEntry& Entry = Entries.AddDefaulted_GetRef();
                Entry.Component = Component;
                Entry.DistanceSquared = FVector::DistSquared(Component->Bounds.Origin, From);
                if (const float* SweepHitDistance = SweepHitDistances.Find(Component))
                {
                    Entry.bDirectSweepHit = true;
                    Entry.SweepHitDistance = *SweepHitDistance;
                }
            }
        }

        Entries.Sort([](const FPathObjectLogEntry& A, const FPathObjectLogEntry& B)
        {
            if (A.bDirectSweepHit != B.bDirectSweepHit)
            {
                return A.bDirectSweepHit;
            }
            return A.DistanceSquared < B.DistanceSquared;
        });

        const int32 LogLimit = FMath::Max(1, MaxObjectsToLog);
        const int32 LogCount = FMath::Min(LogLimit, Entries.Num());
        UE_LOG(
            LogTemp,
            Log,
            TEXT("BTPathObjectsSummary: pawn=%s from=%s to=%s queryCenter=%s queryExtent=%s total=%d logged=%d truncated=%s navWorld=%s navSystem=%s projected=%s pathCreated=%s pathValid=%s partial=%s points=%d nextPoint=%d projectedGoal=%s localGoal=%s failure=%s blockedBy=%s/%s blockImpact=%s usedDetour=%s"),
            *ControlledPawn.GetName(),
            *From.ToCompactString(),
            *To.ToCompactString(),
            *QueryCenter.ToCompactString(),
            *QueryExtent.ToCompactString(),
            Entries.Num(),
            LogCount,
            Entries.Num() > LogCount ? TEXT("true") : TEXT("false"),
            PathDebug.bHasWorld ? TEXT("true") : TEXT("false"),
            PathDebug.bHasNavSystem ? TEXT("true") : TEXT("false"),
            PathDebug.bProjectedGoal ? TEXT("true") : TEXT("false"),
            PathDebug.bPathCreated ? TEXT("true") : TEXT("false"),
            PathDebug.bPathValid ? TEXT("true") : TEXT("false"),
            PathDebug.bPathPartial ? TEXT("true") : TEXT("false"),
            PathDebug.PathPointCount,
            PathDebug.NextPointIndex,
            *PathDebug.ProjectedGoal.ToCompactString(),
            *PathDebug.LocalGoal.ToCompactString(),
            *PathDebug.FailureReason,
            *PathDebug.BlockActor,
            *PathDebug.BlockComponent,
            *PathDebug.BlockImpact.ToCompactString(),
            PathDebug.bUsedDetour ? TEXT("true") : TEXT("false"));

        for (int32 EntryIndex = 0; EntryIndex < LogCount; ++EntryIndex)
        {
            const FPathObjectLogEntry& Entry = Entries[EntryIndex];
            const UPrimitiveComponent* Component = Entry.Component;
            const AActor* Actor = Component ? Component->GetOwner() : nullptr;
            if (!Component || !Actor)
            {
                continue;
            }

            UE_LOG(
                LogTemp,
                Log,
                TEXT("BTPathObject[%d]: pawn=%s actor=%s actorClass=%s component=%s componentClass=%s loc=%s extent=%s dist=%.1f navAffect=%s collision=%s objectType=%s pawnResponse=%s visibilityResponse=%s mobility=%s directSweep=%s sweepDistance=%.1f effect=%s"),
                EntryIndex,
                *ControlledPawn.GetName(),
                *Actor->GetName(),
                *Actor->GetClass()->GetName(),
                *Component->GetName(),
                *Component->GetClass()->GetName(),
                *Component->Bounds.Origin.ToCompactString(),
                *Component->Bounds.BoxExtent.ToCompactString(),
                FMath::Sqrt(Entry.DistanceSquared),
                Component->CanEverAffectNavigation() ? TEXT("true") : TEXT("false"),
                LexToString(Component->GetCollisionEnabled()),
                LexToString(Component->GetCollisionObjectType()),
                LexToString(Component->GetCollisionResponseToChannel(ECC_Pawn)),
                LexToString(Component->GetCollisionResponseToChannel(ECC_Visibility)),
                LexToString(Component->Mobility),
                Entry.bDirectSweepHit ? TEXT("true") : TEXT("false"),
                Entry.SweepHitDistance,
                *DescribePathfindingEffect(*Component, Entry.bDirectSweepHit));
        }
    }

    float CalculatePlanarYaw(const FVector& Direction)
    {
        FVector FlatDirection = Direction;
        FlatDirection.Z = 0.f;
        if (FlatDirection.IsNearlyZero())
        {
            return 0.f;
        }

        return FlatDirection.Rotation().Yaw;
    }

    bool IsWalkingBot(const APawn& ControlledPawn)
    {
        const ABotCharacter* BotCharacter = Cast<ABotCharacter>(&ControlledPawn);
        return BotCharacter && BotCharacter->IsWalkingBot();
    }

    float CalculatePathLength2D(const TArray<FVector>& PathPoints)
    {
        float Length = 0.f;
        for (int32 Index = 1; Index < PathPoints.Num(); ++Index)
        {
            Length += FVector::Dist2D(PathPoints[Index - 1], PathPoints[Index]);
        }
        return Length;
    }

    FVector GetNextPathDirection2D(const FVector& CurrentLocation, const TArray<FVector>& PathPoints, const FVector& FallbackGoal)
    {
        for (int32 Index = 1; Index < PathPoints.Num(); ++Index)
        {
            FVector ToPoint = PathPoints[Index] - CurrentLocation;
            ToPoint.Z = 0.f;
            if (ToPoint.SizeSquared2D() > FMath::Square(25.f))
            {
                return ToPoint.GetSafeNormal();
            }
        }

        FVector ToFallback = FallbackGoal - CurrentLocation;
        ToFallback.Z = 0.f;
        return ToFallback.GetSafeNormal();
    }

    bool FindSimpleWalkingNavGoal(
        APawn& ControlledPawn,
        const FVector& DesiredGoal,
        const FVector& NavProjectionExtent,
        bool bAllowPartialPath,
        float AcceptanceRadius,
        FVector& OutNavGoal,
        FVector& OutPathDirection,
        FWalkingPathDebugInfo& OutDebugInfo)
    {
        OutDebugInfo = FWalkingPathDebugInfo();
        OutDebugInfo.RequestedGoal = DesiredGoal;

        UWorld* World = ControlledPawn.GetWorld();
        OutDebugInfo.bHasWorld = World != nullptr;
        if (!World)
        {
            OutDebugInfo.FailureReason = TEXT("NoWorld");
            return false;
        }

        UNavigationSystemV1* NavSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
        OutDebugInfo.bHasNavSystem = NavSystem != nullptr;
        if (!NavSystem)
        {
            OutDebugInfo.FailureReason = TEXT("NoNavSystem");
            return false;
        }

        const FVector CurrentLocation = ControlledPawn.GetActorLocation();
        const float Acceptance = FMath::Max(1.f, AcceptanceRadius);
        bool bFoundPath = false;
        bool bFoundAnyProjection = false;
        bool bBestPathPartial = false;
        float BestScore = TNumericLimits<float>::Max();
        FVector BestProjectedGoal = FVector::ZeroVector;
        FVector BestNavGoal = FVector::ZeroVector;
        TArray<FVector> BestPathPoints;

        const auto EvaluateCandidate = [&](const FVector& Candidate, float Penalty)
        {
            FNavLocation ProjectedLocation;
            if (!NavSystem->ProjectPointToNavigation(Candidate, ProjectedLocation, NavProjectionExtent))
            {
                return;
            }

            bFoundAnyProjection = true;
            TArray<FVector> CandidatePathPoints;
            UNavigationPath* Path = NavSystem->FindPathToLocationSynchronously(
                World,
                CurrentLocation,
                ProjectedLocation.Location,
                &ControlledPawn);
            if (!Path || !Path->IsValid() || Path->PathPoints.Num() == 0)
            {
                return;
            }

            const bool bPartial = Path->IsPartial();
            if (bPartial && !bAllowPartialPath)
            {
                return;
            }

            CandidatePathPoints = Path->PathPoints;
            const FVector PathEnd = CandidatePathPoints.Last();
            const float EndDistanceToTarget = FVector::DistSquared2D(PathEnd, DesiredGoal);
            const float PathLength = CalculatePathLength2D(CandidatePathPoints);
            const float PartialPenalty = bPartial ? FMath::Square(Acceptance) : 0.f;
            const float Score = EndDistanceToTarget + PathLength * 0.25f + Penalty + PartialPenalty;
            if (!bFoundPath || Score < BestScore)
            {
                bFoundPath = true;
                bBestPathPartial = bPartial;
                BestScore = Score;
                BestProjectedGoal = ProjectedLocation.Location;
                BestNavGoal = PathEnd;
                BestPathPoints = MoveTemp(CandidatePathPoints);
            }
        };

        EvaluateCandidate(DesiredGoal, 0.f);

        static constexpr float SearchRadii[] = { 150.f, 300.f, 600.f, 1000.f, 1500.f, 2200.f };
        static constexpr int32 DirectionCount = 16;
        for (float Radius : SearchRadii)
        {
            for (int32 DirectionIndex = 0; DirectionIndex < DirectionCount; ++DirectionIndex)
            {
                const float AngleRadians = (2.f * PI * DirectionIndex) / DirectionCount;
                const FVector Offset(FMath::Cos(AngleRadians) * Radius, FMath::Sin(AngleRadians) * Radius, 0.f);
                EvaluateCandidate(DesiredGoal + Offset, FMath::Square(Radius) * 0.01f);
            }
        }

        OutDebugInfo.bProjectedGoal = bFoundAnyProjection;
        OutDebugInfo.bPathCreated = bFoundPath;
        OutDebugInfo.bPathValid = bFoundPath;
        OutDebugInfo.bPathPartial = bBestPathPartial;
        OutDebugInfo.ProjectedGoal = BestProjectedGoal;
        OutDebugInfo.LocalGoal = BestNavGoal;
        OutDebugInfo.PathPoints = BestPathPoints;
        OutDebugInfo.PathPointCount = BestPathPoints.Num();
        OutDebugInfo.NextPointIndex = BestPathPoints.Num() > 1 ? 1 : INDEX_NONE;
        OutDebugInfo.bUsedReachableGoal =
            bFoundPath &&
            FVector::DistSquared2D(BestNavGoal, DesiredGoal) > FMath::Square(Acceptance);

        if (!bFoundPath)
        {
            OutDebugInfo.FailureReason = bFoundAnyProjection ? TEXT("NoPathToProjectedGoal") : TEXT("NoProjectedGoal");
            return false;
        }

        OutNavGoal = BestNavGoal;
        OutPathDirection = GetNextPathDirection2D(CurrentLocation, BestPathPoints, BestNavGoal);
        OutDebugInfo.FailureReason = TEXT("None");
        return !OutPathDirection.IsNearlyZero();
    }

    void DrawSimpleWalkingPathDebug(
        UWorld& World,
        const FVector& CurrentLocation,
        const FVector& TargetLocation,
        const FVector& MoveGoal,
        const FVector& NavGoal,
        float HoldRadius,
        const FWalkingPathDebugInfo& PathDebug)
    {
        const FColor PathColor = PathDebug.bPathPartial ? FColor::Yellow : FColor::Green;
        const FColor GoalColor = PathDebug.bPathValid ? FColor::Blue : FColor::Red;
        DrawDebugSphere(&World, TargetLocation, HoldRadius, 16, FColor::Cyan, false, 0.25f, 0, 1.5f);
        DrawDebugSphere(&World, MoveGoal, 55.f, 12, FColor::Purple, false, 0.25f, 0, 2.f);
        DrawDebugSphere(&World, NavGoal, 70.f, 12, GoalColor, false, 0.25f, 0, 3.f);
        DrawDebugLine(&World, CurrentLocation, MoveGoal, FColor::Cyan, false, 0.25f, 0, 1.5f);

        const TArray<FVector>& Points = PathDebug.PathPoints;
        if (Points.Num() > 1)
        {
            for (int32 Index = 1; Index < Points.Num(); ++Index)
            {
                DrawDebugLine(&World, Points[Index - 1], Points[Index], PathColor, false, 0.25f, 0, 8.f);
                DrawDebugSphere(&World, Points[Index], 28.f, 8, PathColor, false, 0.25f, 0, 2.f);
            }
        }
        else if (!PathDebug.bPathValid)
        {
            DrawDebugLine(&World, CurrentLocation, MoveGoal, FColor::Red, false, 0.25f, 0, 6.f);
        }
    }

    bool IsWalkingSegmentBlocked(
        APawn& ControlledPawn,
        const FVector& From,
        const FVector& To,
        FHitResult* OutHit = nullptr)
    {
        UWorld* World = ControlledPawn.GetWorld();
        if (!World)
        {
            return false;
        }

        float CollisionRadius = 34.f;
        float CollisionHalfHeight = 88.f;
        ControlledPawn.GetSimpleCollisionCylinder(CollisionRadius, CollisionHalfHeight);

        const FVector TraceOffset(0.f, 0.f, FMath::Clamp(CollisionHalfHeight * 0.5f, 40.f, 120.f));
        const FVector TraceStart = From + TraceOffset;
        const FVector TraceEnd = To + TraceOffset;

        FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(BTTaskWalkingPathObstacle), false, &ControlledPawn);
        QueryParams.AddIgnoredActor(&ControlledPawn);

        FCollisionObjectQueryParams ObjectParams;
        ObjectParams.AddObjectTypesToQuery(ECC_WorldStatic);
        ObjectParams.AddObjectTypesToQuery(ECC_WorldDynamic);

        FHitResult Hit;
        const bool bBlocked = World->SweepSingleByObjectType(
            Hit,
            TraceStart,
            TraceEnd,
            FQuat::Identity,
            ObjectParams,
            FCollisionShape::MakeSphere(FMath::Max(20.f, CollisionRadius * 0.75f)),
            QueryParams);

        if (bBlocked && OutHit)
        {
            *OutHit = Hit;
        }
        return bBlocked;
    }

    bool TryFindCollisionAwareDetour(
        APawn& ControlledPawn,
        UNavigationSystemV1& NavSystem,
        const FVector& CurrentLocation,
        const FVector& UltimateGoal,
        const FVector& BlockedGoal,
        const FHitResult& BlockHit,
        const FVector& NavProjectionExtent,
        FVector& OutDetour)
    {
        FVector ToBlockedGoal = BlockedGoal - CurrentLocation;
        ToBlockedGoal.Z = 0.f;
        FVector PathDirection = ToBlockedGoal.GetSafeNormal();
        if (PathDirection.IsNearlyZero())
        {
            PathDirection = ControlledPawn.GetActorForwardVector();
            PathDirection.Z = 0.f;
            PathDirection.Normalize();
        }

        FVector SideDirection = FVector::CrossProduct(FVector::UpVector, PathDirection).GetSafeNormal();
        if (SideDirection.IsNearlyZero())
        {
            SideDirection = ControlledPawn.GetActorRightVector();
            SideDirection.Z = 0.f;
            SideDirection.Normalize();
        }

        FVector BlockCenter = BlockHit.ImpactPoint;
        if (BlockCenter.IsNearlyZero())
        {
            BlockCenter = CurrentLocation + ToBlockedGoal * 0.5f;
        }
        BlockCenter.Z = CurrentLocation.Z;

        static constexpr float SideOffsets[] = { 180.f, 320.f, 520.f, 800.f };
        bool bFound = false;
        float BestScore = TNumericLimits<float>::Max();
        FVector BestDetour = FVector::ZeroVector;

        for (float SideSign : { -1.f, 1.f })
        {
            for (float SideOffset : SideOffsets)
            {
                const FVector Candidate = BlockCenter + (SideDirection * SideSign * SideOffset) + (PathDirection * 80.f);
                FNavLocation ProjectedCandidate;
                if (!NavSystem.ProjectPointToNavigation(Candidate, ProjectedCandidate, NavProjectionExtent))
                {
                    continue;
                }

                if (IsWalkingSegmentBlocked(ControlledPawn, CurrentLocation, ProjectedCandidate.Location))
                {
                    continue;
                }

                const float Score =
                    FVector::DistSquared(ProjectedCandidate.Location, UltimateGoal) +
                    FVector::DistSquared(ProjectedCandidate.Location, CurrentLocation) * 0.25f;
                if (!bFound || Score < BestScore)
                {
                    BestScore = Score;
                    BestDetour = ProjectedCandidate.Location;
                    bFound = true;
                }
            }
        }

        if (bFound)
        {
            OutDetour = BestDetour;
        }
        return bFound;
    }

    bool TryFindReachableApproachPathDirection(
        APawn& ControlledPawn,
        UNavigationSystemV1& NavSystem,
        const FVector& DesiredGoal,
        const FVector& NavProjectionExtent,
        float MinWaypointDistance,
        FVector& OutPathGoal,
        FVector& OutPathDirection,
        FWalkingPathDebugInfo* OutDebugInfo = nullptr)
    {
        UWorld* World = ControlledPawn.GetWorld();
        if (!World)
        {
            return false;
        }

        const FVector CurrentLocation = ControlledPawn.GetActorLocation();
        FVector ToDesiredGoal = DesiredGoal - CurrentLocation;
        ToDesiredGoal.Z = 0.f;
        FVector GoalDirection = ToDesiredGoal.GetSafeNormal();
        if (GoalDirection.IsNearlyZero())
        {
            GoalDirection = ControlledPawn.GetActorForwardVector();
            GoalDirection.Z = 0.f;
            GoalDirection.Normalize();
        }

        const FVector SideDirection = FVector::CrossProduct(FVector::UpVector, GoalDirection).GetSafeNormal();
        const float MinWaypointDistanceSquared = FMath::Square(FMath::Max(20.f, MinWaypointDistance));
        static constexpr float SearchRadii[] = { 150.f, 250.f, 400.f, 600.f, 900.f };
        static constexpr int32 DirectionCount = 12;

        bool bFound = false;
        float BestScore = TNumericLimits<float>::Max();
        FVector BestGoal = FVector::ZeroVector;
        FVector BestDirection = FVector::ZeroVector;
        FVector BestProjectedGoal = FVector::ZeroVector;
        int32 BestPointCount = 0;
        int32 BestNextPointIndex = INDEX_NONE;
        int32 CandidateCount = 0;

        for (float Radius : SearchRadii)
        {
            for (int32 DirectionIndex = 0; DirectionIndex < DirectionCount; ++DirectionIndex)
            {
                const float AngleRadians = (2.f * PI * DirectionIndex) / DirectionCount;
                const FVector Offset =
                    GoalDirection * (FMath::Cos(AngleRadians) * Radius) +
                    SideDirection * (FMath::Sin(AngleRadians) * Radius);
                const FVector Candidate = DesiredGoal + Offset;

                FNavLocation ProjectedCandidate;
                if (!NavSystem.ProjectPointToNavigation(Candidate, ProjectedCandidate, NavProjectionExtent))
                {
                    continue;
                }

                ++CandidateCount;
                UNavigationPath* CandidatePath = UNavigationSystemV1::FindPathToLocationSynchronously(
                    World,
                    CurrentLocation,
                    ProjectedCandidate.Location,
                    &ControlledPawn);
                if (!CandidatePath || !CandidatePath->IsValid() || CandidatePath->IsPartial() || CandidatePath->PathPoints.Num() < 2)
                {
                    continue;
                }

                int32 NextPointIndex = CandidatePath->PathPoints.Num() - 1;
                for (int32 PathPointIndex = 1; PathPointIndex < CandidatePath->PathPoints.Num(); ++PathPointIndex)
                {
                    FVector ToPathPoint = CandidatePath->PathPoints[PathPointIndex] - CurrentLocation;
                    ToPathPoint.Z = 0.f;
                    if (ToPathPoint.SizeSquared() > MinWaypointDistanceSquared)
                    {
                        NextPointIndex = PathPointIndex;
                        break;
                    }
                }

                FVector LocalGoal = CandidatePath->PathPoints[NextPointIndex];
                FVector ToLocalGoal = LocalGoal - CurrentLocation;
                ToLocalGoal.Z = 0.f;
                const FVector CandidateDirection = ToLocalGoal.GetSafeNormal();
                if (CandidateDirection.IsNearlyZero() || IsWalkingSegmentBlocked(ControlledPawn, CurrentLocation, LocalGoal))
                {
                    continue;
                }

                float PathLength = 0.f;
                for (int32 PathPointIndex = 1; PathPointIndex < CandidatePath->PathPoints.Num(); ++PathPointIndex)
                {
                    PathLength += FVector::Dist(
                        CandidatePath->PathPoints[PathPointIndex - 1],
                        CandidatePath->PathPoints[PathPointIndex]);
                }

                const float Score =
                    FVector::DistSquared2D(ProjectedCandidate.Location, DesiredGoal) +
                    FMath::Square(PathLength) * 0.1f;
                if (!bFound || Score < BestScore)
                {
                    BestScore = Score;
                    BestGoal = LocalGoal;
                    BestDirection = CandidateDirection;
                    BestProjectedGoal = ProjectedCandidate.Location;
                    BestPointCount = CandidatePath->PathPoints.Num();
                    BestNextPointIndex = NextPointIndex;
                    bFound = true;
                }
            }
        }

        if (OutDebugInfo)
        {
            OutDebugInfo->ReachableGoalCandidates = CandidateCount;
        }
        if (!bFound)
        {
            return false;
        }

        OutPathGoal = BestGoal;
        OutPathDirection = BestDirection;
        if (OutDebugInfo)
        {
            OutDebugInfo->bUsedReachableGoal = true;
            OutDebugInfo->bUsedDetour = false;
            OutDebugInfo->bProjectedGoal = true;
            OutDebugInfo->bPathCreated = true;
            OutDebugInfo->bPathValid = true;
            OutDebugInfo->bPathPartial = false;
            OutDebugInfo->PathPointCount = BestPointCount;
            OutDebugInfo->NextPointIndex = BestNextPointIndex;
            OutDebugInfo->ProjectedGoal = BestProjectedGoal;
            OutDebugInfo->LocalGoal = BestGoal;
            OutDebugInfo->FailureReason = TEXT("None");
        }
        return true;
    }

    void ClearWalkingRoute(FBTTaskFlyToBlackboardLocationMemory& Memory)
    {
        Memory.bHasWalkingRoute = false;
        Memory.WalkingRouteTarget = FVector::ZeroVector;
        Memory.WalkingRoutePointCount = 0;
        Memory.WalkingRoutePointIndex = 0;
        Memory.BestRoutePointDistance = FLT_MAX;
        Memory.LastRouteProgressTime = -FLT_MAX;
    }

    bool BuildWalkingRoute(
        APawn& ControlledPawn,
        const FVector& Goal,
        const FVector& NavProjectionExtent,
        float MinPointDistance,
        float CurrentTime,
        FBTTaskFlyToBlackboardLocationMemory& Memory)
    {
        ClearWalkingRoute(Memory);

        UWorld* World = ControlledPawn.GetWorld();
        UNavigationSystemV1* NavSystem = World ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
        if (!World || !NavSystem)
        {
            return false;
        }

        FNavLocation ProjectedGoal;
        const FVector RouteGoal = NavSystem->ProjectPointToNavigation(Goal, ProjectedGoal, NavProjectionExtent)
            ? ProjectedGoal.Location
            : Goal;

        const FVector CurrentLocation = ControlledPawn.GetActorLocation();
        UNavigationPath* Path = UNavigationSystemV1::FindPathToLocationSynchronously(
            World,
            CurrentLocation,
            RouteGoal,
            &ControlledPawn);

        const float MinPointDistanceSquared = FMath::Square(FMath::Max(20.f, MinPointDistance));
        FVector LastAcceptedPoint = CurrentLocation;
        auto AddRoutePoint = [&](const FVector& Point)
        {
            if (Memory.WalkingRoutePointCount >= MaxWalkingRoutePoints)
            {
                return;
            }

            Memory.WalkingRoutePoints[Memory.WalkingRoutePointCount++] = Point;
            LastAcceptedPoint = Point;
        };

        if (Path && Path->IsValid() && Path->PathPoints.Num() >= 2)
        {
            for (int32 PathPointIndex = 1; PathPointIndex < Path->PathPoints.Num(); ++PathPointIndex)
            {
                const FVector& PathPoint = Path->PathPoints[PathPointIndex];
                const bool bIsFinalPoint = PathPointIndex == Path->PathPoints.Num() - 1;
                if (bIsFinalPoint || FVector::DistSquared2D(PathPoint, LastAcceptedPoint) >= MinPointDistanceSquared)
                {
                    AddRoutePoint(PathPoint);
                }
            }
        }

        if (Memory.WalkingRoutePointCount == 0)
        {
            AddRoutePoint(RouteGoal);
        }

        if (Memory.WalkingRoutePointCount == 0)
        {
            return false;
        }

        Memory.bHasWalkingRoute = true;
        Memory.WalkingRouteTarget = RouteGoal;
        Memory.WalkingRoutePointIndex = 0;
        Memory.BestRoutePointDistance = FVector::Dist2D(CurrentLocation, Memory.WalkingRoutePoints[0]);
        Memory.LastRouteProgressTime = CurrentTime;
        return true;
    }

    bool UseWalkingRouteWaypoint(
        APawn& ControlledPawn,
        const FVector& DesiredGoal,
        const FVector& NavProjectionExtent,
        float MinPointDistance,
        float WaypointAcceptanceRadius,
        float RouteRebuildDistance,
        float StuckTimeout,
        float StuckMinProgress,
        float CurrentTime,
        FBTTaskFlyToBlackboardLocationMemory& Memory,
        FVector& OutGoal,
        FVector& OutDirection,
        FWalkingPathDebugInfo& DebugInfo)
    {
        const bool bNeedsRoute =
            !Memory.bHasWalkingRoute ||
            Memory.WalkingRoutePointIndex < 0 ||
            Memory.WalkingRoutePointIndex >= Memory.WalkingRoutePointCount ||
            FVector::DistSquared2D(DesiredGoal, Memory.WalkingRouteTarget) >
                FMath::Square(FMath::Max(0.f, RouteRebuildDistance));

        if (bNeedsRoute &&
            !BuildWalkingRoute(
                ControlledPawn,
                DesiredGoal,
                NavProjectionExtent,
                MinPointDistance,
                CurrentTime,
                Memory))
        {
            return false;
        }

        const FVector CurrentLocation = ControlledPawn.GetActorLocation();
        const float Acceptance = FMath::Max(20.f, WaypointAcceptanceRadius);
        while (Memory.WalkingRoutePointIndex + 1 < Memory.WalkingRoutePointCount &&
            FVector::DistSquared2D(CurrentLocation, Memory.WalkingRoutePoints[Memory.WalkingRoutePointIndex]) <=
                FMath::Square(Acceptance))
        {
            ++Memory.WalkingRoutePointIndex;
            Memory.BestRoutePointDistance = FVector::Dist2D(CurrentLocation, Memory.WalkingRoutePoints[Memory.WalkingRoutePointIndex]);
            Memory.LastRouteProgressTime = CurrentTime;
            Memory.bHasLastMoveRequest = false;
        }

        FVector RoutePoint = Memory.WalkingRoutePoints[Memory.WalkingRoutePointIndex];
        float DistanceToRoutePoint = FVector::Dist2D(CurrentLocation, RoutePoint);
        if (IsWalkingSegmentBlocked(ControlledPawn, CurrentLocation, RoutePoint))
        {
            DebugInfo.bRouteStuck = true;
            Memory.bHasLastMoveRequest = false;
            if (Memory.WalkingRoutePointIndex + 1 < Memory.WalkingRoutePointCount)
            {
                ++Memory.WalkingRoutePointIndex;
                RoutePoint = Memory.WalkingRoutePoints[Memory.WalkingRoutePointIndex];
                DistanceToRoutePoint = FVector::Dist2D(CurrentLocation, RoutePoint);
                Memory.BestRoutePointDistance = DistanceToRoutePoint;
                Memory.LastRouteProgressTime = CurrentTime;
            }
            else
            {
                ClearWalkingRoute(Memory);
                return false;
            }
        }
        if (DistanceToRoutePoint + FMath::Max(1.f, StuckMinProgress) < Memory.BestRoutePointDistance)
        {
            Memory.BestRoutePointDistance = DistanceToRoutePoint;
            Memory.LastRouteProgressTime = CurrentTime;
        }
        else if (StuckTimeout > 0.f && CurrentTime - Memory.LastRouteProgressTime >= StuckTimeout)
        {
            DebugInfo.bRouteStuck = true;
            Memory.bHasLastMoveRequest = false;
            if (Memory.WalkingRoutePointIndex + 1 < Memory.WalkingRoutePointCount)
            {
                ++Memory.WalkingRoutePointIndex;
                RoutePoint = Memory.WalkingRoutePoints[Memory.WalkingRoutePointIndex];
                DistanceToRoutePoint = FVector::Dist2D(CurrentLocation, RoutePoint);
                Memory.BestRoutePointDistance = DistanceToRoutePoint;
                Memory.LastRouteProgressTime = CurrentTime;
            }
            else
            {
                if (!BuildWalkingRoute(
                    ControlledPawn,
                    DesiredGoal,
                    NavProjectionExtent,
                    MinPointDistance,
                    CurrentTime,
                    Memory))
                {
                    return false;
                }
                RoutePoint = Memory.WalkingRoutePoints[Memory.WalkingRoutePointIndex];
                DistanceToRoutePoint = FVector::Dist2D(CurrentLocation, RoutePoint);
            }
        }

        FVector ToRoutePoint = RoutePoint - CurrentLocation;
        ToRoutePoint.Z = 0.f;
        const FVector RouteDirection = ToRoutePoint.GetSafeNormal();
        if (RouteDirection.IsNearlyZero())
        {
            return false;
        }

        OutGoal = RoutePoint;
        OutDirection = RouteDirection;
        DebugInfo.bUsedRouteWaypoint = true;
        DebugInfo.RoutePointCount = Memory.WalkingRoutePointCount;
        DebugInfo.RoutePointIndex = Memory.WalkingRoutePointIndex;
        DebugInfo.RoutePointDistance = DistanceToRoutePoint;
        DebugInfo.LocalGoal = RoutePoint;
        return true;
    }

    bool FindWalkingPathDirection(
        APawn& ControlledPawn,
        const FVector& Goal,
        const FVector& NavProjectionExtent,
        bool bAllowPartialPath,
        float MinWaypointDistance,
        FVector& OutProjectedGoal,
        FVector& OutPathDirection,
        bool& bOutSegmentBlocked,
        bool& bOutUsedDetour,
        FWalkingPathDebugInfo* OutDebugInfo = nullptr)
    {
        bOutSegmentBlocked = false;
        bOutUsedDetour = false;
        if (OutDebugInfo)
        {
            *OutDebugInfo = FWalkingPathDebugInfo();
            OutDebugInfo->RequestedGoal = Goal;
        }

        UWorld* World = ControlledPawn.GetWorld();
        UNavigationSystemV1* NavSystem = World ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
        if (OutDebugInfo)
        {
            OutDebugInfo->bHasWorld = World != nullptr;
            OutDebugInfo->bHasNavSystem = NavSystem != nullptr;
        }
        if (!World || !NavSystem)
        {
            if (OutDebugInfo)
            {
                OutDebugInfo->FailureReason = !World ? TEXT("NoWorld") : TEXT("NoNavSystem");
            }
            return false;
        }

        FNavLocation ProjectedGoal;
        if (!NavSystem->ProjectPointToNavigation(Goal, ProjectedGoal, NavProjectionExtent))
        {
            if (OutDebugInfo)
            {
                OutDebugInfo->FailureReason = TEXT("ProjectGoalFailed");
            }
            return false;
        }
        if (OutDebugInfo)
        {
            OutDebugInfo->bProjectedGoal = true;
            OutDebugInfo->ProjectedGoal = ProjectedGoal.Location;
        }

        const FVector CurrentLocation = ControlledPawn.GetActorLocation();
        UNavigationPath* Path = UNavigationSystemV1::FindPathToLocationSynchronously(
            World,
            CurrentLocation,
            ProjectedGoal.Location,
            &ControlledPawn);
        if (OutDebugInfo)
        {
            OutDebugInfo->bPathCreated = Path != nullptr;
            OutDebugInfo->bPathValid = Path && Path->IsValid();
            OutDebugInfo->bPathPartial = Path && Path->IsPartial();
            OutDebugInfo->PathPointCount = Path ? Path->PathPoints.Num() : 0;
        }
        if (!Path || !Path->IsValid() || (!bAllowPartialPath && Path->IsPartial()) || Path->PathPoints.Num() < 2)
        {
            const bool bProjectedGoalNearCurrent =
                FVector::DistSquared2D(ProjectedGoal.Location, CurrentLocation) <= FMath::Square(FMath::Max(20.f, MinWaypointDistance));
            FVector ToRequestedGoal = Goal - CurrentLocation;
            ToRequestedGoal.Z = 0.f;
            if (Path && !Path->IsValid() && bProjectedGoalNearCurrent && !ToRequestedGoal.IsNearlyZero())
            {
                FHitResult InvalidPathBlockHit;
                if (IsWalkingSegmentBlocked(ControlledPawn, CurrentLocation, Goal, &InvalidPathBlockHit))
                {
                    bOutSegmentBlocked = true;
                    if (OutDebugInfo)
                    {
                        OutDebugInfo->bSegmentBlocked = true;
                        OutDebugInfo->BlockImpact = InvalidPathBlockHit.ImpactPoint;
                        OutDebugInfo->BlockActor = GetNameSafe(InvalidPathBlockHit.GetActor());
                        OutDebugInfo->BlockComponent = GetNameSafe(InvalidPathBlockHit.GetComponent());
                        OutDebugInfo->FailureReason = TEXT("InvalidPathGoalSnappedToCurrent");
                    }

                    FVector DetourGoal = FVector::ZeroVector;
                    if (TryFindCollisionAwareDetour(
                        ControlledPawn,
                        *NavSystem,
                        CurrentLocation,
                        Goal,
                        Goal,
                        InvalidPathBlockHit,
                        NavProjectionExtent,
                        DetourGoal))
                    {
                        FVector ToDetour = DetourGoal - CurrentLocation;
                        ToDetour.Z = 0.f;
                        OutProjectedGoal = DetourGoal;
                        OutPathDirection = ToDetour.GetSafeNormal();
                        bOutUsedDetour = !OutPathDirection.IsNearlyZero();
                        if (OutDebugInfo)
                        {
                            OutDebugInfo->bUsedDetour = bOutUsedDetour;
                            OutDebugInfo->LocalGoal = DetourGoal;
                            OutDebugInfo->FailureReason = bOutUsedDetour ? TEXT("None") : TEXT("InvalidPathDetourZeroDirection");
                        }
                        return bOutUsedDetour;
                    }

                    FVector ReachableGoal = FVector::ZeroVector;
                    FVector ReachableDirection = FVector::ZeroVector;
                    if (TryFindReachableApproachPathDirection(
                        ControlledPawn,
                        *NavSystem,
                        Goal,
                        NavProjectionExtent,
                        MinWaypointDistance,
                        ReachableGoal,
                        ReachableDirection,
                        OutDebugInfo))
                    {
                        OutProjectedGoal = ReachableGoal;
                        OutPathDirection = ReachableDirection;
                        return true;
                    }

                    if (OutDebugInfo)
                    {
                        OutDebugInfo->FailureReason = TEXT("InvalidPathNoDetour");
                    }
                    return false;
                }
            }

            if (OutDebugInfo)
            {
                if (!Path)
                {
                    OutDebugInfo->FailureReason = TEXT("FindPathFailed");
                }
                else if (!Path->IsValid())
                {
                    OutDebugInfo->FailureReason = TEXT("InvalidPath");
                }
                else if (!bAllowPartialPath && Path->IsPartial())
                {
                    OutDebugInfo->FailureReason = TEXT("PartialPathRejected");
                }
                else
                {
                    OutDebugInfo->FailureReason = TEXT("TooFewPathPoints");
                }
            }
            return false;
        }

        const float MinWaypointDistanceSquared = FMath::Square(FMath::Max(20.f, MinWaypointDistance));
        int32 NextPointIndex = Path->PathPoints.Num() - 1;
        for (int32 PathPointIndex = 1; PathPointIndex < Path->PathPoints.Num(); ++PathPointIndex)
        {
            FVector ToPathPoint = Path->PathPoints[PathPointIndex] - CurrentLocation;
            ToPathPoint.Z = 0.f;
            if (ToPathPoint.SizeSquared() > MinWaypointDistanceSquared)
            {
                NextPointIndex = PathPointIndex;
                break;
            }
        }
        if (OutDebugInfo)
        {
            OutDebugInfo->NextPointIndex = NextPointIndex;
        }

        FVector LocalGoal = Path->PathPoints[NextPointIndex];
        FVector ToNextPoint = LocalGoal - CurrentLocation;
        ToNextPoint.Z = 0.f;
        if (OutDebugInfo)
        {
            OutDebugInfo->LocalGoal = LocalGoal;
            OutDebugInfo->FailureReason = TEXT("None");
        }

        if (ToNextPoint.IsNearlyZero() && Path->IsPartial())
        {
            FVector ToProjectedGoal = ProjectedGoal.Location - CurrentLocation;
            ToProjectedGoal.Z = 0.f;

            if (!ToProjectedGoal.IsNearlyZero())
            {
                FHitResult PartialBlockHit;
                if (IsWalkingSegmentBlocked(ControlledPawn, CurrentLocation, ProjectedGoal.Location, &PartialBlockHit))
                {
                    bOutSegmentBlocked = true;
                    if (OutDebugInfo)
                    {
                        OutDebugInfo->bSegmentBlocked = true;
                        OutDebugInfo->BlockImpact = PartialBlockHit.ImpactPoint;
                        OutDebugInfo->BlockActor = GetNameSafe(PartialBlockHit.GetActor());
                        OutDebugInfo->BlockComponent = GetNameSafe(PartialBlockHit.GetComponent());
                        OutDebugInfo->FailureReason = TEXT("PartialPathBlocked");
                    }

                    FVector DetourGoal = FVector::ZeroVector;
                    if (TryFindCollisionAwareDetour(
                        ControlledPawn,
                        *NavSystem,
                        CurrentLocation,
                        ProjectedGoal.Location,
                        ProjectedGoal.Location,
                        PartialBlockHit,
                        NavProjectionExtent,
                        DetourGoal))
                    {
                        LocalGoal = DetourGoal;
                        ToNextPoint = LocalGoal - CurrentLocation;
                        ToNextPoint.Z = 0.f;
                        bOutUsedDetour = true;
                        if (OutDebugInfo)
                        {
                            OutDebugInfo->bUsedDetour = true;
                            OutDebugInfo->LocalGoal = LocalGoal;
                            OutDebugInfo->FailureReason = TEXT("None");
                        }
                    }
                    else
                    {
                    if (OutDebugInfo)
                    {
                        OutDebugInfo->FailureReason = TEXT("PartialPathBlockedNoDetour");
                    }

                    FVector ReachableGoal = FVector::ZeroVector;
                    FVector ReachableDirection = FVector::ZeroVector;
                    if (TryFindReachableApproachPathDirection(
                        ControlledPawn,
                        *NavSystem,
                        Goal,
                        NavProjectionExtent,
                        MinWaypointDistance,
                        ReachableGoal,
                        ReachableDirection,
                        OutDebugInfo))
                    {
                        OutProjectedGoal = ReachableGoal;
                        OutPathDirection = ReachableDirection;
                        return true;
                    }
                    return false;
                }
                }
                else
                {
                    LocalGoal = ProjectedGoal.Location;
                    ToNextPoint = ToProjectedGoal;
                    if (OutDebugInfo)
                    {
                        OutDebugInfo->LocalGoal = LocalGoal;
                        OutDebugInfo->FailureReason = TEXT("PartialPathDirectToProjectedGoal");
                    }
                }
            }
        }

        FHitResult BlockHit;
        if (IsWalkingSegmentBlocked(ControlledPawn, CurrentLocation, LocalGoal, &BlockHit))
        {
            bOutSegmentBlocked = true;
            if (OutDebugInfo)
            {
                OutDebugInfo->bSegmentBlocked = true;
                OutDebugInfo->BlockImpact = BlockHit.ImpactPoint;
                OutDebugInfo->BlockActor = GetNameSafe(BlockHit.GetActor());
                OutDebugInfo->BlockComponent = GetNameSafe(BlockHit.GetComponent());
                OutDebugInfo->FailureReason = TEXT("SegmentBlocked");
            }

            FVector DetourGoal = FVector::ZeroVector;
            if (TryFindCollisionAwareDetour(
                ControlledPawn,
                *NavSystem,
                CurrentLocation,
                ProjectedGoal.Location,
                LocalGoal,
                BlockHit,
                NavProjectionExtent,
                DetourGoal))
            {
                LocalGoal = DetourGoal;
                ToNextPoint = LocalGoal - CurrentLocation;
                ToNextPoint.Z = 0.f;
                bOutUsedDetour = true;
                if (OutDebugInfo)
                {
                    OutDebugInfo->bUsedDetour = true;
                    OutDebugInfo->LocalGoal = LocalGoal;
                    OutDebugInfo->FailureReason = TEXT("None");
                }
            }
            else
            {
                if (OutDebugInfo)
                {
                    OutDebugInfo->FailureReason = TEXT("BlockedNoDetour");
                }

                FVector ReachableGoal = FVector::ZeroVector;
                FVector ReachableDirection = FVector::ZeroVector;
                if (TryFindReachableApproachPathDirection(
                    ControlledPawn,
                    *NavSystem,
                    Goal,
                    NavProjectionExtent,
                    MinWaypointDistance,
                    ReachableGoal,
                    ReachableDirection,
                    OutDebugInfo))
                {
                    OutProjectedGoal = ReachableGoal;
                    OutPathDirection = ReachableDirection;
                    return true;
                }
                return false;
            }
        }

        OutProjectedGoal = LocalGoal;
        OutPathDirection = ToNextPoint.GetSafeNormal();
        if (OutPathDirection.IsNearlyZero())
        {
            if (OutDebugInfo)
            {
                OutDebugInfo->FailureReason = TEXT("ZeroDirection");
            }
            return false;
        }
        if (OutDebugInfo)
        {
            OutDebugInfo->FailureReason = TEXT("None");
        }
        return true;
    }
}

UBTTask_FlyToBlackboardLocation::UBTTask_FlyToBlackboardLocation()
{
    NodeName = TEXT("Fly To Blackboard Location");
    bNotifyTick = true;

    TargetLocationKey.SelectedKeyName = TEXT("DetectedTargetLocation");
    HasTargetKey.SelectedKeyName = TEXT("HasDetectedTarget");
    HasTargetKey.AddBoolFilter(this, GET_MEMBER_NAME_CHECKED(UBTTask_FlyToBlackboardLocation, HasTargetKey));
    UseFlyingMovementKey.SelectedKeyName = TEXT("UseFlyingMovement");
    UseFlyingMovementKey.AddBoolFilter(this, GET_MEMBER_NAME_CHECKED(UBTTask_FlyToBlackboardLocation, UseFlyingMovementKey));
    MaintainDistanceKey.SelectedKeyName = TEXT("MaintainDistance");
    MaintainDistanceKey.AddBoolFilter(this, GET_MEMBER_NAME_CHECKED(UBTTask_FlyToBlackboardLocation, MaintainDistanceKey));
}

EBTNodeResult::Type UBTTask_FlyToBlackboardLocation::ExecuteTask(
    UBehaviorTreeComponent& OwnerComp,
    uint8* NodeMemory)
{
    AAIController* AIController = OwnerComp.GetAIOwner();
    APawn* ControlledPawn = AIController ? AIController->GetPawn() : nullptr;
    UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent();
    if (!ControlledPawn || !Blackboard)
    {
        return EBTNodeResult::Failed;
    }

    FBTTaskFlyToBlackboardLocationMemory* Memory =
        reinterpret_cast<FBTTaskFlyToBlackboardLocationMemory*>(NodeMemory);
    if (Memory)
    {
        *Memory = FBTTaskFlyToBlackboardLocationMemory();
    }

    if (Memory)
    {
        if (ABotAIController* BotAIController = Cast<ABotAIController>(AIController))
        {
            BotAIController->PushRandomMovementBlock();
            Memory->bRandomMovementBlocked = true;
        }
    }

    if (bDrawDebug && IsPathfindingDecisionLoggingEnabled() && GEngine)
    {
        GEngine->AddOnScreenDebugMessage(
            reinterpret_cast<uint64>(this),
            0.5f,
            FColor::Cyan,
            TEXT("Fly task: started"));
    }
    return EBTNodeResult::InProgress;
}

void UBTTask_FlyToBlackboardLocation::TickTask(
    UBehaviorTreeComponent& OwnerComp,
    uint8* NodeMemory,
    float DeltaSeconds)
{
    Super::TickTask(OwnerComp, NodeMemory, DeltaSeconds);

    AAIController* AIController = OwnerComp.GetAIOwner();
    APawn* ControlledPawn = AIController ? AIController->GetPawn() : nullptr;
    UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent();
    if (!ControlledPawn || !Blackboard)
    {
        FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
        return;
    }

    FBTTaskFlyToBlackboardLocationMemory* Memory =
        reinterpret_cast<FBTTaskFlyToBlackboardLocationMemory*>(NodeMemory);
    UWorld* World = ControlledPawn->GetWorld();
    const float TaskTime = World ? World->GetTimeSeconds() : 0.f;
    const bool bUseFlyingMovement = ResolveUseFlyingMovement(*ControlledPawn, *Blackboard);
    const FVector CurrentLocation = ControlledPawn->GetActorLocation();
    const FVector RawTargetLocation = Blackboard->GetValueAsVector(TargetLocationKey.SelectedKeyName);
    FVector TargetLocation = RawTargetLocation;
    if (!bUseFlyingMovement && Memory)
    {
        const bool bSnapshotTooOld =
            WalkingTargetSnapshotMaxAge > 0.f &&
            TaskTime - Memory->LastTargetSnapshotTime >= WalkingTargetSnapshotMaxAge;
        const bool bTargetMovedEnough =
            !Memory->bHasTargetSnapshot ||
            FVector::DistSquared2D(RawTargetLocation, Memory->TargetSnapshot) >=
                FMath::Square(FMath::Max(0.f, WalkingTargetSnapshotRefreshDistance));
        if (bTargetMovedEnough || bSnapshotTooOld)
        {
            Memory->TargetSnapshot = RawTargetLocation;
            Memory->LastTargetSnapshotTime = TaskTime;
            Memory->bHasTargetSnapshot = true;
        }
        TargetLocation = Memory->TargetSnapshot;
    }
    FVector ToTarget = TargetLocation - CurrentLocation;
    if (!bUseFlyingMovement)
    {
        ToTarget.Z = 0.f;
    }
    const float Distance = ToTarget.Size();
    const FVector DirectionToTarget = ToTarget.GetSafeNormal();
    const bool bMaintainDistance = ResolveMaintainDistance(*Blackboard);
    const float ActiveDesiredTargetDistance = DesiredTargetDistance;
    const float ActiveDistanceTolerance = DistanceTolerance;
    const float EffectiveWalkingMoveAcceptanceRadius = bUseFlyingMovement
        ? WalkingMoveAcceptanceRadius
        : FMath::Max3(WalkingMoveAcceptanceRadius, MinWalkingMoveAcceptanceRadius, 120.f);
    const float EffectiveWalkingPathAcceptanceRadius = bUseFlyingMovement
        ? WalkingMoveAcceptanceRadius
        : FMath::Clamp(WalkingPathAcceptanceRadius, 5.f, EffectiveWalkingMoveAcceptanceRadius);

    float HoldRadius = AcceptanceRadius;
    float MoveDistance = 0.f;
    FVector MoveDirection = FVector::ZeroVector;
    FVector MoveGoal = TargetLocation;
    ETargetMoveIntent MoveIntent = ETargetMoveIntent::Hold;

    if (!bMaintainDistance)
    {
        HoldRadius = FMath::Max(HoldRadius, MinApproachStopDistance);
    }

    if (!bUseFlyingMovement)
    {
        HoldRadius = FMath::Max(0.f, HoldRadius);
        if (!bMaintainDistance)
        {
            HoldRadius = FMath::Max(HoldRadius, EffectiveWalkingMoveAcceptanceRadius);
            HoldRadius += WalkingCompletionSlack;
        }
    }

    if (bMaintainDistance)
    {
        const float DistanceError = Distance - ActiveDesiredTargetDistance;
        HoldRadius = ActiveDistanceTolerance;

        if (FMath::Abs(DistanceError) > ActiveDistanceTolerance)
        {
            MoveIntent = DistanceError > 0.f ? ETargetMoveIntent::Approach : ETargetMoveIntent::Retreat;
            MoveDirection = DistanceError > 0.f ? DirectionToTarget : -DirectionToTarget;
            if (MoveDirection.IsNearlyZero())
            {
                MoveDirection = -ControlledPawn->GetActorForwardVector();
                if (!bUseFlyingMovement)
                {
                    MoveDirection.Z = 0.f;
                }
                MoveDirection = MoveDirection.GetSafeNormal();
            }
            MoveDistance = FMath::Abs(DistanceError);
            MoveGoal = CurrentLocation + (MoveDirection * MoveDistance);
        }
    }
    else if (Distance > HoldRadius)
    {
        MoveIntent = ETargetMoveIntent::Approach;
        MoveDirection = DirectionToTarget;
        MoveDistance = FMath::Max(0.f, Distance - HoldRadius);
        MoveGoal = TargetLocation;
    }

    if (!bUseFlyingMovement)
    {
        MoveGoal.Z = CurrentLocation.Z;
    }

    const bool bActionComplete = bMaintainDistance
        ? FMath::Abs(Distance - ActiveDesiredTargetDistance) <= ActiveDistanceTolerance
        : Distance <= HoldRadius;

    FVector WalkingNavGoal = MoveGoal;
    FWalkingPathDebugInfo WalkingPathDebug;
    bool bHasWalkingNavGoal = false;
    bool bWalkingPathSegmentBlocked = false;
    bool bWalkingPathUsedDetour = false;
    bool bWalkingMoveRequestReused = false;
    bool bWalkingMoveStuck = false;
    if (!bUseFlyingMovement && bUseNavMeshForWalking && MoveIntent != ETargetMoveIntent::Hold)
    {
        FVector WalkingPathDirection = FVector::ZeroVector;
        if (FindSimpleWalkingNavGoal(
            *ControlledPawn,
            MoveGoal,
            NavProjectionExtent,
            bAllowPartialNavPath,
            EffectiveWalkingPathAcceptanceRadius,
            WalkingNavGoal,
            WalkingPathDirection,
            WalkingPathDebug))
        {
            bHasWalkingNavGoal = true;
            MoveDirection = WalkingPathDirection;
        }
        bWalkingPathSegmentBlocked = !bHasWalkingNavGoal;
    }

    const bool bIsRetreating = MoveIntent == ETargetMoveIntent::Retreat;
    const bool bShouldFaceMoveDirection = !bUseFlyingMovement &&
        bRotateWalkingTowardMoveDirection &&
        !MoveDirection.IsNearlyZero() &&
        (!bFaceTargetWhileRetreating || !bIsRetreating);
    const TCHAR* RotationDriver = bShouldFaceMoveDirection ? TEXT("MoveDirection") : TEXT("TargetDirection");
    FRotator LastDesiredRotation = ControlledPawn->GetActorRotation();
    FRotator LastCameraForwardOffset = FRotator::ZeroRotator;
    float LastYawErrorDegrees = 0.f;

    const auto LogMovementDecision = [&, NodeMemory](const TCHAR* MovementDriver, int32 NavResult = INDEX_NONE) -> bool
    {
        if (!bLogMovementDecision || !IsPathfindingDecisionLoggingEnabled())
        {
            return false;
        }

        UWorld* World = ControlledPawn->GetWorld();
        if (!World)
        {
            return false;
        }

        FBTTaskFlyToBlackboardLocationMemory* Memory =
            reinterpret_cast<FBTTaskFlyToBlackboardLocationMemory*>(NodeMemory);
        const float CurrentTime = World->GetTimeSeconds();
        if (Memory && CurrentTime - Memory->LastMovementDecisionLogTime < MovementDecisionLogInterval)
        {
            return false;
        }

        if (Memory)
        {
            Memory->LastMovementDecisionLogTime = CurrentTime;
        }

        UE_LOG(
            LogTemp,
            Log,
            TEXT("BTMoveDecision: pawn=%s intent=%s driver=%s navResult=%d rotation=%s locomotion=%s/%s targetMode=%s/%s taskMode=%s dist=%.1f desired=%.1f tolerance=%.1f moveDistance=%.1f target=%s rawTarget=%s pawnLoc=%s goal=%s navGoal=%s navGoalValid=%s navBlocked=%s navDetour=%s navRequestReused=%s navRoute=%s navRouteIndex=%d navRouteCount=%d navRouteDist=%.1f navRouteStuck=%s navReachableGoal=%s navReachableCandidates=%d navBlockActor=%s navBlockComponent=%s navBlockImpact=%s navFailure=%s navProjected=%s navPathCreated=%s navPathValid=%s navPartial=%s navPoints=%d navNextPoint=%d direction=%s targetDirection=%s actorRot=%s desiredRot=%s yawError=%.1f cameraOffset=%s alignCamera=%s targetKey=%s maintainKey=%s maintainValue=%s useFlyingKey=%s useFlyingValue=%s"),
            *ControlledPawn->GetName(),
            LexToString(MoveIntent),
            MovementDriver,
            NavResult,
            RotationDriver,
            bUseFlyingMovement ? TEXT("Flying") : TEXT("Walking"),
            LexToString(LocomotionModeSource),
            bMaintainDistance ? TEXT("MaintainDistance") : TEXT("ApproachTarget"),
            LexToString(TargetMovementModeSource),
            LexToString(TargetMovementMode),
            Distance,
            bMaintainDistance ? ActiveDesiredTargetDistance : 0.f,
            bMaintainDistance ? ActiveDistanceTolerance : HoldRadius,
            MoveDistance,
            *TargetLocation.ToCompactString(),
            *RawTargetLocation.ToCompactString(),
            *CurrentLocation.ToCompactString(),
            *MoveGoal.ToCompactString(),
            *WalkingNavGoal.ToCompactString(),
            bHasWalkingNavGoal ? TEXT("true") : TEXT("false"),
            bWalkingPathSegmentBlocked ? TEXT("true") : TEXT("false"),
            bWalkingPathUsedDetour ? TEXT("true") : TEXT("false"),
            bWalkingMoveRequestReused ? TEXT("true") : TEXT("false"),
            WalkingPathDebug.bUsedRouteWaypoint ? TEXT("true") : TEXT("false"),
            WalkingPathDebug.RoutePointIndex,
            WalkingPathDebug.RoutePointCount,
            WalkingPathDebug.RoutePointDistance,
            WalkingPathDebug.bRouteStuck ? TEXT("true") : TEXT("false"),
            WalkingPathDebug.bUsedReachableGoal ? TEXT("true") : TEXT("false"),
            WalkingPathDebug.ReachableGoalCandidates,
            *WalkingPathDebug.BlockActor,
            *WalkingPathDebug.BlockComponent,
            *WalkingPathDebug.BlockImpact.ToCompactString(),
            *WalkingPathDebug.FailureReason,
            WalkingPathDebug.bProjectedGoal ? TEXT("true") : TEXT("false"),
            WalkingPathDebug.bPathCreated ? TEXT("true") : TEXT("false"),
            WalkingPathDebug.bPathValid ? TEXT("true") : TEXT("false"),
            WalkingPathDebug.bPathPartial ? TEXT("true") : TEXT("false"),
            WalkingPathDebug.PathPointCount,
            WalkingPathDebug.NextPointIndex,
            *MoveDirection.ToCompactString(),
            *DirectionToTarget.ToCompactString(),
            *ControlledPawn->GetActorRotation().ToCompactString(),
            *LastDesiredRotation.ToCompactString(),
            LastYawErrorDegrees,
            *LastCameraForwardOffset.ToCompactString(),
            bAlignCameraForwardToTarget ? TEXT("true") : TEXT("false"),
            *TargetLocationKey.SelectedKeyName.ToString(),
            *MaintainDistanceKey.SelectedKeyName.ToString(),
            bMaintainDistance ? TEXT("true") : TEXT("false"),
            *UseFlyingMovementKey.SelectedKeyName.ToString(),
            bUseFlyingMovement ? TEXT("true") : TEXT("false"));

        if (bLogPathfindingObjects && IsPathfindingObjectLoggingEnabled() && !bUseFlyingMovement && MoveIntent != ETargetMoveIntent::Hold)
        {
            const FVector ObjectLogGoal = bHasWalkingNavGoal ? WalkingNavGoal : MoveGoal;
            LogPresentPathfindingObjects(
                *ControlledPawn,
                CurrentLocation,
                ObjectLogGoal,
                WalkingPathDebug,
                PathfindingObjectLogCorridorRadius,
                PathfindingObjectLogVerticalExtent,
                MaxPathfindingObjectsToLog);
        }

        return true;
    };

    const auto FinishCompletedAction = [&]()
    {
        if (bClearHasTargetOnActionComplete && !HasTargetKey.SelectedKeyName.IsNone())
        {
            Blackboard->SetValueAsBool(HasTargetKey.SelectedKeyName, false);
        }
        FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
    };

    const FVector RotationDirection = bShouldFaceMoveDirection ? MoveDirection : DirectionToTarget;
    if (bRotateTowardTarget && !RotationDirection.IsNearlyZero())
    {
        if (AIController)
        {
            AIController->ClearFocus(EAIFocusPriority::Gameplay);
        }

        FRotator DesiredRotation = RotationDirection.Rotation() + RotationOffset;
        if (bAlignCameraForwardToTarget)
        {
            if (const UCameraComponent* Camera = ControlledPawn->FindComponentByClass<UCameraComponent>())
            {
                const float ActorForwardYaw = CalculatePlanarYaw(ControlledPawn->GetActorForwardVector());
                const float CameraForwardYaw = CalculatePlanarYaw(Camera->GetForwardVector());
                const float CameraYawOffset = FMath::FindDeltaAngleDegrees(ActorForwardYaw, CameraForwardYaw);
                LastCameraForwardOffset = FRotator(0.f, CameraYawOffset, 0.f);
                DesiredRotation = FRotator(
                    RotationDirection.Rotation().Pitch,
                    RotationDirection.Rotation().Yaw - CameraYawOffset,
                    0.f) + RotationOffset;
            }
        }
        if (!bUsePitchRotation || !bUseFlyingMovement)
        {
            DesiredRotation.Pitch = 0.f;
        }
        DesiredRotation.Roll = 0.f;
        DesiredRotation.Normalize();
        LastDesiredRotation = DesiredRotation;
        LastYawErrorDegrees = FMath::FindDeltaAngleDegrees(
            ControlledPawn->GetActorRotation().Yaw,
            DesiredRotation.Yaw);

        const FRotator NewRotation = FMath::RInterpTo(
            ControlledPawn->GetActorRotation(),
            DesiredRotation,
            DeltaSeconds,
            RotationInterpSpeed);
        if (AIController)
        {
            AIController->SetControlRotation(NewRotation);
        }
        ControlledPawn->SetActorRotation(NewRotation);
        LastYawErrorDegrees = FMath::FindDeltaAngleDegrees(
            NewRotation.Yaw,
            DesiredRotation.Yaw);
    }

    const bool bShouldHold = MoveDirection.IsNearlyZero()
        || bActionComplete;
    if (bShouldHold)
    {
        if (ACharacter* Character = Cast<ACharacter>(ControlledPawn))
        {
            if (UCharacterMovementComponent* MovementComponent = Character->GetCharacterMovement())
            {
                if (bUseFlyingMovement)
                {
                    MovementComponent->Velocity = FVector::ZeroVector;
                }
                else
                {
                    MovementComponent->Velocity.X = 0.f;
                    MovementComponent->Velocity.Y = 0.f;
                }
            }
        }

        if (!bUseFlyingMovement && AIController)
        {
            AIController->StopMovement();
        }
        if (Memory)
        {
            Memory->bHasLastMoveRequest = false;
            ClearWalkingRoute(*Memory);
        }

        LogMovementDecision(TEXT("Hold"));

        const bool bShouldDrawDecisionDebug = bDrawDebug && IsPathfindingDecisionLoggingEnabled();
        const bool bShouldDrawPathDebug =
            !IsDetectionAndPathDebugDrawingDisabled() && bDrawPathDebug && IsPathfindingObjectLoggingEnabled();

        if (bShouldDrawDecisionDebug && GEngine)
        {
            GEngine->AddOnScreenDebugMessage(
                reinterpret_cast<uint64>(this),
                0.2f,
                FColor::Cyan,
                FString::Printf(
                    TEXT("Fly task: holding target dist %.0f desired %.0f"),
                    Distance,
                    bMaintainDistance ? ActiveDesiredTargetDistance : 0.f));
        }
        if (bShouldDrawPathDebug && !bUseFlyingMovement)
        {
            if (UWorld* DebugWorld = ControlledPawn->GetWorld())
            {
                DrawSimpleWalkingPathDebug(
                    *DebugWorld,
                    ControlledPawn->GetActorLocation(),
                    TargetLocation,
                    MoveGoal,
                    WalkingNavGoal,
                    HoldRadius,
                    WalkingPathDebug);
            }
        }
        if (bFinishWhenActionComplete && bActionComplete)
        {
            FinishCompletedAction();
        }
        return;
    }

    if (ACharacter* Character = Cast<ACharacter>(ControlledPawn))
    {
        if (UCharacterMovementComponent* MovementComponent = Character->GetCharacterMovement())
        {
            if (bUseFlyingMovement && MovementComponent->MovementMode != MOVE_Flying)
            {
                MovementComponent->SetMovementMode(MOVE_Flying);
            }
            else if (!bUseFlyingMovement && MovementComponent->MovementMode == MOVE_Flying)
            {
                MovementComponent->Velocity.Z = 0.f;
                MovementComponent->SetMovementMode(MOVE_Walking);
            }

            if (bUseFlyingMovement)
            {
                const float MovementSpeed = FMath::Max(MinFlySpeed, MovementComponent->GetMaxSpeed() * MovementScale);
                MovementComponent->Velocity = MoveDirection * MovementSpeed;
                LogMovementDecision(TEXT("DirectFlightVelocity"));
            }
            else
            {
                bool bMovedWithNavMesh = false;
                EPathFollowingRequestResult::Type MoveResult = EPathFollowingRequestResult::Failed;
                if (bUseNavMeshForWalking && AIController)
                {
                    if (bHasWalkingNavGoal)
                    {
                        const bool bSameMoveGoal = Memory &&
                            Memory->bHasLastMoveRequest &&
                            FVector::DistSquared2D(WalkingNavGoal, Memory->LastMoveRequestGoal) <=
                                FMath::Square(FMath::Max(0.f, WalkingMoveReissueDistance));
                        if (Memory && bSameMoveGoal)
                        {
                            const float CurrentDistanceToMoveGoal = FVector::Dist2D(CurrentLocation, WalkingNavGoal);
                            if (CurrentDistanceToMoveGoal + FMath::Max(0.f, WalkingStuckMinProgress) < Memory->BestMoveRequestDistance)
                            {
                                Memory->BestMoveRequestDistance = CurrentDistanceToMoveGoal;
                                Memory->LastMoveProgressTime = TaskTime;
                            }
                            else if (WalkingStuckTimeout > 0.f &&
                                TaskTime - Memory->LastMoveProgressTime >= WalkingStuckTimeout)
                            {
                                bWalkingMoveStuck = true;
                                WalkingPathDebug.bRouteStuck = true;
                                WalkingPathDebug.FailureReason = TEXT("MoveProgressStuck");
                                AIController->StopMovement();
                                Memory->bHasLastMoveRequest = false;
                            }
                        }

                        const bool bRecentlyIssuedMove = Memory &&
                            TaskTime - Memory->LastMoveRequestTime < FMath::Max(0.f, WalkingMoveReissueInterval);
                        const EPathFollowingStatus::Type MoveStatus = AIController->GetMoveStatus();
                        const bool bPathFollowingActive =
                            MoveStatus == EPathFollowingStatus::Moving ||
                            MoveStatus == EPathFollowingStatus::Waiting ||
                            MoveStatus == EPathFollowingStatus::Paused;

                        if (!bWalkingMoveStuck && bSameMoveGoal && (bRecentlyIssuedMove || bPathFollowingActive))
                        {
                            bWalkingMoveRequestReused = true;
                            bMovedWithNavMesh = true;
                            MoveResult = Memory ? Memory->LastMoveRequestResult : EPathFollowingRequestResult::RequestSuccessful;
                        }
                        else
                        {
                            MoveResult = AIController->MoveToLocation(
                                WalkingNavGoal,
                                EffectiveWalkingPathAcceptanceRadius,
                                false,
                                true,
                                false,
                                true,
                                nullptr,
                                bAllowPartialNavPath);
                            bMovedWithNavMesh = MoveResult != EPathFollowingRequestResult::Failed;
                            if (Memory && bMovedWithNavMesh)
                            {
                                Memory->bHasLastMoveRequest = true;
                                Memory->LastMoveRequestGoal = WalkingNavGoal;
                                Memory->LastMoveRequestTime = TaskTime;
                                Memory->LastMoveRequestResult = MoveResult;
                                Memory->bLastMoveRequestUsedDetour = bWalkingPathUsedDetour;
                                Memory->BestMoveRequestDistance = FVector::Dist2D(CurrentLocation, WalkingNavGoal);
                                Memory->LastMoveProgressTime = TaskTime;
                            }
                            else if (Memory)
                            {
                                Memory->bHasLastMoveRequest = false;
                            }
                        }
                        if (MoveResult == EPathFollowingRequestResult::AlreadyAtGoal && bActionComplete && bFinishWhenActionComplete)
                        {
                            LogMovementDecision(TEXT("NavAlreadyAtGoal"), static_cast<int32>(MoveResult));
                            FinishCompletedAction();
                            return;
                        }
                        if (Memory && MoveResult != EPathFollowingRequestResult::AlreadyAtGoal)
                        {
                            Memory->bHoldingBestEffortGoal = false;
                        }
                        if (MoveResult == EPathFollowingRequestResult::AlreadyAtGoal && !bActionComplete && Memory)
                        {
                            Memory->bHasLastMoveRequest = false;
                            ClearWalkingRoute(*Memory);
                            MovementComponent->Velocity.X = 0.f;
                            MovementComponent->Velocity.Y = 0.f;
                        }
                    }
                }

                const bool bCanUseDirectWalkingFallback =
                    MovementComponent->MovementMode == MOVE_Walking ||
                    MovementComponent->MovementMode == MOVE_NavWalking ||
                    MovementComponent->IsMovingOnGround();
                if (!bMovedWithNavMesh && bFallbackToDirectWalkingWhenNavMoveFails && bCanUseDirectWalkingFallback)
                {
                    ControlledPawn->AddMovementInput(MoveDirection, MovementScale, true);
                }

                const TCHAR* WalkingMovementDriver = bWalkingMoveStuck
                    ? TEXT("NavRepathAfterStuck")
                    : (bMovedWithNavMesh
                    ? (bWalkingMoveRequestReused
                        ? (bWalkingPathUsedDetour ? TEXT("NavMoveDetourCached") : TEXT("NavMoveCached"))
                        : (bWalkingPathUsedDetour ? TEXT("NavMoveDetour") : TEXT("NavMove")))
                    : (bFallbackToDirectWalkingWhenNavMoveFails && bCanUseDirectWalkingFallback
                        ? TEXT("DirectWalkFallback")
                        : (bWalkingPathSegmentBlocked ? TEXT("NoNavPath") : TEXT("NavMoveFailed"))));
                LogMovementDecision(
                    WalkingMovementDriver,
                    static_cast<int32>(MoveResult));
            }
        }
    }
    else
    {
        ControlledPawn->AddMovementInput(MoveDirection, MovementScale, true);
        LogMovementDecision(TEXT("DirectPawnInput"));
    }

    if (AIController && bUpdateFocus && (bUseFlyingMovement || !bRotateWalkingTowardMoveDirection))
    {
        AIController->SetFocalPoint(TargetLocation);
    }
    else if (AIController && !bUseFlyingMovement && bRotateWalkingTowardMoveDirection)
    {
        AIController->ClearFocus(EAIFocusPriority::Gameplay);
    }

    const bool bShouldDrawDecisionDebug = bDrawDebug && IsPathfindingDecisionLoggingEnabled();
    const bool bShouldDrawPathDebug =
        !IsDetectionAndPathDebugDrawingDisabled() && bDrawPathDebug && IsPathfindingObjectLoggingEnabled();

    if (bShouldDrawDecisionDebug || bShouldDrawPathDebug)
    {
        if (bShouldDrawDecisionDebug && GEngine)
        {
            GEngine->AddOnScreenDebugMessage(
                reinterpret_cast<uint64>(this),
                0.2f,
                FColor::Cyan,
                FString::Printf(
                    TEXT("Fly task: cur=%s target=%s dist=%.0f"),
                    *ControlledPawn->GetActorLocation().ToCompactString(),
                    *TargetLocation.ToCompactString(),
                    Distance));
        }

        if (bShouldDrawPathDebug)
        {
            if (UWorld* DebugWorld = ControlledPawn->GetWorld())
            {
                if (!bUseFlyingMovement)
                {
                    DrawSimpleWalkingPathDebug(
                        *DebugWorld,
                        ControlledPawn->GetActorLocation(),
                        TargetLocation,
                        MoveGoal,
                        WalkingNavGoal,
                        HoldRadius,
                        WalkingPathDebug);
                }
                else
                {
                    DrawDebugSphere(DebugWorld, TargetLocation, HoldRadius, 12, FColor::Cyan, false, 0.2f);
                    DrawDebugLine(DebugWorld, ControlledPawn->GetActorLocation(), MoveGoal, FColor::Cyan, false, 0.2f, 0, 2.f);
                }
                if (bMaintainDistance)
                {
                    DrawDebugSphere(World, TargetLocation, ActiveDesiredTargetDistance, 24, FColor::Emerald, false, 0.2f);
                }
            }
        }
    }
}

uint16 UBTTask_FlyToBlackboardLocation::GetInstanceMemorySize() const
{
    return sizeof(FBTTaskFlyToBlackboardLocationMemory);
}

void UBTTask_FlyToBlackboardLocation::OnTaskFinished(
    UBehaviorTreeComponent& OwnerComp,
    uint8* NodeMemory,
    EBTNodeResult::Type TaskResult)
{
    Super::OnTaskFinished(OwnerComp, NodeMemory, TaskResult);

    AAIController* AIController = OwnerComp.GetAIOwner();
    FBTTaskFlyToBlackboardLocationMemory* Memory =
        reinterpret_cast<FBTTaskFlyToBlackboardLocationMemory*>(NodeMemory);
    if (Memory && Memory->bRandomMovementBlocked)
    {
        if (ABotAIController* BotAIController = Cast<ABotAIController>(AIController))
        {
            BotAIController->PopRandomMovementBlock();
        }
        Memory->bRandomMovementBlocked = false;
    }

    APawn* ControlledPawn = AIController ? AIController->GetPawn() : nullptr;
    UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent();
    const bool bUseFlyingMovement = ControlledPawn && Blackboard
        ? ResolveUseFlyingMovement(*ControlledPawn, *Blackboard)
        : true;
    if (ACharacter* Character = Cast<ACharacter>(ControlledPawn))
    {
        if (UCharacterMovementComponent* MovementComponent = Character->GetCharacterMovement())
        {
            if (bUseFlyingMovement)
            {
                MovementComponent->Velocity = FVector::ZeroVector;
            }
            else
            {
                MovementComponent->Velocity.X = 0.f;
                MovementComponent->Velocity.Y = 0.f;
            }
        }
    }

    if (AIController)
    {
        AIController->StopMovement();
        AIController->ClearFocus(EAIFocusPriority::Gameplay);
    }
}

bool UBTTask_FlyToBlackboardLocation::ResolveUseFlyingMovement(
    const APawn& ControlledPawn,
    const UBlackboardComponent& Blackboard) const
{
    if (IsWalkingBot(ControlledPawn))
    {
        return false;
    }

    switch (LocomotionModeSource)
    {
    case EBTLocomotionModeSource::ForceWalking:
        return false;
    case EBTLocomotionModeSource::ForceFlying:
        return true;
    case EBTLocomotionModeSource::BlackboardBool:
        if (!UseFlyingMovementKey.SelectedKeyName.IsNone())
        {
            return Blackboard.GetValueAsBool(UseFlyingMovementKey.SelectedKeyName);
        }
        break;
    case EBTLocomotionModeSource::BotLocomotion:
    default:
        break;
    }

    const ABotCharacter* BotCharacter = Cast<ABotCharacter>(&ControlledPawn);
    return !BotCharacter || BotCharacter->IsFlyingBot();
}

bool UBTTask_FlyToBlackboardLocation::ResolveMaintainDistance(const UBlackboardComponent& Blackboard) const
{
    if (TargetMovementModeSource == EBTTargetMovementModeSource::BlackboardBool &&
        !MaintainDistanceKey.SelectedKeyName.IsNone())
    {
        return Blackboard.GetValueAsBool(MaintainDistanceKey.SelectedKeyName);
    }

    return TargetMovementMode == EBTTargetMovementMode::MaintainDistance;
}
