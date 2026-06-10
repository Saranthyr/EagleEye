# Detection Model Host Project Settings

- Created: 2026-06-10T11:28:00+03:00
- Task: Add DetectionModelHostActor settings into project Detection settings

## Entries

### 2026-06-10T11:28:56+03:00 - decision: Project settings now own model-host knobs

- Detail: Added bPreloadModelHostOnBeginPlay, MaxActiveModelUsers, MaxQueuedModelFrames,
  FrameSourceFPS/Width/Height, MaxModelUserDistanceToPlayer, bStaggerInitialFrameSourceCapture,
  MaxInitialFrameSourceCaptureDelay to UEagleEyeDetectionSettings and DefaultGame.ini.
  DetectionModelHostActor reads them on BeginPlay and no longer exposes duplicate editable actor
  properties.

### 2026-06-10T11:29:23+03:00 - validation: Build passed

- Detail: Ran Unreal Build.sh EagleEyeEditor Linux Development. Result: Succeeded. Existing MIGraphX EP
  missing hint still logged, unrelated.

## Report Notes

- Main findings:
  - Added DetectionModelHostActor settings to UEagleEyeDetectionSettings so they appear under Project Settings > Detection. DetectionModelHostActor now reads project settings on BeginPlay and uses them to configure CrowVisionSubsystem and CrowDetectionShareSubsystem. Duplicate editable actor Details properties were removed to keep project settings as source of truth. DefaultGame.ini now contains default values for model host and frame source settings. Build passed.
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
