#ifndef NM_HASHMAP_H
#define NM_HASHMAP_H

#include "nm_types.h"

/*
 * Hash Map Functions for File Directory
 * All functions marked "unsafe" require g_system_mutex to be held
 */

// Hash function for filenames
unsigned long hash_string(const char* str);

// Insert a file into the hash map
void hash_map_insert_unsafe(FileLocation file);

// Find a file in the hash map by filename
FileLocation* hash_map_find_unsafe(const char* filename);

// Delete a file from the hash map
int hash_map_delete_unsafe(const char* filename);

// External reference to the global hash map
extern HashNode* g_file_hash_map[];
extern pthread_mutex_t g_system_mutex;

#endif // NM_HASHMAP_H
