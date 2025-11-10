#include "nm_hashmap.h"
#include "nm_logging.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Simple string hash function (djb2) for main map
 */
unsigned long hash_string(const char* str) {
    unsigned long hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c; // hash * 33 + c
    }
    return hash % HASH_MAP_SIZE;
}

/*
 * Inserts a FileLocation into the hash map.
 * "unsafe" - g_system_mutex must be held.
 */
void hash_map_insert_unsafe(FileLocation file) {
    unsigned long hash_index = hash_string(file.filename);
    
    HashNode* new_node = (HashNode*)malloc(sizeof(HashNode));
    if (new_node == NULL) {
        log_to_file("Internal", "NameServer", "CRITICAL: malloc failed for new hash map node.");
        return;
    }
    
    strncpy(new_node->key, file.filename, sizeof(new_node->key) - 1);
    new_node->file = file;
    
    // Insert at the front of the chain
    new_node->next = g_file_hash_map[hash_index];
    g_file_hash_map[hash_index] = new_node;
}

/*
 * Finds a FileLocation in the hash map by filename.
 * Returns a pointer to the FileLocation if found, NULL otherwise.
 * "unsafe" - g_system_mutex must be held.
 */
FileLocation* hash_map_find_unsafe(const char* filename) {
    unsigned long hash_index = hash_string(filename);
    HashNode* node = g_file_hash_map[hash_index];

    while (node != NULL) {
        if (strcmp(node->key, filename) == 0) {
            return &node->file; // Found it
        }
        node = node->next;
    }
    return NULL; // Not found
}

/*
 * Removes a file from the hash map by filename.
 * Returns 1 on success, 0 on failure (not found).
 * "unsafe" - g_system_mutex must be held.
 */
int hash_map_delete_unsafe(const char* filename) {
    unsigned long hash_index = hash_string(filename);
    HashNode* node = g_file_hash_map[hash_index];
    HashNode* prev = NULL;

    while (node != NULL) {
        if (strcmp(node->key, filename) == 0) {
            // Found it. Now delete it.
            if (prev == NULL) {
                // It's the first node in the chain
                g_file_hash_map[hash_index] = node->next;
            } else {
                // It's in the middle or at the end
                prev->next = node->next;
            }
            free(node);
            return 1; // Success
        }
        prev = node;
        node = node->next;
    }
    return 0; // Not found
}
