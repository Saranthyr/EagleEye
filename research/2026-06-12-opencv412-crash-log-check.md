# OpenCV 4.12 crash log check

- Created: 2026-06-12T17:42:23+03:00
- Task: Check logs and stack trace for OpenCV DNN crash after OpenCV 4.12 Win64 integration

## Entries

### 2026-06-12T17:44:02+03:00 - finding: Logs show OpenCV 4.12 loads model, CUDA warning unrelated

- Detail: Latest Saved/Logs/EagleEye.log: OpenCV DNN selected, CUDA requested but unavailable/no CUDA-capable
  OpenCV device found, CPU backend configured, model loaded successfully. No TopK failure after 4.12
  update.

### 2026-06-12T17:44:02+03:00 - decision: Avoid std::vector<cv::Mat> output handoff across OpenCV DLL boundary

- Detail: User stack trace crashes at std::vector<cv::Mat> destructor after RunOpenCVDNNInference_BG returns.
  Patched OpenCV DNN forward path to use single-output OpenCVDnnNet.forward() into cv::Mat instead
  of OpenCVDnnNet.forward(Outputs, OutNames). This avoids OpenCV DLL mutating/destructing STL vector
  allocation owned by Unreal module.

### 2026-06-12T17:44:02+03:00 - validation: Build blocked by active Live Coding

- Detail: E:/UE_5.6/Engine/Build/BatchFiles/Build.bat EagleEyeEditor Win64 Development failed before compile:
  'Unable to build while Live Coding is active. Exit the editor and game, or press Ctrl+Alt+F11'.

## Report Notes

- Main findings:
  - Logs show OpenCV 4.12 loads yolo26l via OpenCV DNN; TopK issue is gone. CUDA unavailable warning is expected per user/device and OpenCV falls back to CPU. Provided stack trace points to std::vector<cv::Mat> destruction after RunOpenCVDNNInference_BG returns, consistent with cross-DLL STL/vector output handoff risk. Source patched to use single-output OpenCVDnnNet.forward() into cv::Mat instead of OpenCVDnnNet.forward(std::vector<cv::Mat>&, names). Build not completed because UBT reports Live Coding active; close editor/game or press Ctrl+Alt+F11, then rebuild.
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
