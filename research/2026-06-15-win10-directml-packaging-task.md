# Win10 DirectML Packaging Task

Task: Make task for packaging Win10 build with ONNX Runtime and DirectML.

### 2026-06-15T10:29:23+03:00 - source: workspace and dependency layout

- Evidence: `Win10EagleEye.code-workspace`, `Scripts/setup-inference-deps.ps1`, `Source/EagleEye/EagleEye.Build.cs`, `ThirdParty/OnnxRuntimeDirectML`, `ThirdParty/DirectML`.
- Detail: Win10 workspace has inline VS Code tasks but no packaging task. ONNX Runtime DirectML and DirectML redistributable already exist under `ThirdParty`. Build.cs can discover project-local deps and stage `onnxruntime*.dll` plus `DirectML.dll`.
- Impact: New task should wrap UAT packaging and validate DLL staging instead of changing C++ linkage.

### 2026-06-15T10:31:00+03:00 - artifact: Win10 DirectML packaging script and task

- Evidence: `Scripts/package-win10-directml.ps1`, `Win10EagleEye.code-workspace`.
- Detail: Added PowerShell packaging script that validates ONNX Runtime DirectML and DirectML layouts, sets `ONNXRUNTIME_ROOT`/`ORT_ROOT`/`DIRECTML_ROOT`, runs `RunUAT.bat BuildCookRun` for Win64 Shipping, then verifies/copies `onnxruntime.dll`, `onnxruntime_providers_shared.dll`, and `DirectML.dll` into packaged runtime dirs. Added VS Code task `Package EagleEye Win10 DirectML`.
- Impact: User can run one workspace task to build/cook/package with DirectML runtime included.

### 2026-06-15T10:32:00+03:00 - validation: syntax and dependency checks

- Evidence: PowerShell parser check on `Scripts/package-win10-directml.ps1`; `Scripts/setup-inference-deps.ps1 -OnnxRuntimeRoot ThirdParty\OnnxRuntimeDirectML -DirectMLRoot ThirdParty\DirectML -EnvFile Saved\InferenceDeps.ps1`.
- Detail: Script syntax parsed OK. Dependency validator reported ONNX Runtime DirectML ready. No existing `Builds\Win10-DirectML` archive was present, so staging-only package validation was not run.
- Impact: Task is wired and dependency layout is valid; full UAT package still needs run to validate complete archive.

## Report Notes

- Main findings: Project already has ONNX Runtime DirectML and DirectML redistributable under `ThirdParty`; Build.cs supports them. Missing part was user-facing packaging task.
- Evidence to cite: `Source/EagleEye/EagleEye.Build.cs`, `Scripts/setup-inference-deps.ps1`, `Win10EagleEye.code-workspace`, `Scripts/package-win10-directml.ps1`.
- Decisions and rationale: Add script wrapper rather than duplicate long UAT command in task; script can validate deps and copy/check runtime DLLs after archive.
- Validation performed: PowerShell syntax parse; dependency setup validation.
- Unresolved questions: Full package run not performed.
- Suggested report angle: Win10 DirectML packaging now one command/task, with explicit DLL verification.

### 2026-06-15T10:36:35+03:00 - decision: switch package task to Development metrics build

- Evidence: User requested development build for metrics, not Shipping.
- Detail: Changed `Scripts/package-win10-directml.ps1` default `Config` from `Shipping` to `Development`, default archive root from `Builds\Win10-DirectML` to `Builds\Win10-DirectML-Metrics`, and VS Code task label/args to `Package EagleEye Win10 DirectML Metrics`.
- Impact: Workspace task now creates Development Win64 package suitable for metrics collection while still staging ONNX Runtime DirectML runtime DLLs.

### 2026-06-15T10:37:00+03:00 - validation: syntax check after metrics change

- Evidence: PowerShell parser check on `Scripts\package-win10-directml.ps1`.
- Detail: Script syntax parsed OK after changing defaults.
- Impact: Task wiring change is syntactically valid; full UAT package still not run.
