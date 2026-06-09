# Unreal build duplicate helper fix

- Created: 2026-06-08T05:37:30+03:00
- Task: Fix EagleEyeEditor build errors: redefinition of IsDetectionMetricLoggingEnabled and EscapeCsvField

## Entries

### 2026-06-08T05:37:41+03:00 - finding: Unity build merges two cpp files with same anonymous-namespace helper names

- Detail: Build error shows Module.EagleEye.3.cpp includes BTServ_UpdateCrowTargetDetection.cpp and
  MyActorComponent.cpp; both define IsDetectionMetricLoggingEnabled and EscapeCsvField. Anonymous
  namespace does not protect against duplicate definitions inside same unity translation unit.

### 2026-06-08T05:37:54+03:00 - decision: Renamed MyActorComponent-local helpers

- Detail: Kept behavior local and avoided broader refactor. New names:
  IsActorComponentDetectionMetricLoggingEnabled and EscapeActorComponentCsvField.

### 2026-06-08T05:38:19+03:00 - validation: EagleEyeEditor build succeeded

- Detail: Ran Build.sh EagleEyeEditor Linux Development EagleEye.uproject -WaitMutex. Result: Succeeded. Total
  execution time: 11.71 seconds.

## Report Notes

- Main findings:
  - Fixed EagleEyeEditor build failure by renaming MyActorComponent-local helper functions that collided with same-named helpers in BTServ_UpdateCrowTargetDetection.cpp during Unreal unity build. Build now succeeds.
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
