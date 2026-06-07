# Settings menu for YOLO

- Created: 2026-06-06T21:25:59+03:00
- Task: Add settings menu with options to change YOLO model and parameters

## Entries

### 2026-06-06T21:26:42+03:00 - source: Scanned detection and UI code

- Detail: Checked UEagleEyeDetectionSettings, UMyActorComponent, AMyHUD, AEagleEyeCharacter input,
  DefaultInput.ini. Existing project has developer settings for model paths/thresholds but no
  runtime menu or UMG menu source.

### 2026-06-06T21:27:45+03:00 - decision: Use C++ Canvas settings menu

- Detail: No existing UMG/menu assets were found. AHUD overlay plus character key binds avoids asset creation
  and keeps change source-only. Menu will edit UEagleEyeDetectionSettings, save config, and ask
  UMyActorComponent instances to reload settings/model.

### 2026-06-06T21:31:08+03:00 - artifact: Added source implementation

- Detail: Touched MyHUD, EagleEyeCharacter, and MyActorComponent. HUD owns menu state/options/apply; character
  binds F10/arrows/enter/escape; detection component resolves runtime paths and reloads model from
  UEagleEyeDetectionSettings.

### 2026-06-06T21:31:45+03:00 - validation: Unreal build passed

- Detail: Ran Build.sh EagleEyeEditor Linux Development /home/saranthyr/Documents/Unreal
  Projects/EagleEye/EagleEye.uproject -WaitMutex. Result: Succeeded.

### 2026-06-06T21:32:18+03:00 - validation: Rebuild passed after input blocking

- Detail: Re-ran Build.sh EagleEyeEditor Linux Development after blocking Move/Look while settings menu is
  open. Result: Succeeded.

## Report Notes

- Main findings:
  - Implemented source-only in-game detection settings menu. AMyHUD now draws a F10 overlay that cycles available YOLO model/name files and edits backend, ONNX provider, confidence, NMS, input size, letterbox, and letterbox value. Applying saves UEagleEyeDetectionSettings and reloads active UMyActorComponent instances. AEagleEyeCharacter binds F10/Escape/arrows/Enter and blocks movement/look while the menu is open. UMyActorComponent now has runtime config reload/path resolution and inference locking. Validation: EagleEyeEditor Linux Development build succeeded.
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
