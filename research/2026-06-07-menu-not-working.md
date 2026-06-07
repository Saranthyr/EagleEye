# Detection menu not working

- Created: 2026-06-07T18:25:22+03:00
- Task: Debug and harden detection settings menu activation

## Entries

### 2026-06-07T18:26:21+03:00 - finding: Active default game mode is Blueprint

- Detail: Config/DefaultEngine.ini sets
  GlobalDefaultGameMode=/Game/ThirdPerson/Blueprints/GameModeMain.GameModeMain_C. C++
  AEagleEyeGameMode assigns HUDClass=AMyHUD, but Blueprint game mode may override HUD class, causing
  character menu relays to no-op when Cast<AMyHUD>(GetHUD()) fails.

### 2026-06-07T18:26:21+03:00 - artifact: Hardened HUD acquisition

- Detail: AEagleEyeCharacter menu relay functions now call EnsureDetectionSettingsHud(), which uses
  PlayerController->ClientSetHUD(AMyHUD::StaticClass()) if current HUD is not AMyHUD. Move/Look
  checks use non-creating helper. ToggleDetectionSettingsMenuInput is Exec for console smoke tests.

## Report Notes

- Main findings:
  - Likely root cause: active GameModeMain_C may not use AMyHUD, so menu relay functions silently failed when Cast<AMyHUD>(PlayerController->GetHUD()) returned null. Hardened AEagleEyeCharacter relays to call PlayerController->ClientSetHUD(AMyHUD::StaticClass()) when needed, and made ToggleDetectionSettingsMenuInput an Exec command for console testing. Validation: EagleEyeEditor Linux Development build succeeded.
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
