#!/bin/bash

# Test script to demonstrate checkpoint functionality
# Note: This script shows the commands - actual testing requires running nameserver and storageserver

echo "=== Checkpoint Functionality Demo ==="
echo ""
echo "This script demonstrates the checkpoint commands."
echo "To actually test, you need to:"
echo "1. Start the nameserver"
echo "2. Start the storageserver"
echo "3. Send these commands through the nameserver"
echo ""

# Create a test file
echo "Step 1: Create a test file"
echo "Creating ss_storage/demo.txt with initial content..."
echo "This is version 1 of the file." > ss_storage/demo.txt
cat ss_storage/demo.txt
echo ""

# Simulate checkpoint creation
echo "Step 2: Create checkpoint 'v1'"
echo "Command: CHECKPOINT demo.txt v1"
echo "Expected: Checkpoint file created at ss_storage/demo.txt.checkpoint.v1"
echo ""

# Create the checkpoint manually for demo
cp ss_storage/demo.txt ss_storage/demo.txt.checkpoint.v1
echo "Checkpoint v1 created (manually for demo)"
ls -lh ss_storage/demo.txt*
echo ""

# Modify the file
echo "Step 3: Modify the file"
echo "This is version 2 of the file - significantly changed!" > ss_storage/demo.txt
cat ss_storage/demo.txt
echo ""

# Create another checkpoint
echo "Step 4: Create checkpoint 'v2'"
echo "Command: CHECKPOINT demo.txt v2"
cp ss_storage/demo.txt ss_storage/demo.txt.checkpoint.v2
echo "Checkpoint v2 created"
ls -lh ss_storage/demo.txt*
echo ""

# Modify again
echo "Step 5: Make more changes"
echo "This is version 3 - latest changes!" > ss_storage/demo.txt
cat ss_storage/demo.txt
echo ""

# List checkpoints
echo "Step 6: List all checkpoints"
echo "Command: LISTCHECKPOINTS demo.txt"
echo "Available checkpoints:"
for f in ss_storage/demo.txt.checkpoint.*; do
    if [ -f "$f" ]; then
        tag=$(basename "$f" | sed 's/demo.txt.checkpoint.//')
        echo "  - $tag"
    fi
done
echo ""

# View a checkpoint
echo "Step 7: View checkpoint v1"
echo "Command: VIEWCHECKPOINT demo.txt v1"
echo "Checkpoint 'v1' content:"
echo "---"
cat ss_storage/demo.txt.checkpoint.v1
echo "---"
echo ""

# Show current file
echo "Step 8: Current file content (before revert)"
cat ss_storage/demo.txt
echo ""

# Revert to checkpoint
echo "Step 9: Revert to checkpoint v1"
echo "Command: REVERT demo.txt v1"
cp ss_storage/demo.txt.checkpoint.v1 ss_storage/demo.txt
echo "File reverted to v1"
echo ""

echo "Step 10: Verify revert"
echo "Current file content (after revert):"
cat ss_storage/demo.txt
echo ""

echo "=== Demo Complete ==="
echo ""
echo "Checkpoint files created:"
ls -lh ss_storage/demo.txt*
echo ""
echo "To clean up: rm ss_storage/demo.txt*"
