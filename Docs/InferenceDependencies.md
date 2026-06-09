# Inference Dependencies

Use `Scripts/setup-inference-deps.sh` to detect or install optional inference backends.

AMD / MIGraphX:

```bash
./Scripts/setup-inference-deps.sh --install-system --build-onnxruntime --yes --skip-tensorrt
source Saved/InferenceDeps.env
```

NVIDIA / TensorRT:

```bash
./Scripts/setup-inference-deps.sh --install-system --force-tensorrt --yes --skip-migraphx
source Saved/InferenceDeps.env
```

Detect only:

```bash
./Scripts/setup-inference-deps.sh
```

The script writes `Saved/InferenceDeps.env`, which exports `ONNXRUNTIME_ROOT`, `ORT_ROOT`, `ROCM_HOME`, and `LD_LIBRARY_PATH` for the editor/build shell.
It also enables persistent MIGraphX caches under `Saved/MIGraphXCache/`:

```bash
ORT_MIGRAPHX_CACHE_PATH=Saved/MIGraphXCache/data
ORT_MIGRAPHX_MODEL_CACHE_PATH=Saved/MIGraphXCache/models
ORT_MIGRAPHX_EXHAUSTIVE_TUNE=0
```

The first launch still compiles. Later launches with the same model, input shape, ORT/MIGraphX/ROCm versions, and provider options should reuse cached compiled artifacts.

Build or package from a shell that has sourced the env file:

```bash
source Saved/InferenceDeps.env
/home/saranthyr/Unreal_Engine_5.6.1/Engine/Build/BatchFiles/Linux/Build.sh EagleEye Linux Development "$PWD/EagleEye.uproject" -WaitMutex
```

For packaged builds, Unreal stages `libonnxruntime.so*` and `libonnxruntime_providers*.so` beside the Linux target. ROCm/MIGraphX runtime libraries still come from the machine ROCm install, so launch packaged builds with:

```bash
source Saved/InferenceDeps.env
./EagleEye.sh
```

Notes:

- MIGraphX install uses apt packages `migraphx` and `migraphx-dev` when the ROCm apt repository is already configured.
- TensorRT install uses apt packages from the configured NVIDIA repository. If apt cannot find them, install/configure NVIDIA CUDA/TensorRT repository first and rerun.
- ONNX Runtime is built from source with `--use_migraphx`, `--build_shared_lib`, and `--skip_tests` into `$HOME/dev/onnxruntime-install` by default.
