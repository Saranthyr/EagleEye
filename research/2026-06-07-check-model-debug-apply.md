# Check model reload and debug disable

- Created: 2026-06-07T19:20:59+03:00
- Task: Verify whether settings menu applied new YOLO model and disabled debug logging

## Entries

### 2026-06-07T19:21:28+03:00 - source: Checked config and latest log

- Detail: Config/DefaultGame.ini still has ModelPathOverride=yolo26x.plan and
  bEnableDetectionDebugLogs=True/bEnableDetectionPerformanceLogs=True/bEnableDetectionMetricLogs=Tru
  e. Latest log shows yolo26x.plan loaded with TensorRT and ongoing CrowDetectionDebug lines through
  16:20.

### 2026-06-07T19:21:46+03:00 - finding: Model switched to yolo26l.plan

- Detail: Saved/Logs/EagleEye.log shows repeated Detection model path entries for
  /Source/EagleEye/yolo26l.plan at 16:19:30 onward, plus LoadYOLO selected backend TensorRT and
  TensorRT engine loaded for yolo26l.plan.

### 2026-06-07T19:22:03+03:00 - finding: Debug logging still enabled

- Detail: CrowDetectionDebug lines continue after model reload and after menu close at 16:19:49 through
  16:20:01. Config/DefaultGame.ini still has bEnableDetectionDebugLogs=True,
  bEnableDetectionPerformanceLogs=True, bEnableDetectionMetricLogs=True.

## Report Notes

- Main findings:
  - Verification: model reload did happen in the running session, switching from yolo26x.plan to yolo26l.plan and loading TensorRT engine. Debug was not disabled: CrowDetectionDebug logs continued after apply/close, and config still has debug/perf/metric logging flags true. Persistence also appears incomplete: Config/DefaultGame.ini still lists yolo26x.plan even though runtime loaded yolo26l.plan.
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
