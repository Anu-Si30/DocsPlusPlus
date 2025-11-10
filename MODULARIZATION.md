# Code Modularization Guide

This document describes the modularization of the codebase into smaller, manageable modules.

## Directory Structure

```
course-project-be-creative/
├── nm_modules/           # Name Server modules
│   ├── nm_types.h        # Data structures and constants
│   ├── nm_hashmap.h/c    # Hash map implementation
│   ├── nm_cache.h/c      # LRU cache implementation
│   ├── nm_logging.h/c    # Logging functionality
│   └── nm_persistence.h/c # File persistence (save/load registry)
├── ss_modules/           # Storage Server modules
│   ├── ss_types.h        # Data structures and constants
│   ├── ss_document.h/c   # Document parsing and manipulation
│   └── ss_logging.h/c    # Logging functionality
├── nameserver.c          # Name Server main file (handlers + main)
├── storageserver.c       # Storage Server main file (handlers + main)
├── client.c              # Client (unchanged)
├── common.h              # Shared definitions
└── Makefile              # Updated to compile modular components

```

## Name Server Modules

### nm_types.h
- Defines all data structures: `FileLocation`, `StorageServer`, `ClientInfo`, `FileLock`
- Defines hash map and cache structures
- Contains all constants and defines

### nm_hashmap.c
- `hash_string()` - Hash function for filenames
- `hash_map_insert_unsafe()` - Insert file into hash map
- `hash_map_find_unsafe()` - Find file by filename
- `hash_map_delete_unsafe()` - Delete file from hash map

### nm_cache.c
- `cache_hash_string()` - Hash function for cache
- `cache_get_unsafe()` - Get item from cache (with LRU)
- `cache_put_unsafe()` - Put item into cache
- `cache_delete_unsafe()` - Delete item from cache
- Internal helper functions for LRU management

### nm_logging.c
- `log_to_file()` - Thread-safe logging to file

### nm_persistence.c
- `save_registry_to_disk_unsafe()` - Save file registry to disk
- `load_registry_from_disk()` - Load file registry from disk

### nameserver.c (remaining code)
- Global variable definitions
- Client request handlers (handle_view_files, handle_create_file, etc.)
- Storage server registration
- Main function and server loop

## Storage Server Modules

### ss_types.h
- Defines `WordNode` structure
- Contains constants (ports, directories, etc.)

### ss_document.c
- `create_word_node()` - Create word node
- `is_delimiter()` - Check sentence delimiters
- `free_document()` - Free document structure
- `parse_file_to_list()` - Parse file into linked list
- `flatten_list_to_file()` - Convert linked list back to file
- `get_sentence()` - Get sentence by index
- `insert_word_at()` - Insert word at position
- `get_file_metadata()` - Get file statistics

### ss_logging.c
- `log_to_file()` - Log to file only
- `log_message()` - Log to both file and terminal

### storageserver.c (remaining code)
- Global variable definitions
- Client request handlers (handle_client_request, etc.)
- Name server command handlers
- Main function and server loops

## Benefits of Modularization

1. **Better Organization**: Related functionality grouped together
2. **Easier Maintenance**: Smaller files are easier to understand and modify
3. **Reusability**: Modules can be reused in other projects
4. **Faster Compilation**: Only changed modules need recompilation
5. **Better Testing**: Individual modules can be tested separately
6. **Clear Dependencies**: Module headers clearly define interfaces

## Compilation

The Makefile has been updated to compile modules separately:

```makefile
# Compile Name Server
nameserver: nameserver.c $(NM_OBJS)
    $(CC) $(CFLAGS) nameserver.c $(NM_OBJS) -o nameserver $(LDFLAGS)

# Compile Storage Server
storageserver: storageserver.c $(SS_OBJS)
    $(CC) $(CFLAGS) storageserver.c $(SS_OBJS) -o storageserver $(LDFLAGS)
```

Modules are compiled to `.o` files first, then linked with the main files.

## Next Steps

The main source files (`nameserver.c` and `storageserver.c`) still contain inline definitions
of functions that are now in modules. To complete the modularization:

1. Remove duplicate function definitions from main files
2. Add `#include` directives for module headers
3. Keep only handler functions and main() in the main files

This ensures the codebase is truly modular while maintaining all functionality.
