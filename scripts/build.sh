#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build"
DIST_DIR="$PROJECT_DIR/dist"
MODULE_ID="bristol"

cd "$PROJECT_DIR"

# Detect if we need Docker for cross-compilation
if [ -z "$CROSS_PREFIX" ]; then
    # Check if we're on ARM64 Linux (native build possible)
    if [ "$(uname -m)" = "aarch64" ] && [ "$(uname -s)" = "Linux" ]; then
        echo "Building natively on ARM64 Linux..."
        CROSS_PREFIX=""
    else
        echo "Cross-compiling via Docker..."

        # Build Docker image if needed
        if ! docker images | grep -q "bristol-builder"; then
            echo "Building Docker image..."
            docker build -t bristol-builder -f scripts/Dockerfile .
        fi

        # Run build in Docker
        docker run --rm -v "$PROJECT_DIR:/build" -w /build bristol-builder ./scripts/build.sh
        exit $?
    fi
fi

# Create build directory
mkdir -p "$BUILD_DIR"

# Compiler settings
CC="${CROSS_PREFIX}gcc"
CXX="${CROSS_PREFIX}g++"
CFLAGS="-O3 -fPIC -Wall -I$PROJECT_DIR/src/dsp"
CXXFLAGS="-O3 -fPIC -Wall -std=c++17 -I$PROJECT_DIR/src/dsp"
LDFLAGS="-shared -lm"

echo "Compiling Bristol engine..."
$CC $CFLAGS -c src/dsp/bristol_engine.c -o "$BUILD_DIR/bristol_engine.o"

echo "Compiling Bristol plugin..."
$CXX $CXXFLAGS -c src/dsp/bristol_plugin.cpp -o "$BUILD_DIR/bristol_plugin.o"

echo "Linking dsp.so..."
$CXX $LDFLAGS "$BUILD_DIR/bristol_engine.o" "$BUILD_DIR/bristol_plugin.o" -o "$BUILD_DIR/dsp.so"

# Create distribution directory
echo "Creating distribution package..."
rm -rf "$DIST_DIR"
mkdir -p "$DIST_DIR/$MODULE_ID"

# Copy files
cp "$BUILD_DIR/dsp.so" "$DIST_DIR/$MODULE_ID/"
cp src/module.json "$DIST_DIR/$MODULE_ID/"

# Create UI file (minimal for chain integration)
cat > "$DIST_DIR/$MODULE_ID/ui.js" << 'EOF'
// Bristol Mini - Minimoog emulation
// UI handled by chain host shadow UI

globalThis.init = function() {
    return true;
};

globalThis.tick = function() {
    return true;
};
EOF

# Create tarball for release
echo "Creating release tarball..."
cd "$DIST_DIR"
tar -czvf "${MODULE_ID}-module.tar.gz" "$MODULE_ID/"
cd "$PROJECT_DIR"

echo ""
echo "Build complete!"
echo "  DSP plugin: $BUILD_DIR/dsp.so"
echo "  Distribution: $DIST_DIR/$MODULE_ID/"
echo "  Release tarball: $DIST_DIR/${MODULE_ID}-module.tar.gz"
