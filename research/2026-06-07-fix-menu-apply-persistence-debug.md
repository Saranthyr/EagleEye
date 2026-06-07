# Fix menu apply persistence and debug toggles

- Created: 2026-06-07T19:25:07+03:00
- Task: Make detection settings menu persist model/debug settings and ensure debug logging disables live

## Entries

### 2026-06-07T19:25:23+03:00 - finding: Debug flag OR prevents disable

- Detail: BTServ_UpdateCrowTargetDetection::PrintCrowDetectionDebug logs when bLogDebug ||
  Settings->bEnableDetectionDebugLogs. Any BT service instance with bLogDebug=true keeps
  CrowDetectionDebug alive even when menu/global setting is false.

### 2026-06-07T19:26:05+03:00 - artifact: Patched debug gate and config persistence

- Detail: BTServ_UpdateCrowTargetDetection now requires bEnableDetectionDebugLogs true before logging
  CrowDetectionDebug, so global setting false overrides BT-service bLogDebug. MyHUD
  ApplyPendingDetectionSettings now SaveConfig(CPF_Config, DefaultConfigFilename),
  TryUpdateDefaultConfigFile(), flushes GConfig, and logs applied flags/model.

### 2026-06-07T19:26:31+03:00 - validation: Build passed

- Detail: Ran Build.sh EagleEyeEditor Linux Development. Result: Succeeded.

## Report Notes

- Main findings:
  - Implemented fixes: CrowDetectionDebug now respects bEnableDetectionDebugLogs as a global hard gate, so turning debug off in the menu stops local bLogDebug from forcing logs. Detection settings menu apply now force-saves to the default config filename, calls TryUpdateDefaultConfigFile, flushes GConfig, and logs applied model/debug/perf/metric/path flags. Build succeeded.
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
