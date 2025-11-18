# Checkpoint Functionality Test Guide

## Overview
The checkpoint mechanism allows users to save the state of a file at specific points in time and revert to these checkpoints if needed.

## Commands Implemented

### 1. CHECKPOINT
Creates a checkpoint with the given tag.
```
CHECKPOINT <filename> <checkpoint_tag>
```

**Example:**
```
CHECKPOINT myfile.txt v1
CHECKPOINT myfile.txt backup_before_changes
```

**Storage Format:**
- Checkpoints are stored as: `ss_storage/<filename>.checkpoint.<tag>`
- For files in subdirectories: `ss_storage/<dir>/<basename>.checkpoint.<tag>`

### 2. LISTCHECKPOINTS
Lists all checkpoints for the specified file.
```
LISTCHECKPOINTS <filename>
```

**Example:**
```
LISTCHECKPOINTS myfile.txt
```

**Output Format:**
```
Checkpoints for myfile.txt:
  - v1
  - v2
  - backup_before_changes
```

### 3. VIEWCHECKPOINT
Views the content of the specified checkpoint.
```
VIEWCHECKPOINT <filename> <checkpoint_tag>
```

**Example:**
```
VIEWCHECKPOINT myfile.txt v1
```

**Output Format:**
```
Checkpoint 'v1' content:
---
[checkpoint file content here]
---
```

### 4. REVERT
Reverts the file to the specified checkpoint.
```
REVERT <filename> <checkpoint_tag>
```

**Example:**
```
REVERT myfile.txt v1
```

## Implementation Details

### Helper Functions

1. **copy_file(src_path, dest_path)**
   - Copies file content from source to destination
   - Uses 4KB buffer for efficient copying
   - Returns 0 on success, -1 on failure

2. **get_checkpoint_path(filename, tag, checkpoint_path, size)**
   - Constructs the checkpoint file path
   - Handles both root files and files in subdirectories
   - Format: `ss_storage/<path>/<basename>.checkpoint.<tag>`

### Response Codes

| Command | Success Response | Failure Response |
|---------|-----------------|------------------|
| CHECKPOINT | ACK_CHECKPOINT_SUCCESS | ACK_CHECKPOINT_FAIL |
| LISTCHECKPOINTS | (checkpoint list) | ACK_LISTCHECKPOINTS_FAIL |
| VIEWCHECKPOINT | (checkpoint content) | ACK_VIEWCHECKPOINT_FAIL |
| REVERT | ACK_REVERT_SUCCESS | ACK_REVERT_FAIL |

## Usage Workflow

### Typical Workflow:
1. Create initial checkpoint: `CHECKPOINT myfile.txt initial`
2. Make changes to the file
3. Create another checkpoint: `CHECKPOINT myfile.txt after_edit`
4. Make more changes
5. List checkpoints: `LISTCHECKPOINTS myfile.txt`
6. View a checkpoint: `VIEWCHECKPOINT myfile.txt initial`
7. Revert if needed: `REVERT myfile.txt initial`

### Example Session:
```bash
# Create a test file
echo "Version 1 content" > ss_storage/test.txt

# Create checkpoint v1
CHECKPOINT test.txt v1

# Modify file
echo "Version 2 content" > ss_storage/test.txt

# Create checkpoint v2
CHECKPOINT test.txt v2

# List all checkpoints
LISTCHECKPOINTS test.txt
# Output: Checkpoints for test.txt:
#   - v1
#   - v2

# View checkpoint v1
VIEWCHECKPOINT test.txt v1
# Output: Checkpoint 'v1' content:
# ---
# Version 1 content
# ---

# Revert to v1
REVERT test.txt v1

# File now contains: "Version 1 content"
```

## Features

✅ **Multiple Checkpoints**: Create unlimited checkpoints per file with different tags  
✅ **Nested Files**: Works with files in subdirectories  
✅ **View Before Revert**: Inspect checkpoint content before reverting  
✅ **List All Checkpoints**: See all available checkpoints for a file  
✅ **Safe Operations**: Validates file and checkpoint existence before operations  
✅ **Detailed Logging**: All checkpoint operations are logged  

## Error Handling

- **File doesn't exist**: Returns failure when trying to checkpoint a non-existent file
- **Checkpoint doesn't exist**: Returns failure when viewing/reverting to non-existent checkpoint
- **Directory access issues**: Returns failure if cannot open directory for listing
- **Copy failures**: Returns failure if file copy operation fails
- **Invalid format**: Returns failure if command format is incorrect

## Notes

- Checkpoint tags can be any string without spaces (e.g., v1, backup1, before_merge)
- Checkpoints are stored in the same directory as the original file
- No automatic cleanup of old checkpoints (manual deletion required)
- Large files will create large checkpoint copies (no compression)
