#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
UE_ROOT="${UE_ROOT:-/home/saranthyr/Unreal_Engine_5.6.1}"
ENV_FILE="${ENV_FILE:-$PROJECT_ROOT/Saved/InferenceDeps.env}"
CONFIG="${CONFIG:-Development}"
ARCHIVE_ROOT="${ARCHIVE_ROOT:-$PROJECT_ROOT/Builds/Linux-Metrics}"
SOURCE_CACHE_DIR="${SOURCE_CACHE_DIR:-$PROJECT_ROOT/Saved/MIGraphXCache}"
PACKAGE_ROOT="$ARCHIVE_ROOT/EagleEye"
PACKAGE_CACHE_DIR="$PACKAGE_ROOT/Saved/MIGraphXCache"
MAPS="${MAPS:-/Game/ThirdPerson/Blueprints/TestMap/TestWorld+/Game/ThirdPerson/Blueprints/ProcGen/ProcGen}"
SKIP_UAT="${SKIP_UAT:-0}"

if [[ ! -f "$ENV_FILE" ]]; then
    echo "Missing $ENV_FILE"
    echo "Run: ./Scripts/setup-inference-deps.sh --install-system --build-onnxruntime --yes --skip-tensorrt"
    exit 1
fi

# shellcheck disable=SC1090
source "$ENV_FILE"

if [[ "$SKIP_UAT" != "1" ]]; then
    "$UE_ROOT/Engine/Build/BatchFiles/RunUAT.sh" BuildCookRun \
        -project="$PROJECT_ROOT/EagleEye.uproject" \
        -noP4 \
        -platform=Linux \
        -clientconfig="$CONFIG" \
        -build \
        -cook \
        -stage \
        -pak \
        -archive \
        -archivedirectory="$ARCHIVE_ROOT" \
        -map="$MAPS"
fi

mkdir -p "$PACKAGE_CACHE_DIR/data" "$PACKAGE_CACHE_DIR/models"
if [[ -d "$SOURCE_CACHE_DIR" ]]; then
    cp -a "$SOURCE_CACHE_DIR/." "$PACKAGE_CACHE_DIR/"
fi

cat > "$ARCHIVE_ROOT/EagleEye.sh" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail

UE_TRUE_SCRIPT_NAME="$(readlink -f "$0")"
UE_PROJECT_ROOT="$(dirname "$UE_TRUE_SCRIPT_NAME")"
PACKAGE_ROOT="$UE_PROJECT_ROOT/EagleEye"

export ORT_MIGRAPHX_CACHE_PATH="${ORT_MIGRAPHX_CACHE_PATH:-$PACKAGE_ROOT/Saved/MIGraphXCache/data}"
export ORT_MIGRAPHX_MODEL_CACHE_PATH="${ORT_MIGRAPHX_MODEL_CACHE_PATH:-$PACKAGE_ROOT/Saved/MIGraphXCache/models}"
export ORT_MIGRAPHX_EXHAUSTIVE_TUNE="${ORT_MIGRAPHX_EXHAUSTIVE_TUNE:-0}"

mkdir -p "$ORT_MIGRAPHX_CACHE_PATH" "$ORT_MIGRAPHX_MODEL_CACHE_PATH"
chmod +x "$PACKAGE_ROOT/Binaries/Linux/EagleEye"
exec "$PACKAGE_ROOT/Binaries/Linux/EagleEye" EagleEye "$@"
EOF

chmod +x "$ARCHIVE_ROOT/EagleEye.sh"

echo "Packaged build: $ARCHIVE_ROOT"
echo "MIGraphX cache staged: $PACKAGE_CACHE_DIR"
