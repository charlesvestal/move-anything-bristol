#!/usr/bin/env bash
# Build Bristol Mini module for Move Anything (ARM64)
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"
IMAGE_NAME="bristol-builder"

# Check if we need Docker
if [ -z "$CROSS_PREFIX" ] && [ ! -f "/.dockerenv" ]; then
    echo "=== Bristol Mini Build (via Docker) ==="
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

echo "=== Building Bristol Mini ==="
echo "Cross prefix: $CROSS_PREFIX"

# Create directories
mkdir -p build
mkdir -p "dist/bristol-mini/presets/mini"

# Build shared bristol_mem_loader
echo ""
echo "=== Building shared components ==="
${CROSS_PREFIX}gcc -g -O3 -c -fPIC \
    src/shared/bristol_mem_loader.c \
    -o build/bristol_mem_loader.o \
    -Isrc/shared

# Build Mini synth
echo ""
echo "=== Building Mini engine ==="
${CROSS_PREFIX}g++ -g -O3 -shared -fPIC -std=c++14 \
    src/synths/mini/mini_engine.c \
    src/synths/mini/mini_plugin.cpp \
    build/bristol_mem_loader.o \
    -o build/mini-dsp.so \
    -Isrc/synths/mini \
    -Isrc/shared \
    -lm

# Package
echo "Packaging..."
cat src/synths/mini/module.json > dist/bristol-mini/module.json
[ -f src/help.json ] && cat src/help.json > dist/bristol-mini/help.json
cat src/synths/mini/ui.js > dist/bristol-mini/ui.js
cat build/mini-dsp.so > dist/bristol-mini/dsp.so
chmod +x dist/bristol-mini/dsp.so

# Copy presets
echo "Copying presets..."
for f in presets/mini/*.mem; do
    [ -e "$f" ] && cat "$f" > "dist/bristol-mini/presets/mini/$(basename "$f")"
done

# Create tarball
echo ""
echo "Creating tarball..."
cd dist
tar -czvf bristol-mini-module.tar.gz bristol-mini/
cd ..

echo ""
echo "=== Build Complete ==="
echo "Output: dist/bristol-mini/"
echo ""
echo "To install on Move:"
echo "  ./scripts/install.sh"
