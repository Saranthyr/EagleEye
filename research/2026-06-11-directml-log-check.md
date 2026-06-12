# DirectML log check

- Created: 2026-06-11T07:18:30+03:00
- Task: Investigate why DirectML is not loaded from VS Code/Unreal logs

## Entries

### 2026-06-11T07:19:00+03:00 - finding: Latest log shows ONNX Runtime absent at compile time

- Detail: Saved/Logs/EagleEye.log lines around 1507-1516: backend requested Auto on Windows AMD, then warning
  'ONNX Runtime is not compiled into this build. Auto will use OpenCV DNN CPU fallback.' It then
  loads OpenCV DNN and fails YOLO TopK parsing.

### 2026-06-11T07:19:00+03:00 - finding: Editor module older than installed deps

- Detail: Binaries/Win64/UnrealEditor-EagleEye.dll timestamp 2026-06-11 07:01:47; OnnxRuntimeDirectML/DirectML
  packages installed around 07:07-07:09. Current binary was built before deps existed, so
  WITH_ONNXRUNTIME stayed 0.

### 2026-06-11T07:20:37+03:00 - validation: Forced rebuild enabled DirectML artifacts

- Detail: Initial Build.bat said target up to date. Ran Clean.bat then Build.bat for EagleEyeEditor Win64
  Development. Build output copied onnxruntime_providers_shared.dll, onnxruntime.dll, and
  DirectML.dll, then compiled MyActorComponent.cpp and linked UnrealEditor-EagleEye.dll
  successfully.

## Report Notes

- Main findings:
  - Latest Unreal log was from pre-DirectML binary: ONNX Runtime was not compiled, so Auto fell back to OpenCV DNN CPU and failed TopK. Forced Clean.bat + Build.bat for EagleEyeEditor Win64 Development. Rebuild succeeded and copied onnxruntime.dll, onnxruntime_providers_shared.dll, and DirectML.dll to Binaries/Win64; UnrealEditor-EagleEye.dll now timestamped after rebuild. No OnnxRuntimeGpuCrashGuard.txt found.
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
