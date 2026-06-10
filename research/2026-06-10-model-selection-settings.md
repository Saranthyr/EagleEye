# Model Selection Settings

- Created: 2026-06-10T09:57:17+03:00
- Task: Replace free-text model path with selectable model names and resolve folders containing platform plans plus ONNX fallback

## Entries

### 2026-06-10T10:01:08+03:00 - decision: Use model-name selection with backend-specific file resolution

- Detail: Added shared model discovery/resolution utility. Settings/HUD list model base names from flat files
  or model folders. Runtime keeps config value as a model selection and resolves to platform .plan
  for TensorRT, .onnx for ONNX Runtime/OpenCV DNN, and .weights only for OpenCV DNN fallback.
- Impact: Supports folder layouts like Models/yolo26s/linux.plan + windows.plan + yolo26s.onnx while keeping
  flat legacy files usable.

### 2026-06-10T10:01:44+03:00 - validation: Validated model selection build

- Detail: git diff --check passed. Env-backed Unreal build succeeded after adding
  UEagleEyeDetectionSettings::GetAvailableModelNames, model discovery utility, HUD model-name list,
  backend-specific model resolution, and recursive model-folder staging.
- Impact: Confirms settings UFUNCTION and new source files compile under UHT/UBT.

## Report Notes

- Main findings:
  - Project settings and HUD now list model base names rather than raw file paths. Runtime resolves the selected name per backend: platform .plan for TensorRT, .onnx for ONNX Runtime/OpenCV DNN, and .weights only for OpenCV DNN. Model folder layouts are supported under Models/<name>/ and Source/EagleEye/Models/<name>/, with recursive staging for packaging. Legacy flat files still work.
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
