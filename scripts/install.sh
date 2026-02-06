#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

# Default Move IP
MOVE_IP="${MOVE_IP:-move.local}"
MOVE_USER="${MOVE_USER:-root}"
MOVE_PATH="/data/UserData/move-anything/modules/sound_generators"

# Modules to install
MODULES=("bristol-mini" "bristol-juno")

# Check if dist exists
for MODULE_ID in "${MODULES[@]}"; do
    if [ ! -d "$PROJECT_DIR/dist/$MODULE_ID" ]; then
        echo "Error: Distribution for $MODULE_ID not found. Run ./scripts/build.sh first."
        exit 1
    fi
done

echo "Installing Bristol modules to Move at $MOVE_IP..."

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
echo "Restart Move Anything or rescan modules to use Bristol synths."
