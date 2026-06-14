# ProcGen Nav Debug

- Created: 2026-06-14T08:56:39+03:00
- Task: Check why bots cannot walk on pathfinding on ProcGen map though NavMeshBoundsVolume present and green

## Entries

### 2026-06-14T08:59:23+03:00 - finding: Navmesh exists and path requests can succeed on ProcGen

- Detail: Saved/Logs/EagleEye.log shows ProcGen navSystem=valid, defaultNav=RecastNavMesh-Default,
  NavBoundsCreated, UNavigationSystemV1::Build. Later BTMoveDecision for AnotherBot_C_10 has
  driver=NavMove, navResult=2, navPathCreated=true, navPathValid=true, navPartial=false.

### 2026-06-14T08:59:47+03:00 - decision: Patch random-walk StopMovement ownership

- Detail: UpdateRandomWalking previously called StopMovement every tick when HasDetectedTarget blocked random
  walking. That same blackboard key activates BT target movement, so BT MoveToLocation could be
  canceled repeatedly despite valid nav paths. Added bRandomWalkMoveActive so blocked random walking
  only stops a previous random-walk request once.

### 2026-06-14T09:00:09+03:00 - validation: EagleEyeEditor build succeeded

- Detail: Ran /home/saranthyr/Unreal_Engine_5.6.1/Engine/Build/BatchFiles/Linux/Build.sh EagleEyeEditor Linux
  Development EagleEye.uproject -WaitMutex. Result: Succeeded. Build warned ONNX Runtime not found,
  unrelated to nav/movement.

## Report Notes

- Main findings:
  - ProcGen navmesh generation appears healthy; latest logs show valid Recast nav data and successful BT path creation. Walking bots likely failed to visibly follow paths because ABotAIController::UpdateRandomWalking called StopMovement every tick while HasDetectedTarget blocked random walking, racing/canceling BT MoveToLocation target-pursuit requests. Patch adds bRandomWalkMoveActive so only an owned random-walk MoveTo is stopped once, leaving behavior-tree nav movement alone. Build validated.
- Evidence to cite:
  -
- Decisions and rationale:
  -
- Validation performed:
  -
- Unresolved questions:
  -
- Suggested report angle:
  -

### 2026-06-14T09:02:20+03:00 - finding: User requested explicit BT task random movement gating

- Detail: Need disable random walking/random movement while UBTTask_FlyToBlackboardLocation runs, then restore
  when task completes/aborts.

### 2026-06-14T09:04:15+03:00 - decision: Add BT task random movement block counter

- Detail: Added ABotAIController PushRandomMovementBlock/PopRandomMovementBlock and BT task memory flag.
  UBTTask_FlyToBlackboardLocation pushes on ExecuteTask and pops in OnTaskFinished, so random
  flight/walk resumes after success/failure/abort. Random flight blackboard block no longer calls
  StopMovement because it has no owned path request.

### 2026-06-14T09:04:44+03:00 - validation: EagleEyeEditor build succeeded after BT random movement gating

- Detail: Ran /home/saranthyr/Unreal_Engine_5.6.1/Engine/Build/BatchFiles/Linux/Build.sh EagleEyeEditor Linux
  Development EagleEye.uproject -WaitMutex. Result: Succeeded. ONNX Runtime warning remains
  unrelated.

## Report Notes

- Main findings:
  - Final fix: UBTTask_FlyToBlackboardLocation explicitly suspends ABotAIController random flight/walk while active and resumes when task finishes. ABotAIController tracks random-walk-owned MoveTo requests separately so StopMovement only cancels random movement, not BT path following. Build validated successfully.
- Evidence to cite:
  -
- Decisions and rationale:
  -
- Validation performed:
  -
- Unresolved questions:
  -
- Suggested report angle:
  -
