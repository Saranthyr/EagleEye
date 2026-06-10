# Validate OpenCV DNN Weird Detection Link

- Created: 2026-06-10T12:23:56+03:00
- Task: Check whether current weird detection is related to prior OpenCV DNN fixes

## Entries

### 2026-06-10T12:24:50+03:00 - finding: Current logs use ONNX Runtime MIGraphX, not OpenCV DNN

- Detail: EagleEye.log shows Auto backend on Linux/AMD selecting ONNX Runtime provider MIGraphX and loading
  yolo26l.onnx. No OpenCVDNN runtime/parser stats appear in the active PIE log slice.
  SharedVisionFrame detection counts remain high (roughly 43-87 detections/frame), followed by
  scene-depth misses/weak clusters and false-looking person targets with conf=1.00.

## Report Notes

- Main findings:
  - Validated current weird detection is not from OpenCV DNN fixes. Active backend in Saved/Logs/EagleEye.log is ONNX Runtime with MIGraphX on Linux/AMD. Weird behavior comes after ONNX inference: 40-80+ detections/frame, many conf=1.00 person boxes, then scene-depth miss/weak-cluster logs. Next validation is to run PIE after latest build and verify ONNX output shape logs; if absent, editor did not load the updated module. If present, next fix target is ONNX postprocess score/objectness/output parsing.
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

### 2026-06-10T12:25:53+03:00 - question: User clarified concern is shared OpenCV DNN fixes, not OpenCV runtime

- Detail: Need inspect previous OpenCV-related patches for shared parser/postprocess/frame/depth side effects
  that could affect ONNX/MIGraphX detection.

### 2026-06-10T12:26:40+03:00 - finding: OpenCV DNN 6-column parser fix leaked into ONNX path

- Detail: The adaptive 6-column layout chooser was inside shared ProcessWithOpenCV_BG postprocess and chose
  swapped score/class layout for any 6-column tensor, including ONNX Runtime/MIGraphX. Patched it so
  adaptive layout applies only when EffectiveInferenceBackend == OpenCVDNN; ONNX/TensorRT keep the
  previous fixed [x1,y1,x2,y2,score,class_id] interpretation.

### 2026-06-10T12:27:08+03:00 - validation: Editor build passes after scoping parser fix

- Detail: Build.sh EagleEyeEditor Linux Development succeeded. UBT warned ONNX Runtime not found because this
  shell did not source Saved/InferenceDeps.env, so a full ONNX-enabled validation build still needs
  the dependency env.

## Report Notes

- Main findings:
  - Validated user concern: OpenCV DNN fix did affect shared ONNX postprocess. Root cause was adaptive 6-column score/class layout selection being applied to all 6-column outputs in ProcessWithOpenCV_BG, including ONNX Runtime/MIGraphX. Fixed by scoping adaptive swapped-layout selection and OpenCVDNN layout stats to EffectiveInferenceBackend == OpenCVDNN; ONNX/TensorRT now keep fixed standard [x1,y1,x2,y2,score,class_id] handling. ONNX-enabled EagleEyeEditor Linux Development build succeeded after sourcing Saved/InferenceDeps.env.
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
