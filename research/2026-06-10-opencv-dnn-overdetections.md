# OpenCV DNN Overdetections

- Created: 2026-06-10T10:25:53+03:00
- Task: Investigate whether OpenCV DNN postprocess is causing weird high-confidence detections

## Entries

### 2026-06-10T10:27:45+03:00 - decision: Made six-column OpenCV parsing deterministic and diagnostic

- Detail: Changed OpenCV/ONNX shared six-column parser to evaluate standard [score,class] and swapped
  [class,score] layouts across whole output, choose the layout with more mapped accepted rows, then
  parse all rows consistently. Added decimated OpenCVDNN layout stats log. Set player-resolved
  target validation default false because it is gameplay validation, not pure depth/DNN.

## Report Notes

- Main findings:
  - OpenCV preprocessing matches ONNX Runtime/TensorRT, so current suspicion is six-column output interpretation or model output confidence distribution. Implemented global six-column layout choice and decimated layout diagnostics. Changed player target validation default off. Build and diff check passed; next PIE run should provide OpenCVDNN 6-col layout stats and detection counts for confirmation.
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
