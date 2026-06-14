# Bot kill HUD and restart

- Created: 2026-06-14T10:13:58+03:00
- Task: Add bot kill counter on HUD and restart session on any input after player death

## Entries

### 2026-06-14T10:14:26+03:00 - source: Inspected gameplay classes

- Detail: Checked Source/EagleEye/Public/EagleEyeCharacter.h, Private/EagleEyeCharacter.cpp,
  Public/AI/BotCharacter.h, Private/BotCharacter.cpp, Public/MyHUD.h, Private/MyHUD.cpp, and
  EagleEyeGameMode.* for death, damage, HUD, and input flow.

### 2026-06-14T10:15:01+03:00 - finding: Death and HUD hooks found

- Detail: ABotCharacter::HandleDeath broadcasts death and has EventInstigator/DamageCauser. AEagleEyeCharacter
  owns health/death and already uses AMyHUD. AMyHUD::DrawPlayerHealth draws current player health
  each frame.
- Impact: Kill count can live on player character, bot death can resolve player killer, HUD can read player
  character state directly.

### 2026-06-14T10:16:33+03:00 - decision: Use player-owned native state

- Detail: BotKillCount added to AEagleEyeCharacter. ABotCharacter resolves player killer from EventInstigator,
  DamageCauser, owner, and instigator. AMyHUD reads player pawn for count/death prompt. Restart
  polls EKeys::AnyKey after short delay and opens current level.
- Impact: Keeps feature native and scoped without adding Blueprint asset dependencies.

### 2026-06-14T10:17:00+03:00 - validation: Editor target built successfully

- Detail: Ran Build.sh EagleEyeEditor Linux Development with project .uproject and -WaitMutex.
  UnrealHeaderTool generated reflection code, C++ compiled and linked libUnrealEditor-EagleEye.so.
  Result: Succeeded.
- Impact: Confirms new C++ APIs, headers, and Unreal reflection macros compile.

### 2026-06-14T10:17:49+03:00 - validation: Final rebuild succeeded after HUD ordering tweak

- Detail: Re-ran Build.sh EagleEyeEditor Linux Development after changing DrawHUD order. Result: Succeeded;
  MyHUD.cpp compiled and libUnrealEditor-EagleEye.so linked.
- Impact: Confirms final working tree version compiles.

## Report Notes

- Main findings:
  - Implemented native bot kill count, HUD display, death prompt, and any-button restart.
  - Kill count increments from `ABotCharacter::HandleDeath` when player is resolved as killer.
  - `AEagleEyeCharacter` holds count and polls `EKeys::AnyKey` after death delay to reload current level.
  - `AMyHUD` draws live kill count and death restart prompt.
- Evidence to cite:
  - `Source/EagleEye/Private/BotCharacter.cpp`
  - `Source/EagleEye/Private/EagleEyeCharacter.cpp`
  - `Source/EagleEye/Private/MyHUD.cpp`
- Decisions and rationale:
  - Keep feature native and scoped to existing player/HUD/bot death classes; no Blueprint asset edits required.
- Validation performed:
  - `Build.sh EagleEyeEditor Linux Development ... -WaitMutex` succeeded after final patch.
- Unresolved questions:
  - Runtime PIE behavior not manually played in editor.
- Suggested report angle:
  - Small gameplay loop completion: scoring feedback plus fast restart after death.
