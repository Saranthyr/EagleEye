# Host-only shared detection implementation

- Created: 2026-06-09T12:57:45+03:00
- Task: Implement bots using only detection model host actor with host-controlled max users and queue/in-flight gating

## Entries

### 2026-06-09T13:04:28+03:00 - finding: Existing architecture already had host actor and subsystem fallback

- Detail: AEagleEyeGameMode spawns ADetectionModelHostActor; UCrowVisionSubsystem also ensures a host actor on
  world begin play. Bot constructor already set SetUseSharedVisionModel(true).

### 2026-06-09T13:07:21+03:00 - validation: Unreal C++ build succeeded

- Detail: Ran E:\\UE_5.6\\Engine\\Build\\BatchFiles\\Build.bat EagleEyeEditor Win64 Development
  -Project=E:\\EagleEye\\EagleEye.uproject -WaitMutex. Result: Succeeded.

### 2026-06-09T13:07:21+03:00 - artifact: Implemented host-only shared detection queue gating

- Detail: Changed DetectionModelHostActor to expose MaxActiveModelUsers and MaxQueuedModelFrames; host
  configures UCrowVisionSubsystem; subsystem now uses weak model-host pointer, bounded PendingFrames
  FIFO, InFlightFrame, per-requester active gate, and serial-based delivery.

## Report Notes

- Main findings:
  - Implemented host-only shared detection: bots reassert shared mode at BeginPlay; non-host shared components refuse local model loading; DetectionModelHostActor owns preload plus max active users/queued frame limits; UCrowVisionSubsystem enforces bounded FIFO, one active request per requester, weak host/requester references, serial delivery, and shutdown generation checks. Build succeeded.
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
