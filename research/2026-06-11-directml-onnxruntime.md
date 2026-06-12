# DirectML ONNX Runtime setup

- Created: 2026-06-11T07:06:59+03:00
- Task: Get DirectML and ONNX Runtime dependencies for EagleEye project

## Entries

### 2026-06-11T07:10:23+03:00 - command: Installed NuGet inference packages

- Detail: Ran Scripts/setup-inference-deps.ps1 -InstallDirectML -InstallDirectMLRedist with network approval.
  Installed Microsoft.ML.OnnxRuntime.DirectML 1.24.4 to ThirdParty/OnnxRuntimeDirectML and
  Microsoft.AI.DirectML 1.15.2 to ThirdParty/DirectML.

### 2026-06-11T07:10:42+03:00 - finding: Win64 compile not run

- Detail: Checked default Epic paths for UE_5.6/UE_5.5/UE_5.4 Build.bat; none found. Dependency validation
  completed via setup script and file layout checks.
irdParty roots. Re-ran Scripts/setup-inference-deps.ps1 without
  install switches; script passed and regenerated Saved/InferenceDeps.ps1.

## Report Notes

- Main findings:
  - DirectML + ONNX Runtime dependencies installed into project ignored ThirdParty folders. ONNX Runtime DirectML 1.24.4 and DirectML redistributable 1.15.2 validate with existing setup script. Use Saved/InferenceDeps.ps1 when building/running from a shell that does not auto-detect project ThirdParty paths.
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
