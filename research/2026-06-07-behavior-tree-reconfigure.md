# Behavior tree reconfiguration

- Created: 2026-06-07T07:36:13+03:00
- Task: Reconfigure behavior tree functions for newly implemented dynamic detection features

## Entries

### 2026-06-07T07:36:47+03:00 - artifact: Reconfigured BT defaults to generic target keys

- Detail: Updated BT service and fly-to task defaults from HasDetectedPerson/DetectedPersonLocation to
  HasDetectedTarget/DetectedTargetLocation. Updated ABotAIController defaults and
  FBotRandomMovementSettings blocked-by defaults to HasDetectedTarget.

### 2026-06-07T07:39:15+03:00 - artifact: Removed legacy Person naming from BT target pipeline

- Detail: Renamed BT service class/files to UBTServ_UpdateCrowTargetDetection, converted service memory/key
  variables to Target naming, renamed controller blackboard FName members to target naming, and
  removed PublishPersonDetection/GetBestRecentPersonDetection wrappers from
  CrowDetectionShareSubsystem.

### 2026-06-07T07:39:56+03:00 - validation: EagleEyeEditor build succeeded after legacy removal

- Detail: Ran Unreal BuildTool EagleEyeEditor Linux Development with -WaitMutex. UHT generated reflection for
  renamed BT service and target-named APIs; link succeeded.

## Report Notes

- Main findings:
  - Behavior tree target logic reconfigured with legacy person path removed. BT service is now UBTServ_UpdateCrowTargetDetection with target-named memory/properties and HasDetectedTarget/DetectedTargetLocation defaults. BotAIController and BTTask_FlyToBlackboardLocation use generic target blackboard defaults. CrowDetectionShareSubsystem now exposes only target publication/query APIs and target detection storage. FOV-only person detection in MyActorComponent was intentionally left because it is a separate diagnostics/fallback feature, not the BT target pipeline. EagleEyeEditor Linux Development build succeeded.
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
