# Bot Detection Metrics Review

- Created: 2026-06-06T12:43:09+03:00
- Task: Identify detection performance metrics not already present for bots

## Entries

### 2026-06-06T12:43:47+03:00 - source: Reviewed existing detection metrics and logs

- Detail: MyActorComponent has DetectionFrameTimes.csv with frame_sequence, backend, source size, total_ms,
  inference_ms, detections; FovDetectionMetrics.csv with TP/TN/FP/FN, expected_in_fov,
  actual_person_detected, distance, angles, expected pixel, detection count; CrowVisionSubsystem
  logs queue, worker, infer, end_to_end. BT service tracks consecutive detections/misses and shared
  detection use in memory/debug logs, but no persisted bot behavior metrics.

### 2026-06-06T12:43:47+03:00 - finding: Missing metrics cluster around localization, temporal stability, shared flock utility, and behavior outcome

- Detail: Current FOV metric treats detection as binary person present/absent based on player in owner camera
  FOV. It does not evaluate bounding-box quality, target world-location error, time-to-detect,
  target lock churn, false target switching, detection freshness at BT tick, or shared-detection hit
  rate.

## Report Notes

- Main findings:
  - Existing bot detection metrics cover timing and FOV-level binary TP/TN/FP/FN. Recommended additions: localization error, bounding-box IoU/center error, time-to-first-detect, detection persistence, target-lock churn, shared detection adoption rate, stale-frame rate, depth resolve success, threshold curves, and behavior outcome metrics.
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

### 2026-06-06T12:45:19+03:00 - question: User asked how missing metrics can improve depth detection

- Detail: Need connect proposed metrics to scene depth resolve path and tuning variables in
  BTServ_UpdateCrowPersonDetection.

### 2026-06-06T12:45:32+03:00 - finding: Depth resolver has tunable stages measurable with structured metrics

- Detail: TryResolveTargetFromSceneDepth samples inner YOLO box, filters non-finite/near/far depths, clusters
  depth samples, scores by sample count, pixel distance, reference target distance, and depth order.
  BT then rejects weak clusters by MinSceneDepthClusterSamples/MinSceneDepthClusterSampleRatio and
  clamps jumps by MaxTrackedTargetJumpDistance.

### 2026-06-06T12:52:25+03:00 - decision: Proceeding with depth metrics implementation

- Detail: User requested implementation. Plan: add BT-service-local CSV logger for per-box scene-depth
  outcomes, using existing detection metric logging setting to avoid new config surface.

### 2026-06-06T12:55:13+03:00 - artifact: Implemented depth metrics CSV writer

- Detail: BT service now writes per-frame local detection/depth outcomes to
  Saved/Profiling/DepthDetectionMetrics.csv when bEnableDetectionMetricLogs is true.

### 2026-06-06T12:55:39+03:00 - validation: Initial build failed

- Detail: Unreal build failed compiling BTServ_UpdateCrowPersonDetection.cpp: fatal error 'HAL/ScopeLock.h'
  file not found. Need correct include path for FScopeLock.

### 2026-06-06T12:56:41+03:00 - validation: Build succeeded

- Detail: Built EagleEyeEditor Linux Development using Unreal Build.sh after metrics implementation; result
  succeeded.

## Report Notes

- Main findings:
  - Implemented per-bot depth detection metrics in BTServ_UpdateCrowPersonDetection.cpp. Metrics write to Saved/Profiling/DepthDetectionMetrics.csv when bEnableDetectionMetricLogs is true. Rows cover no accepted box, depth miss, shared fallback, weak cluster, candidate, and committed target outcomes, with target errors, cluster stats, depth stats, tracking/jump state, frame age, and detection counts. Build succeeded.
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

### 2026-06-06T12:57:07+03:00 - validation: CSV schema sanity passed

- Detail: DepthDetectionMetrics.csv header has 59 columns and row Printf format has 59 specifiers; Unreal
  build also succeeded after final fallback-row fix.

### 2026-06-07T10:38:45+03:00 - source: User provided generated DepthDetectionMetrics.csv for analysis

- Detail: Opened Saved/Profiling/DepthDetectionMetrics.csv to inspect recorded bot depth detection metrics
  after test run.

### 2026-06-07T10:40:21+03:00 - finding: DepthDetectionMetrics.csv analysis

- Detail: CSV has 1051 rows across 4 appended sessions. Outcomes: 677 no_accepted_box, 114
  no_accepted_box_shared, 250 depth_success rows, 10 depth_miss rows. Successful rows excluding
  >1000 cm outliers have planar target error avg ~49.9 cm, median ~12 cm, p90 ~189 cm. Depth
  failures are rare; high no_accepted_box dominates but lacks expected_in_fov, so cannot distinguish
  outside-FOV from model miss. depth_max often 65504 and depth_avg is polluted by too_far samples.
  Five >1000 cm target-error rows likely represent wrong target/person identity or appended multi-
  phase data.

### 2026-06-07T10:44:35+03:00 - decision: Implement requested depth metric improvements

- Detail: User requested expected FOV context plus real player position per row. Will extend
  DepthDetectionMetrics.csv schema with run_id, expected_in_fov, player location, nearest resolved
  actor identity, and valid-depth-only stats.

### 2026-06-07T10:47:21+03:00 - validation: Enhanced depth metrics build succeeded

- Detail: Added schema_version/run_id, expected_in_fov, real player position, nearest resolved pawn identity,
  and valid-depth-only stats to BTServ_UpdateCrowTargetDetection.cpp. CSV schema sanity check
  passed: 74 columns and 74 Printf fields. EagleEyeEditor Linux Development build succeeded.

### 2026-06-07T10:50:07+03:00 - source: User requested post-run check with suspected pathfinding issue

- Detail: Inspecting latest Saved/Profiling CSV outputs and logs to separate detection-depth errors from
  movement/pathfinding failures.

### 2026-06-07T10:51:00+03:00 - finding: Latest run suggests pathfinding failure after good depth detection

- Detail: DepthDetectionMetrics_v2.csv has 347 rows, 104 successes. For expected_in_fov=true, successful rows
  have median planar target error ~12 cm and resolved_actor mostly Test_C_0/player. EagleEye.log
  shows BTMoveDecision for AnotherBot_C_1 using target around (11,-1,257), but
  navFailure=NoProjectedGoal and driver=DirectWalkFallback with direct segment blocked by
  Building1_C_0 Cube1 plus player mesh/capsule.

### 2026-06-07T10:51:35+03:00 - finding: Pathfinding likely root cause in latest test

- Detail: Latest BTMoveDecision lines: 4/4 are pawn=AnotherBot_C_1 driver=DirectWalkFallback
  navFailure=NoProjectedGoal navGoalValid=false navBlocked=true locomotion=Walking/BotLocomotion.
  Movement target is accurate near player, but ProjectPointToNavigation fails around the goal.
  BTPathObjectsSummary shows direct segment blockers: Building1_C_0 Cube1, player mesh/capsule.
  Behavior tree warnings also show UseFlyingMovementKey and MaintainDistanceKey are bound to missing
  old keys, remapped to first matching key.

### 2026-06-07T11:27:43+03:00 - source: Checked current target lock behavior

- Detail: User asked whether bot keeps trying to reach its target unless a new one is detected. Inspecting
  BTServ_UpdateCrowTargetDetection target memory and BTTask_FlyToBlackboardLocation walking target
  snapshot behavior.
