# Bot healer AI

- Created: 2026-06-07T17:32:07+03:00
- Task: Extend bot logic so healer bot detects other bots, checks HP threshold percent, approaches and heals using negative melee damage.

## Entries

### 2026-06-07T17:32:32+03:00 - finding: Current close hitbox cannot heal

- Detail: Source/EagleEye/Private/BotCharacter.cpp rejects CloseDamage <= 0, only overlaps AEagleEyeCharacter,
  and IsValidDamageTarget only accepts player targets. ABotCharacter already has Heal(),
  GetCurrentHealth(), GetMaxHealth().
- Impact: Negative melee damage requires BotCharacter target validation and application changes.

### 2026-06-07T17:33:07+03:00 - decision: Use negative CloseDamage as healer mode

- Detail: Avoid new BT asset changes. Controller will publish injured ally into existing
  HasDetectedTarget/DetectedTargetLocation/TargetActor blackboard keys. Existing
  FlyToBlackboardLocation task can approach target. BotCharacter close hitbox applies
  Heal(-CloseDamage) to bot targets.
- Impact: Healer can be configured in BP/C++ by setting CloseDamage negative and tuning controller
  threshold/radius.

### 2026-06-07T17:35:25+03:00 - validation: Unreal build succeeded

- Detail: Ran Build.sh EagleEyeEditor Linux Development EagleEye.uproject -WaitMutex. Result: Succeeded.
- Impact: C++ changes compile with UnrealHeaderTool and linker.

### 2026-06-07T17:35:32+03:00 - artifact: Healer AI code paths added

- Detail: Changed BotCharacter.h/cpp for negative close damage healing and health percent helpers. Changed
  BotAIController.h/cpp for ally scan, threshold selection, blackboard target publishing, and
  projectile skip in healer mode.
- Impact: Config path: set bot CloseDamage < 0, keep close hitbox enabled, tune HealingHealthPercentThreshold
  and HealingSearchRadius on AI controller defaults/BP.

## Report Notes

- Main findings:
  - Implemented healer bot logic using negative close damage. ABotCharacter now treats negative damage as healing, close hitbox heals other ABotCharacter instances, and ABotAIController scans for nearby injured bots under a configurable percent threshold, pushes target location into existing blackboard keys, approaches through existing BT movement, and suppresses projectile fire while in healer mode. Build succeeded.
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
