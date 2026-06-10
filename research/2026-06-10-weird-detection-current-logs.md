# Current Weird Detection Log Check

- Created: 2026-06-10T11:40:35+03:00
- Task: Check current logs for weird detection behavior

## Entries

### 2026-06-10T11:40:47+03:00 - finding: Current weird detection is ONNX Runtime MIGraphX over-detection

- Detail: Logs show backend Auto on AMD chose ONNX Runtime MIGraphX and loaded yolo26l.onnx. Shared frames
  report 30-86 detections/frame, all person[0] confidence 1.00, with huge boxes and jumping scene-
  depth targets. This is not current OpenCV DNN path.

### 2026-06-10T11:42:39+03:00 - decision: Harden ONNX output selection

- Detail: Current code selected largest float tensor from ONNX Runtime. If model has aux/proto outputs, that
  can parse non-detection data as boxes, causing many high-confidence false detections. Patched
  selection to require detection-like two-dimensional tensor shape and log output shapes/selected
  shape.

### 2026-06-10T11:43:44+03:00 - validation: Build passed after ONNX output selection hardening

- Detail: Ran Unreal Build.sh EagleEyeEditor Linux Development with Saved/InferenceDeps.env. Result:
  Succeeded.

## Report Notes

- Main findings:
  - Checked current EagleEye.log. Runtime is ONNX Runtime MIGraphX, not OpenCV DNN. Logs show yolo26l.onnx loaded with MIGraphX and shared inference returns 30-86 detections per frame, many person[0] with confidence 1.00 and large boxes. BT then resolves those boxes into scene-depth targets and shares them across flock, making movement/targets weird. Patched ONNX output handling so it logs all output shapes and selects a detection-like two-dimensional output instead of the largest float tensor; this prevents auxiliary/proto tensors from being parsed as boxes. Build passed. Next PIE run should be checked for ONNX Runtime output[...] and selected output logs plus detection counts.
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
