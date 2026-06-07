# Detection regression check

- Created: 2026-05-29T06:50:56+03:00
- Task: Check worsened bot detection behavior and fix obvious regression

## Entries

### 2026-05-29T06:51:05+03:00 - finding: Hard tracked-target gate pins stale coordinate

- Detail: Saved/Logs/EagleEye.log shows repeated 'tracked target=V(X=10.61,Y=3.00,Z=265.25)' while
  detected/real target moved near X=-126..-138. pendingAge stays 0.00 because bMatchedTrackedBox
  forced same target regardless of world-space movement.

### 2026-05-29T06:51:30+03:00 - decision: Removed box-track override from locked target memory

- Detail: Box tracking remains available for prediction/debug, but target memory now switches only by world-
  space distance plus pending confirmation. This prevents matched YOLO box from suppressing
  pending/new target logic.

### 2026-05-29T06:51:47+03:00 - validation: EagleEyeEditor build succeeded

- Detail: Ran Unreal Build.sh for EagleEyeEditor Linux Development. BTServ_UpdateCrowPersonDetection.cpp
  compiled and libUnrealEditor-EagleEye.so linked successfully.

## Report Notes

- Main findings:
  - Regression likely caused by box-track match being allowed to force same-target state. Logs showed stale memorized coordinate while real/detected target moved. Fix removes that override, preserves distance-based same-target gate, adds smaller pending stability threshold and pendingAge debug, and keeps blackboard pointed at current committed memory until new target confirms. Build succeeded.
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
