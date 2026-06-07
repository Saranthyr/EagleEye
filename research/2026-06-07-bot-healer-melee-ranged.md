# Bot healer melee/ranged selection

- Created: 2026-06-07T17:57:39+03:00
- Task: Extend healer bots so they heal melee when target shares locomotion and is reachable, otherwise heal with ranged projectile.

## Entries

### 2026-06-07T17:58:17+03:00 - finding: Projectile path damage-only

- Detail: ABotDamageProjectile::SetDamage clamps to >=0 and TryDamageActor rejects bot targets for bot-owned
  projectiles. BotCharacter::TryThrowProjectileAtActor always sends positive ProjectileDamage.
- Impact: Ranged healing needs signed projectile damage and bot-to-bot target allowance when damage is
  negative.

### 2026-06-07T17:59:22+03:00 - decision: Melee/ranged selection

- Detail: Healer remains defined by CloseDamage < 0. If target locomotion mode matches and target is
  reachable, controller writes blackboard target for melee approach. If locomotion differs or reach
  check fails, controller clears approach target and fires TryThrowProjectileAtActor with negative
  projectile damage.
- Impact: No BT asset edits needed; existing movement task handles melee approach only.

### 2026-06-07T18:00:16+03:00 - validation: Build and diff checks passed

- Detail: Ran git diff --check on edited files: no output. Ran Build.sh EagleEyeEditor Linux Development
  EagleEye.uproject -WaitMutex. Result: Succeeded.
- Impact: Changes compile with UHT/linker and no whitespace errors.

## Report Notes

- Main findings:
  - Implemented melee/ranged healer selection. Healer bots still use CloseDamage < 0. Same-locomotion, reachable targets are pushed into existing blackboard keys for melee approach and close-hitbox healing. Different-locomotion or unreachable targets use ranged healing via negative projectile damage. Projectile code now allows signed damage and bot-owned negative projectiles to affect bots. Build succeeded.
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
