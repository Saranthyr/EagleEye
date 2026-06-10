# OpenCV DNN Fix

- Created: 2026-06-10T08:39:23+03:00
- Task: Check logs regarding OpenCV DNN and implement a fix

## Entries

### 2026-06-10T08:40:29+03:00 - finding: Latest OpenCV DNN run loads model then hits backend/target mismatch

- Detail: Saved/Logs/EagleEye.log lines 1492-1500 select OpenCV DNN for yolo26l.onnx on AMD RX 7900 XTX; line
  1573 logs OpenCV validateBackendAndTarget assertion because DNN_BACKEND_CUDA was paired with a
  non-CUDA target; later frames continue via CPU fallback but first inference pays error/fallback
  cost.
- Impact: Fix should prevent invalid CUDA backend selection on non-CUDA hosts instead of relying on forward-
  time fallback.

### 2026-06-10T08:43:45+03:00 - finding: AMD ONNX Runtime setup missing MIGraphX runtime dependency

- Detail: ldd Binaries/Linux/libonnxruntime_providers_migraphx.so reports libmigraphx_c.so.3 => not found.
  June 9 crash logs show Auto on AMD selected ONNX Runtime MIGraphX, provider append failed on the
  same missing lib, and later inference crashed inside libonnxruntime.so.
- Impact: AMD setup needs dependency preflight/staging, not just provider-header/provider-library detection.

### 2026-06-10T08:49:29+03:00 - validation: Validated OpenCV DNN and AMD provider fixes with Unreal build

- Detail: Ran unsourced build: succeeded, but ONNX Runtime not found. Ran sourced env build after fixes:
  succeeded; UBT now logs ONNX Runtime MIGraphX EP not found because complete MIGraphX runtime deps
  are absent, avoiding broken WITH_ONNXRUNTIME_MIGRAPHX=1. git diff --check passed.
- Impact: Confirms compile and build-rule behavior after fix.

## Report Notes

- Main findings:
  - Implemented OpenCV DNN backend preflight and AMD/MIGraphX dependency gating. OpenCV now avoids selecting CUDA when no CUDA device exists and uses CPU cleanly. AMD ONNX Runtime setup now requires complete MIGraphX runtime libs before compiling/enabling MIGraphX and runtime preflight falls back/errors before provider append when dependencies cannot load. Builds succeeded.
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
