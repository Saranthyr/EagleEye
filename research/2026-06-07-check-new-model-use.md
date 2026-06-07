# Check New Model Use

- Created: 2026-06-07T19:38:37+03:00
- Task: Verify whether runtime uses newly loaded YOLO model

## Entries

### 2026-06-07T19:39:16+03:00 - finding: Logs show yolo26s.plan was selected, released/reloaded, then used for later shared inference.

- Detail: At 16:35:03.560 Detection settings applied: model=yolo26s.plan ... reloaded=13. At 16:35:03.606
  LoadYOLO selected backend TensorRT model=/home/.../yolo26s.plan. At 16:35:03.641 TensorRT engine
  loaded. SharedVisionFrame entries begin immediately after, e.g. 16:35:03.651
  host=DetectionModelHostActor_0 and continue through 16:35:22.

### 2026-06-07T19:39:22+03:00 - finding: Code path prevents stale backend after reload completes.

- Detail: ApplyRuntimeDetectionSettingsFromConfig applies settings, resolves ModelPathOverride, then under
  InferenceMutex releases TensorRT/ONNX/OpenCV net and sets bIsModelLoaded=false. EnsureModelLoaded,
  worker inference, and ProcessSharedVisionFrame all take InferenceMutex and call LoadYOLO before
  ProcessWithOpenCV_BG when bIsModelLoaded=false.

## Report Notes

- Main findings:
  - Yes: latest runtime apply changed model to yolo26s.plan, reloaded 13 detection components, LoadYOLO loaded TensorRT engine from yolo26s.plan, and subsequent SharedVisionFrame inference ran through that host. Because reload and inference share InferenceMutex and reload resets bIsModelLoaded=false, next inference loads the new path before processing. Caveat: frames already in flight before the reload lock may complete with the old model, but post-reload frames use the new one.
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
