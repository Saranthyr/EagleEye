# Pawn and AI overlap cleanup

- Created: 2026-06-05T10:14:52+03:00
- Task: Proceed with cleanup of overlapping pawn/controller variables and behaviours

## Entries

### 2026-06-05T10:16:10+03:00 - artifact: Patched BotAIController random movement and focus ownership

- Detail: Replaced controller flight/walking scalar UPROPERTY surface with DefaultRandomMovementSettings +
  private ActiveRandomMovementSettings. Controller now sanitizes active settings on possess/apply
  and no longer clears Gameplay focus while HasDetectedPerson is true and bFocusProjectileTarget is
  false.

### 2026-06-05T10:16:46+03:00 - validation: EagleEyeEditor build succeeded

- Detail: Ran /home/saranthyr/Unreal_Engine_5.6.1/Engine/Build/BatchFiles/Linux/Build.sh EagleEyeEditor Linux
  Development EagleEye.uproject -WaitMutex. UnrealHeaderTool generated reflection code and target
  linked successfully.

## Report Notes

- Main findings:
  - Cleanup implemented in BotAIController. Random movement controller defaults now use FBotRandomMovementSettings as a single UPROPERTY and runtime uses private ActiveRandomMovementSettings. OnPossess initializes from controller default settings, then applies pawn overrides when pawn settings differ from struct defaults. Controller no longer clears Gameplay focus while a detected target exists unless bFocusProjectileTarget is enabled, reducing conflict with BTTask_FlyToBlackboardLocation focus/rotation. EagleEyeEditor Linux Development build succeeded.
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
