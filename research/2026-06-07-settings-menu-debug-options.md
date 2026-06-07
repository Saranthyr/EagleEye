# Settings menu debug options

- Created: 2026-06-07T18:18:13+03:00
- Task: Extend detection settings menu with debug/logging controls

## Entries

### 2026-06-07T18:19:17+03:00 - artifact: Added debug/logging menu toggles

- Detail: Added five pending bools and menu rows for bEnablePathfindingDecisionLogs,
  bEnablePathfindingObjectLogs, bEnableDetectionDebugLogs, bEnableDetectionPerformanceLogs, and
  bEnableDetectionMetricLogs. Apply writes them to UEagleEyeDetectionSettings and saves config.

## Report Notes

- Main findings:
  - Extended AMyHUD detection settings menu with five debug/logging toggles backed by UEagleEyeDetectionSettings: pathfinding decision logs, pathfinding object logs, detection debug logs, detection performance logs, and detection metric logs. Apply writes values and saves config. Updated menu copy to generic IMC action names. Validation: EagleEyeEditor Linux Development build succeeded.
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
