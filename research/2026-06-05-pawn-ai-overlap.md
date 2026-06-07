# Pawn and AI controller overlap review

- Created: 2026-06-05T10:10:53+03:00
- Task: Check for overlapping variables and behaviours in pawn classes and AI controllers

## Entries

### 2026-06-05T10:12:02+03:00 - source: Reviewed pawn/controller sources

- Detail: Checked ABotCharacter, ABotAIController, AEagleEyeCharacter, FBotRandomMovementSettings, and
  BTTask_FlyToBlackboardLocation for duplicated variables and movement/combat ownership.

### 2026-06-05T10:12:02+03:00 - finding: Random movement settings duplicated between pawn struct and controller properties

- Detail: FBotRandomMovementSettings defines flight/walk fields; ABotCharacter stores one struct;
  ABotAIController redeclares same scalar fields and copies custom pawn settings on possess. This
  creates two editable surfaces with possible drift, especially controller defaults vs pawn
  defaults.

### 2026-06-05T10:12:02+03:00 - finding: Movement authorship split among controller, pawn, and BT task

- Detail: ABotCharacter owns locomotion mode and movement component settings. ABotAIController generates
  random destinations and uses AddMovementInput/MoveToLocation. BTTask_FlyToBlackboardLocation also
  sets movement modes, velocity, AddMovementInput/MoveToLocation, rotation, focus, and StopMovement.
  Behaviour tree and controller Tick can both drive movement/focus in same frame unless blocked by
  HasDetectedPerson.

### 2026-06-05T10:12:02+03:00 - finding: Death stop logic duplicated across pawn and controller

- Detail: ABotCharacter::HandleDeath stops movement, disables movement, and stops brain logic.
  ABotAIController::Tick also checks IsDead and StopMovement/ClearFocus. This is redundant but not
  hazardous; controller tick path may never matter after BrainComponent::StopLogic.

### 2026-06-05T10:12:02+03:00 - finding: Combat is split but mostly sane: controller decides target, pawn executes projectile

- Detail: ABotAIController reads HasDetectedPerson/DetectedPersonLocation and calls
  ABotCharacter::TryThrowProjectileAtLocation every tick; ABotCharacter owns projectile
  damage/range/cooldown/spawn. This is acceptable if controller is decision layer, but
  names/categories expose combat settings on pawn while firing cadence is driven from controller
  tick.

### 2026-06-05T10:12:14+03:00 - finding: Player and bot pawns duplicate health/projectile/death patterns

- Detail: AEagleEyeCharacter and ABotCharacter both own MaxHealth/CurrentHealth/bIsDead,
  TakeDamage/ApplyHealthDamage/Heal/ResetHealth/HandleDeath, projectile
  class/damage/speed/range/cooldown/spawn, and ragdoll setup. Differences: player has input/camera
  impulse/melee; bot has close damage, AI brain stop, destroy-on-death, viewport recording.

## Report Notes

- Main findings:
  - Overlap review complete. Highest-risk overlap is movement/focus ownership split across ABotAIController Tick and BTTask_FlyToBlackboardLocation while ABotCharacter owns locomotion mode and movement component settings. Random movement settings also have duplicated property surfaces in FBotRandomMovementSettings and ABotAIController. Combat target decision vs projectile execution split is acceptable but should be documented. Health/projectile/death logic is duplicated between player and bot pawns and is a refactor candidate, not urgent.
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
