# Runtime performance log split

- Created: 2026-06-07T19:42:44+03:00
- Task: Add performance logging to different files based on runtime: TensorRT, ONNX + DirectML, MIGraphX

## Entries

### 2026-06-07T19:42:55+03:00 - source: Located metric writers and runtime config symbols

- Detail: rg found FOV metrics in Source/EagleEye/Private/MyActorComponent.cpp +
  Source/EagleEye/Public/MyActorComponent.h, depth metrics in
  Source/EagleEye/Private/BTServ_UpdateCrowTargetDetection.cpp, runtime enums in
  Source/EagleEye/Public/DetectionInferenceTypes.h.

### 2026-06-07T19:45:08+03:00 - decision: Split frame timing logs by effective runtime

- Detail: Default FrameTimeCsvPath now resolves to DetectionFrameTimes_<runtime>.csv after model load, using
  EffectiveInferenceBackend and EffectiveOnnxRuntimeExecutionProvider. Explicit FrameTimeCsvPath
  remains respected as an override.

### 2026-06-07T19:45:53+03:00 - validation: Unreal editor build succeeded

- Detail: Command: /home/saranthyr/Unreal_Engine_5.6.1/Engine/Build/BatchFiles/Linux/Build.sh EagleEyeEditor
  Linux Development EagleEye.uproject -WaitMutex. Result: Succeeded.

## Report Notes

- Main findings:
  - Implemented runtime-split frame timing CSV defaults. Default performance logs now go to Saved/Profiling/DetectionFrameTimes_TensorRT.csv, DetectionFrameTimes_ONNXRuntime_DirectML.csv, DetectionFrameTimes_ONNXRuntime_MIGraphX.csv, DetectionFrameTimes_ONNXRuntime_CPU.csv, or OpenCVDNN/Auto equivalents based on effective runtime. Explicit FrameTimeCsvPath still overrides default split. Unreal editor build succeeded.
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
