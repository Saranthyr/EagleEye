# Weird Detection Behavior

- Created: 2026-06-10T10:18:55+03:00
- Task: Inspect logs for weird detection behavior and identify likely cause/fix

## Entries

### 2026-06-10T10:19:30+03:00 - finding: Logs show many high-confidence person detections and target sharing

- Detail: Saved/Logs/EagleEye.log after latest run has SharedVisionFrame detections commonly 20-39 per 640x640
  frame. CrowDetectionDebug lines show class=person[0] conf=1.00, weak depth clusters, target jumps
  hundreds of units, and shared same/new target messages propagating targets across bots.

### 2026-06-10T10:20:07+03:00 - source: Checked depth detection metrics

- Detail: Saved/Profiling/DepthDetectionMetrics*.csv used to compare outcomes, resolved actor matches, target
  error, confidence, and detection counts.

### 2026-06-10T10:21:21+03:00 - decision: Reject non-player resolved targets

- Detail: Added bRequirePlayerResolvedTarget and PlayerTargetMatchRadius to BT service. After scene-depth
  resolve, target must resolve to player pawn within radius before blackboard write or flock
  publish. Logs showed accepted targets nearest AnotherBot/CrowBot and then shared across flock.

## Report Notes

- Main findings:
  - Logs showed OpenCV DNN producing detections, but target selection accepted many non-player pawns as person targets and then shared them through flock memory. Added player-resolved target validation in BT service: scene-depth target must resolve to the player pawn within PlayerTargetMatchRadius before blackboard write/publish. Build and diff check passed. Runtime replay needed to confirm new reject-target logs and reduced weird chasing.
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
