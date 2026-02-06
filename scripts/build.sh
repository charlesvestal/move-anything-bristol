#!/usr/bin/env bash
# Build Bristol modules for Move Anything (ARM64)
#
# Builds both bristol-mini and bristol-juno from the monorepo.
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

# Create build directories
mkdir -p build
mkdir -p dist/bristol-mini/presets/mini
mkdir -p dist/bristol-juno/presets/juno

# ============================================
# Build Bristol Mini (Minimoog emulation)
# ============================================
echo ""
echo "=== Building Bristol Mini ==="
${CROSS_PREFIX}gcc -g -O3 -c -fPIC \
    src/shared/bristol_mem_loader.c \
    -o build/bristol_mem_loader.o \
    -Isrc/shared

${CROSS_PREFIX}g++ -g -O3 -shared -fPIC -std=c++14 \
    src/synths/mini/mini_engine.c \
    src/synths/mini/mini_plugin.cpp \
    build/bristol_mem_loader.o \
    -o build/mini-dsp.so \
    -Isrc/synths/mini \
    -Isrc/shared \
    -lm

# Package Bristol Mini
echo "Packaging Bristol Mini..."
cat src/synths/mini/module.json > dist/bristol-mini/module.json
cat src/synths/mini/ui.js > dist/bristol-mini/ui.js
cat build/mini-dsp.so > dist/bristol-mini/dsp.so
chmod +x dist/bristol-mini/dsp.so

# Copy Mini presets from Bristol source (if available)
if [ -d presets/mini ]; then
    echo "Copying Mini presets..."
    for f in presets/mini/*.mem; do
        [ -e "$f" ] && cat "$f" > "dist/bristol-mini/presets/mini/$(basename "$f")"
    done
fi

# ============================================
# Build Bristol Juno (Juno-style DCO synth)
# ============================================
echo ""
echo "=== Building Bristol Juno ==="
${CROSS_PREFIX}g++ -g -O3 -shared -fPIC -std=c++14 \
    src/synths/juno/juno_engine.c \
    src/synths/juno/juno_plugin.cpp \
    build/bristol_mem_loader.o \
    -o build/juno-dsp.so \
    -Isrc/synths/juno \
    -Isrc/shared \
    -lm

# Package Bristol Juno
echo "Packaging Bristol Juno..."
cat src/synths/juno/module.json > dist/bristol-juno/module.json
cat src/synths/juno/ui.js > dist/bristol-juno/ui.js
cat build/juno-dsp.so > dist/bristol-juno/dsp.so
chmod +x dist/bristol-juno/dsp.so

# Copy Juno presets from Bristol source (if available)
if [ -d presets/juno ]; then
    echo "Copying Juno presets..."
    for f in presets/juno/*.mem; do
        [ -e "$f" ] && cat "$f" > "dist/bristol-juno/presets/juno/$(basename "$f")"
    done
fi

# ============================================
# Create tarballs for release
# ============================================
echo ""
echo "Creating tarballs..."
cd dist
tar -czvf bristol-mini-module.tar.gz bristol-mini/
tar -czvf bristol-juno-module.tar.gz bristol-juno/
cd ..

echo ""
echo "=== Build Complete ==="
echo "Output:"
echo "  dist/bristol-mini/"
echo "  dist/bristol-juno/"
echo ""
echo "Tarballs:"
echo "  dist/bristol-mini-module.tar.gz"
echo "  dist/bristol-juno-module.tar.gz"
echo ""
echo "To install on Move:"
echo "  ./scripts/install.sh"
