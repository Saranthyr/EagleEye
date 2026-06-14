# Linux-Metrics startup crash

- Created: 2026-06-14T11:32:23+03:00
- Task: Check logs again. Crash on startup after packaging ONNX/MIGraphX libs.

## Entries

### 2026-06-14T11:32:59+03:00 - finding: Startup crash occurs during explicit MIGraphX provider dlopen

- Detail: Latest crash pid 76857: SIGSEGV null read. Callstack starts in libonnxruntime_providers_migraphx.so
  during ld-linux dlopen, reached from FUnixPlatformProcess::GetDllHandle -> TryLoadRuntimeLibrary
  -> UMyActorComponent::LoadOnnxRuntime line 626. Log reaches LoadYOLO selected ONNX Runtime
  MIGraphX, then crashes before provider configured log.

### 2026-06-14T11:37:12+03:00 - decision: Stop manually loading ONNX Runtime provider plugin

- Detail: Removed explicit runtime probe for libonnxruntime_providers_shared.so and
  libonnxruntime_providers_migraphx.so. Kept MIGraphX dependency probes including libmigraphx_gpu.so,
  so missing ROCm/MIGraphX companion libraries are still detected before session creation.
- Reason: ONNX Runtime should own provider plugin loading. The crash happens inside the provider plugin
  while loaded by the availability probe, before Ort session/provider configuration code runs.

### 2026-06-14T11:35:18+03:00 - validation: Packaged startup no longer segfaults in provider dlopen

- Command: Rebuilt EagleEye Linux Development with Build.sh, copied Binaries/Linux/EagleEye into
  Builds/Linux-Metrics/EagleEye/Binaries/Linux/EagleEye, then ran packaged build for 25s with
  -nullrhi -nosound -unattended -stdout -FullStdOutLogOutput.
- Result: Build succeeded. Smoke run was terminated by timeout, not crash. No new Saved/Crashes entry
  appeared after the previous pid 76857 crash.
- Log evidence: Builds/Linux-Metrics/EagleEye/Saved/Logs/EagleEye.log reaches
  "ONNX Runtime provider configured: MIGraphX", proving the previous provider dlopen crash is gone.
- Remaining issue: Session/model creation then reports "HIP failure 100: no ROCm-capable device is detected".
  This is device/provider availability on the smoke-test host, not missing packaged libraries or the previous
  startup segfault.
