# OpenCV 4.12 TopK support

- Created: 2026-06-11T07:54:35+03:00
- Task: Research whether OpenCV 4.12 DNN ONNX importer supports TopK

## Entries

### 2026-06-11T07:56:20+03:00 - source: Checked OpenCV 4.12 source for TopK

- Detail: OpenCV 4.12.0 onnx_importer.cpp includes parseTopK and the 4.12 layers directory lists
  topk_layer.cpp. parseTopK sets k from constant input and errors if K is non-constant. GitHub
  release page confirms OpenCV 4.12.0 tag/release.

## Report Notes

- Main findings:
  - OpenCV 4.12.0 appears to add DNN ONNX TopK support: source contains ONNXImporter::parseTopK and topk_layer.cpp, while 4.10.0 importer source does not expose TopK parser. For EagleEye's current 4.10 error, upgrading OpenCV can likely remove the immediate TopK layer creation failure, provided the model's K input is constant. ONNX Runtime/DirectML remains safer for YOLO26 because OpenCV DNN may hit later unsupported ops or behavior differences.
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
