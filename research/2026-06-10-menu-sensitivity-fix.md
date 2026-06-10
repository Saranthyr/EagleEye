# Menu sensitivity fix

- Created: 2026-06-10T14:16:17+03:00
- Task: Investigate and reduce detection settings menu input sensitivity

## Entries

### 2026-06-10T14:16:48+03:00 - finding: Menu handlers have no debounce

- Detail: AMyHUD::HandleDetectionSettingsMenuUp/Down/Left/Right/Confirm mutate menu state every call. Caller
  functions in AEagleEyeCharacter forward directly to HUD; menu input bindings appear blueprint-
  driven or external, so repeated Triggered events can skip rows/toggle values quickly.

### 2026-06-10T14:17:54+03:00 - artifact: Added HUD input cooldowns

- Detail: AMyHUD now rate-limits menu toggle, row navigation, value changes, and confirm/cancel with separate
  timers. Opening menu resets navigation/value/confirm timers so first intentional input is
  accepted.

### 2026-06-10T14:18:21+03:00 - validation: EagleEyeEditor Linux Development build succeeded

- Detail: Ran Build.sh for EagleEyeEditor Linux Development. Result: Succeeded. Existing warning: ONNX Runtime
  not found.

## Report Notes

- Main findings:
  - Reduced detection settings menu sensitivity by adding HUD-level input cooldowns. Toggle is limited to 0.30s, up/down to 0.16s, left/right value edits to 0.14s, and confirm/cancel to 0.25s. This prevents repeated Enhanced Input trigger events from rapidly skipping rows or flipping values. Validation: EagleEyeEditor Linux Development build succeeded.
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
