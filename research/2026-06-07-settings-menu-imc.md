# Settings menu IMC functions

- Created: 2026-06-07T15:31:07+03:00
- Task: Remove hard key bindings for HUD settings menu and expose callable functions for Enhanced Input IMC wiring

## Entries

### 2026-06-07T15:31:16+03:00 - source: Reviewed current hard key binding

- Detail: AEagleEyeCharacter::SetupPlayerInputComponent binds F10/Escape/arrow/Enter with
  UInputComponent::BindKey. Menu relay functions exist but are protected and not BlueprintCallable.
  AMyHUD menu handlers exist but only open/toggle functions are BlueprintCallable.

### 2026-06-07T15:31:51+03:00 - artifact: Removed hard bindings and exposed IMC-callable functions

- Detail: Deleted UInputComponent::BindKey calls from AEagleEyeCharacter::SetupPlayerInputComponent. Added
  BlueprintCallable UFUNCTION declarations for character menu relay methods and AMyHUD
  HandleDetectionSettingsMenu* methods.

## Report Notes

- Main findings:
  - Removed hardcoded HUD settings menu key binds from AEagleEyeCharacter::SetupPlayerInputComponent. Exposed character relay methods and AMyHUD menu handlers as BlueprintCallable so Enhanced Input IMC/InputAction assets can trigger them. Validation: EagleEyeEditor Linux Development build succeeded.
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
