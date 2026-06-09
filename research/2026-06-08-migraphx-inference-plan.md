
### 2026-06-08T05:24:22+03:00 - finding: ROCm GPU runtime not accessible from current Codex session

- Detail: rocminfo exits with: ROCk module is loaded; Unable to open /dev/kfd read-write: No such file or
  directory. /dev/dri and /dev/kfd are absent inside this session, while /sys/class/drm shows
  card/render nodes. User groups show no render/video in id output despite rocminfo text saying
  video.
- Impact: Can verify installed packages, but cannot validate GPU execution from this sandbox/session until
  /dev/kfd and /dev/dri are exposed and user/group permissions are correct.

### 2026-06-08T05:24:22+03:00 - command: Checked AMD Ubuntu machine for MIGraphX and ROCm

- Detail: Found /usr/bin/rocminfo and /usr/bin/hipconfig. Packages include rocm 7.1.0, rocm-core 7.2.4,
  migraphx 2.15.0.70204-93~24.04, migraphx-dev same version. /opt/rocm-7.2.4/bin/migraphx-driver
  exists and reports MIGraphX Version: 2.15.0.20250912-17-221-gdd11a7555. RX 7900-class Navi 31 GPU
  visible via lspci.
- Impact: MIGraphX SDK/runtime is installed on target machine.

### 2026-06-08T05:24:22+03:00 - finding: ONNX Runtime MIGraphX EP not present yet

- Detail: No ONNXRUNTIME_ROOT/ORT_ROOT env vars. find under /opt/rocm, /usr, and project found no
  libonnxruntime.so, libonnxruntime_providers_migraphx.so, migraphx_provider_factory.h, or
  onnxruntime_cxx_api.h.
- Impact: Unreal cannot compile WITH_ONNXRUNTIME_MIGRAPHX=1 yet; need build/install ONNX Runtime with
  --use_migraphx.

### 2026-06-08T05:51:25+03:00 - command: Started investigating failed ONNX Runtime MIGraphX build

- Detail: Searching home directory for onnxruntime source/build directories and generated build logs.
- Impact: Need exact configure/compile error before proposing fix.

### 2026-06-08T05:52:12+03:00 - finding: ONNX Runtime MIGraphX build failed during CMake configure

- Detail: Rerun of /home/saranthyr/dev/onnxruntime/build.sh --config Release --parallel --use_migraphx
  --migraphx_home /opt/rocm failed at /opt/rocm/lib/cmake/hip/hip-config.cmake:31: File or directory
  /opt/rocm/bin/hipcc referenced by variable hip_HIPCC_EXECUTABLE does not exist.
- Impact: Build cannot proceed until HIP/ROCm path mismatch is fixed or build is pointed at the HIP
  installation path.

### 2026-06-08T06:00:04+03:00 - validation: Built ONNX Runtime with MIGraphX EP

- Detail: Original build failed due /opt/rocm/bin/hipcc missing. Correct build succeeded with
  --build_shared_lib --skip_tests --compile_no_warning_as_error --cmake_extra_defines
  hip_DIR=/usr/lib/x86_64-linux-gnu/cmake/hip. Installed to /home/saranthyr/dev/onnxruntime-install,
  including libonnxruntime.so and libonnxruntime_providers_migraphx.so. Added staging header
  compatibility for EagleEye.
- Impact: Set ONNXRUNTIME_ROOT=/home/saranthyr/dev/onnxruntime-install before rebuilding EagleEye.

### 2026-06-08T06:05:55+03:00 - validation: EagleEye built with ONNX Runtime MIGraphX

- Detail: Configured DefaultGame.ini for InferenceBackend=ONNXRuntime, OnnxRuntimeExecutionProvider=MIGraphX,
  ModelPathOverride=yolo26s.onnx. Removed unnecessary migraphx_provider_factory.h include and
  adapted LoadOnnxRuntime to ORT 1.28 C++ API (UTF-8 path + auto tensor shape info). Build command
  with ONNXRUNTIME_ROOT=/home/saranthyr/dev/onnxruntime-install succeeded.
- Impact: Project now compiles against ONNX Runtime MIGraphX EP; next validation is runtime launch with
  LD_LIBRARY_PATH including ORT and ROCm paths.

### 2026-06-08T06:07:25+03:00 - command: Started checking runtime logs after MIGraphX setup

- Detail: Searching project Saved/Logs, UnrealBuildTool logs, and home directory for latest Unreal/EagleEye
  logs.
- Impact: Need verify whether runtime selected ONNX Runtime MIGraphX or hit loader/provider errors.

### 2026-06-08T06:09:18+03:00 - finding: Runtime launch failed from ONNX Runtime library collision

- Detail: Saved/Logs/EagleEye.log line 1303: dlopen failed because UE NNERuntimeORT bundled
  libonnxruntime.so.1.20 lacks VERS_1.28.0 required by Binaries/Linux/libUnrealEditor-EagleEye.so.
  Log also shows NNEDenoiser and NNERuntimeORT mounted; NNEDenoiser.uplugin is EnabledByDefault=true
  and depends on NNERuntimeORT.
- Impact: Not a MIGraphX provider failure yet; EagleEye module cannot load until UE bundled ORT is kept out or
  ORT versions are unified.

### 2026-06-08T06:11:30+03:00 - validation: Disabled NNE ORT collision and smoke-tested editor load

- Detail: Added explicit project plugin disables for NNEDenoiser and NNERuntimeORT in EagleEye.uproject.
  Rebuilt EagleEyeEditor successfully. Headless UnrealEditor-Cmd launch with ONNXRUNTIME_ROOT and
  LD_LIBRARY_PATH reached Engine initialized and loaded TestWorld; fresh Saved/Logs/EagleEye.log
  contains no NNERuntimeORT/NNEDenoiser/dlopen failed matches.
- Impact: Previous module-load blocker is cleared; next validation must run gameplay/inference actor to create
  an ONNX Runtime MIGraphX session.

## Report Notes

- Main findings:
  - Runtime log root cause was UE NNERuntimeORT bundled ONNX Runtime 1.20 loading before EagleEye's ONNX Runtime 1.28, causing VERS_1.28.0 symbol mismatch and preventing game module load. Explicitly disabling NNEDenoiser and NNERuntimeORT in EagleEye.uproject cleared the module-load crash. Rebuild succeeded. Headless editor smoke test reached engine/map load with no NNERuntimeORT/NNEDenoiser/dlopen failed lines. With LD_LIBRARY_PATH set to /home/saranthyr/dev/onnxruntime-install/lib:/opt/rocm/lib:/opt/rocm/lib/migraphx/lib, ldd resolves libonnxruntime.so.1 to the built ORT 1.28 install and resolves MIGraphX provider ROCm dependencies. Remaining validation: launch/play so UMyActorComponent::LoadOnnxRuntime creates the session and logs 'ONNX Runtime provider configured: MIGraphX'.
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

### 2026-06-08T06:13:26+03:00 - validation: Logs confirm MIGraphX provider configured in PIE

- Detail: Saved/Logs/EagleEye-backup-2026.06.08-03.12.16.log lines 1552-1560 show requested backend ONNX
  Runtime, provider MIGraphX, AMD RX 7900 XTX adapter, model yolo26s.onnx, and 'ONNX Runtime
  provider configured: MIGraphX'. Latest active EagleEye.log at 06:13 only shows editor startup and
  LD_LIBRARY_PATH; no inference actor/session lines yet.
- Impact: MIGraphX path is now successfully selected/configured at runtime; remaining checks should focus on
  session/model load completion and per-frame inference output/performance.
