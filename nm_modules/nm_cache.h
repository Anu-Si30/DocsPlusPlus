#ifndef NM_CACHE_H
#define NM_CACHE_H

#include "nm_types.h"

/*
 * LRU Cache Functions for Name Server
 * All functions marked "unsafe" require g_system_mutex to be held
 */

// Hash function for cache
unsigned long cache_hash_string(const char* str);

// Move a cache node to the front (most recently used)
void cache_move_to_front_unsafe(CacheNode* node);

// Find a cache map entry
CacheMapEntry* cache_map_find_unsafe(const char* filename);

// Delete an entry from the cache hash map
void cache_map_delete_unsafe(const char* filename);

// Evict the least recently used item
void cache_evict_last_unsafe();

// Add a node to the front of the cache list
void cache_add_to_front_unsafe(CacheNode* node);

// Add an entry to the cache hash map
void cache_map_put_unsafe(const char* filename, CacheNode* node_ptr);

// Get an item from cache (returns NULL on miss)
FileLocation* cache_get_unsafe(const char* filename);

// Put an item into cache
void cache_put_unsafe(const char* filename, FileLocation* file_ptr);

// Delete an item from cache
void cache_delete_unsafe(const char* filename);

// External references to cache globals
extern CacheMapEntry* g_cache_map[];
extern CacheNode* g_cache_head;
extern CacheNode* g_cache_tail;
extern int g_cache_size;

#endif // NM_CACHE_H
