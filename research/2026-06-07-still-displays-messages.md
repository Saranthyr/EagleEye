# Still Displays Messages

- Created: 2026-06-07T19:30:55+03:00
- Task: Investigate why messages still display after debug flags disabled

## Entries

### 2026-06-07T19:31:38+03:00 - finding: On-screen debug was not globally gated.

- Detail: Source/EagleEye/Private/BTServ_UpdateCrowTargetDetection.cpp gates UE_LOG with
  bEnableDetectionDebugLogs, but AddOnScreenDebugMessage and DrawDebugSphere/Line still use local
  bDrawDebug only. Source/EagleEye/Private/BTTask_FlyToBlackboardLocation.cpp likewise uses local
  bDrawDebug/bDrawPathDebug for AddOnScreenDebugMessage and DrawDebug shapes.

### 2026-06-07T19:32:41+03:00 - validation: Build succeeded after screen-debug gates.

- Detail: Ran Unreal Build.sh EagleEyeEditor Linux Development. Result: Succeeded. Changed
  BTServ_UpdateCrowTargetDetection.cpp and BTTask_FlyToBlackboardLocation.cpp so global menu flags
  gate AddOnScreenDebugMessage and DrawDebug calls, not only UE_LOG.

## Report Notes

- Main findings:
  - Root cause: previous fixes disabled log output only, while on-screen debug messages/draw shapes still used local Behavior Tree booleans. Fix: detection debug flag now gates Crow AddOnScreenDebugMessage, Crow DrawDebugSphere/Line, and Crow UE_LOG; pathfinding decision flag gates Fly task on-screen messages; pathfinding object flag gates Fly task debug shapes. Build succeeded.
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
