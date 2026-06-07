# YOLO26 plan files

- Created: 2026-06-07T07:02:33+03:00
- Task: Make .plan files for yolo26l and yolo26s

## Entries

### 2026-06-07T07:02:44+03:00 - finding: Found YOLO26 ONNX inputs and existing plan layout

- Detail: Repo root has yolo26l.onnx and yolo26s.onnx. Existing yolo26x.plan is in Source/EagleEye and
  Binaries/Linux/Models.

### 2026-06-07T07:03:15+03:00 - command: Initial TensorRT probe failed in sandbox

- Detail: Command: trtexec --version. Output: TensorRT v101501 then Cuda failure: no CUDA-capable device is
  detected. Need rerun TensorRT build outside sandbox for GPU access.

### 2026-06-07T07:03:26+03:00 - finding: YOLO26 ONNX files use static 640 input and NMS-like output

- Detail: yolo26s.onnx, yolo26l.onnx, and existing yolo26x.onnx all have input images [1,3,640,640], output
  output0 [1,300,6], opset 17. Suitable for static TensorRT plan generation.

### 2026-06-07T07:06:32+03:00 - artifact: Built Source/EagleEye/yolo26s.plan

- Detail: Command: trtexec --onnx=yolo26s.onnx --saveEngine=Source/EagleEye/yolo26s.plan --fp16
  --skipInference. TensorRT 10.15.1 on RTX 4070 Laptop GPU. Engine generation completed in 164.742
  sec; engine size 21.5319 MiB; PASSED.

### 2026-06-07T07:10:54+03:00 - artifact: Built Source/EagleEye/yolo26l.plan

- Detail: Command: trtexec --onnx=yolo26l.onnx --saveEngine=Source/EagleEye/yolo26l.plan --fp16
  --skipInference. TensorRT 10.15.1 on RTX 4070 Laptop GPU. Engine generation completed in 188.11
  sec; engine size 51.2058 MiB; PASSED.

### 2026-06-07T07:11:21+03:00 - validation: Verified generated plan artifacts

- Detail: Source/EagleEye/yolo26s.plan and Binaries/Linux/Models/yolo26s.plan are 22M with matching SHA256
  1192c1e1b43890960e145a9b5d8338bdc3bddbfb85eab6f7c7f59db52f716f06. Source/EagleEye/yolo26l.plan and
  Binaries/Linux/Models/yolo26l.plan are 52M with matching SHA256
  fcccfe8eeaa14d99b91bf24393aaf3d227066d4916285eed47afc7ca54eaf884.

## Report Notes

- Main findings:
  - Generated FP16 TensorRT .plan files for yolo26s and yolo26l from static 640 ONNX inputs. Stored canonical copies in Source/EagleEye for build staging and copied runtime copies into Binaries/Linux/Models. TensorRT build passed for both engines on RTX 4070 Laptop GPU with TensorRT 10.15.1.
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
