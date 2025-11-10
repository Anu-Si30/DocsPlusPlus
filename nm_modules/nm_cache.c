#include "nm_cache.h"
#include "nm_logging.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Simple string hash function (djb2) for cache map
 */
unsigned long cache_hash_string(const char* str) {
    unsigned long hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c;
    }
    return hash % CACHE_MAP_SIZE;
}

/*
 * Moves a given cache node to the front of the linked list (most recent).
 * "unsafe" - g_system_mutex must be held.
 */
void cache_move_to_front_unsafe(CacheNode* node) {
    if (node == g_cache_head) {
        return; // Already at front
    }

    // Unlink node
    if (node->prev) node->prev->next = node->next;
    if (node->next) node->next->prev = node->prev;

    if (node == g_cache_tail && node->prev) {
        g_cache_tail = node->prev;
    }

    // Link to front
    node->next = g_cache_head;
    node->prev = NULL;
    if (g_cache_head) {
        g_cache_head->prev = node;
    }
    g_cache_head = node;

    if (g_cache_tail == NULL) {
        g_cache_tail = node;
    }
}

/*
 * Finds a cache map entry.
 * "unsafe" - g_system_mutex must be held.
 */
CacheMapEntry* cache_map_find_unsafe(const char* filename) {
    unsigned long hash_index = cache_hash_string(filename);
    CacheMapEntry* entry = g_cache_map[hash_index];
    while (entry != NULL) {
        if (strcmp(entry->filename, filename) == 0) {
            return entry;
        }
        entry = entry->next;
    }
    return NULL;
}

/*
 * Deletes an entry from the cache hash map.
 * "unsafe" - g_system_mutex must be held.
 */
void cache_map_delete_unsafe(const char* filename) {
    unsigned long hash_index = cache_hash_string(filename);
    CacheMapEntry* entry = g_cache_map[hash_index];
    CacheMapEntry* prev = NULL;

    while (entry != NULL) {
        if (strcmp(entry->filename, filename) == 0) {
            if (prev == NULL) {
                g_cache_map[hash_index] = entry->next;
            } else {
                prev->next = entry->next;
            }
            free(entry);
            return;
        }
        prev = entry;
        entry = entry->next;
    }
}

/*
 * Evicts the least recently used item (tail) from the cache.
 * "unsafe" - g_system_mutex must be held.
 */
void cache_evict_last_unsafe() {
    if (g_cache_tail == NULL) {
        return; // Cache is empty
    }

    CacheNode* to_delete = g_cache_tail;
    
    // Update tail pointer
    g_cache_tail = to_delete->prev;
    if (g_cache_tail) {
        g_cache_tail->next = NULL;
    } else {
        g_cache_head = NULL; // Cache is now empty
    }

    // Remove from cache hash map
    cache_map_delete_unsafe(to_delete->filename);

    // Free the node
    free(to_delete);
    g_cache_size--;
    
    log_to_file("Internal", "NameServer", "CACHE: Evicted item from cache.");
}

/*
 * Adds a new node to the front of the cache list.
 * "unsafe" - g_system_mutex must be held.
 */
void cache_add_to_front_unsafe(CacheNode* node) {
    node->next = g_cache_head;
    node->prev = NULL;
    if (g_cache_head) {
        g_cache_head->prev = node;
    }
    g_cache_head = node;
    if (g_cache_tail == NULL) {
        g_cache_tail = node;
    }
}

/*
 * Adds a new entry to the cache hash map.
 * "unsafe" - g_system_mutex must be held.
 */
void cache_map_put_unsafe(const char* filename, CacheNode* node_ptr) {
    unsigned long hash_index = cache_hash_string(filename);
    CacheMapEntry* new_entry = (CacheMapEntry*)malloc(sizeof(CacheMapEntry));
    if (new_entry == NULL) {
        log_to_file("Internal", "NameServer", "CRITICAL: malloc failed for new cache map entry.");
        return;
    }
    strncpy(new_entry->filename, filename, sizeof(new_entry->filename) - 1);
    new_entry->node_ptr = node_ptr;
    
    // Insert at front of chain
    new_entry->next = g_cache_map[hash_index];
    g_cache_map[hash_index] = new_entry;
}

/*
 * Gets an item from the cache. Returns pointer if hit, NULL if miss.
 * Moves the item to the front of the list if hit.
 * "unsafe" - g_system_mutex must be held.
 */
FileLocation* cache_get_unsafe(const char* filename) {
    CacheMapEntry* entry = cache_map_find_unsafe(filename);
    if (entry != NULL) {
        // Cache Hit!
        log_to_file("Internal", "NameServer", "CACHE: HIT for '%s'", filename);
        cache_move_to_front_unsafe(entry->node_ptr);
        return entry->node_ptr->file_ptr;
    }
    
    // Cache Miss
    log_to_file("Internal", "NameServer", "CACHE: MISS for '%s'", filename);
    return NULL;
}

/*
 * Puts an item into the cache.
 * "unsafe" - g_system_mutex must be held.
 */
void cache_put_unsafe(const char* filename, FileLocation* file_ptr) {
    CacheMapEntry* entry = cache_map_find_unsafe(filename);
    if (entry != NULL) {
        // Already in cache, just move to front
        cache_move_to_front_unsafe(entry->node_ptr);
        return;
    }

    // Not in cache, add new entry
    if (g_cache_size == MAX_CACHE_SIZE) {
        cache_evict_last_unsafe();
    }

    // Create new list node
    CacheNode* new_node = (CacheNode*)malloc(sizeof(CacheNode));
    if (new_node == NULL) {
        log_to_file("Internal", "NameServer", "CRITICAL: malloc failed for new cache node.");
        return;
    }
    strncpy(new_node->filename, filename, sizeof(new_node->filename) - 1);
    new_node->file_ptr = file_ptr;

    cache_add_to_front_unsafe(new_node);
    cache_map_put_unsafe(filename, new_node);
    g_cache_size++;
    log_to_file("Internal", "NameServer", "CACHE: ADD for '%s'", filename);
}

/*
 * Deletes an item from the cache (e.g., when file is deleted).
 * "unsafe" - g_system_mutex must be held.
 */
void cache_delete_unsafe(const char* filename) {
    CacheMapEntry* entry = cache_map_find_unsafe(filename);
    if (entry != NULL) {
        CacheNode* node_to_delete = entry->node_ptr;
        
        // Unlink from list
        if (node_to_delete->prev) node_to_delete->prev->next = node_to_delete->next;
        if (node_to_delete->next) node_to_delete->next->prev = node_to_delete->prev;
        if (g_cache_head == node_to_delete) g_cache_head = node_to_delete->next;
        if (g_cache_tail == node_to_delete) g_cache_tail = node_to_delete->prev;

        // Delete from map
        cache_map_delete_unsafe(filename);
        
        // Free node
        free(node_to_delete);
        g_cache_size--;
        log_to_file("Internal", "NameServer", "CACHE: DELETE for '%s'", filename);
    }
}
