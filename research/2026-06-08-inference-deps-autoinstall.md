# Inference dependency auto-install

- Created: 2026-06-08T06:16:06+03:00
- Task: Implement auto-installation/detection for MIGraphX, ONNX Runtime MIGraphX EP, and TensorRT

## Entries

### 2026-06-08T06:18:28+03:00 - artifact: Added inference dependency setup script

- Detail: Created Scripts/setup-inference-deps.sh to detect/install MIGraphX packages, detect/build ONNX
  Runtime with MIGraphX EP, detect/install TensorRT packages from configured NVIDIA repo, and write
  Saved/InferenceDeps.env. Added Docs/InferenceDependencies.md with commands.
- Impact: Dependency setup is explicit and repeatable instead of hidden inside Unreal Build.cs.

### 2026-06-08T06:34:52+03:00 - validation: Validated dependency setup changes

- Detail: Ran bash -n Scripts/setup-inference-deps.sh successfully. Ran ./Scripts/setup-inference-deps.sh
  --skip-tensorrt on AMD machine; it found /opt/rocm/bin/migraphx-driver and
  /home/saranthyr/dev/onnxruntime-install, wrote Saved/InferenceDeps.env, and exited 0. Rebuilt
  EagleEyeEditor with source Saved/InferenceDeps.env; Unreal build succeeded.
- Impact: Script and Build.cs changes are syntactically valid and compatible with current AMD MIGraphX setup.

## Report Notes

- Main findings:
  - Implemented a project setup script rather than hidden package installation inside Unreal Build.cs. Scripts/setup-inference-deps.sh detects MIGraphX/ROCm, TensorRT/CUDA, and ONNX Runtime with MIGraphX EP; optionally installs apt packages with --install-system; optionally clones/builds ORT with --build-onnxruntime; writes Saved/InferenceDeps.env for ONNXRUNTIME_ROOT/ORT_ROOT/ROCM_HOME/LD_LIBRARY_PATH. Build.cs now prints actionable hints when ONNX Runtime or MIGraphX EP is missing, with TensorRT hints gated by EAGLEEYE_VERBOSE_DEPS=1. Docs/InferenceDependencies.md records AMD and NVIDIA commands. Validation: bash -n passed, AMD detection-only run succeeded, and EagleEyeEditor rebuild succeeded after sourcing Saved/InferenceDeps.env.
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

### 2026-06-08T06:36:56+03:00 - validation: Validated non-editor build target dependency staging

- Detail: Patched EagleEye.Build.cs to stage libonnxruntime.so* in addition to libonnxruntime_providers*.so.
  Built non-editor target with source Saved/InferenceDeps.env: Build.sh EagleEye Linux Development
  ... succeeded. Build output copied libonnxruntime.so, libonnxruntime.so.1,
  libonnxruntime.so.1.28.0, libonnxruntime_providers_migraphx.so, and
  libonnxruntime_providers_shared.so.
- Impact: Packaged/non-PIE builds now receive ONNX Runtime shared libraries in the target output; ROCm runtime
  still must be present on host and in LD_LIBRARY_PATH.
