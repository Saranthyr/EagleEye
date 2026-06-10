# Auto Inference Fallback

- Created: 2026-06-10T09:40:42+03:00
- Task: Adjust Auto backend/provider logic to try GPU vendor paths first, then ONNX Runtime CPU, then OpenCV DNN CPU

## Entries

### 2026-06-10T09:43:10+03:00 - decision: Auto backend policy implemented as ordered fallback chain

- Detail: Auto now builds candidates from OS/GPU/model: NVIDIA on Windows/Linux gets TensorRT first when
  compiled; tensor-family models then get ONNX Runtime when compiled; OpenCV DNN is final CPU
  fallback. ONNX Runtime provider Auto is DirectML only on Windows AMD, MIGraphX only on Linux AMD,
  otherwise CPU.
- Impact: Matches requested ambiguous/try-fallback behavior without changing explicit backend choices.

### 2026-06-10T09:43:52+03:00 - validation: Validated Auto fallback compile

- Detail: Ran git diff --check for changed C++ files: clean. Ran env-backed Unreal build: succeeded. UBT
  reports MIGraphX EP not found because complete ROCm/MIGraphX runtime is absent, so AMD Auto will
  use ONNX Runtime CPU if ORT is present, then OpenCV DNN CPU if ORT is absent/fails.
- Impact: Confirms requested control flow compiles and current machine resolves broken MIGraphX safely.

## Report Notes

- Main findings:
  - Auto backend/provider logic now follows requested ordered attempts: NVIDIA on Windows/Linux tries TensorRT first; tensor-family models then try ONNX Runtime; ONNX Runtime provider Auto selects DirectML only for Windows AMD, MIGraphX only for Linux AMD, otherwise CPU; Auto retries ONNX Runtime CPU if provider Auto load fails; final Auto fallback is OpenCV DNN CPU. Build validation succeeded.
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
