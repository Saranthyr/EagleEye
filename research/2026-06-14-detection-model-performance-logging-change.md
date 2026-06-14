# Detection model performance logging change

- Created: 2026-06-14T10:58:16+03:00
- Task: Modify model performance CSV logging filename and columns for success/correctness/model used

## Entries

### 2026-06-14T11:02:32+03:00 - finding: Implementation approach selected

- Detail: Local worker remains CSV source. Expected-in-FOV is sampled on game thread when frame is
  captured/enqueued; worker later combines it with HasPersonDetection(detections) to write
  detection_status and detection_correctness without calling Unreal actor/camera APIs from worker
  thread.

### 2026-06-14T11:03:26+03:00 - validation: EagleEyeEditor build succeeded

- Detail: Ran Build.sh EagleEyeEditor Linux Development EagleEye.uproject -WaitMutex. Build completed
  successfully; ONNX Runtime dependency warning was present but not fatal for this editor build.

## Report Notes

- Main findings:
  - Implemented local detection model performance CSV changes: default filename is backend_provider_os.csv; CSV columns include execution_provider, os, detection_model, detection_status, and detection_correctness. Correctness uses game-thread expected-in-FOV snapshot plus worker-side person-detection result. Shared vision CSV remains unchanged because it does not append frame timing CSV rows today.
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

### 2026-06-14T11:05:33+03:00 - finding: Shared vision added to CSV recording

- Detail: Extended CrowVisionSubsystem::SubmitFrame to carry requester expected-in-FOV flags and added
  UMyActorComponent::RecordDetectionModelFrameTiming so shared model host writes the same
  performance CSV using host backend/provider/model and requester correctness context.

### 2026-06-14T11:05:58+03:00 - validation: EagleEyeEditor build succeeded after shared path changes

- Detail: Ran Build.sh EagleEyeEditor Linux Development EagleEye.uproject -WaitMutex. Build succeeded.
  Existing ONNX Runtime dependency warning remained non-fatal.

## Report Notes

- Main findings:
  - Implemented detection model performance CSV changes for local and shared model paths: default filename is backend_provider_os.csv; rows include backend/provider/os/model/timing/detection_status/detection_correctness. Status is person detected success/fail; correctness compares status to expected player-in-FOV sampled on game thread.
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
