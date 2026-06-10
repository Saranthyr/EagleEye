# Procedural bot random movement switch

- Created: 2026-06-10T13:09:01+03:00
- Task: Add to procedural generation a switch for enabling random movement of bots it spawns

## Entries

### 2026-06-10T13:09:14+03:00 - finding: Existing bot random movement system found

- Detail: Source/EagleEye/Public/AI/BotRandomMovementSettings.h defines FBotRandomMovementSettings.
  ABotAIController exposes ApplyRandomMovementSettings. WorldGen.cpp owns procgen bot spawning.

### 2026-06-10T13:10:10+03:00 - decision: WorldGen-level random movement switch

- Detail: Added bEnableSpawnedBotRandomMovement to AWorldGen and apply it to spawned ABotAIController
  instances by copying each bot class random settings then toggling flight/walking enable flags.
  This preserves radius/hold settings while giving procgen one switch.

### 2026-06-10T13:10:41+03:00 - validation: Unreal build succeeded

- Detail: Ran Build.sh EagleEyeEditor Linux Development EagleEye.uproject -WaitMutex. UnrealHeaderTool and
  compile/link completed successfully. Build noted existing ONNX Runtime not found warning, non-
  fatal.

## Report Notes

- Main findings:
  - Added AWorldGen::bEnableSpawnedBotRandomMovement. WorldGen now configures each spawned ABotCharacter's ABotAIController by copying bot random movement settings and toggling both flight and walking random movement flags from the WorldGen switch. Build succeeded for EagleEyeEditor Linux Development.
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
