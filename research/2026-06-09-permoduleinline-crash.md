# PerModuleInline Crash Research

Task: Check Unreal logs for `W32/0xC0000005` / access violation in `PerModuleInline.inl`.

### 2026-06-09T11:49:22+03:00 - command: Initial log search

- Evidence: `rg -n "0xC0000005|Access violation|Unhandled Exception|Exception|PerModuleInline|Callstack|Fatal error" Saved Source Plugins Config -S`
- Detail: Found many older crash records; newest visible relevant entry includes `Saved\Crashes\UECC-Windows-95AF93844530AE36FAFDD98539578ADC_0000\CrashContext.runtime-xml` with `Unhandled Exception: EXCEPTION_ACCESS_VIOLATION reading address 0x000001c683dbe050`. Also found many `0xe06d7363` C++ exception crashes.
- Impact: User-reported debugger line may be engine inline frame; need actual project-side callstack/crash context.

### 2026-06-09T11:52:00+03:00 - source: Old access violation crash context

- Evidence: `Saved\Crashes\UECC-Windows-95AF93844530AE36FAFDD98539578ADC_0000\CrashContext.runtime-xml`
- Detail: Old UE 5.4 crash callstack top frames were `UMyActorComponent::ProcessWithOpenCV()` and `UMyActorComponent::CaptureAndProcess()`. This is old code/path and does not match current source layout.
- Impact: Confirms `PerModuleInline.inl` is not root; actual failures historically came from project OpenCV/inference code.

### 2026-06-09T11:57:00+03:00 - finding: Current log dies during first shared vision inference

- Evidence: `Saved\Logs\EagleEye.log`; latest relevant tail ends after `DetectionSubmit[AnotherBot_C_0]: capture+submit=45.50ms size=1024x1024`, with no `SharedVisionFrame` line.
- Detail: Startup selected `ONNX Runtime` with `DirectML` on `AMD Radeon RX 7900 XTX`; model `E:/EagleEye/Source/EagleEye/yolo26s.onnx` loaded. Log stops just after first queued detection submit.
- Impact: Crash likely occurs inside worker-side inference (`UCrowVisionSubsystem` -> `UMyActorComponent::ProcessSharedVisionFrame` -> `RunOnnxRuntimeInference_BG`) before worker can log completion.

### 2026-06-09T12:02:00+03:00 - source: Engine inline file context

- Evidence: `E:\UE_5.6\Engine\Source\Runtime\Core\Public\HAL\PerModuleInline.inl`
- Detail: File only contains module boilerplate and memory wrapper macros (`REPLACEMENT_OPERATOR_NEW_AND_DELETE`, `UE_DEFINE_FMEMORY_WRAPPERS`). A crash here means invalid allocation/free or memory corruption surfaced in UE memory wrappers.
- Impact: Do not chase this engine file as root cause; use project callstack/log timing around inference.

### 2026-06-09T12:07:00+03:00 - comparison: Previous session succeeded

- Evidence: `Saved\Logs\EagleEye-backup-2026.06.09-10.30.49.log`
- Detail: Same ONNX Runtime DirectML backend completed many `SharedVisionFrame` entries with 7-30 ms inference. Later failing logs (`10.33.58` and current) stop before the first `SharedVisionFrame`.
- Impact: Crash is intermittent or state-dependent in first worker inference path, not a guaranteed model-load failure.

## Report Notes

- Main findings:
  - `PerModuleInline.inl` is a symptom frame from UE memory wrappers, not likely root cause.
  - Current crash timing points to first shared vision worker inference using ONNX Runtime DirectML.
  - No exact current crash dump with `0xFFFFFFFFFFFFFFFF` was found in project `Saved\Crashes`; debugger likely caught it before CrashReporter wrote context.
- Evidence to cite:
  - `Saved\Logs\EagleEye.log` stops after first `DetectionSubmit`.
  - `Saved\Logs\EagleEye-backup-2026.06.09-10.33.58.log` shows same stop pattern.
  - `Saved\Logs\EagleEye-backup-2026.06.09-10.30.49.log` shows successful `SharedVisionFrame` loop.
  - `Source\EagleEye\Private\CrowVisionSubsystem.cpp:217` calls `Host->ProcessSharedVisionFrame(...)` from worker thread.
  - `Source\EagleEye\Private\MyActorComponent.cpp:2637` calls `ProcessWithOpenCV_BG(...)`; `Source\EagleEye\Private\MyActorComponent.cpp:3906` calls `RunOnnxRuntimeInference_BG(...)`.
- Decisions and rationale:
  - No code edited; request was log triage.
  - Next isolation should test CPU ONNX provider or disable shared vision worker model path.
- Validation performed:
  - Searched logs/crashes for access violations and PerModuleInline references.
  - Compared current and backup logs.
  - Read current inference/thread source paths.
- Unresolved questions:
  - Exact debugger callstack for current `0xFFFFFFFFFFFFFFFF` crash.
  - Whether switching ONNX Runtime provider from DirectML to CPU stops crash.
- Suggested report angle:
  - Treat as memory corruption/invalid free surfacing in UE allocator during background ONNX DirectML inference, with `PerModuleInline.inl` only as wrapper frame.

### 2026-06-09T12:20:00+03:00 - decision: Add ONNX Runtime GPU crash guard

- Evidence: User reported crash gone when running with CPU; driver string provided: AMD Radeon RX 7900 XTX 32.0.31007.5012 05.12.2026 3:00:00.
- Detail: Implement fallback for future Auto/DirectML attempts using a persistent Saved/OnnxRuntimeGpuCrashGuard.txt marker. GPU session writes marker before session creation; clean release clears it; next launch with marker forces CPU.
- Impact: Hard access violations cannot be caught in-process, so fallback must activate on next launch after unclean GPU inference session.

### 2026-06-09T12:23:00+03:00 - validation: Build attempted

- Evidence: `E:\UE_5.6\Engine\Build\BatchFiles\Build.bat EagleEyeEditor Win64 Development -Project="E:\EagleEye\EagleEye.uproject" -WaitMutex -NoUBTMakefiles`
- Detail: UHT succeeded and fallback code compiled far enough to link. Link failed with pre-existing unresolved symbols across unrelated classes (`ABotAIController`, `ABotCharacter`, `AEagleEyeCharacter`, `UCrowVisionSubsystem`, `ADetectionModelHostActor`, etc.).
- Impact: No compile error found in fallback changes, but full build remains blocked by unrelated linker errors.


### 2026-06-09T12:05:00+03:00 - finding: Link failure caused by adaptive unity response file

- Evidence: Attached build log and Intermediate/Build/Win64/x64/UnrealEditor/Development/EagleEye/UnrealEditor-EagleEye.lib.rsp omitted separate implementation objects such as BotAIController.cpp.obj, BotCharacter.cpp.obj, and CrowVisionSubsystem.cpp.obj, while .rsp.old included them.
- Detail: Generated unity objects referenced UHT symbols for classes whose implementation objects were not linked. Added UseUnity = false; in Source/EagleEye/EagleEye.Build.cs to avoid adaptive-unity object omission for this module.
- Impact: Rebuild with -NoUBTMakefiles succeeded.

### 2026-06-09T12:06:00+03:00 - validation: Windows editor build passed

- Evidence: E:\UE_5.6\Engine\Build\BatchFiles\Build.bat EagleEyeEditor Win64 Development E:\EagleEye\EagleEye.uproject -waitmutex -NoUBTMakefiles returned Result: Succeeded.
- Detail: UHT succeeded, 10 build actions ran, and UnrealEditor-EagleEye.dll linked.
- Impact: Linker errors from attached log resolved.


### 2026-06-09T12:10:00+03:00 - finding: Startup exception in TFuture reference state

- Evidence: User reported editor startup access violation in MSVC tomic: 	his->WorkerFuture.State.SharedReferenceCount.ReferenceController was 0x100000100.
- Detail: Replaced TFuture<void> worker holders in UMyActorComponent and UCrowVisionSubsystem with owned std::thread instances joined/reset explicitly. This removes UE shared-reference state writes during worker start/stop.
- Impact: Should eliminate crash path in TFuture/atomic reference-count internals.

### 2026-06-09T12:11:00+03:00 - validation: Build passed after thread replacement

- Evidence: E:\UE_5.6\Engine\Build\BatchFiles\Build.bat EagleEyeEditor Win64 Development E:\EagleEye\EagleEye.uproject -waitmutex -NoUBTMakefiles returned Result: Succeeded.
- Detail: UHT wrote 2 files; UnrealEditor-EagleEye.dll linked successfully.
- Impact: Thread replacement compiles and links.


### 2026-06-09T12:15:15+03:00 - finding: Editor crash now occurs before EagleEye gameplay logs

- Evidence: Saved\Logs\EagleEye.log tail ends after LogUdpMessaging: Display: Using asynchronous task graph for message deserialization.; no Inference backend requested, Shared vision model host spawned, or SharedVisionFrame entries.
- Detail: Current log shows Live Coding initializes immediately before the last startup section. Binaries\Win64 contains old UnrealEditor-EagleEye*.patch_* Live Coding artifacts plus fresh full DLLs.
- Impact: Points away from ONNX inference path and toward startup/live-coding/module-layout issue.

### 2026-06-09T12:18:00+03:00 - decision: Remove smart thread pointer members from UObjects

- Evidence: Source\EagleEye\Public\MyActorComponent.h, Source\EagleEye\Public\AI\CrowVisionSubsystem.h, corresponding .cpp lifecycle code.
- Detail: Replaced TUniquePtr<std::thread> with raw std::thread* that is always joined/detached then deleted in Stop/Close paths. Avoids non-trivial smart-pointer destructor state inside UObject/subsystem memory during Live Coding/reinstancing.
- Impact: Keeps previous TFuture crash fix while reducing UObject startup/layout risk.
### 2026-06-09T12:23:00+03:00 - finding: Stale object files kept old crashy detector code

- Evidence: `Intermediate\Build\Win64\x64\UnrealEditor\Development\EagleEye\MyActorComponent.cpp.obj` was not recompiled until deleted manually; subsequent build compiled `CrowVisionSubsystem.cpp`, `DetectionModelHostActor.cpp`, and `MyActorComponent.cpp`.
- Detail: Linker and startup behavior changed only after removing stale `Intermediate` object artifacts.
- Impact: Future similar unresolved symbols/startup crashes may require forcing affected `.cpp.obj` rebuilds or cleaning module intermediates.

### 2026-06-09T12:26:00+03:00 - validation: Startup crash no longer repros in headless editor smoke test

- Evidence: `E:\UE_5.6\Engine\Binaries\Win64\UnrealEditor-Cmd.exe E:\EagleEye\EagleEye.uproject -NoLiveCoding -NullRHI -unattended -nop4 -stdout -FullStdOutLogOutput -ExecCmds="Quit"`.
- Detail: Process reached `Engine is initialized`, loaded `TestWorld.umap`, printed `Cmd: Quit`, and did not hit the previous `VCRUNTIME140.dll` / `CoreUObject` / `CreateDefaultSubobject<UMyActorComponent>` access violation. Test process hung after quit until stopped manually.
- Impact: Startup UObject crash path is fixed; remaining full GUI/GPU validation should be done in the editor.

## Report Notes

- Main findings: ONNX GPU crash was separate from startup UObject crash. Startup crash came from stale/unsafe detector component construction during CDO/default-subobject load, first at `ADetectionModelHostActor`, then at `ABotCharacter` while stale `MyActorComponent.cpp.obj` was still linked.
- Evidence to cite: `Saved\Logs\EagleEye.log` callstacks at `DetectionModelHostActor.cpp:15` and `BotCharacter.cpp:25`; smoke-test log showing `Engine is initialized`, map load, and `Cmd: Quit`.
- Decisions and rationale: Use CPU fallback/guard for ONNX GPU provider; replace `TFuture` members with explicit `std::thread*` join/delete lifecycle; create shared model host detector component at instance initialization instead of actor constructor.
- Validation performed: `Build.bat EagleEyeEditor Win64 Development ... -NoUBTMakefiles` succeeded; `UnrealEditor-Cmd.exe ... -NoLiveCoding -NullRHI ... -ExecCmds="Quit"` passed previous crash point and loaded editor/map.
- Unresolved questions: GUI editor with real D3D12 and play-in-editor still needs user-side confirmation; existing missing `/Game/ThirdPerson/Blueprints/BP_ThirdPersonCharacter` CDO asset reference remains unrelated.
- Suggested report angle: Separate GPU inference stability from UObject startup construction faults; document stale Intermediate artifact issue as part of Windows UE build hygiene.

### 2026-06-09T12:29:00+03:00 - validation: DebugGame Editor rebuilt with fresh detector objects

- Evidence: `E:\UE_5.6\Engine\Build\BatchFiles\Build.bat EagleEyeEditor Win64 DebugGame E:\EagleEye\EagleEye.uproject -waitmutex -NoUBTMakefiles`.
- Detail: First DebugGame build hit same stale-object unresolved `ADetectionModelHostActor::PostInitializeComponents`; after deleting DebugGame `DetectionModelHostActor.cpp.obj`, `MyActorComponent.cpp.obj`, and `CrowVisionSubsystem.cpp.obj`, build compiled all three and linked `UnrealEditor-EagleEye-Win64-DebugGame.dll`.
- Impact: Visual Studio DebugGame launch should use fresh fixed code.

### 2026-06-09T14:13:00+03:00 - finding: Immediate PIE crash follows multiple async readbacks in one frame

- Evidence: Latest Saved\Logs\EagleEye.log shows DirectML loaded, then AsyncReadbackEnqueue for CrowBot_C_1, AnotherBot_C_0, and AnotherBot_C_1 in frame 130. First AsyncReadbackPoll[CrowBot_C_1] succeeds, then log stops before DetectionSubmit.
- Detail: No new crash report was written; debugger likely caught exception before UE crash reporter. Pattern points to RHI/readback pressure/concurrency, not initial DirectML load.
- Impact: Keep async path but throttle RHI readback operations globally per frame.
