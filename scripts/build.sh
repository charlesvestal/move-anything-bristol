#!/usr/bin/env bash
# Build Bristol modules for Move Anything (ARM64)
#
# Builds all Bristol synth emulations from the monorepo.
# Automatically uses Docker for cross-compilation if needed.
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"
IMAGE_NAME="bristol-builder"

# Check if we need Docker
if [ -z "$CROSS_PREFIX" ] && [ ! -f "/.dockerenv" ]; then
    echo "=== Bristol Modules Build (via Docker) ==="
    echo ""

    # Build Docker image if needed
    if ! docker image inspect "$IMAGE_NAME" &>/dev/null; then
        echo "Building Docker image (first time only)..."
        docker build -t "$IMAGE_NAME" -f "$SCRIPT_DIR/Dockerfile" "$REPO_ROOT"
        echo ""
    fi

    # Run build inside container
    echo "Running build..."
    docker run --rm \
        -v "$REPO_ROOT:/build" \
        -u "$(id -u):$(id -g)" \
        -w /build \
        "$IMAGE_NAME" \
        ./scripts/build.sh

    echo ""
    echo "=== Done ==="
    exit 0
fi

# === Actual build (runs in Docker or with cross-compiler) ===
CROSS_PREFIX="${CROSS_PREFIX:-aarch64-linux-gnu-}"

cd "$REPO_ROOT"

echo "=== Building Bristol Modules ==="
echo "Cross prefix: $CROSS_PREFIX"

# Create build directory
mkdir -p build

# Build shared bristol_mem_loader
echo ""
echo "=== Building shared components ==="
${CROSS_PREFIX}gcc -g -O3 -c -fPIC \
    src/shared/bristol_mem_loader.c \
    -o build/bristol_mem_loader.o \
    -Isrc/shared

# Function to build a synth module
build_synth() {
    local id=$1
    local name=$2
    local preset_dir=$3

    echo ""
    echo "=== Building $name ==="

    mkdir -p "dist/bristol-${id}/presets/${preset_dir}"

    ${CROSS_PREFIX}g++ -g -O3 -shared -fPIC -std=c++14 \
        "src/synths/${id}/${id}_engine.c" \
        "src/synths/${id}/${id}_plugin.cpp" \
        build/bristol_mem_loader.o \
        -o "build/${id}-dsp.so" \
        "-Isrc/synths/${id}" \
        -Isrc/shared \
        -lm

    echo "Packaging $name..."
    cat "src/synths/${id}/module.json" > "dist/bristol-${id}/module.json"
    cat "src/synths/${id}/ui.js" > "dist/bristol-${id}/ui.js"
    cat "build/${id}-dsp.so" > "dist/bristol-${id}/dsp.so"
    chmod +x "dist/bristol-${id}/dsp.so"

    # Copy presets
    if [ -d "presets/${preset_dir}" ]; then
        echo "Copying ${name} presets..."
        for f in "presets/${preset_dir}"/*.mem; do
            [ -e "$f" ] && cat "$f" > "dist/bristol-${id}/presets/${preset_dir}/$(basename "$f")"
        done
    fi
}

# Build all synths
build_synth "mini" "Bristol Mini" "mini"
build_synth "juno" "Bristol Juno" "juno"
build_synth "prophet" "Prophet-5" "prophet"
build_synth "obx" "OB-X" "obx"
build_synth "odyssey" "ARP Odyssey" "odyssey"
build_synth "jupiter" "Jupiter-8" "jupiter"
build_synth "pro1" "Pro-One" "pro1"
build_synth "axxe" "ARP Axxe" "axxe"
build_synth "poly6" "Poly-6" "poly6"
build_synth "solina" "Solina" "solina"
build_synth "rhodes" "Rhodes" "rhodes"
build_synth "obxa" "OB-Xa" "obxa"
build_synth "roadrunner" "Roadrunner" "roadrunner"
build_synth "memmoog" "MemMoog" "memmoog"
build_synth "bit1" "Bit-1" "bit1"
build_synth "vox" "Vox" "vox"

# ============================================
# Create tarballs for release
# ============================================
echo ""
echo "Creating tarballs..."
cd dist
for d in bristol-*/; do
    name="${d%/}"
    tar -czvf "${name}-module.tar.gz" "$name/"
done
cd ..

echo ""
echo "=== Build Complete ==="
echo "Output:"
ls -d dist/bristol-*/
echo ""
echo "To install on Move:"
echo "  ./scripts/install.sh"
