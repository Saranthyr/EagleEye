# Run EagleEye from VS Code

- Created: 2026-06-11T07:15:32+03:00
- Task: Explain how to run EagleEye from VS Code with installed inference deps

## Entries

## Report Notes

- Main findings:
  - VS Code run configs live in Win11EagleEye.code-workspace and LinuxEagleEye.code-workspace, not .vscode/launch.json. Windows DirectML/ONNX Runtime dependencies are project-local under ThirdParty and auto-detected by EagleEye.Build.cs. Existing Win11 workspace paths still reference C:\Users\Saranthyr\Documents\Unreal Projects\EagleEye and UE_5.6 under Program Files; adjust if running from E:\EagleEye.
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
