# Dynamic classes detection cleanup

- Created: 2026-06-05T10:52:14+03:00
- Task: Remove HasDetectedPerson fallback option and switch to dynamic classes behaviour

## Entries

### 2026-06-05T10:52:29+03:00 - finding: Located fallback and fixed-class behavior

- Detail: UBTServ_UpdateCrowPersonDetection has bAllowPlayerPawnLocationFallback path that writes
  HasDetectedPerson=true and hardcoded class person[0]. Constructor also hardcodes
  ActionableClassIds.Add(0) and ActionableClassLabels.Add(person). IsActionableDetection currently
  rejects all classes unless listed.

### 2026-06-05T10:54:08+03:00 - artifact: Removed player fallback and enabled dynamic class filters

- Detail: Removed bAllowPlayerPawnLocationFallback plus related player-location properties/code. Removed
  constructor defaults for person[0]. IsActionableDetection and shared detection class filter now
  accept all detected classes when ActionableClassIds and ActionableClassLabels are both empty.

### 2026-06-05T10:54:56+03:00 - validation: EagleEyeEditor build succeeded

- Detail: Ran Unreal BuildTool via /home/saranthyr/Unreal_Engine_5.6.1/Engine/Build/BatchFiles/Linux/Build.sh
  EagleEyeEditor Linux Development EagleEye.uproject -WaitMutex. UHT and link succeeded after
  removing fallback and enabling dynamic class filters.

## Report Notes

- Main findings:
  - Removed player-location fallback from UBTServ_UpdateCrowPersonDetection and removed hardcoded person[0] default target class. Detection service and shared flock lookup now treat empty ActionableClassIds/ActionableClassLabels as dynamic accept-all behaviour; configured arrays still act as allow-list. Removed player-specific debug comparison from detection output. EagleEyeEditor Linux Development build succeeded.
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
