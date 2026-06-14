# Final build MIGraphX model cache

- Created: 2026-06-14T11:41:16+03:00
- Task: Add model caching already implemented for editor to final build script

## Entries

### 2026-06-14T11:42:02+03:00 - finding: Editor MIGraphX cache comes from InferenceDeps env

- Detail: Scripts/setup-inference-deps.sh writes ORT_MIGRAPHX_CACHE_PATH=/Saved/MIGraphXCache/data and
  ORT_MIGRAPHX_MODEL_CACHE_PATH=/Saved/MIGraphXCache/models. Saved/MIGraphXCache/models currently
  has two .mxr files totaling about 138MB. Packaged EagleEye.sh lacked these env vars.

### 2026-06-14T11:42:34+03:00 - artifact: Added final Linux metrics package script with MIGraphX cache staging

- Detail: Created Scripts/package-linux-metrics.sh. Script sources Saved/InferenceDeps.env, runs RunUAT
  BuildCookRun to Builds/Linux-Metrics, copies Saved/MIGraphXCache into Builds/Linux-
  Metrics/EagleEye/Saved/MIGraphXCache, and rewrites EagleEye.sh to export ORT_MIGRAPHX_CACHE_PATH
  and ORT_MIGRAPHX_MODEL_CACHE_PATH to packaged cache paths. Added VS Code build task Package
  EagleEye Linux Metrics (MIGraphX Cache).

### 2026-06-14T11:42:39+03:00 - validation: Validated script syntax, workspace JSON, and packaged cache dry run

- Detail: Ran bash -n Scripts/package-linux-metrics.sh, python3 -m json.tool LinuxEagleEye.code-workspace, and
  SKIP_UAT=1 Scripts/package-linux-metrics.sh. Dry run copied 138MB MIGraphX cache into
  Builds/Linux-Metrics/EagleEye/Saved/MIGraphXCache and patched Builds/Linux-Metrics/EagleEye.sh.
  Full UAT package was not rerun.

## Report Notes

- Main findings:
  - Added Linux final metrics packaging path for MIGraphX model cache. New Scripts/package-linux-metrics.sh sources the same inference env used by editor, runs RunUAT, copies Saved/MIGraphXCache into the archived package, and rewrites EagleEye.sh so packaged runtime exports ORT_MIGRAPHX_CACHE_PATH and ORT_MIGRAPHX_MODEL_CACHE_PATH to package-local Saved/MIGraphXCache paths. Added VS Code task. Validated syntax, workspace JSON, and SKIP_UAT dry-run cache staging; full UAT package not rerun.
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
