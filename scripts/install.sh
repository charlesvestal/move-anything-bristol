#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
MODULE_ID="bristol"

# Default Move IP
MOVE_IP="${MOVE_IP:-move.local}"
MOVE_USER="${MOVE_USER:-root}"
MOVE_PATH="/data/UserData/move-anything/modules"

# Check if dist exists
if [ ! -d "$PROJECT_DIR/dist/$MODULE_ID" ]; then
    echo "Error: Distribution not found. Run ./scripts/build.sh first."
    exit 1
fi

echo "Installing Bristol module to Move at $MOVE_IP..."

# Create module directory on Move
ssh "$MOVE_USER@$MOVE_IP" "mkdir -p $MOVE_PATH/$MODULE_ID"

# Copy files
scp -r "$PROJECT_DIR/dist/$MODULE_ID/"* "$MOVE_USER@$MOVE_IP:$MOVE_PATH/$MODULE_ID/"

echo ""
echo "Installation complete!"
echo "Module installed to: $MOVE_PATH/$MODULE_ID"
echo ""
echo "Restart Move Anything or rescan modules to use Bristol."
