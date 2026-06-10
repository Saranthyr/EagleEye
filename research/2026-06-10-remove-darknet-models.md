# Remove Darknet Model Support

- Created: 2026-06-10T10:03:42+03:00
- Task: Remove .weights and .cfg model support while keeping .names support

## Entries

### 2026-06-10T10:05:18+03:00 - decision: Removed Darknet model format support

- Detail: Removed .weights from model discovery, OpenCV DNN load path, and recursive staging. Removed .cfg
  from settings/component/HUD/config/staging. Kept .names discovery and NamesPathOverride.
- Impact: Runtime model formats are now .onnx and .plan only; .names still supported for class labels.

### 2026-06-10T10:06:26+03:00 - validation: Build and reference sweep passed

- Commands: Unreal Build Tool for EagleEyeEditor Linux Development; rg for weights/Darknet/CfgPath/cfg/readNetFromDarknet/.cfg/.weights under Source, Config, Plugins.
- Result: Build succeeded. Reference sweep returned no remaining Darknet/.cfg/.weights support refs in searched project code/config.
- Note: Build still reports ONNX Runtime MIGraphX EP not found in current environment; unrelated to Darknet removal.
