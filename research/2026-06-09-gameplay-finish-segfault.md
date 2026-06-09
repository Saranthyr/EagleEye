# Gameplay Finish Segfault Log Review

- Created: 2026-06-09T13:45:11+03:00
- Task: Verify logs for segfault on gameplay finish

## Entries

### 2026-06-09T13:45:23+03:00 - source: Located Unreal logs and crash artifacts

- Detail: Searched Saved/Logs and Saved/Crashes for .log, CrashContext.runtime-xml, Diagnostics.txt, and
  minidumps.
- Impact: Crash analysis will focus on newest crash directories and current game logs.

### 2026-06-09T13:45:45+03:00 - finding: Newest crash occurs during world teardown while ONNX worker is running

- Detail: Crash log: BeginTearingDown and UWorld::CleanupWorld at 10:42:03.688-10:42:03.695, then SIGSEGV at
  10:42:03.699 in libonnxruntime.so from UMyActorComponent::RunOnnxRuntimeInference_BG ->
  ProcessSharedVisionFrame -> UCrowVisionSubsystem::StartWorker lambda.
- Impact: Root cause likely teardown/lifetime race around shared vision worker and ONNX Runtime session.

### 2026-06-09T13:46:32+03:00 - source: Reviewed shared vision and ONNX shutdown code

- Detail: Read CrowVisionSubsystem.cpp StartWorker/StopWorker/HandleWorldCleanup and MyActorComponent.cpp
  BeginPlay/EndPlay/StopWorker/ProcessSharedVisionFrame/ReleaseOnnxRuntime/RunOnnxRuntimeInference_B
  G.
- Impact: Code confirms shared worker dispatches Host->ProcessSharedVisionFrame on background thread and
  StopWorker waits only after current inference returns.

### 2026-06-09T13:46:32+03:00 - finding: Non-crash warnings present but not primary failure

- Detail: Logs show MIGraphX provider fallback to CPU and CDO missing BP_ThirdPersonCharacter, but SIGSEGV
  stack is in ONNX Runtime inference worker at gameplay teardown.
- Impact: Fix should target shared vision worker/inference shutdown, not these warnings first.

## Report Notes

- Main findings:
  - Verified latest gameplay-finish segfaults. Both newest crash reports are SIGSEGV at 0x00000000000000c0 inside libonnxruntime.so.1 during Ort::Session::Run from UMyActorComponent::RunOnnxRuntimeInference_BG, called by shared vision worker in UCrowVisionSubsystem::StartWorker. Crash happens immediately after PIE/TestWorld BeginTearingDown/UWorld::CleanupWorld, so primary suspect is shared vision ONNX inference still active during gameplay teardown, with possible ONNX Runtime session/lifetime race. MIGraphX missing-provider warning and missing BP_ThirdPersonCharacter CDO warning are present but not primary crash stack.
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

### 2026-06-09T13:48:03+03:00 - source: Checked teardown delegate availability before patch

- Detail: UE 5.6 World.h exposes FWorldDelegates::OnWorldBeginTearDown(UWorld*) before OnWorldCleanup; current
  subsystem only binds OnWorldCleanup.
- Impact: Use earlier delegate to stop shared vision worker before world cleanup/resource destruction.

### 2026-06-09T13:49:12+03:00 - decision: Use ONNX Runtime run termination plus earlier world teardown stop

- Detail: onnxruntime_cxx_api.h documents RunOptions::SetTerminate terminates currently executing Session::Run
  calls using that instance. Patch will keep persistent RunOptions, set terminate before worker
  waits, bind OnWorldBeginTearDown, and release model resources only after InferenceMutex.
- Impact: Prevents PIE teardown from destroying host/session while shared worker is still in Ort::Session::Run
  and makes active inference cancellable.

### 2026-06-09T13:52:16+03:00 - artifact: Implemented teardown-safe ONNX inference shutdown

- Detail: Patched CrowVisionSubsystem to stop on OnWorldBeginTearDown and request host inference shutdown
  before waiting. Patched MyActorComponent to keep persistent Ort::RunOptions, call SetTerminate on
  shutdown/StopWorker, block shared inference during shutdown, and release ONNX resources under
  InferenceMutex.
- Impact: Expected to prevent gameplay-finish segfault caused by active shared vision worker inside
  Ort::Session::Run during PIE teardown.

### 2026-06-09T13:53:01+03:00 - validation: EagleEyeEditor build succeeded

- Detail: Ran UE Build.sh EagleEyeEditor Linux Development with -WaitMutex. Result: Succeeded. Note: build
  reported ONNX Runtime deps env not sourced, but project compiled and linked existing target.
- Impact: Patch compiles under UE 5.6.1 Linux Development editor target.

## Report Notes

- Main findings:
  - Implemented and validated gameplay-finish segfault fix. Shared vision subsystem now binds FWorldDelegates::OnWorldBeginTearDown and stops worker before cleanup, with OnWorldCleanup retained as backup. Shutdown requests are propagated to registered, pending, and in-flight model hosts. Detection component now uses persistent Ort::RunOptions and calls SetTerminate during EndPlay/StopWorker/shared shutdown, blocks new shared inference once shutdown requested, and releases ONNX/TensorRT/OpenCV model resources after acquiring InferenceMutex. EagleEyeEditor Linux Development build succeeded.
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
