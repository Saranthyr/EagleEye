# Healer bot healing logic investigation

- Created: 2026-06-10T14:19:18+03:00
- Task: Check why healer bot does not perform healing logic and fix if issue is found

## Entries

### 2026-06-10T14:20:49+03:00 - finding: Healer behavior tree lacks movement task

- Detail: strings on Content/ThirdPerson/Blueprints/Bots/Healer/HealerBotBehaviour.uasset shows
  BTServ_UpdateCrowTargetDetection but no BTTask_FlyToBlackboardLocation, while Land/Air bot
  behavior trees include BTTask_FlyToBlackboardLocation and HasDetectedTarget decorator.
  ABotAIController::UpdateHealingTarget only sets blackboard/focus for melee healing and relies on
  BT movement, so healer can select a target but never approach it.

### 2026-06-10T14:25:38+03:00 - decision: Use negative ProjectileDamage for projectile healing

- Detail: User clarified: implement projectile healing with same logic as melee heal. Removed separate
  projectile-heal boolean. ABotCharacter::IsProjectileHealing now means ProjectileDamage < 0.f.
  Projectile target validation switches to wounded bots when ProjectileDamage is negative;
  controller treats negative projectile bots as healers and avoids shooting healing projectiles at
  player target locations.

### 2026-06-10T14:26:08+03:00 - validation: EagleEyeEditor Linux Development build succeeded

- Detail: Ran Build.sh for EagleEyeEditor Linux Development after projectile healing changes. Result:
  Succeeded. Existing ONNX Runtime not found warning remains unrelated.

## Report Notes

- Main findings:
  - Implemented projectile healing using same sign convention as melee healing. ProjectileDamage < 0 now marks projectile as healing, targets wounded bots, spawns projectile with negative damage so ABotCharacter::TakeDamage heals on impact, and BotAIController treats projectile-heal bots as healer bots instead of shooting players. Validation: EagleEyeEditor Linux Development build succeeded.
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

### 2026-06-10T14:29:42+03:00 - finding: Projectile healer path did not drive approach

- Detail: Current UpdateHealingTarget treats non-melee healing as ranged fallback: clears blackboard/focus,
  calls TryThrowProjectileAtActor once, then returns. If target is outside ProjectileAttackRange, no
  movement is requested. If CloseDamage is negative too, controller may choose melee before
  projectile, so projectile heal never fires.

### 2026-06-10T14:30:07+03:00 - artifact: Projectile healing now drives target and approach

- Detail: UpdateHealingTarget now sets healing target blackboard/focus for projectile healers, fires
  TryThrowProjectileAtActor, and issues MoveToActor toward wounded ally until within projectile
  range. Projectile healing takes priority over melee when ProjectileDamage < 0.

### 2026-06-10T14:30:56+03:00 - decision: Melee healing keeps priority

- Detail: Per user correction, controller now computes melee reach first. If CloseDamage < 0 and melee target
  is reachable, blackboard target stays set and melee healing path runs. Projectile healing is used
  only when melee is unavailable/unreachable, and respects bUseRangedHealingWhenMeleeUnreachable for
  mixed melee/projectile healers.

### 2026-06-10T14:31:11+03:00 - validation: Build succeeded after melee-priority projectile healing

- Detail: Ran EagleEyeEditor Linux Development build. Result: Succeeded. Existing ONNX Runtime warning remains
  unrelated.

### 2026-06-10T14:39:23+03:00 - finding: Melee priority was gated by sync nav path

- Detail: ShouldUseMeleeHealing required CanReachHealingTargetForMelee, which used
  UNavigationSystemV1::FindPathToLocationSynchronously. If nav path was invalid/partial/transient,
  controller fell through to projectile healing. Changed melee reach check to same locomotion +
  HealingMeleeApproachMaxRange + valid flying/walking movement, letting BT movement attempt the path
  before projectile fallback.

### 2026-06-10T14:39:53+03:00 - validation: Build succeeded after melee priority reach fix

- Detail: Ran EagleEyeEditor Linux Development build. Result: Succeeded. Existing ONNX Runtime warning remains
  unrelated.

### 2026-06-10T14:41:13+03:00 - finding: Healing target clear did not stop previous movement

- Detail: UpdateHealingTarget clears CurrentHealingTarget/blackboard when target no longer needs healing, but
  ClearHealingTarget only clears state/blackboard. Any MoveToActor issued while projectile-healing
  toward that target can continue, making healer keep moving despite target health.

### 2026-06-10T14:41:29+03:00 - artifact: Stop stale healer movement on target clear

- Detail: ClearHealingTarget now calls StopMovement and ClearFocus before clearing blackboard. When current
  target becomes healthy or no target is found, previous MoveToActor/BT movement no longer keeps
  healer moving to that actor.

### 2026-06-10T14:41:50+03:00 - validation: Build succeeded after stopping stale healer movement

- Detail: Ran EagleEyeEditor Linux Development build. Result: Succeeded. Existing ONNX Runtime warning remains
  unrelated.

### 2026-06-10T14:45:48+03:00 - finding: Melee healer could stop outside overlap radius

- Detail: Healer defaults: CloseDamage=-20, bEnableCloseHitboxDamage=true, CloseDamageHitbox radius=130.
  Behavior tree movement acceptance can stop farther than hitbox overlap. Added
  HealingMeleeMoveAcceptanceRadius=55 and direct MoveToActor when melee healing is selected, so
  melee priority actually closes into overlap range.

### 2026-06-10T14:48:11+03:00 - finding: Melee heal needed active range call

- Detail: Close heal existed in ABotCharacter::TryApplyCloseDamage, but only overlap callback/polling invoked
  it. Added public wrapper TryApplyCloseDamageToActor and GetCloseDamageRange so ABotAIController
  can fire same melee-heal logic when selected melee target is inside close range. Also set healing
  MoveToActor bStopOnOverlap=false so acceptance radius is not inflated by capsule overlap.

### 2026-06-10T14:48:35+03:00 - validation: Build passed after active melee heal fix

- Detail: Ran Unreal BuildTool via Build.sh for EagleEyeEditor Linux Development. Result: Succeeded. Existing
  ONNX Runtime not found warning remains unrelated.

## Report Notes

- Main findings:
  - Melee healing now has two triggers: original close-hitbox overlap polling and controller-driven active call when selected melee healing target is inside close range. Controller keeps melee priority, moves closer without inflated stop-overlap acceptance, and projectile fallback remains only when melee is not selected/possible. Build succeeded.
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

### 2026-06-10T14:50:20+03:00 - source: User reports melee heal still does nothing

- Detail: Next check Blueprint defaults/assets because C++ melee path now exists and build passes. Need verify
  HealerBot BP, AI controller BP, behavior tree, close hitbox, damage values, movement mode, and
  runtime controller class/default overrides.

### 2026-06-10T14:51:47+03:00 - dead-end: Unreal Python commandlet hit DDC write restriction

- Detail: UnrealEditor-Cmd crashed before script with 'Unable to use default cache graph ... no writable nodes
  available'. Retrying with -DDC-ForceMemoryCache.

### 2026-06-10T14:54:14+03:00 - finding: CloseDamageHitbox Blueprint collision blocked melee

- Detail: Runtime/PIE logs showed CloseDamageHitbox as objectType=PhysicsBody pawnResponse=Block and
  navRelevant, despite C++ constructor intending WorldDynamic + pawn overlap. Blueprint defaults can
  override constructor component settings. Added ConfigureCloseDamageHitbox and call it after BP
  component defaults load, on BeginPlay, and before enable/disable. Also disabled navigation
  affect/character step-up.

### 2026-06-10T14:54:42+03:00 - validation: Build passed after Blueprint collision override fix

- Detail: Ran Build.sh EagleEyeEditor Linux Development. Result: Succeeded. Existing ONNX Runtime warning
  remains unrelated.

## Report Notes

- Main findings:
  - Blueprint defaults were still overriding the close heal hitbox into blocking/nav-affecting collision, so healer could approach but never overlap and movement/pathing saw hitboxes as obstacles. Runtime code now forces CloseDamageHitbox to WorldDynamic + Pawn overlap + no navigation affect after BP defaults load and before enabling it. Controller active melee range also accounts for target capsule radius. Build succeeded.
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
