# Performance CSV logging logic

- Created: 2026-06-14T10:46:51+03:00
- Task: Explain current performance logging logic for CSV files

## Entries

### 2026-06-14T10:47:57+03:00 - finding: Frame-time CSV controlled by bRecordFrameTimes and runtime path

- Detail: Checked Config/DefaultGame.ini and Source/EagleEye/Private/MyActorComponent.cpp. Settings apply
  bRecordFrameTimes, FrameTimeCsvPath, bResetFrameTimeLogOnBeginPlay, FrameTimeFlushInterval.
  Default path is Saved/Profiling/DetectionFrameTimes_<runtime>.csv when FrameTimeCsvPath empty.

### 2026-06-14T10:47:57+03:00 - finding: bEnableDetectionPerformanceLogs mostly gates UE_LOG, not frame CSV writes

- Detail: ShouldLogFrameTimings returns bLogFrameTimings && bEnableDetectionPerformanceLogs, but
  AppendFrameTimingLogLine writes when bRecordFrameTimes is true. Local worker calls
  AppendFrameTimingLogLine after inference. Shared vision uses ShouldLogFrameTimings only for log
  messages; it does not append frame-time CSV.

### 2026-06-14T10:47:57+03:00 - finding: Metric CSV tables are separate from frame-time CSV

- Detail: FovDetectionMetrics.csv uses bLogFovDetectionMetrics component flag and optional reset.
  DepthDetectionMetrics.csv initializes in BTServ_UpdateCrowTargetDetection with schema fallback to
  DepthDetectionMetrics_v2.csv if existing header differs.

### 2026-06-14T10:48:14+03:00 - finding: bLogFrameTimings has no config assignment found

- Detail: rg found bLogFrameTimings default false in MyActorComponent.h and reads in ShouldLogFrameTimings,
  but no assignment from DefaultGame.ini. This means ShouldLogFrameTimings may remain false unless
  set per component/Blueprint, while bRecordFrameTimes still controls CSV writes.

## Report Notes

- Main findings:
  - Frame-time CSV: enabled by DefaultGame bRecordFrameTimes=True; path defaults to Saved/Profiling/DetectionFrameTimes_<runtime>.csv; reset once per runtime path per process if configured; header written if missing; rows buffered and flushed every FrameTimeFlushInterval or on shutdown. bEnableDetectionPerformanceLogs gates console logging, not local frame-time CSV writes. Shared vision currently emits perf UE_LOG only, no frame-time CSV row append.
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

### 2026-06-14T10:50:23+03:00 - command: Investigating detection model performance logging

- Detail: Follow-up request narrows scope from CSV files to detection model performance logging: console logs,
  inference timings, and CSV frame timing rows.

### 2026-06-14T10:51:14+03:00 - finding: Detection model timing split into CSV, gated UE_LOG, and bLogPerf

- Detail: Source/EagleEye/Private/MyActorComponent.cpp: local worker measures FrameTotalMs around inference
  lock/load/process/result publish and FrameInferMs inside ProcessWithOpenCV_BG around
  RunTensorRT/RunOnnxRuntime/RunOpenCVDNN. CSV row writes when bRecordFrameTimes true. UE_LOG perf
  messages require ShouldLogFrameTimings (component bLogFrameTimings and global
  bEnableDetectionPerformanceLogs). bLogPerf separately logs backend forward ms/output shape but
  defaults false and has no config assignment found.

### 2026-06-14T10:51:14+03:00 - finding: Shared vision performance logs are console-only

- Detail: CrowVisionSubsystem logs queue, worker, infer, and delivery end-to-end timing when requester
  ShouldLogFrameTimings is true; ProcessSharedVisionFrame logs host total/infer when host
  ShouldLogFrameTimings is true. No shared-vision path calls AppendFrameTimingLogLine, so no frame
  timing CSV rows for shared vision.

## Report Notes

- Main findings:
  - Detection model perf logging: local model worker writes CSV when bRecordFrameTimes is true; infer_ms measures only backend forward call; total_ms measures local worker processing after frame dequeue. Console perf logs need bLogFrameTimings and bEnableDetectionPerformanceLogs. Shared vision has detailed console timings but no CSV append. bLogPerf is a separate per-component debug flag for backend forward/output shape, default false with no config setter found.
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
