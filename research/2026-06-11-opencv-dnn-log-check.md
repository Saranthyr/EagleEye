# OpenCV DNN log check

- Created: 2026-06-11T07:53:04+03:00
- Task: Check Unreal logs for OpenCV DNN issues

## Entries

### 2026-06-11T07:53:19+03:00 - finding: OpenCV DNN fails on TopK in yolo26l.onnx

- Detail: Latest Saved/Logs/EagleEye.log shows OpenCV DNN selected at 2026-06-11 04:52:34+ for model
  E:/EagleEye/Source/EagleEye/yolo26l.onnx. OpenCV 4.10 importer fails: Can't create layer
  'onnx_node!/model.23/TopK' of type 'TopK'.

## Report Notes

- Main findings:
  - Latest log shows DirectML/ONNX Runtime works, but later project config selected OpenCV DNN with yolo26l.onnx. OpenCV 4.10 fails importing the model because model contains TopK node, unsupported by this OpenCV DNN path. Current Config/DefaultGame.ini forces InferenceBackend=OpenCVDNN and OnnxRuntimeExecutionProvider=CPU, causing repeated OpenCV DNN failures. Recommendation: use ONNX Runtime/DirectML or export a model variant without TopK for OpenCV DNN.
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
