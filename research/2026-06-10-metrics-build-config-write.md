# Metrics build config write investigation

- Created: 2026-06-10T14:05:30+03:00
- Task: Check gameplay logs of metrics build; determine why config was not written

## Entries

### 2026-06-10T14:06:15+03:00 - source: Initial metrics/config evidence

- Detail: Project Config/DefaultGame.ini contains UEagleEyeDetectionSettings with ModelPathOverride=yolo26l
  and bEnableDetectionMetricLogs=True. Only HUD config save logs found were June 9 with
  config=updated. Builds/Linux-Metrics manifests stage EagleEye/Config/DefaultGame.ini as UFS and
  Engine/Config/StagedBuild_EagleEye.ini as NonUFS.

### 2026-06-10T14:06:48+03:00 - finding: Packaged metrics build config save failed

- Detail: Builds/Linux-Metrics/EagleEye/Saved/Logs/EagleEye.log line 3420: Detection settings applied
  model=yolo26s debug=true perf=true metrics=true pathDecision=true pathObject=true config=failed
  reloaded=14. Earlier backup log line 4457 also config=failed; line 13597 toggled metrics=false and
  also config=failed.

### 2026-06-10T14:06:48+03:00 - finding: Packaged run did not create writable detection config

- Detail: find Builds/Linux-Metrics/EagleEye/Saved shows only Saved/Config/Linux/GameUserSettings.ini; no
  Saved/Config/Linux/Game.ini. StagedBuild_EagleEye.ini is 3 bytes/BOM-only; project DefaultGame.ini
  is inside UFS/pak per Manifest_UFSFiles_Linux.txt.

### 2026-06-10T14:07:08+03:00 - finding: DefaultGame.ini is read-only in packaged build

- Detail: Builds/Linux-Metrics/EagleEye/Saved/Logs/EagleEye.log line 3389 warns: Ini File
  '../../../EagleEye/Config/DefaultGame.ini' is read-only and cannot be written to. UnrealPak
  listing shows EagleEye/Config/DefaultGame.ini inside EagleEye-Linux.pak, and loose Builds/Linux-
  Metrics/EagleEye/Config is empty.

## Report Notes

- Main findings:
  - Metrics build did not write config because ApplyPendingDetectionSettings() saves UEagleEyeDetectionSettings to Settings->GetDefaultConfigFilename()/TryUpdateDefaultConfigFile(), which targets ../../../EagleEye/Config/DefaultGame.ini in packaged runtime. In Linux-Metrics that file is staged inside EagleEye-Linux.pak and treated read-only. Gameplay logs show config=failed and LogObj read-only warnings. Runtime metrics CSV writing works; persistent detection config writing does not because it targets DefaultGame.ini instead of a writable user config path.
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
