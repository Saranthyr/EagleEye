#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
UE_ROOT="${UE_ROOT:-/home/saranthyr/Unreal_Engine_5.6.1}"
ENV_FILE="${ENV_FILE:-$PROJECT_ROOT/Saved/InferenceDeps.env}"
CONFIG="${CONFIG:-Development}"
SKIP_BUILD="${SKIP_BUILD:-0}"

if [[ ! -f "$ENV_FILE" ]]; then
    echo "Missing $ENV_FILE"
    echo "Run: ./Scripts/setup-inference-deps.sh --install-system --build-onnxruntime --yes --skip-tensorrt"
    exit 1
fi

# shellcheck disable=SC1090
source "$ENV_FILE"

if [[ "$SKIP_BUILD" != "1" ]]; then
    "$UE_ROOT/Engine/Build/BatchFiles/Linux/Build.sh" \
        EagleEyeEditor \
        Linux \
        "$CONFIG" \
        "$PROJECT_ROOT/EagleEye.uproject" \
        -WaitMutex
fi

exec "$UE_ROOT/Engine/Binaries/Linux/UnrealEditor" \
    "$PROJECT_ROOT/EagleEye.uproject" \
    "$@"
