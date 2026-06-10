# Detection Settings Ownership Refactor

- Created: 2026-06-10T10:42:33+03:00
- Task: Move shared detection/gameplay settings out of project/component into designated owners

## Entries

### 2026-06-10T10:43:29+03:00 - finding: Settings ownership currently mixed

- Detail: UEagleEyeDetectionSettings contains global model config plus
  MaxActiveSharedDetectionBots/SharedDetectionMaxBotDistance. UMyActorComponent exposes
  capture/model/postprocess/shared model settings. DetectionModelHostActor has active/queue limits
  but was overridden by project setting from previous fix. BT service already owns gameplay
  detection thresholds and shared publish/use flags.

### 2026-06-10T10:45:43+03:00 - decision: Move shared admission/FPS/range to DetectionModelHostActor

- Detail: DetectionModelHostActor now exposes active users, queue size, frame-source FPS/resolution, max
  model-user distance, and capture staggering. It configures CrowVisionSubsystem for frame-source
  settings and CrowDetectionShareSubsystem for distance/active detector admission.

### 2026-06-10T10:48:00+03:00 - decision: Move flock sharing policy to bot class

- Detail: Removed shared detection publish/use/age/reporter-distance properties from BT service. Added per-
  bot/class Detection|Sharing properties to ABotCharacter. BT service now reads share policy from
  controlled bot while keeping gameplay thresholds and action commit rules in service.

### 2026-06-10T10:54:53+03:00 - validation: Build and ownership searches passed

- Detail: Unreal Build.sh EagleEyeEditor Linux Development succeeded. Searches found no
  MaxActiveSharedDetectionBots/SharedDetectionMaxBotDistance/Shared Detection category and no
  editable component Detection|Model/Capture/Shared
  Model/FOV/Preprocess/Postprocess/Performance/Recording categories.

### 2026-06-10T10:55:05+03:00 - validation: Component editable categories restricted

- Detail: Component-specific search for editable Detection|Model/Capture/Shared
  Model/FOV/Preprocess/Postprocess/Performance/Recording UPROPERTY categories returned no matches.
  Remaining host actor Detection|Shared Model properties are intentional ownership surface.

## Report Notes

- Main findings:
  - Refactor completed. Project settings no longer expose shared detector admission knobs. UMyActorComponent editable details are restricted to Logging, Metrics, and Benchmark categories; model/preprocess/postprocess/capture/shared-model values are runtime plumbing driven by project settings, host actor, or bot code. DetectionModelHostActor owns shared-model throughput and frame-source policy: active users, queue size, capture FPS, capture size, max distance, and initial capture staggering. CrowDetectionShareSubsystem now uses host-configured detector admission. BT service keeps gameplay detection/action thresholds and reads flock sharing policy from ABotCharacter. ABotCharacter now owns per-bot/class sharing flags and shared detection age/reporter-distance limits. Build passed.
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
