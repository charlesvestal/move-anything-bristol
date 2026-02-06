#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

# Default Move IP
MOVE_IP="${MOVE_IP:-move.local}"
MOVE_USER="${MOVE_USER:-root}"
MOVE_PATH="/data/UserData/move-anything/modules/sound_generators"

# Find all built modules dynamically
if [ ! -d "$PROJECT_DIR/dist" ]; then
    echo "Error: No dist directory found. Run ./scripts/build.sh first."
    exit 1
fi

MODULES=()
for d in "$PROJECT_DIR/dist"/bristol-*/; do
    if [ -d "$d" ]; then
        MODULE_ID=$(basename "$d")
        MODULES+=("$MODULE_ID")
    fi
done

if [ ${#MODULES[@]} -eq 0 ]; then
    echo "Error: No modules found in dist/. Run ./scripts/build.sh first."
    exit 1
fi

echo "Installing Bristol Mini to Move at $MOVE_IP..."

for MODULE_ID in "${MODULES[@]}"; do
    echo ""
    echo "Installing $MODULE_ID..."

    # Create module directory on Move
    ssh "$MOVE_USER@$MOVE_IP" "mkdir -p $MOVE_PATH/$MODULE_ID"

    # Copy files
    scp -r "$PROJECT_DIR/dist/$MODULE_ID/"* "$MOVE_USER@$MOVE_IP:$MOVE_PATH/$MODULE_ID/"

    echo "  -> $MOVE_PATH/$MODULE_ID"
done

echo ""
echo "Installation complete!"
echo ""
echo "Modules installed:"
for MODULE_ID in "${MODULES[@]}"; do
    echo "  - $MODULE_ID"
done
echo ""
echo "Restart Move Anything or rescan modules to use Bristol Mini."
