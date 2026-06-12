#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEV_ROOT="${DEV_ROOT:-$HOME/dev}"
ONNXRUNTIME_SRC="${ONNXRUNTIME_SRC:-$DEV_ROOT/onnxruntime}"
ONNXRUNTIME_PREFIX="${ONNXRUNTIME_PREFIX:-$DEV_ROOT/onnxruntime-install}"
ROCM_HOME="${ROCM_HOME:-/opt/rocm}"
ENV_FILE="${ENV_FILE:-$PROJECT_ROOT/Saved/InferenceDeps.env}"

INSTALL_SYSTEM=0
BUILD_ONNXRUNTIME=0
ASSUME_YES=0
FORCE_TENSORRT=0
SKIP_TENSORRT=0
SKIP_MIGRAPHX=0

usage() {
    cat <<USAGE
Usage: $0 [options]

Detect and optionally install inference dependencies for EagleEye.

Options:
  --install-system       Install missing apt packages for ROCm/MIGraphX and TensorRT when available.
  --build-onnxruntime    Clone/build/install ONNX Runtime with MIGraphX EP when missing.
  --yes                  Pass -y to apt and skip prompts where possible.
  --onnxruntime-src DIR  ONNX Runtime source dir. Default: $ONNXRUNTIME_SRC
  --onnxruntime-prefix DIR
                         ONNX Runtime install prefix. Default: $ONNXRUNTIME_PREFIX
  --rocm-home DIR        ROCm/MIGraphX root. Default: $ROCM_HOME
  --env-file FILE        Write sourceable exports here. Default: $ENV_FILE
  --force-tensorrt       Try TensorRT install even without NVIDIA GPU.
  --skip-tensorrt        Do not detect/install TensorRT.
  --skip-migraphx        Do not detect/install ROCm/MIGraphX.
  -h, --help             Show this help.

Examples:
  $0
  $0 --install-system --build-onnxruntime --yes
  source Saved/InferenceDeps.env
USAGE
}

log() {
    printf '[EagleEye deps] %s\n' "$*"
}

warn() {
    printf '[EagleEye deps] WARNING: %s\n' "$*" >&2
}

die() {
    printf '[EagleEye deps] ERROR: %s\n' "$*" >&2
    exit 1
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --install-system)
            INSTALL_SYSTEM=1
            ;;
        --build-onnxruntime)
            BUILD_ONNXRUNTIME=1
            ;;
        --yes|-y)
            ASSUME_YES=1
            ;;
        --onnxruntime-src)
            ONNXRUNTIME_SRC="${2:?missing dir for --onnxruntime-src}"
            shift
            ;;
        --onnxruntime-prefix)
            ONNXRUNTIME_PREFIX="${2:?missing dir for --onnxruntime-prefix}"
            shift
            ;;
        --rocm-home)
            ROCM_HOME="${2:?missing dir for --rocm-home}"
            shift
            ;;
        --env-file)
            ENV_FILE="${2:?missing file for --env-file}"
            shift
            ;;
        --force-tensorrt)
            FORCE_TENSORRT=1
            ;;
        --skip-tensorrt)
            SKIP_TENSORRT=1
            ;;
        --skip-migraphx)
            SKIP_MIGRAPHX=1
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            die "Unknown option: $1"
            ;;
    esac
    shift
done

APT_UPDATED=0
apt_update_once() {
    if [[ "$APT_UPDATED" -eq 0 ]]; then
        log "Running apt-get update"
        sudo apt-get update
        APT_UPDATED=1
    fi
}

apt_has_package() {
    apt-cache show "$1" >/dev/null 2>&1
}

apt_install_available() {
    local packages=()
    local skipped=()
    local package

    for package in "$@"; do
        if apt_has_package "$package"; then
            packages+=("$package")
        else
            skipped+=("$package")
        fi
    done

    if [[ "${#skipped[@]}" -gt 0 ]]; then
        warn "apt repo lacks packages: ${skipped[*]}"
    fi

    if [[ "${#packages[@]}" -eq 0 ]]; then
        return 1
    fi

    apt_update_once
    if [[ "$ASSUME_YES" -eq 1 ]]; then
        sudo apt-get install -y "${packages[@]}"
    else
        sudo apt-get install "${packages[@]}"
    fi
}

find_first_existing_file() {
    local candidate
    for candidate in "$@"; do
        if [[ -f "$candidate" || -L "$candidate" ]]; then
            printf '%s\n' "$candidate"
            return 0
        fi
    done
    return 1
}

find_first_existing_dir() {
    local candidate
    for candidate in "$@"; do
        if [[ -d "$candidate" ]]; then
            printf '%s\n' "$candidate"
            return 0
        fi
    done
    return 1
}

has_amd_gpu() {
    command -v lspci >/dev/null 2>&1 && lspci | grep -Eiq 'VGA|Display|3D' && lspci | grep -Eiq 'AMD|ATI'
}

has_nvidia_gpu() {
    command -v nvidia-smi >/dev/null 2>&1 || { command -v lspci >/dev/null 2>&1 && lspci | grep -Eiq 'VGA|Display|3D' && lspci | grep -Eiq 'NVIDIA'; }
}

detect_migraphx() {
    if [[ "$SKIP_MIGRAPHX" -eq 1 ]]; then
        return 0
    fi

    local driver=""
    local lib=""

    driver="$(command -v migraphx-driver || true)"
    if [[ -z "$driver" && -x "$ROCM_HOME/bin/migraphx-driver" ]]; then
        driver="$ROCM_HOME/bin/migraphx-driver"
    fi

    lib="$(find_first_existing_file \
        "$ROCM_HOME/lib/libmigraphx_c.so" \
        "$ROCM_HOME/lib/migraphx/libmigraphx_c.so" \
        /opt/rocm/lib/libmigraphx_c.so \
        /opt/rocm/lib/migraphx/libmigraphx_c.so \
        /usr/lib/x86_64-linux-gnu/libmigraphx_c.so 2>/dev/null || true)"

    if [[ -n "$driver" && -n "$lib" ]]; then
        log "MIGraphX found: $driver"
        return 0
    fi

    warn "MIGraphX not found."
    if [[ "$INSTALL_SYSTEM" -eq 0 ]]; then
        warn "Run with --install-system to try apt install for migraphx/migraphx-dev."
        return 1
    fi

    apt_install_available migraphx migraphx-dev || {
        warn "Could not install MIGraphX. Configure AMD ROCm apt repo, then rerun."
        return 1
    }
}

detect_tensorrt() {
    if [[ "$SKIP_TENSORRT" -eq 1 ]]; then
        return 0
    fi

    if [[ "$FORCE_TENSORRT" -eq 0 ]] && ! has_nvidia_gpu; then
        log "No NVIDIA GPU detected; skipping TensorRT install. Use --force-tensorrt to override."
        return 0
    fi

    local tensorrt_root="${TENSORRT_ROOT:-${TENSORRT_PATH:-}}"
    local cuda_root="${CUDA_HOME:-${CUDA_PATH:-}}"
    local include_dir=""
    local infer_lib=""
    local plugin_lib=""
    local cuda_lib=""

    if [[ -z "$cuda_root" ]]; then
        cuda_root="$(find_first_existing_dir /usr/local/cuda /usr/local/cuda-* 2>/dev/null | tail -n 1 || true)"
    fi

    if [[ -n "$tensorrt_root" ]]; then
        include_dir="$tensorrt_root/include"
        infer_lib="$(find_first_existing_file "$tensorrt_root/lib/libnvinfer.so" "$tensorrt_root/lib64/libnvinfer.so" 2>/dev/null || true)"
        plugin_lib="$(find_first_existing_file "$tensorrt_root/lib/libnvinfer_plugin.so" "$tensorrt_root/lib64/libnvinfer_plugin.so" 2>/dev/null || true)"
    else
        include_dir="/usr/include/x86_64-linux-gnu"
        infer_lib="$(find_first_existing_file /usr/lib/x86_64-linux-gnu/libnvinfer.so /lib/x86_64-linux-gnu/libnvinfer.so 2>/dev/null || true)"
        plugin_lib="$(find_first_existing_file /usr/lib/x86_64-linux-gnu/libnvinfer_plugin.so /lib/x86_64-linux-gnu/libnvinfer_plugin.so 2>/dev/null || true)"
    fi

    if [[ -n "$cuda_root" ]]; then
        cuda_lib="$(find_first_existing_file "$cuda_root/lib64/libcudart.so" "$cuda_root/lib/libcudart.so" 2>/dev/null || true)"
    fi

    if [[ -d "$include_dir" && -n "$infer_lib" && -n "$plugin_lib" && -n "$cuda_lib" ]]; then
        log "TensorRT found: $infer_lib"
        return 0
    fi

    warn "TensorRT/CUDA not found."
    if [[ "$INSTALL_SYSTEM" -eq 0 ]]; then
        warn "Run with --install-system to try apt install for TensorRT packages from configured NVIDIA repo."
        return 1
    fi

    apt_install_available \
        libnvinfer-dev \
        libnvinfer-plugin-dev \
        libnvonnxparsers-dev \
        libnvinfer-bin || {
        warn "Could not install TensorRT. Configure NVIDIA CUDA/TensorRT apt repo, then rerun."
        return 1
    }
}

detect_onnxruntime_migraphx() {
    local root="${ONNXRUNTIME_ROOT:-${ORT_ROOT:-$ONNXRUNTIME_PREFIX}}"
    local include_dir="$root/include"
    local ort_lib=""
    local migraphx_provider=""

    ort_lib="$(find_first_existing_file "$root/lib/libonnxruntime.so" "$root/lib64/libonnxruntime.so" 2>/dev/null || true)"
    migraphx_provider="$(find_first_existing_file "$root/lib/libonnxruntime_providers_migraphx.so" "$root/lib64/libonnxruntime_providers_migraphx.so" 2>/dev/null || true)"

    if [[ -d "$include_dir" && -n "$ort_lib" && -n "$migraphx_provider" ]]; then
        ONNXRUNTIME_PREFIX="$root"
        log "ONNX Runtime MIGraphX EP found: $root"
        return 0
    fi

    warn "ONNX Runtime MIGraphX EP not found under $root."
    if [[ "$BUILD_ONNXRUNTIME" -eq 0 ]]; then
        warn "Run with --build-onnxruntime to clone/build ONNX Runtime with --use_migraphx."
        return 1
    fi

    build_onnxruntime_migraphx
}

prepare_onnxruntime_headers() {
    local include_dir="$ONNXRUNTIME_PREFIX/include"
    local nested_include="$include_dir/onnxruntime"

    if [[ -d "$nested_include" ]]; then
        local header
        while IFS= read -r header; do
            ln -sf "onnxruntime/$(basename "$header")" "$include_dir/$(basename "$header")"
        done < <(find "$nested_include" -maxdepth 1 -type f -name '*.h' | sort)
    fi

    local provider_src="$ONNXRUNTIME_SRC/onnxruntime/core/providers/migraphx/migraphx_provider_factory.h"
    local provider_dst="$include_dir/onnxruntime/core/providers/migraphx/migraphx_provider_factory.h"
    if [[ -f "$provider_src" && ! -f "$provider_dst" ]]; then
        mkdir -p "$(dirname "$provider_dst")"
        cp "$provider_src" "$provider_dst"
    fi
}

build_onnxruntime_migraphx() {
    mkdir -p "$DEV_ROOT"

    if [[ ! -d "$ONNXRUNTIME_SRC/.git" ]]; then
        log "Cloning ONNX Runtime into $ONNXRUNTIME_SRC"
        git clone --recursive https://github.com/microsoft/onnxruntime "$ONNXRUNTIME_SRC"
    else
        log "Using existing ONNX Runtime source: $ONNXRUNTIME_SRC"
        git -C "$ONNXRUNTIME_SRC" submodule update --init --recursive
    fi

    local cmake_defs=()
    if [[ ! -x "$ROCM_HOME/bin/hipcc" && -d /usr/lib/x86_64-linux-gnu/cmake/hip ]]; then
        cmake_defs+=(--cmake_extra_defines hip_DIR=/usr/lib/x86_64-linux-gnu/cmake/hip)
    fi

    log "Building ONNX Runtime with MIGraphX EP"
    (
        cd "$ONNXRUNTIME_SRC"
        ./build.sh \
            --config Release \
            --parallel \
            --use_migraphx \
            --migraphx_home "$ROCM_HOME" \
            --build_shared_lib \
            --skip_tests \
            --compile_no_warning_as_error \
            "${cmake_defs[@]}"
    )

    log "Installing ONNX Runtime into $ONNXRUNTIME_PREFIX"
    cmake --install "$ONNXRUNTIME_SRC/build/Linux/Release" --prefix "$ONNXRUNTIME_PREFIX"
    prepare_onnxruntime_headers
}

write_env_file() {
    mkdir -p "$(dirname "$ENV_FILE")"
    cat > "$ENV_FILE" <<EOF
# Source this before building/running EagleEye with ONNX Runtime MIGraphX.
export ONNXRUNTIME_ROOT="$ONNXRUNTIME_PREFIX"
export ORT_ROOT="$ONNXRUNTIME_PREFIX"
export ROCM_HOME="$ROCM_HOME"
export ORT_MIGRAPHX_CACHE_PATH="$PROJECT_ROOT/Saved/MIGraphXCache/data"
export ORT_MIGRAPHX_MODEL_CACHE_PATH="$PROJECT_ROOT/Saved/MIGraphXCache/models"
export ORT_MIGRAPHX_EXHAUSTIVE_TUNE="\${ORT_MIGRAPHX_EXHAUSTIVE_TUNE:-0}"
export LD_LIBRARY_PATH="$ONNXRUNTIME_PREFIX/lib:$ROCM_HOME/lib:$ROCM_HOME/lib/migraphx/lib:\${LD_LIBRARY_PATH:-}"
EOF
    mkdir -p "$PROJECT_ROOT/Saved/MIGraphXCache/data" "$PROJECT_ROOT/Saved/MIGraphXCache/models"

    if [[ "$SKIP_TENSORRT" -eq 0 ]]; then
        if [[ -n "${TENSORRT_ROOT:-}" ]]; then
            printf 'export TENSORRT_ROOT="%s"\n' "$TENSORRT_ROOT" >> "$ENV_FILE"
        fi
        if [[ -n "${CUDA_HOME:-}" ]]; then
            printf 'export CUDA_HOME="%s"\n' "$CUDA_HOME" >> "$ENV_FILE"
        fi
    fi

    log "Wrote env file: $ENV_FILE"
}

main() {
    [[ "$(uname -s)" == "Linux" ]] || die "This setup script currently supports Linux only."

    local failures=0

    detect_migraphx || failures=$((failures + 1))
    detect_onnxruntime_migraphx || failures=$((failures + 1))
    detect_tensorrt || failures=$((failures + 1))

    write_env_file

    if [[ "$failures" -gt 0 ]]; then
        warn "$failures dependency group(s) still missing."
        warn "For AMD/MIGraphX builds, TensorRT can be absent. For NVIDIA/TensorRT builds, MIGraphX can be absent."
        exit 1
    fi

    log "Dependency setup complete."
    log "Next: source \"$ENV_FILE\""
}

main "$@"
