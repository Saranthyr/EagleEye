# Check Logs Screen Debug

- Created: 2026-06-07T19:35:42+03:00
- Task: Check latest logs after screen debug gating fix

## Entries

### 2026-06-07T19:36:11+03:00 - finding: Latest menu apply re-enabled every debug/log flag.

- Detail: Saved/Logs/EagleEye.log line 20755: [2026.06.07-16.35.03] Detection settings applied:
  model=yolo26s.plan debug=true perf=true metrics=true pathDecision=true pathObject=true
  config=updated reloaded=13. Config/DefaultGame.ini now has ModelPathOverride=yolo26s.plan and all
  five debug/log flags True. Tail after that contains CrowDetectionDebug, BTMoveDecision,
  SharedVisionFrame, AsyncReadback, DetectionSubmit, SharedResult.

## Report Notes

- Main findings:
  - Current logs show messages because settings were re-enabled, not because off-gates failed. Latest apply at 2026-06-07 16:35:03 UTC set debug=true, perf=true, metrics=true, pathDecision=true, pathObject=true and yolo26s.plan. Config/DefaultGame.ini matches with all flags True. Need apply menu with those flags Off to validate silence path again.
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
