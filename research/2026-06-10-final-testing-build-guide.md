# Final Testing Build Guide

- Created: 2026-06-10T13:24:09+03:00
- Task: Guide how to build final build for testing for EagleEye Unreal project

## Entries

### 2026-06-10T13:25:03+03:00 - source: Reviewed EagleEye build and packaging inputs

- Detail: Checked EagleEye.uproject, Config/DefaultEngine.ini, Config/DefaultGame.ini,
  Source/EagleEye/EagleEye.Build.cs, Plugins/OpenCV412 Build.cs, setup scripts, existing staged
  build.

### 2026-06-10T13:25:03+03:00 - finding: Project targets UE 5.6 and stages runtime models/libs

- Detail: EagleEyeTarget is game target. Default map is /Game/ThirdPerson/Blueprints/TestWorld. Build.cs
  stages .onnx/.names/.plan from Source/EagleEye and Models into packaged output Models folder,
  plus ONNX Runtime, DirectML/MIGraphX/TensorRT/OpenCV runtime files when env deps exist.

## Report Notes

- Main findings:
  - Guide should recommend RunUAT BuildCookRun for Linux and Win64, Development for tester builds with logs, Shipping for final smoke. Linux needs source Saved/InferenceDeps.env. Windows needs Scripts/setup-inference-deps.ps1 -InstallDirectML -InstallDirectMLRedist and dot-source Saved/InferenceDeps.ps1. Validate by checking executable, Content/Paks, Models folder, runtime DLL/SO files, and Saved/Logs.
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

### 2026-06-10T13:26:36+03:00 - finding: Metrics/statistics builds should use Development config

- Detail: DefaultGame.ini enables bRecordFrameTimes and detection metric/performance logs. MyActorComponent
  writes frame timing CSVs to Saved/Profiling/DetectionFrameTimes_<backend>.csv, FOV metrics to
  Saved/Profiling/FovDetectionMetrics.csv. BTServ_UpdateCrowTargetDetection writes depth metrics to
  Saved/Profiling/DepthDetectionMetrics.csv or DepthDetectionMetrics_v2.csv.

### 2026-06-10T13:32:47+03:00 - validation: Checked LocalBuildLogs and fixed Linux cook blockers

- Detail: Log.txt showed BuildCookRun Linux Development failed during cook with ExitCode=25 because
  GameDefaultMap used redirected /Game/ThirdPerson/Blueprints/TestWorld and AEagleEyeGameMode CDO
  hard-loaded missing /Game/ThirdPerson/Blueprints/BP_ThirdPersonCharacter. Updated DefaultEngine
  map to /Game/ThirdPerson/Blueprints/TestMap/TestWorld and changed GameMode default pawn fallback
  to native AEagleEyeCharacter. Build.sh EagleEyeEditor Linux Development succeeded. Reran UAT cook
  with corrected map; BUILD SUCCESSFUL, ExitCode=0, Success - 0 errors, 0 warnings.

### 2026-06-10T13:38:05+03:00 - finding: Latest metrics build did not include ProcGen map

- Detail: LocalBuildLogs/Log.txt shows MapsToCook=/Game/ThirdPerson/Blueprints/TestWorld only.
  Saved/Cooked/Linux/EagleEye contains Content/ThirdPerson/Blueprints/TestMap/TestWorld.umap and no
  ProcGen.umap. To include both intended maps use
  -map=/Game/ThirdPerson/Blueprints/TestMap/TestWorld+/Game/ThirdPerson/Blueprints/ProcGen/ProcGen
  or configure ProjectPackagingSettings MapsToCook.

### 2026-06-10T13:53:18+03:00 - finding: GPU crash guard exists but did not trip in latest Linux-Metrics run

- Detail: MyActorComponent writes Saved/OnnxRuntimeGpuCrashGuard.txt when ONNX Runtime GPU provider is
  configured and clears it during ReleaseOnnxRuntime. Latest packaged logs show MIGraphX configured,
  then ONNX Runtime model load failed: Failed to call function, then Auto retried CPU. No
  OnnxRuntimeGpuCrashGuard.txt exists under Builds/Linux-Metrics/EagleEye/Saved. Metrics file
  present is DetectionFrameTimes_ONNXRuntime_CPU.csv.
