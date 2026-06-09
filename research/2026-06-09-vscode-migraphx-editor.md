# VS Code MIGraphX editor launch

- Created: 2026-06-09T13:55:02+03:00
- Task: Explain/start UE editor from VS Code with ONNX Runtime MIGraphX environment

## Entries

### 2026-06-09T13:56:05+03:00 - artifact: Added VS Code MIGraphX editor launcher

- Detail: Created Scripts/launch-editor-migraphx.sh. It sources Saved/InferenceDeps.env, builds EagleEyeEditor
  Linux Development by default, then execs UnrealEditor with the project. Added workspace task
  'Launch EagleEyeEditor Linux Development (MIGraphX)' to EagleEye.code-workspace.
- Impact: VS Code can now start the editor with ONNXRUNTIME_ROOT and LD_LIBRARY_PATH set for MIGraphX.

## Report Notes

- Main findings:
  - Added Scripts/launch-editor-migraphx.sh and EagleEye.code-workspace task 'Launch EagleEyeEditor Linux Development (MIGraphX)'. The launcher sources Saved/InferenceDeps.env, builds EagleEyeEditor Linux Development unless SKIP_BUILD=1, then execs UnrealEditor with the project. Validated script syntax and workspace JSON.
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

### 2026-06-09T13:58:03+03:00 - artifact: Added run/debug launch configs for MIGraphX

- Detail: EagleEye.code-workspace now has task 'EagleEyeEditor Linux Development Build (MIGraphX)' and launch
  configs 'Run EagleEyeEditor (Development MIGraphX)' plus 'Debug EagleEyeEditor (Development
  MIGraphX)'. Both pass ONNXRUNTIME_ROOT, ORT_ROOT, ROCM_HOME, and LD_LIBRARY_PATH to UnrealEditor.
- Impact: VS Code Run and Debug panel can start editor with ONNX Runtime MIGraphX linked.

## Report Notes

- Main findings:
  - Added VS Code task and launch configs for MIGraphX editor workflow. Workspace JSON validates. New preLaunch build task with Saved/InferenceDeps.env succeeded. User can select Run or Debug configs from VS Code Run and Debug panel.
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

### 2026-06-09T14:03:01+03:00 - artifact: Enabled persistent MIGraphX cache env

- Detail: Updated Scripts/setup-inference-deps.sh to write ORT_MIGRAPHX_CACHE_PATH,
  ORT_MIGRAPHX_MODEL_CACHE_PATH, and ORT_MIGRAPHX_EXHAUSTIVE_TUNE=0 into Saved/InferenceDeps.env and
  create Saved/MIGraphXCache/data plus models. Added same env vars to VS Code Run/Debug MIGraphX
  configs. Documented cache behavior.
- Impact: First launch compiles; later launches can reuse MIGraphX .mxr/model-specific cache artifacts and
  avoid repeated compile cost.
