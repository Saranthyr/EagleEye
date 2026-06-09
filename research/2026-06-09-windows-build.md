# Windows Unreal build

- Created: 2026-06-09T05:15:00+03:00
- Task: Continue EagleEye build after switching to Windows

## Entries

### 2026-06-09T05:10:00+03:00 - source: Project state and build files checked

- Evidence: `EagleEye.uproject`, `EagleEye.sln`, `Source/EagleEye/EagleEye.Build.cs`
- Detail: `EagleEye.uproject` uses EngineAssociation `5.6`; existing `.sln` still contains paths to `UE_5.4`.
- Impact: Direct `Build.bat` invocation with known UE 5.6 engine path is safer than trusting stale solution paths.

### 2026-06-09T05:12:00+03:00 - validation: Initial Windows editor build failed on shadow warnings

- Evidence: `E:\Ue_5.6\Engine\Build\BatchFiles\Build.bat EagleEyeEditor Win64 Development E:\EagleEye\EagleEye.uproject -WaitMutex`
- Detail: UBT found VS 2022 toolchain `14.41.34120`, warned preferred compiler is `14.38.33130`, warned ONNX Runtime not found, then failed with `error C4458` in `BotAIController.cpp` and `error C4456` in `BTTask_FlyToBlackboardLocation.cpp`.
- Impact: Code needed warning-clean rename only; inference deps are optional because build defines `WITH_ONNXRUNTIME=0` when absent.

### 2026-06-09T05:13:00+03:00 - decision: Rename shadowing locals only

- Evidence: `Source/EagleEye/Private/BotAIController.cpp`, `Source/EagleEye/Private/BTTask_FlyToBlackboardLocation.cpp`
- Detail: Renamed local `Pawn` to `TargetPawn`; renamed inner debug `World` to `DebugWorld`.
- Impact: Removes Win64 warning-as-error failures without behavior change.

### 2026-06-09T05:14:00+03:00 - validation: Windows editor build succeeded

- Evidence: `E:\Ue_5.6\Engine\Build\BatchFiles\Build.bat EagleEyeEditor Win64 Development E:\EagleEye\EagleEye.uproject -WaitMutex`
- Detail: Result succeeded in 7.78 seconds after compiling and linking `UnrealEditor-EagleEye.dll`.
- Impact: Project now builds as `EagleEyeEditor Win64 Development` on Windows with UE at `E:\Ue_5.6`.

### 2026-06-09T05:20:00+03:00 - validation: Solution/project files regenerated and editor target rebuilt

- Evidence: `E:\Ue_5.6\Engine\Binaries\DotNET\UnrealBuildTool\UnrealBuildTool.exe -ProjectFiles -Project=E:\EagleEye\EagleEye.uproject -Game -Engine -Progress`; `E:\Ue_5.6\Engine\Build\BatchFiles\Build.bat EagleEyeEditor Win64 Development E:\EagleEye\EagleEye.uproject -WaitMutex`
- Detail: Project file generation succeeded in 8.29 seconds. Follow-up editor build reported `Target is up to date` and succeeded in 0.53 seconds.
- Impact: Workspace project files now match UE 5.6, and the editor target remains build-valid.

### 2026-06-09T05:40:00+03:00 - artifact: ONNX Runtime DirectML support implemented for Windows

- Evidence: `Scripts/setup-inference-deps.ps1`, `Source/EagleEye/EagleEye.Build.cs`, `Source/EagleEye/Private/MyActorComponent.cpp`, `Config/DefaultGame.ini`
- Detail: Added PowerShell installer for `Microsoft.ML.OnnxRuntime.DirectML` 1.24.4 into ignored `ThirdParty/OnnxRuntimeDirectML`; Build.cs auto-detects that root, links `onnxruntime.lib`, stages/copies `onnxruntime*.dll`, and enables `WITH_ONNXRUNTIME_DML`. Runtime provider auto-selection now chooses DirectML on Windows when compiled. Config now uses provider `Auto` instead of Linux-only `MIGraphX`.
- Impact: Windows builds no longer need manual `ONNXRUNTIME_ROOT` when local DirectML package is installed; same config can avoid forcing MIGraphX on Windows.

### 2026-06-09T05:41:00+03:00 - validation: ONNX Runtime DirectML Windows build succeeded

- Evidence: `E:\Ue_5.6\Engine\Build\BatchFiles\Build.bat EagleEyeEditor Win64 Development E:\EagleEye\EagleEye.uproject -WaitMutex`; `Binaries\Win64\onnxruntime.dll`; `Binaries\Win64\onnxruntime_providers_shared.dll`
- Detail: Build initially exposed a Windows `min`/`max` macro collision in `MyActorComponent.cpp`; fixed by calling `(std::min)` and `(std::max)`. Final build succeeded and copied ONNX Runtime DLLs into `Binaries\Win64`.
- Impact: Editor module now compiles and links with ONNX Runtime DirectML on Windows.

### 2026-06-09T05:55:00+03:00 - finding: Stop-PIE crash was likely async readback teardown

- Evidence: `Saved\Logs\EagleEye.log`; `Source\EagleEye\Private\MyActorComponent.cpp`; `Source\EagleEye\Private\CrowVisionSubsystem.cpp`
- Detail: Latest PIE run loaded ONNX Runtime, fell back to CPU after DirectML error `887A0004`, and processed many shared-vision frames successfully. Crash occurred on PIE stop with log ending mid-frame and no clean shutdown. `EnqueueAsyncOwnerCameraReadback` queued a render command that captured a raw `FRHIGPUTextureReadback*`; EndPlay could reset the owning `TUniquePtr` while the render command was still pending.
- Impact: Root cause is more consistent with teardown race than inference output crash.

### 2026-06-09T05:56:00+03:00 - validation: Stop-PIE teardown guard build succeeded

- Evidence: `E:\Ue_5.6\Engine\Build\BatchFiles\Build.bat EagleEyeEditor Win64 DebugGame E:\EagleEye\EagleEye.uproject -WaitMutex`
- Detail: Added EndPlay guard flag, blocked capture/submit/delivery during teardown, flushed render commands before resetting pending owner-camera readback, and stopped shared vision subsystem worker from accepting frames during deinit. DebugGame editor build succeeded.
- Impact: Ready for PIE rerun to validate stop no longer crashes.

### 2026-06-09T06:11:00+03:00 - finding: Random PIE crashes point to shared-worker UObject lifetime race

- Evidence: `Saved\Logs\EagleEye.log`; `Source\EagleEye\Private\CrowVisionSubsystem.cpp`
- Detail: Latest logs showed ONNX Runtime CPU fallback running inference successfully for many frames before crashes at varying moments. The shared-vision worker accessed `Requester->ShouldLogFrameTimings()`, `Requester->GetOwner()`, `Host->ShouldLogFrameTimings()`, and `Host->GetOwner()` from a background thread after only weak/raw pointer checks. Host unregister did not wait for an in-flight shared worker call before the host component could continue EndPlay.
- Impact: Random start/mid/stop crashes are more consistent with background-thread UObject lifetime/access than deterministic ONNX output failure.

### 2026-06-09T06:12:00+03:00 - validation: Shared-worker lifetime hardening build succeeded

- Evidence: `E:\Ue_5.6\Engine\Build\BatchFiles\Build.bat EagleEyeEditor Win64 DebugGame E:\EagleEye\EagleEye.uproject -WaitMutex`
- Detail: SubmitFrame now snapshots requester name/log flag on the game thread; worker no longer calls requester owner/log helpers; host unregister stops and waits for the shared worker; host inference logging avoids `GetOwner()` from worker. DebugGame build succeeded.
- Impact: Ready for another PIE stress test.

### 2026-06-09T06:32:00+03:00 - validation: Shared detection ownership token build succeeded

- Evidence: `Source\EagleEye\Public\MyActorComponent.h`, `Source\EagleEye\Private\MyActorComponent.cpp`, `Source\EagleEye\Public\AI\CrowVisionSubsystem.h`, `Source\EagleEye\Private\CrowVisionSubsystem.cpp`; `E:\Ue_5.6\Engine\Build\BatchFiles\Build.bat EagleEyeEditor Win64 DebugGame E:\EagleEye\EagleEye.uproject -WaitMutex`
- Detail: Added a monotonic shared-vision request serial. `SubmitFrame` snapshots the request serial, and `ConsumeSharedVisionResult` only accepts the result if the serial is still current for that detector. EndPlay invalidates outstanding serials.
- Impact: Stale async results from a previous detector/camera ownership interval cannot overwrite the current detector state after ownership handoff.

## Report Notes

- Main findings:
  - Windows build works with UE path `E:\Ue_5.6`.
  - Fixed two Win64 warning-as-error failures caused by local variable shadowing.
  - `.sln` appears stale because it references `UE_5.4` while `.uproject` says `5.6`.
  - ONNX Runtime DirectML is installed locally under `ThirdParty\OnnxRuntimeDirectML` and builds into the editor target.
- Evidence to cite:
  - `Source/EagleEye/Private/BotAIController.cpp`
  - `Source/EagleEye/Private/BTTask_FlyToBlackboardLocation.cpp`
  - UBT build output from 2026-06-09.
- Decisions and rationale:
  - Used direct `Build.bat` target build instead of relying on stale Visual Studio solution.
- Validation performed:
  - `EagleEyeEditor Win64 Development` build succeeded.
  - Project files regenerated with UE 5.6 UBT and follow-up build succeeded.
  - ONNX Runtime DirectML package layout validated and DLL copy verified in `Binaries\Win64`.
  - `EagleEyeEditor Win64 DebugGame` build succeeded after stop-PIE teardown guards.
  - `EagleEyeEditor Win64 DebugGame` build succeeded after shared-worker UObject lifetime hardening.
  - `EagleEyeEditor Win64 DebugGame` build succeeded after shared detection ownership token fix.
- Unresolved questions:
  - ONNX Runtime is not configured on Windows.
  - Visual Studio compiler `14.41.34120` works but is not UE preferred `14.38.33130`.
- Suggested report angle:
  - Windows migration build validation and minimal warning-clean fixes.
## Latest log recheck after user reported fixed

- Checked newest `Saved/Logs/EagleEye.log` (`2026-06-09 06:33:26` local). Gameplay logs looked healthy, but stop PIE still ended dirty.
- Evidence:
  - `BeginTearingDown` at `2026.06.09-03.33.18:288`
  - `UWorld::CleanupWorld for TestWorld` at `2026.06.09-03.33.18:326`
  - `SharedVisionDelivery[AnotherBot_C_1]` still posted after cleanup at `2026.06.09-03.33.18:328`
  - Fatal at `2026.06.09-03.33.26:397`: `SECURE CRT: Invalid parameter detected.`
- Decision: patch late shared-vision delivery so queued game-thread lambda rejects if subsystem is shutting down or requester world is tearing down, before logging or touching component results.
- Also hardened FFmpeg owner-camera video finalization with an atomic finalizer guard and null-before-close pipe handling to reduce double-close/invalid-handle shutdown risk.
- Validation: rebuilt `EagleEyeEditor Win64 DebugGame` with `E:\Ue_5.6\Engine\Build\BatchFiles\Build.bat`; result succeeded.

## Latest mid-play crash recheck

- User reported issue again after previous shutdown patch.
- Newest `Saved/Logs/EagleEye.log` was `2026-06-09 06:38:18` local, size `272610`.
- No new `Saved/Crashes` directory was written. `CrashReportClient.log` showed monitored editor death at `2026.06.09-03.38.21` with `App/ExitCode:-1`.
- UE log ended abruptly mid-play, not at PIE stop. Last meaningful lines:
  - `SharedVisionFrame: requester=CrowBot_C_1 ... detections=1`
  - `SharedVisionDelivery[CrowBot_C_1]: end_to_end=74.40ms`
  - no following `SharedResult[CrowBot_C_1]`
- Finding: `UCrowVisionSubsystem` worker still used `TWeakObjectPtr::Get()` and resolved model hosts from `TWeakObjectPtr`s on the worker thread. That is unsafe during GC/EndPlay/ownership transfer and matches random mid-play access violation behavior.
- Patch:
  - `SubmitFrame` now resolves model host on game thread.
  - `FQueuedFrame` stores a `TStrongObjectPtr<UMyActorComponent>` for the model host so the worker has a stable host pointer while inference runs.
  - Worker no longer resolves model host weak pointers off-thread.
  - `StopWorker(false)` no longer marks subsystem permanently shutting down when host is merely unregistered, so ownership transfer can restart worker.
- Validation: rebuilt `EagleEyeEditor Win64 DebugGame`; result succeeded.

## Crash after host strong-pointer patch

- User reported another crash and asked to reason about likely causes.
- Newest `Saved/Logs/EagleEye.log` timestamp: `2026-06-09 06:42:52` local, size `455631`.
- CrashReportClient monitored editor from `2026.06.09-03.42.29` and reported `App/Death` at `2026.06.09-03.42.54`, `App/ExitCode:-1`.
- UE log again ended abruptly mid-play with no fatal/stack.
- Unlike previous run, several `SharedVisionDelivery` lines were followed by `SharedResult`, so delivery path was not consistently the last missing log.
- Strong clue: near the end, multiple async owner-camera readbacks showed very large latency:
  - `AsyncReadbackPoll[AnotherBot_C_4] latency=2355.33ms`
  - `AsyncReadbackPoll[AnotherBot_C_3] latency=2466.75ms`
  - then normal readbacks resumed briefly before log cut.
- Current hypothesis ranking:
  1. GPU readback/render-target lifetime race or stale `FRHIGPUTextureReadback` after bot activation/ownership transfer.
  2. Too many owner-camera captures/readbacks active because skipped/unavailable bots still capture when video/record settings allow it.
  3. Remaining off-thread UObject ownership/lifetime issue, especially any `TStrongObjectPtr`/`TWeakObjectPtr` destruction or UObject method access from worker threads.
  4. ONNX runtime CPU fallback is less likely because logs show inference completing repeatedly and no ONNX fatal signature.

## Async owner-camera readback lifetime patch

- Proceeded with top hypothesis: render readback lifetime/stale pending readbacks.
- Patch details:
  - `PendingOwnerCameraReadback` changed from `TUniquePtr<FRHIGPUTextureReadback>` to `TSharedPtr<FRHIGPUTextureReadback, ESPMode::ThreadSafe>`.
  - Render command now captures the shared pointer instead of a raw `FRHIGPUTextureReadback*`, keeping the readback object alive until the render command runs.
  - Added `MaxAsyncOwnerCameraReadbackLatencySeconds = 1.0`.
  - `PollAsyncOwnerCameraReadback` now drops a pending readback older than 1s, flushes render commands, and clears metadata/depth buffers.
- Validation: rebuilt `EagleEyeEditor Win64 DebugGame`; result succeeded.

## Shared-result consumption hardening

- Latest run (`Saved/Logs/EagleEye.log` at `2026-06-09 07:08:41`) again died mid-play with no UE fatal stack.
- CrashReportClient reported monitored editor `App/Death` at `2026.06.09-04.08.43`, `ExitCode:-1`.
- Final clue: last log was `SharedVisionDelivery[CrowBot_C_1]`, with no following `SharedResult[CrowBot_C_1]`.
- Patch:
  - `ConsumeSharedVisionResult` now snapshots `World` and `OwnerActor` once and rejects if world is null/tearing down, owner invalid, component unregistered, request serial stale, or ending play.
  - Replaced repeated `GetWorld()`/`GetOwner()` calls with the validated snapshots.
  - Removed `LogFovDetectionMetricSample(TEXT("model_shared"), ...)` from shared delivery path; it touches owner/camera/world again and is not required for stable runtime.
- Validation: rebuilt `EagleEyeEditor Win64 DebugGame`; result succeeded.

### 2026-06-09T11:09:33 - validation: DirectML redist installed and staged

- Evidence: ThirdParty\DirectML\bin\x64-win\DirectML.dll exists; build output copied DirectML.dll; Binaries\Win64\DirectML.dll verified after rebuild.
- Detail: Latest UE log before rebuild still showed DirectML EP failure 887A0004 and CPU fallback because DirectML.dll was absent from Binaries\Win64. Patched EagleEye.Build.cs to explicitly stage and binary-copy the DirectML redist DLL, then rebuilt successfully.
- Impact: Next PIE run should load project-local DirectML 1.15.2 instead of old System32 DirectML.


### 2026-06-09T11:22:26 - decision: harden shared vision worker lifetime

- Evidence: Latest DirectML run showed 7-10ms shared inference and no CPU fallback. Random crashes still match worker/game-thread teardown risk: subsystem worker used TWeakObjectPtr<UCrowVisionSubsystem> off-thread and queued game-thread deliveries could outlive worker generation.
- Detail: Patched UCrowVisionSubsystem to stop worker at FWorldDelegates::OnWorldCleanup, increment DeliveryGeneration on stop/cleanup, avoid worker-thread TWeakObjectPtr validity checks for subsystem, use game-thread-created weak subsystem only in delivery lambda, and clear stale deliveries by generation.
- Impact: Reduces access violation risk in UE UObject weak pointer/lifetime internals during PIE stop, world cleanup, and host transfer.
- Validation: rebuilt EagleEyeEditor Win64 DebugGame; result succeeded.

