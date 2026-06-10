# Runtime detection config fix

- Created: 2026-06-10T14:08:11+03:00
- Task: Implement fix so metrics/detection settings save in packaged metrics build

## Entries

### 2026-06-10T14:08:44+03:00 - decision: Use custom saved detection config

- Detail: User requested custom file. Chosen path: FPaths::ProjectSavedDir()/Config/DetectionSettings.ini,
  writable in packaged builds (Linux-Metrics -> EagleEye/Saved/Config/DetectionSettings.ini). Add
  UEagleEyeDetectionSettings helpers for load/save; update HUD and runtime readers to call helper
  before accessing settings.

### 2026-06-10T14:11:59+03:00 - artifact: Implemented custom runtime detection config file

- Detail: Added UEagleEyeDetectionSettings::GetRuntimeConfigFilename/LoadRuntimeConfig/SaveRuntimeConfig using
  Saved/Config/DetectionSettings.ini. HUD now saves there and logs path. Detection host, component
  metric/perf paths, BT detection debug/metrics, and pathfinding log gates load runtime config
  before GetDefault.

### 2026-06-10T14:12:28+03:00 - validation: EagleEyeEditor Linux Development build succeeded

- Detail: Ran Unreal BuildTool via Build.sh. Result: Succeeded. Note: build warned ONNX Runtime not found,
  existing dependency setup issue unrelated to config write fix.

## Report Notes

- Main findings:
  - Fixed packaged metrics config write by moving detection settings persistence from DefaultGame.ini to writable Saved/Config/DetectionSettings.ini. Runtime consumers load custom file before reading defaults, so saved metric/debug/pathfinding toggles persist across packaged sessions. Validation: EagleEyeEditor Linux Development build succeeded.
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
