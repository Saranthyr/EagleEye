# Detection access violation verification

- Created: 2026-06-09T13:30:21+03:00
- Task: Verify remaining memory access violation risks in bot shared detection subsystem

## Entries

### 2026-06-09T13:30:48+03:00 - finding: ClassNames data race found

- Detail: get_class_names() mutates std::vector ClassNames without InferenceMutex in
  ApplyRuntimeDetectionSettingsFromConfig() and EnsureModelLoaded(); ProcessWithOpenCV_BG reads
  ClassNames while worker holds InferenceMutex. Runtime reload or manual load during inference can
  race and cause invalid vector access.

### 2026-06-09T13:31:47+03:00 - artifact: Patched remaining AV/data-race hazards

- Detail: Moved get_class_names() calls under InferenceMutex during runtime reload/manual model load; removed
  unlocked bIsModelLoaded early read from EnsureModelLoaded(); made bIsModelLoaded atomic; added
  bitmap size/data guard before cv::Mat wraps Bitmap.GetData().

### 2026-06-09T13:32:17+03:00 - validation: Unreal build succeeded after safety patches

- Detail: Ran E:\\UE_5.6\\Engine\\Build\\BatchFiles\\Build.bat EagleEyeEditor Win64 Development
  -Project=E:\\EagleEye\\EagleEye.uproject -WaitMutex. Result: Succeeded.

## Report Notes

- Main findings:
  - Verified shared detection memory safety. Found and fixed concrete ClassNames data race between runtime reload/manual load and worker postprocess, made bIsModelLoaded atomic for cross-thread reads, and added invalid bitmap guard before cv::Mat wraps raw pixels. Remaining lifetime path uses weak host/requester references and StopWorker waits during host EndPlay/world cleanup. Build succeeded.
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
