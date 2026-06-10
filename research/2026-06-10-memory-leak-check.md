# Memory leak check

Date: 2026-06-10

## Scope

- User suspected architecture memory leak.
- User clarified issue is definitely not `WorldGen`, so `WorldGen` findings ignored and no `WorldGen` edits made.
- Focused on detection/model/video/shared-vision ownership.

## Commands and sources checked

- `rg --files`
- `rg -n "\bnew\b|delete\b|malloc|free\(|CreateDefaultSubobject|NewObject|AddDynamic|AddUObject|AddLambda|SetTimer|ClearTimer|BeginDestroy|EndPlay|BeginPlay|TickComponent|Tick\(|TSharedPtr|TUniquePtr|TWeakObjectPtr|UPROPERTY|cv::|cuda|TensorRT|nvinfer|IRuntime|ICudaEngine|IExecutionContext" Source Private Public Plugins -g "*.h" -g "*.cpp" -g "*.cs"`
- `rg -n "void UMyActorComponent::(BeginPlay|EndPlay|StartWorker|StopWorker|StartOwnerCameraVideoWorker|OwnerCameraVideoWorkerLoop|CloseOwnerCameraVideoWriter|FinalizeOwnerCameraVideoWriter|ReleaseTensorRT|ReleaseOnnxRuntime|EnsureOwnerCameraCapture|ResetOwnerCameraReadback|AdvanceOwnerCameraReadbackGeneration|RequestInferenceShutdown|RequestOnnxRuntimeInferenceTerminate)" Source\EagleEye\Private\MyActorComponent.cpp`
- `Source/EagleEye/Public/MyActorComponent.h`
- `Source/EagleEye/Private/MyActorComponent.cpp`
- `Source/EagleEye/Public/AI/CrowVisionSubsystem.h`
- `Source/EagleEye/Private/CrowVisionSubsystem.cpp`
- `Source/EagleEye/Public/AI/CrowDetectionShareSubsystem.h`
- `Source/EagleEye/Private/CrowDetectionShareSubsystem.cpp`
- `Source/EagleEye/Private/DetectionModelHostActor.cpp`

## Findings

- `UMyActorComponent` correctly stops main worker thread in `StopWorker()` and releases TensorRT/CUDA/ONNX/OpenCV model resources through `ReleaseTensorRT()`, `ReleaseOnnxRuntime()`, and `OpenCVDnnNet = cv::dnn::Net()`.
- Owner-camera capture path creates dynamic `UScreenCaptureComponent` instances in `EnsureOwnerCameraCapture()` using the owner actor as outer:
  - `OwnerSceneCapture = NewObject<UScreenCaptureComponent>(Owner, TEXT("OwnerCameraDetectionCapture"))`
  - `OwnerDepthSceneCapture = NewObject<UScreenCaptureComponent>(Owner, TEXT("OwnerCameraDepthCapture"))`
- Before fix, `EndPlay()` reset pending GPU readback and closed video writer, but did not explicitly destroy those dynamic capture components or release their render targets.
- Risk: if detection component is disabled/destroyed while owner actor survives, registered capture components and render resources can remain alive longer than intended.
- Runtime `SetUseOwnerCameraCapture(false)` previously only toggled bool. Existing capture components, render targets, depth buffers, async readback state, and share-subsystem registration could remain.
- `UCrowVisionSubsystem` worker lifecycle looked mostly sound: `Deinitialize()` removes delegates and calls `StopWorker(true)`, queued frames use weak object pointers, async delivery uses weak subsystem/requester checks plus generation.
- `UCrowDetectionShareSubsystem` stores weak actor pointers and removes invalid entries during register/unregister/query paths. No strong-object leak found there.

## Changes made

- Added `UMyActorComponent::ReleaseOwnerCameraCaptureResources()`.
- `ReleaseOwnerCameraCaptureResources()` now:
  - resets pending GPU readback
  - clears published depth buffer
  - nulls texture targets
  - destroys owner/depth `UScreenCaptureComponent` instances
  - releases and nulls color/depth render targets
- `EndPlay()` now calls `ReleaseOwnerCameraCaptureResources()`.
- `SetUseOwnerCameraCapture(false)` now unregisters owner from `UCrowDetectionShareSubsystem` and releases capture resources.
- `SetUseOwnerCameraCapture(true)` now registers owner with `UCrowDetectionShareSubsystem`, matching `BeginPlay()` behavior.

## Validation

- Fast source scan confirmed new declarations and call sites:
  - `Source/EagleEye/Public/MyActorComponent.h:107`
  - `Source/EagleEye/Public/MyActorComponent.h:165`
  - `Source/EagleEye/Private/MyActorComponent.cpp:553`
  - `Source/EagleEye/Private/MyActorComponent.cpp:1012`
  - `Source/EagleEye/Private/MyActorComponent.cpp:1225`
- Initial sandboxed Windows Unreal build failed because UnrealBuildTool could not write/delete its AppData log backup.
- Escalated Windows Unreal build succeeded:
  - Command: `C:\Program Files\Epic Games\UE_5.6\Engine\Build\BatchFiles\Build.bat EagleEyeEditor Win64 Development -Project="C:\Users\Saranthyr\Documents\Unreal Projects\EagleEye\EagleEye.uproject" -WaitMutex -NoHotReload`
  - Result: `Succeeded`
  - Note: XGE printed `License not activated`; build fell back/ran standalone and completed.

## Residual risks

- Best runtime validation: run PIE, toggle owner-camera capture repeatedly, then use `memreport -full`, `obj list class=ScreenCaptureComponent`, and `stat memory` before/after.

## Log check after fix

- Checked latest logs on 2026-06-10:
  - `Saved/Logs/EagleEye.log`
  - `C:\Users\Saranthyr\AppData\Local\UnrealBuildTool\Log.txt`
- Latest build log result: `Succeeded`.
- Latest runtime log has 1 `Error:` and 14 `Warning:` entries.
- Runtime error is unrelated to memory/resource cleanup:
  - `CDO Constructor (EagleEyeGameMode): Failed to find /Game/ThirdPerson/Blueprints/BP_ThirdPersonCharacter.BP_ThirdPersonCharacter_C`
- Important runtime warnings:
  - `TensorRT backend requested but this build was compiled without TensorRT. Falling back.`
  - `ONNX Runtime GPU crash guard found ... Forcing CPU provider for this run. Delete the file to retry GPU inference.`
- Latest runtime run did not include shutdown/exit lines (`LogExit`, `EndPlay`, resource-release logs). Tail stops during active detection at:
  - `[2026.06.10-04.35.19:397][159]LogTemp: DetectionSubmit[CrowBot_C_1]: capture+submit=28.29ms size=1024x1024`
- Detection timings in latest log show CPU ONNX path around 63-73 ms inference per shared frame, with async readback latency commonly around 60-110 ms.
- No log evidence of crash, fatal error, CUDA/TensorRT runtime failure, stale async readback drop, or FFmpeg process leak in latest `EagleEye.log`.

## Debugger crash stack follow-up

- User supplied VS Code call stack showing crash in:
  - `UnrealEditor-Core.dll!_mi_free_block_mt`
  - `std::vector<cv::Mat>::~vector<cv::Mat>()`
  - `UMyActorComponent::RunOnnxRuntimeInference_BG(...)` at `MyActorComponent.cpp:3883`
  - `UCrowVisionSubsystem` worker thread
- Interpretation: crash occurs while unwinding local `std::vector<cv::Mat> Channels` after `cv::split`.
- Likely cause: OpenCV DLL fills/mutates `std::vector<cv::Mat>` through `OutputArrayOfArrays`; allocation can cross module/runtime allocator boundary, then EagleEye module frees through UE `FMemory`/mimalloc.
- Fix:
  - Added `CopyRgbFloatMatToNchw(...)`.
  - Removed `std::vector<cv::Mat> Channels` and `cv::split(...)` from ONNX Runtime preprocessing.
  - Removed same `cv::split(...)` pattern from TensorRT preprocessing.
  - New code copies `CV_32FC3` RGB rows into NCHW input buffer manually.
- Remaining similar pattern:
  - OpenCV DNN backend still uses `std::vector<cv::Mat> Outputs` with `OpenCVDnnNet.forward(...)`.
  - Current crash path is ONNX Runtime, so left OpenCV DNN path unchanged for now.
- Validation:
  - Rebuilt `EagleEyeEditor Win64 Development`.
  - Result: `Succeeded`.

## TensorRT log check

- User reported runtime trying OpenCV DNN instead of TensorRT.
- Checked latest `Saved/Logs/EagleEye.log` and UnrealBuildTool log.
- Runtime log repeatedly says:
  - `Inference backend requested: TensorRT`
  - `TensorRT backend requested but this build was compiled without TensorRT. Falling back.`
  - `LoadYOLO selected backend: OpenCV DNN ... model=.../yolo26l.trt11.plan`
  - `OpenCV DNN cannot load .plan`
- Build rules define `WITH_TENSORRT=1` only when all are present:
  - `TENSORRT_ROOT` or `TENSORRT_PATH`
  - `include`
  - `lib/nvinfer*.lib`
  - `lib/nvinfer_plugin*.lib`
  - `CUDA_PATH/include`
  - `CUDA_PATH/lib/x64/cudart.lib`
- Current environment check:
  - `TENSORRT_ROOT`: not set
  - `TENSORRT_PATH`: not set
  - `CUDA_PATH`: `C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.2`
  - No `nvinfer*.lib` found under `C:\Program Files\NVIDIA GPU Computing Toolkit`
- Conclusion: code is not choosing OpenCV first. Build is compiled without TensorRT, then fallback logic chooses OpenCV DNN for `.plan`, which cannot work.

## TensorRT fix

- Found TensorRT install exists:
  - `C:\Tools\TensorRT-11.0.0.114`
  - Has `include\NvInfer.h`
  - Has `lib\nvinfer_11.lib`
  - Has `lib\nvinfer_plugin_11.lib`
  - Has matching `bin\nvinfer*.dll`
- `TENSORRT_ROOT` is now visible in shell:
  - `C:\Tools\TensorRT-11.0.0.114\`
- Old Unreal makefiles/debug build were stale from earlier `WITH_TENSORRT=0`.
- Cleaned and rebuilt:
  - `EagleEyeEditor Win64 Development`
  - `EagleEyeEditor Win64 DebugGame`
- Verified definitions:
  - `Intermediate\Build\Win64\x64\UnrealEditor\Development\EagleEye\Definitions.EagleEye.h`: `#define WITH_TENSORRT 1`
  - `Intermediate\Build\Win64\x64\UnrealEditor\DebugGame\EagleEye\Definitions.EagleEye.h`: `#define WITH_TENSORRT 1`
- Updated `EagleEye.code-workspace` terminal env to pin:
  - `TENSORRT_ROOT`
  - `TENSORRT_PATH`
  - `CUDA_PATH`
- Hardened backend resolution:
  - `.plan` now maps to TensorRT even if TensorRT is unavailable.
  - If TensorRT is unavailable, code fails once with TensorRT-unavailable error instead of falling through to OpenCV DNN and spamming `.plan` load failures.
- Rebuilt both configs after code change:
  - `EagleEyeEditor Win64 Development`: `Succeeded`
  - `EagleEyeEditor Win64 DebugGame`: `Succeeded`
