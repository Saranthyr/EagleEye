# Linux-Metrics ONNX Runtime missing

- Created: 2026-06-14T11:15:12+03:00
- Task: Check logs of Linux-Metrics in Builds. Why ONNX Runtime is missing?

## Entries

### 2026-06-14T11:16:39+03:00 - source: Checked Linux-Metrics packaged logs and manifests

- Detail: Builds/Linux-Metrics/EagleEye/Saved/Logs/EagleEye.log shows ONNX Runtime requested with MIGraphX,
  provider configured, then repeated model load failure: 'ONNX Runtime model load failed: Failed to
  call function'. Builds/Linux-Metrics/Manifest_NonUFSFiles_Linux.txt lists staged
  libonnxruntime.so, libonnxruntime.so.1, libonnxruntime.so.1.28.0,
  libonnxruntime_providers_migraphx.so, libonnxruntime_providers_shared.so, and libmigraphx*.so
  files.

### 2026-06-14T11:16:45+03:00 - finding: ONNX Runtime itself is staged; provider dependency lookup is broken

- Detail: ldd Builds/Linux-Metrics/EagleEye/Binaries/Linux/EagleEye resolves libonnxruntime.so.1 from the
  package. ldd Builds/Linux-Metrics/EagleEye/Binaries/Linux/libonnxruntime_providers_migraphx.so
  reports 'libmigraphx_c.so.3 => not found' unless LD_LIBRARY_PATH includes Builds/Linux-
  Metrics/EagleEye/Binaries/Linux. readelf shows provider has no RUNPATH/RPATH, while
  libmigraphx_c.so.3 has RUNPATH  for its own deps.

### 2026-06-14T11:16:50+03:00 - finding: Launcher does not expose packaged binary dir to dynamic linker

- Detail: Builds/Linux-Metrics/EagleEye.sh only chmods and executes EagleEye/Binaries/Linux/EagleEye. Packaged
  log line 595 shows Unreal updated LD_LIBRARY_PATH only to
  ../../../Engine/Binaries/ThirdParty/Vulkan/Linux. It does not include EagleEye/Binaries/Linux,
  where libmigraphx_c.so.3 and provider libs are staged.

## Report Notes

- Main findings:
  - Linux-Metrics package stages ONNX Runtime and MIGraphX files, but runtime load path does not include the staged binary directory. Packaged log shows MIGraphX requested/configured, then session creation fails with generic 'Failed to call function'. ldd confirms provider cannot find libmigraphx_c.so.3 without LD_LIBRARY_PATH pointing at EagleEye/Binaries/Linux. Recommended fix: set LD_LIBRARY_PATH in packaged launcher or add RUNPATH/RPATH  to libonnxruntime_providers_migraphx.so/package provider deps.
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

### 2026-06-14T11:20:55+03:00 - decision: Implement source fix in Build.cs plus runtime provider preload

- Detail: Package already had core ORT files, but provider dependency search was fragile. Fix will stage/copy
  Linux ONNX Runtime provider files and MIGraphX runtime files into binary output, add runtime
  library paths for source dirs, and preload ORT provider/shared libs from packaged binary dir
  before session creation.

### 2026-06-14T11:27:54+03:00 - finding: Missing dependency was ROCm/MIGraphX companion libs, not ONNX Runtime core

- Detail: ldd Binaries/Linux/libmigraphx_gpu.so.2015000 initially reported libhipblaslt.so.1, then librocm-
  core.so.1 and librocroller.so.1 as not found. Added packaging patterns for these ROCm companion
  libs plus related
  hiprtc/MIOpen/rocblas/comgr/roctx/hipblas/hipsparse/rocfft/rocsolver/rocsparse/rccl libs.

### 2026-06-14T11:28:03+03:00 - validation: Built and verified Linux runtime dependency closure

- Detail: Ran Build.sh EagleEye Linux Development with Saved/InferenceDeps.env sourced; build succeeded.
  Verified Binaries/Linux and Builds/Linux-Metrics/EagleEye/Binaries/Linux contain 50 ROCm companion
  libs. ldd on libmigraphx_gpu.so.2015000 now resolves hiprtc, MIOpen, rocblas, hipblaslt,
  migraphx_device, migraphx, amd_comgr, rocm-core, roctx64, and rocroller from package/local binary
  dir. ldd on libonnxruntime_providers_migraphx.so resolves migraphx deps when LD_LIBRARY_PATH
  points at binary dir.

## Report Notes

- Main findings:
  - Implemented source packaging fix and patched current Linux-Metrics artifact. EagleEye.Build.cs now stages/copies ONNX Runtime provider files, MIGraphX runtime files, and required ROCm companion libs with duplicate target suppression. MyActorComponent preloads MIGraphX GPU/provider shared libraries before ORT session creation so packaged builds do not rely on launcher LD_LIBRARY_PATH. Build validation succeeded; ldd checks show no missing MIGraphX GPU/provider deps after package copy.
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

### 2026-06-14T11:28:43+03:00 - validation: Final build check passed

- Detail: After formatting cleanup, reran Build.sh EagleEye Linux Development with Saved/InferenceDeps.env
  sourced. UnrealBuildTool result: Succeeded; target up to date.

## Report Notes

- Main findings:
  - Implemented and validated Linux ONNX Runtime/MIGraphX packaging fix. Build.cs stages/copies ONNX Runtime provider libs, MIGraphX libs, and ROCm companion libs with duplicate target suppression. Runtime now preloads MIGraphX GPU/provider libs. Current Linux-Metrics artifact was patched with rebuilt executable and 50 ROCm companion libs. Final Unreal build succeeded; ldd checks show package-local resolution for MIGraphX GPU/provider deps.
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
