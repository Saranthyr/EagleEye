# Windows TensorRT build verification

## Scope
- Verify whether current EagleEye build works with TensorRT on Windows.

## Sources checked
- `Source/EagleEye/EagleEye.Build.cs`
- `Source/EagleEye/Private/MyActorComponent.cpp`
- `Source/EagleEye/Public/MyActorComponent.h`
- `Config/DefaultGame.ini`
- Local environment variables: `TENSORRT_ROOT`, `TENSORRT_PATH`, `CUDA_PATH`, `CUDA_HOME`
- `C:\Tools\TensorRT-11.0.0.114`
- Local Unreal build attempt with UE 5.6 `Build.bat`

## Findings
- Windows TensorRT is compile-time gated by `WITH_TENSORRT`.
- `WITH_TENSORRT=1` only if all exist:
  - `%TENSORRT_ROOT%\include`
  - `%TENSORRT_ROOT%\lib\nvinfer.lib`
  - `%TENSORRT_ROOT%\lib\nvinfer_plugin.lib`
  - `%CUDA_PATH%\include`
  - `%CUDA_PATH%\lib\x64\cudart.lib`
- Current env has `CUDA_PATH=C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.2`.
- Current env has no `TENSORRT_ROOT` or `TENSORRT_PATH`.
- Common NVIDIA install roots checked did not show `nvinfer.lib` or `NvInfer.h`.
- Therefore current Windows build compiles `WITH_TENSORRT=0`.
- `C:\Tools\TensorRT-11.0.0.114` exists and contains:
  - `include\NvInfer.h`
  - `lib\nvinfer_11.lib`
  - `lib\nvinfer_plugin_11.lib`
  - `bin\nvinfer_11.dll`
  - `bin\nvinfer_plugin_11.dll`
- Current Windows build rules expect unversioned TensorRT import libraries:
  - `lib\nvinfer.lib`
  - `lib\nvinfer_plugin.lib`
- TensorRT 11 install therefore is not detected by current `EagleEye.Build.cs`.
- UBT run with `TENSORRT_ROOT=C:\Tools\TensorRT-11.0.0.114` still left Win64 generated definitions at `WITH_TENSORRT 0`.
- Implemented Windows lookup that tries versioned import libraries first (`nvinfer_<major>.lib`, `nvinfer_plugin_<major>.lib`) and falls back to unversioned names.
- Added TensorRT/CUDA runtime DLL copy to `$(BinaryOutputDir)`.
- Added CUDA runtime staging/copy from both `CUDA\bin` and `CUDA\bin\x64`; CUDA 13.2 stores `cudart64_13.dll` under `bin\x64`.
- `Config/DefaultGame.ini` uses `InferenceBackend=Auto` and `ModelPathOverride=yolo26s.trt11.plan`.
- With `WITH_TENSORRT=0`, `.plan` does not select TensorRT; backend falls through to OpenCV DNN path. OpenCV DNN cannot load `.plan`, but code can map to matching `.onnx`; `Source/EagleEye/yolo26s.trt11.onnx` does not exist. Matching `yolo26s.onnx` exists but is not selected by this fallback because base name differs.

## Build attempt
- Command:
  - `C:\Program Files\Epic Games\UE_5.6\Engine\Build\BatchFiles\Build.bat EagleEyeEditor Win64 Development -Project=...\EagleEye.uproject -WaitMutex -NoHotReload`
- First sandboxed run failed before compile due `UnauthorizedAccessException` writing UBT log under `AppData`.
- Escalated run reached UBT, then failed because Live Coding is active:
  - `Unable to build while Live Coding is active. Exit the editor and game, or press Ctrl+Alt+F11 if iterating on code in the editor or game`
- Retry with `-NoLiveCoding` still failed with same Live Coding lock.
- After editor closed, reran:
  - `C:\Program Files\Epic Games\UE_5.6\Engine\Build\BatchFiles\Build.bat EagleEyeEditor Win64 Development -Project=...\EagleEye.uproject -WaitMutex -NoHotReload`
- Result succeeded:
  - `Rebuild All: 1 succeeded, 0 failed, 0 skipped`
  - Output copied `onnxruntime.dll`, `onnxruntime_providers_shared.dll`, and `DirectML.dll`.
  - No TensorRT DLLs copied, consistent with `WITH_TENSORRT=0`.
- After implementation, rebuilt with `TENSORRT_ROOT=C:\Tools\TensorRT-11.0.0.114`.
- Result succeeded:
  - `WITH_TENSORRT=1` in `Intermediate\Build\Win64\x64\UnrealEditor\Development\EagleEye\Definitions.EagleEye.h`
  - `WITH_TENSORRT=1` in `Intermediate\Build\Win64\EagleEyeEditor\Development\EagleEyeEditor.uhtmanifest`
  - 13 `nvinfer*.dll` files copied to `Binaries\Win64`
  - `cudart64_13.dll` copied to `Binaries\Win64`

## Decision
- Current local Windows build compiles with TensorRT enabled when `TENSORRT_ROOT=C:\Tools\TensorRT-11.0.0.114`.
- Versioned TensorRT 11 import libraries now work; unversioned names remain fallback.
