# OpenCV DNN Not Working

- Created: 2026-06-10T10:11:02+03:00
- Task: Investigate user report that OpenCV DNN may not work and implement fix

## Entries

### 2026-06-10T10:11:23+03:00 - finding: OpenCV DNN loads but returns zero detections

- Detail: Saved/Logs/EagleEye.log shows OpenCV DNN model loaded for yolo26s/yolo26l, CPU backend configured,
  then warning: OpenCVDNN produced 0 final boxes (raw=0, shape=[300,6], value_range includes
  coordinate-like values). Old backups show reshape errors before CPU backend fix; current log no
  forward exception.

### 2026-06-10T10:12:43+03:00 - decision: Accept both 6-column detector layouts

- Detail: Patched Attrs==6 postprocess to parse both [x1,y1,x2,y2,score,class_id] and
  [x1,y1,x2,y2,class_id,score]. Heuristic uses integer class-id detection and confidence
  normalization. This targets current log shape [300,6] with raw=0 despite coordinate-like output
  values.

## Report Notes

- Main findings:
  - OpenCV DNN did load and forward in current logs, but postprocess produced raw=0 boxes for [300,6] output. Implemented layout-tolerant six-column parsing so both score/class and class/score exporters work. Build and diff check passed; live gameplay validation still needed to confirm boxes appear.
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

### 2026-06-10T10:15:48+03:00 - validation: Latest log shows OpenCV DNN working after reload

- Detail: Saved/Logs/EagleEye.log: before hot reload there are OpenCVDNN produced 0 final boxes warnings at
  lines 1636 and 3180. After hot reload around line 5841 and model load at 5936-5944, tail shows
  SharedVisionFrame detections=20..30 and no later OpenCVDNN produced 0 final boxes warning in
  searched output.
