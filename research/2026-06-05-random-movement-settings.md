# Random movement settings bug

- Created: 2026-06-05T09:34:01+03:00
- Task: Fix bots not following random movement settings

## Entries

### 2026-06-05T09:34:40+03:00 - finding: Random movement settings live on ABotAIController only

- Detail: Source/EagleEye/Public/AI/BotAIController.h exposes bEnableRandomFlight, FlightRadius, hold times,
  and walking equivalents as protected EditDefaultsOnly. ABotCharacter has no matching per-bot
  random movement settings; constructor sets AIControllerClass = ABotAIController::StaticClass().
- Impact: Per-bot settings in BotCharacter/placed actors cannot affect random movement.

### 2026-06-05T09:36:11+03:00 - decision: Added per-bot random movement settings without breaking controller defaults

- Detail: Added FBotRandomMovementSettings, exposed it on ABotCharacter, and made ABotAIController apply pawn
  settings only when they differ from built-in defaults. Existing AIController Blueprint defaults
  remain authoritative when bot settings stay default.
- Impact: Supports both controller-level and placed/spawned bot-level random movement settings.

### 2026-06-05T09:36:53+03:00 - validation: Unreal editor target builds

- Detail: Command: /home/saranthyr/Unreal_Engine_5.6.1/Engine/Build/BatchFiles/Linux/Build.sh EagleEyeEditor
  Linux Development /home/saranthyr/Documents/Unreal Projects/EagleEye/EagleEye.uproject -WaitMutex.
  Result: Succeeded.
- Impact: UHT and C++ compile accepted new FBotRandomMovementSettings and controller integration.

## Report Notes

- Main findings:
  - Fixed random movement settings path by adding FBotRandomMovementSettings, exposing it on ABotCharacter, and letting ABotAIController consume custom pawn settings during possession while preserving controller defaults. Validated with successful EagleEyeEditor Linux Development build.
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

### 2026-06-05T09:37:23+03:00 - artifact: Files changed for random movement fix

- Detail: Changed Source/EagleEye/Public/AI/BotRandomMovementSettings.h,
  Source/EagleEye/Public/AI/BotCharacter.h, Source/EagleEye/Public/AI/BotAIController.h,
  Source/EagleEye/Private/BotAIController.cpp. Research log: research/2026-06-05-random-movement-
  settings.md.
- Impact: Patch scope separated from unrelated dirty files already in worktree.

## Report Notes

- Main findings:
  - Fixed random movement settings path by adding FBotRandomMovementSettings, exposing it on ABotCharacter, and letting ABotAIController consume custom pawn settings during possession while preserving controller defaults. Validated with successful EagleEyeEditor Linux Development build.
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
