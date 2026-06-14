# Package task path spaces fix

- Created: 2026-06-14T11:58:31+03:00
- Task: Fix VS Code task failing to launch package-linux-metrics.sh because project path contains spaces

## Entries

### 2026-06-14T11:59:08+03:00 - finding: VS Code shell task split script path at space

- Detail: Package task used type=shell and command=/home/saranthyr/Documents/Unreal
  Projects/EagleEye/Scripts/package-linux-metrics.sh. VS Code launched bash -c with unquoted
  command, so /home/saranthyr/Documents/Unreal was treated as executable.

### 2026-06-14T11:59:18+03:00 - validation: Workspace JSON and script checks passed

- Detail: Ran test -x Scripts/package-linux-metrics.sh, bash -n Scripts/package-linux-metrics.sh, and python3
  -m json.tool LinuxEagleEye.code-workspace. All succeeded. Did not run package build because it
  cooks/archives project.

## Report Notes

- Main findings:
  - Fixed Package EagleEye Linux Metrics task by changing type from shell to process, avoiding bash -c splitting the project path at the space in Unreal Projects.
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

### 2026-06-14T12:00:08+03:00 - validation: Workspace JSON valid after formatting cleanup

- Detail: Re-ran python3 -m json.tool LinuxEagleEye.code-workspace after normalizing task indentation; JSON
  parse succeeded.
