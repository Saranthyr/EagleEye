# Healer bot flying investigation

- Created: 2026-06-10T14:34:39+03:00
- Task: Check why healer bot is flying

## Entries

### 2026-06-10T14:34:56+03:00 - finding: Healer blueprint lacks walking movement component override

- Detail: strings HealerBot.uasset shows LocomotionMode enum metadata but no
  DefaultLandMovementMode/MOVE_NavWalking. Land/AnotherBot.uasset includes DefaultLandMovementMode
  and MOVE_NavWalking. ABotCharacter C++ default LocomotionMode is Flying, constructor
  ApplyBotMovementSettings sets movement to Flying before BP override data is useful for component
  defaults.

### 2026-06-10T14:35:56+03:00 - finding: Healer CDO has walking locomotion but flying movement component default

- Detail: Unreal Python CDO inspection: HealerBot_C LocomotionMode=WALKING, CloseDamage=-20,
  ProjectileDamage=-15, movement.default_land_movement_mode=MOVE_FLYING. Land AnotherBot_C has
  LocomotionMode=WALKING and default_land_movement_mode=MOVE_NAV_WALKING. Air CrowBot_C has
  LocomotionMode=FLYING and default_land_movement_mode=MOVE_FLYING.

### 2026-06-10T14:36:23+03:00 - artifact: Applied bot movement settings after Blueprint defaults

- Detail: Added ABotCharacter::PostInitializeComponents override that calls ApplyBotMovementSettings. This
  makes runtime instances honor Blueprint LocomotionMode before gameplay/AI instead of retaining
  constructor-applied MOVE_Flying on the movement component.

### 2026-06-10T14:36:46+03:00 - validation: EagleEyeEditor Linux Development build succeeded

- Detail: Build.sh result succeeded after adding PostInitializeComponents movement reapply. Existing ONNX
  Runtime warning remains unrelated.

## Report Notes

- Main findings:
  - Healer bot was flying because its Blueprint CDO had LocomotionMode=WALKING but CharacterMovement default land mode still MOVE_FLYING, inherited from native constructor setup. Added PostInitializeComponents movement reapply so runtime instances honor Blueprint LocomotionMode before gameplay/AI. Validation: EagleEyeEditor Linux Development build succeeded.
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
