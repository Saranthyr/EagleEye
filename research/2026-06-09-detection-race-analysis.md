# Detection race analysis

- Created: 2026-06-09T11:27:14+03:00
- Task: Analyze how current detection logic can be improved to prevent race conditions/memory access violations or other errors

## Entries

### 2026-06-09T11:35:24+03:00 - source: Inspected detection component and shared vision subsystem

- Detail: Reviewed Source/EagleEye/Private/MyActorComponent.cpp, Public/MyActorComponent.h,
  Private/CrowVisionSubsystem.cpp, Public/AI/CrowVisionSubsystem.h, CrowDetectionShareSubsystem.cpp,
  BT service slices.

### 2026-06-09T11:37:44+03:00 - finding: Crash logs show worker-thread model load/forward failures in old builds

- Detail: Logs show SIGABRT in UMyActorComponent::LoadYOLO via OpenCV readNetFromDarknet and SIGABRT in
  ProcessWithOpenCV_BG/OpenCV forward. Current code catches cv::Exception inside worker, but native
  aborts/AVs remain possible if lifetime or backend state races occur.

### 2026-06-09T11:37:44+03:00 - finding: Inference state mostly locked, but model-loaded flag and ClassNames read outside lock in public paths

- Detail: StartWorker and ProcessSharedVisionFrame hold InferenceMutex across LoadYOLO/ProcessWithOpenCV_BG.
  However IsModelLoaded() and EnsureModelLoaded() check bIsModelLoaded without lock/atomic, and
  ProcessWithOpenCV_BG reads ClassNames/output state assuming caller holds lock.

### 2026-06-09T11:37:44+03:00 - finding: Highest race risk: shared vision stores raw ModelHost pointer for worker thread

- Detail: UCrowVisionSubsystem::FQueuedFrame has UMyActorComponent* ModelHost. Worker calls
  Host->ProcessSharedVisionFrame off game thread. Unregister removes host from arrays but cannot
  invalidate already copied raw pointer if component/actor destruction overlaps worker execution.

## Report Notes

- Main findings:
  - Detection logic has good frame/result mutexes, but should harden lifetime ownership for shared model host, make inference state access consistently locked/atomic, validate frame sizes before cv::Mat wrapping, remove mutable static logging from worker path, and add shutdown/reload generation checks.
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
