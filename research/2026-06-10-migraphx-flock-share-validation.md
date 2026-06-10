# MIGraphX and Flock Share Settings Validation

- Created: 2026-06-10T11:34:39+03:00
- Task: Validate why MIGraphX is not provided and why flock share setting is missing

## Entries

### 2026-06-10T11:35:02+03:00 - finding: MIGraphX build macro disabled because required ROCm libs missing

- Detail: Build.cs requires ONNX Runtime MIGraphX provider header+lib and ROCm libs libmigraphx.so*,
  libmigraphx_onnx.so*, libmigraphx_tf.so*, libmigraphx_c.so*. Local ORT provider/header exist under
  /home/saranthyr/dev/onnxruntime-install, but /opt/rocm/lib listing shows libmigraphx_c.so* only;
  checking all libmigraphx files next.

### 2026-06-10T11:35:55+03:00 - decision: Fix MIGraphX library discovery and expose flock share clearly

- Detail: Added /opt/rocm/lib/migraphx/lib to Build.cs MIGraphX runtime search/staging dirs and stage/copy
  from all MIGraphX runtime dirs when EP is complete. Moved flock share properties to public
  ABotCharacter category Detection|Flock Sharing with clear display names.

### 2026-06-10T11:37:33+03:00 - validation: Build confirms MIGraphX is now provided

- Detail: After adding /opt/rocm/lib/migraphx/lib to Build.cs, Unreal build succeeded without the ONNX Runtime
  MIGraphX EP missing hint and copied libmigraphx*.so files into the build output. This indicates
  WITH_ONNXRUNTIME_MIGRAPHX is now enabled.

## Report Notes

- Main findings:
  - Validated MIGraphX and flock share settings. MIGraphX was not provided because Build.cs did not search /opt/rocm/lib/migraphx/lib where ROCm installs libmigraphx.so, libmigraphx_onnx.so, libmigraphx_tf.so, and related core libs. ORT provider header and lib existed, but bWithOnnxRuntimeMIGraphX stayed false due missing search path. Added the nested ROCm MIGraphX lib directory to search/staging/copy paths. Build now succeeds without the MIGraphX EP missing hint and copies libmigraphx*.so into Binaries/Linux, indicating WITH_ONNXRUNTIME_MIGRAPHX is enabled. Flock share settings were on ABotCharacter under Detection|Sharing; made them public and renamed category to Detection|Flock Sharing with clear display names so they are easier to find in bot class defaults/details.
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
