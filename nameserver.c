#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>
#include <time.h>     
#include <stdarg.h>  // For va_list
#include "common.h"

/*
 * --- Global Data Structures ---
 */

typedef struct {
    char username[256];
    char permission; // 'R' for Read, 'W' for Read/Write
} AccessControl;

#define MAX_PERMISSIONS 10 

typedef struct {
    char filename[256];
    char owner_username[256];        
    AccessControl acl[MAX_PERMISSIONS]; 
    int num_permissions;                

    char last_accessed_by[256]; 
    char last_accessed_ts[128]; 

    int ss_client_port;      
    char ss_ip_addr[INET_ADDRSTRLEN]; 
} FileLocation;


typedef struct {
    int ss_nm_port;          
    int ss_client_port;      
    char ss_ip_addr[INET_ADDRSTRLEN];
} StorageServer;

typedef struct {
    char username[256];
    char ip_addr[INET_ADDRSTRLEN];
    int client_socket_fd; 
} ClientInfo;

typedef struct {
    char filename[256];
    int sentence_num;
    char username[256]; // Who holds the lock
} FileLock;

// --- **** NEW: HASH MAP STRUCTURES **** ---
typedef struct HashNode {
    char key[256];             // The filename
    FileLocation file;         // The FileLocation struct
    struct HashNode* next;     // Pointer to the next node in the chain
} HashNode;

// --- **** NEW: LRU CACHE STRUCTURES **** ---
typedef struct CacheNode {
    char filename[256];
    FileLocation* file_ptr;     // Pointer to the FileLocation in the main hash map
    struct CacheNode* prev;
    struct CacheNode* next;
} CacheNode;

typedef struct CacheMapEntry {
    char filename[256];
    CacheNode* node_ptr;        // Pointer to the node in the linked list
    struct CacheMapEntry* next;
} CacheMapEntry;

#define HASH_MAP_SIZE 100 // Size of the main file directory hash map
#define CACHE_MAP_SIZE 20  // A smaller hash map for the cache
#define MAX_CACHE_SIZE 10  // Max 10 items in the LRU cache

// --- **** Global Defines **** ---
#define MAX_SERVERS 10
#define MAX_CLIENTS 50 
#define MAX_LOCKS 50
#define EXEC_OUTPUT_BUFFER_SIZE 8192 
#define NM_REGISTRY_FILE "nm_registry.dat"
#define NM_LOG_FILE "nameserver.log"

// --- **** Global Variables **** ---
HashNode* g_file_hash_map[HASH_MAP_SIZE]; // The main file directory
StorageServer server_list[MAX_SERVERS];
ClientInfo client_list[MAX_CLIENTS]; 
FileLock g_file_locks[MAX_LOCKS];
int g_num_servers = 0;
int g_num_clients = 0; 
int g_num_locks = 0;

// --- **** NEW: LRU CACHE GLOBALS **** ---
CacheMapEntry* g_cache_map[CACHE_MAP_SIZE];
CacheNode* g_cache_head = NULL;
CacheNode* g_cache_tail = NULL;
int g_cache_size = 0;

// --- **** Global Mutexes **** ---
pthread_mutex_t g_system_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t g_log_mutex = PTHREAD_MUTEX_INITIALIZER; 


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
 * Writes a formatted, detailed message ONLY to the log file.
 * This function is thread-safe.
 */
void log_to_file(const char* client_addr, const char* username, const char* format, ...) {
    time_t now = time(NULL);
    char time_buf[64];
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", localtime(&now));

    va_list args;
    va_start(args, format);

    pthread_mutex_lock(&g_log_mutex);
    
    FILE* log_file = fopen(NM_LOG_FILE, "a");
    if (log_file) {
        // [Timestamp] [Client: IP:Port] [User: Username] Message
        fprintf(log_file, "[%s] [Client: %s] [User: %s] ", time_buf, client_addr, username);
        vfprintf(log_file, format, args);
        fprintf(log_file, "\n");
        fclose(log_file);
    }
    
    pthread_mutex_unlock(&g_log_mutex);
    va_end(args);
}

// --- **** HASH MAP HELPER FUNCTIONS **** ---

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


// --- **** LRU CACHE HELPER FUNCTIONS **** ---

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

// --- **** PERSISTENCE FUNCTIONS (MODIFIED FOR HASH MAP) **** ---

/*
 * Saves the current file directory state to disk.
 * "unsafe" - g_system_mutex must be held.
 */
void save_registry_to_disk_unsafe() {
    FILE* file = fopen(NM_REGISTRY_FILE, "wb"); // "wb" = Write Binary
    if (file == NULL) {
        perror("NM: (Persistence) Failed to open registry for saving");
        log_to_file("Internal", "NameServer", "CRITICAL: (Persistence) Failed to open registry for saving: %m");
        return;
    }

    // 1. Count the number of files
    int g_num_files = 0;
    for (int i = 0; i < HASH_MAP_SIZE; i++) {
        HashNode* node = g_file_hash_map[i];
        while (node != NULL) {
            g_num_files++;
            node = node->next;
        }
    }

    // 2. Write the count
    if (fwrite(&g_num_files, sizeof(int), 1, file) != 1) {
        perror("NM: (Persistence) Failed to write file count to registry");
        log_to_file("Internal", "NameServer", "CRITICAL: (Persistence) Failed to write file count to registry: %m");
        fclose(file);
        return;
    }

    // 3. Write each FileLocation struct
    if (g_num_files > 0) {
        for (int i = 0; i < HASH_MAP_SIZE; i++) {
            HashNode* node = g_file_hash_map[i];
            while (node != NULL) {
                if (fwrite(&node->file, sizeof(FileLocation), 1, file) != 1) {
                    perror("NM: (Persistence) Failed to write file data to registry");
                    log_to_file("Internal", "NameServer", "CRITICAL: (Persistence) Failed to write file data to registry: %m");
                    fclose(file);
                    return;
                }
                node = node->next;
            }
        }
    }

    fclose(file);
    printf("NM: (Persistence) System state saved to disk (%d files).\n", g_num_files);
    log_to_file("Internal", "NameServer", "INFO: (Persistence) System state saved to disk (%d files).", g_num_files);
}

/*
 * Loads the file directory state from disk on startup.
 * Runs before any threads, so no mutex is needed.
 */
void load_registry_from_disk() {
    FILE* file = fopen(NM_REGISTRY_FILE, "rb"); // "rb" = Read Binary
    if (file == NULL) {
        printf("NM: (Persistence) No existing registry file found. Starting fresh.\n");
        log_to_file("Internal", "NameServer", "INFO: (Persistence) No existing registry file found. Starting fresh.");
        return;
    }

    int g_num_files = 0;
    if (fread(&g_num_files, sizeof(int), 1, file) != 1) {
        perror("NM: (Persistence) Failed to read file count. Starting fresh");
        log_to_file("Internal", "NameServer", "ERROR: (Persistence) Failed to read file count. Starting fresh.");
        fclose(file);
        return;
    }

    if (g_num_files < 0) {
        printf("NM: (Persistence) Registry file corrupted (count=%d). Starting fresh.\n", g_num_files);
        log_to_file("Internal", "NameServer", "ERROR: (Persistence) Registry file corrupted (count=%d). Starting fresh.", g_num_files);
        fclose(file);
        return;
    }

    // Read each FileLocation and insert it into the hash map
    for (int i = 0; i < g_num_files; i++) {
        FileLocation temp_file;
        if (fread(&temp_file, sizeof(FileLocation), 1, file) != 1) {
            perror("NM: (Persistence) Failed to read file data. Halting load");
            log_to_file("Internal", "NameServer", "ERROR: (Persistence) Failed to read file data. Halting load.");
            break; // Stop loading, but keep what we have
        }
        hash_map_insert_unsafe(temp_file);
    }

    fclose(file);
    printf("NM: (Persistence) Successfully loaded %d files from registry.\n", g_num_files);
    log_to_file("Internal", "NameServer", "INFO: (Persistence) Successfully loaded %d files from registry.", g_num_files);
}


// --- (SS Forwarding Helper Functions - UNCHANGED) ---

int forward_create_to_ss(const char* ss_ip, int ss_nm_port, const char* filename) {
    int ss_sock;
    struct sockaddr_in ss_addr;
    char command[BUFFER_SIZE];
    char response[BUFFER_SIZE];

    if ((ss_sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("NM (SS-Client): socket failed");
        return -1;
    }
    ss_addr.sin_family = AF_INET;
    ss_addr.sin_port = htons(ss_nm_port);
    if (inet_pton(AF_INET, ss_ip, &ss_addr.sin_addr) <= 0) {
        perror("NM (SS-Client): invalid address");
        close(ss_sock);
        return -1;
    }
    if (connect(ss_sock, (struct sockaddr*)&ss_addr, sizeof(ss_addr)) < 0) {
        perror("NM (SS-Client): connect to SS failed");
        close(ss_sock);
        return -1;
    }
    snprintf(command, sizeof(command), "CREATE_FILE %s\n", filename);
    if (write(ss_sock, command, strlen(command)) < 0) {
        perror("NM (SS-Client): write to SS failed");
        close(ss_sock);
        return -1;
    }
    ssize_t bytes_read = read(ss_sock, response, sizeof(response) - 1);
    close(ss_sock); 
    if (bytes_read <= 0) {
        printf("NM (SS-Client): No response from SS.\n");
        return -1;
    }
    response[bytes_read] = '\0';
    if (strncmp(response, "ACK_CREATE_SUCCESS", 18) == 0) {
        return 0; 
    } else {
        return -1; 
    }
}

int forward_delete_to_ss(const char* ss_ip, int ss_nm_port, const char* filename) {
    int ss_sock;
    struct sockaddr_in ss_addr;
    char command[BUFFER_SIZE];
    char response[BUFFER_SIZE];

    if ((ss_sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("NM (SS-Client): socket failed");
        return -1;
    }
    ss_addr.sin_family = AF_INET;
    ss_addr.sin_port = htons(ss_nm_port);
    if (inet_pton(AF_INET, ss_ip, &ss_addr.sin_addr) <= 0) {
        perror("NM (SS-Client): invalid address");
        close(ss_sock);
        return -1;
    }
    if (connect(ss_sock, (struct sockaddr*)&ss_addr, sizeof(ss_addr)) < 0) {
        perror("NM (SS-Client): connect to SS failed");
        close(ss_sock);
        return -1;
    }
    snprintf(command, sizeof(command), "DELETE_FILE %s\n", filename);
    if (write(ss_sock, command, strlen(command)) < 0) {
        perror("NM (SS-Client): write to SS failed");
        close(ss_sock);
        return -1;
    }
    ssize_t bytes_read = read(ss_sock, response, sizeof(response) - 1);
    close(ss_sock); 
    if (bytes_read <= 0) {
        printf("NM (SS-Client): No response from SS.\n");
        return -1;
    }
    response[bytes_read] = '\0';
    if (strncmp(response, "ACK_DELETE_SUCCESS", 18) == 0) {
        return 0; 
    } else {
        return -1; 
    }
}

int get_metadata_from_ss(const char* ss_ip, int ss_nm_port, const char* filename,
                         int* out_words, int* out_chars, 
                         char* out_created_ts, char* out_modified_ts, int ts_len) 
{
    int ss_sock;
    struct sockaddr_in ss_addr;
    char command[BUFFER_SIZE];
    char response[BUFFER_SIZE];

    if ((ss_sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) { /* ... */ return -1; }
    ss_addr.sin_family = AF_INET;
    ss_addr.sin_port = htons(ss_nm_port);
    if (inet_pton(AF_INET, ss_ip, &ss_addr.sin_addr) <= 0) { /* ... */ close(ss_sock); return -1; }
    if (connect(ss_sock, (struct sockaddr*)&ss_addr, sizeof(ss_addr)) < 0) { /* ... */ close(ss_sock); return -1; }
    
    snprintf(command, sizeof(command), "GET_METADATA %s\n", filename);
    if (write(ss_sock, command, strlen(command)) < 0) { /* ... */ close(ss_sock); return -1; }
    
    ssize_t bytes_read = read(ss_sock, response, sizeof(response) - 1);
    close(ss_sock); 
    
    if (bytes_read <= 0) { /* ... */ return -1; }
    response[bytes_read] = '\0';
    
    if (strncmp(response, "METADATA_RESPONSE", 17) == 0) {
        char created_date[64], created_time[64];
        char modified_date[64], modified_time[64];
        
        int items = sscanf(response, "METADATA_RESPONSE %d %d %63s %63s %63s %63s", 
                 out_words, out_chars, 
                 created_date, created_time,
                 modified_date, modified_time);
        
        if (items != 6) { 
            printf("NM: Failed to parse metadata response: %s\n", response);
            return -1;
        }
        
        snprintf(out_created_ts, ts_len, "%s %s", created_date, created_time);
        snprintf(out_modified_ts, ts_len, "%s %s", modified_date, modified_time);
        
        return 0; // Success
    } else {
        return -1; // Failure
    }
}

int get_file_content_from_ss(const char* ss_ip, int ss_client_port, const char* filename, char* out_buffer, int out_len) {
    int ss_sock;
    struct sockaddr_in ss_addr;
    char command[BUFFER_SIZE];

    if ((ss_sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) { 
        perror("NM (SS-Client): socket failed");
        return -1; 
    }
    ss_addr.sin_family = AF_INET;
    ss_addr.sin_port = htons(ss_client_port);
    if (inet_pton(AF_INET, ss_ip, &ss_addr.sin_addr) <= 0) { 
        perror("NM (SS-Client): invalid address");
        close(ss_sock); 
        return -1; 
    }
    if (connect(ss_sock, (struct sockaddr*)&ss_addr, sizeof(ss_addr)) < 0) { 
        perror("NM (SS-Client): connect to SS failed");
        close(ss_sock); 
        return -1; 
    }
    
    snprintf(command, sizeof(command), "READ_FILE %s\n", filename);
    if (write(ss_sock, command, strlen(command)) < 0) { 
        perror("NM (SS-Client): write to SS failed");
        close(ss_sock); 
        return -1; 
    }

    ssize_t total_bytes_read = 0;
    ssize_t bytes_read;
    while (total_bytes_read < out_len - 1 && 
           (bytes_read = read(ss_sock, out_buffer + total_bytes_read, out_len - 1 - total_bytes_read)) > 0) {
        total_bytes_read += bytes_read;
    }
    
    out_buffer[total_bytes_read] = '\0'; // Null-terminate
    close(ss_sock);
    
    if (bytes_read < 0) {
        perror("NM (SS-Client): read file content failed");
        return -1;
    }
    
    return 0; // Success
}

int forward_undo_to_ss(const char* ss_ip, int ss_nm_port, const char* filename) {
    int ss_sock;
    struct sockaddr_in ss_addr;
    char command[BUFFER_SIZE];
    char response[BUFFER_SIZE];

    if ((ss_sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("NM (SS-Client-Undo): socket failed");
        return -1;
    }
    ss_addr.sin_family = AF_INET;
    ss_addr.sin_port = htons(ss_nm_port);
    if (inet_pton(AF_INET, ss_ip, &ss_addr.sin_addr) <= 0) {
        perror("NM (SS-Client-Undo): invalid address");
        close(ss_sock);
        return -1;
    }
    if (connect(ss_sock, (struct sockaddr*)&ss_addr, sizeof(ss_addr)) < 0) {
        perror("NM (SS-Client-Undo): connect to SS failed");
        close(ss_sock);
        return -1;
    }
    
    snprintf(command, sizeof(command), "UNDO_FILE %s\n", filename);
    if (write(ss_sock, command, strlen(command)) < 0) {
        perror("NM (SS-Client-Undo): write to SS failed");
        close(ss_sock);
        return -1;
    }
    
    ssize_t bytes_read = read(ss_sock, response, sizeof(response) - 1);
    close(ss_sock); 
    
    if (bytes_read <= 0) {
        printf("NM (SS-Client-Undo): No response from SS.\n");
        return -1;
    }
    response[bytes_read] = '\0';
    
    if (strncmp(response, "ACK_UNDO_SUCCESS", 16) == 0) {
        return 0; // Success
    } else if (strncmp(response, "ACK_UNDO_FAIL_NO_BAK", 20) == 0) {
        return -2; // No backup file
    } else {
        return -1; // Other failure
    }
}

// --- (Permission & Time Helpers - UNCHANGED) ---

int check_permission(FileLocation* file, const char* user_requesting, char permission_needed) {
    if (strcmp(file->owner_username, user_requesting) == 0) {
        return 1; 
    }
    for (int i = 0; i < file->num_permissions; i++) {
        if (strcmp(file->acl[i].username, user_requesting) == 0) {
            if (file->acl[i].permission == 'W') {
                return 1; 
            }
            if (file->acl[i].permission == 'R' && permission_needed == 'R') {
                return 1; 
            }
        }
    }
    return 0;
}


void get_current_timestamp(char* buffer, int len) {
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    strftime(buffer, len, "%Y-%m-%d %H:%M", tm_info);
}

// ---------------------------------------

/*
 * This is the function that each thread will run.
 */
void* handle_connection(void* arg) {
    int client_socket = *((int*)arg);
    free(arg); 
    
    struct sockaddr_in peer_addr;
    socklen_t peer_len = sizeof(peer_addr);
    getpeername(client_socket, (struct sockaddr*)&peer_addr, &peer_len);
    
    char peer_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &peer_addr.sin_addr, peer_ip, sizeof(peer_ip));
    int peer_port = ntohs(peer_addr.sin_port);

    char client_addr_str[INET_ADDRSTRLEN + 7]; // IP + ':' + 5-digit port + null
    snprintf(client_addr_str, sizeof(client_addr_str), "%s:%d", peer_ip, peer_port);
    
    char err_msg[256]; // Buffer for sending error messages
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read;

    bytes_read = read(client_socket, buffer, sizeof(buffer) - 1);
    if (bytes_read <= 0) {
        printf("Connection from %s closed before identification.\n", client_addr_str);
        close(client_socket);
        return NULL;
    }
    buffer[bytes_read] = '\0'; 

    // 3. Identify the connection type
    if (strncmp(buffer, "REGISTER_SS", 11) == 0) {
        printf("New connection from a Storage Server (%s).\n", client_addr_str);
        log_to_file(client_addr_str, "StorageServer", "REQ: REGISTER_SS");
        
        int ss_nm_port, ss_client_port;
        char* token;
        char* rest = buffer;
        token = strtok_r(rest, " \n", &rest); 
        token = strtok_r(NULL, " \n", &rest); 
        ss_nm_port = token ? atoi(token) : -1;
        token = strtok_r(NULL, " \n", &rest); 
        ss_client_port = token ? atoi(token) : -1;

        if (ss_nm_port <= 0 || ss_client_port <= 0) {
            printf("Failed to parse SS registration message.\n");
            log_to_file(client_addr_str, "StorageServer", "RES: Failed to parse SS registration message.");
            close(client_socket);
            return NULL;
        }

        pthread_mutex_lock(&g_system_mutex);
        
        if (g_num_servers < MAX_SERVERS) {
            StorageServer* new_ss = &server_list[g_num_servers++];
            new_ss->ss_nm_port = ss_nm_port;
            new_ss->ss_client_port = ss_client_port;
            strncpy(new_ss->ss_ip_addr, peer_ip, INET_ADDRSTRLEN);
            printf("  Registered SS (NM Port %d, Client Port %d)\n", ss_nm_port, ss_client_port);
            log_to_file(client_addr_str, "StorageServer", "INFO: Registered SS (NM Port %d, Client Port %d)", ss_nm_port, ss_client_port);
        }

        while ((token = strtok_r(NULL, " \n", &rest)) != NULL) {
            FileLocation* file = hash_map_find_unsafe(token);
            if (file != NULL) {
                file->ss_client_port = ss_client_port;
                strncpy(file->ss_ip_addr, peer_ip, INET_ADDRSTRLEN);
                printf("  Re-linking existing file: %s\n", token);
                log_to_file(client_addr_str, "StorageServer", "INFO: Re-linking existing file '%s'.", token);
            } else {
                 printf("  SS registered new non-persistent file: %s\n", token);
                 log_to_file(client_addr_str, "StorageServer", "INFO: Registered new non-persistent file: %s", token);
            }
        }
        
        pthread_mutex_unlock(&g_system_mutex);
        
        close(client_socket);
        printf("Storage Server registration complete. Connection closed.\n");
        log_to_file(client_addr_str, "StorageServer", "INFO: Registration complete. Connection closed.");
        return NULL; 
        
    } else if (strncmp(buffer, "REGISTER_CLIENT", 15) == 0) {
        char username[256]; 
        if (sscanf(buffer, "REGISTER_CLIENT %s", username) != 1) {
            printf("  Failed to parse username. Closing connection.\n");
            log_to_file(client_addr_str, "Unknown", "ERROR: Failed to parse username from REGISTER_CLIENT.");
            close(client_socket);
            return NULL;
        }
        username[255] = '\0'; 
        printf("New connection from a Client (%s), user '%s'.\n", client_addr_str, username);
        log_to_file(client_addr_str, username, "REQ: REGISTER_CLIENT");

        pthread_mutex_lock(&g_system_mutex);
        if (g_num_clients < MAX_CLIENTS) {
            ClientInfo* new_client = &client_list[g_num_clients++];
            strncpy(new_client->username, username, 255);
            new_client->username[255] = '\0';
            strncpy(new_client->ip_addr, peer_ip, INET_ADDRSTRLEN);
            new_client->client_socket_fd = client_socket;
            printf("  User '%s' registered from %s.\n", username, client_addr_str);
            log_to_file(client_addr_str, username, "RES: Client registration successful.");
        } else {
            printf("  Max clients reached. Rejecting %s.\n", username);
            log_to_file(client_addr_str, username, "RES: Client registration failed: Max clients reached.");
        }
        pthread_mutex_unlock(&g_system_mutex);

        // 4. Enter a loop to handle commands from *this* client
        while ((bytes_read = read(client_socket, buffer, sizeof(buffer) - 1)) > 0) {
            buffer[bytes_read] = '\0';
            buffer[strcspn(buffer, "\n")] = 0; // Remove trailing newline

            if (strncmp(buffer, "VIEW", 4) == 0) {
                printf("Client requested VIEW\n");
                log_to_file(client_addr_str, username, "REQ: VIEW");
                
                int all_flag = 0;
                int long_flag = 0;
                if (strstr(buffer, "-al") != NULL || strstr(buffer, "-la") != NULL) {
                    all_flag = 1;
                    long_flag = 1;
                } else if (strstr(buffer, "-a") != NULL) {
                    all_flag = 1;
                } else if (strstr(buffer, "-l") != NULL) {
                    long_flag = 1;
                }
                
                char response_buffer[BUFFER_SIZE] = {0};
                int offset = 0;
                int files_shown = 0;

                if (long_flag) {
                    offset += sprintf(response_buffer,
                        "| %-20s | %5s | %5s | %-16s | %-10s |\n",
                        "Filename", "Words", "Chars", "Last Access Time", "Owner");
                    offset += sprintf(response_buffer + offset,
                        "|----------------------|-------|-------|------------------|------------|\n");
                }

                pthread_mutex_lock(&g_system_mutex);

                // Iterate over hash map
                for (int i = 0; i < HASH_MAP_SIZE; i++) {
                    HashNode* node = g_file_hash_map[i];
                    while (node != NULL) {
                        FileLocation* file = &node->file;
                        int has_permission = check_permission(file, username, 'R');
                        
                        if (all_flag || has_permission) {
                            if (long_flag) {
                                int words = 0, chars = 0;
                                char created_ts[128], modified_ts[128]; 
                                StorageServer* ss = NULL;
                                for (int j = 0; j < g_num_servers; j++) {
                                    if (strcmp(server_list[j].ss_ip_addr, file->ss_ip_addr) == 0 &&
                                        server_list[j].ss_client_port == file->ss_client_port) {
                                        ss = &server_list[j];
                                        break;
                                    }
                                }
                                
                                if (ss != NULL) {
                                    pthread_mutex_unlock(&g_system_mutex);
                                    get_metadata_from_ss(ss->ss_ip_addr, ss->ss_nm_port, file->filename, 
                                                                   &words, &chars, created_ts, modified_ts, 128);
                                    pthread_mutex_lock(&g_system_mutex);
                                }
                                
                                offset += sprintf(response_buffer + offset,
                                    "| %-20s | %5d | %5d | %-16s | %-10s |\n",
                                    file->filename, words, chars, 
                                    file->last_accessed_ts, 
                                    file->owner_username);
                                
                            } else {
                                offset += sprintf(response_buffer + offset, "%s\n", file->filename);
                            }
                            files_shown++;
                        }

                        if (offset > BUFFER_SIZE - 256) {
                            write(client_socket, response_buffer, offset);
                            offset = 0; 
                        }
                        node = node->next;
                    }
                }
                
                pthread_mutex_unlock(&g_system_mutex);
                
                if (files_shown == 0 && offset == 0) {
                    offset += sprintf(response_buffer + offset, "No files to display.\n");
                }
                
                if (offset > 0) {
                    write(client_socket, response_buffer, offset);
                }
                log_to_file(client_addr_str, username, "RES: VIEW success. Sent %d files.", files_shown);
            
            } else if (strncmp(buffer, "READ", 4) == 0) {
                char filename[256];
                if (sscanf(buffer, "READ %s", filename) != 1) {
                    snprintf(err_msg, sizeof(err_msg), "ERROR: Invalid READ format. Use: READ <filename>\n");
                    write(client_socket, err_msg, strlen(err_msg));
                    continue;
                }
                printf("Client requested READ for '%s'\n", filename);
                log_to_file(client_addr_str, username, "REQ: READ for '%s'", filename);

                char ss_ip[INET_ADDRSTRLEN];
                int ss_port;
                int permitted = 0; 
                FileLocation* file = NULL;

                pthread_mutex_lock(&g_system_mutex);
                
                file = cache_get_unsafe(filename); // Check cache first
                if (file == NULL) {
                    file = hash_map_find_unsafe(filename); // Check main map
                    if (file != NULL) {
                        cache_put_unsafe(filename, file); // Add to cache
                    }
                }
                
                if (file != NULL) {
                    if (check_permission(file, username, 'R')) {
                        permitted = 1;
                        strncpy(ss_ip, file->ss_ip_addr, INET_ADDRSTRLEN);
                        ss_port = file->ss_client_port;
                        get_current_timestamp(file->last_accessed_ts, 128);
                        strncpy(file->last_accessed_by, username, 255);
                    }
                }
                pthread_mutex_unlock(&g_system_mutex);

                char response_buffer[BUFFER_SIZE];
                if (file == NULL) {
                    printf("  File not found.\n");
                    log_to_file(client_addr_str, username, "RES: READ for '%s' failed: File not found.", filename);
                    snprintf(response_buffer, sizeof(response_buffer), "ERROR %d: File not found.\n", ERROR_FILE_NOT_FOUND);
                } else if (!permitted) {
                    printf("  Access Denied for user '%s'.\n", username);
                    log_to_file(client_addr_str, username, "RES: READ for '%s' failed: Access Denied.", filename);
                    snprintf(response_buffer, sizeof(response_buffer), "ERROR %d: Access Denied.\n", ERROR_ACCESS_DENIED);
                } else {
                    printf("  Access Granted. Sending location to client: %s %d\n", ss_ip, ss_port);
                    log_to_file(client_addr_str, username, "RES: READ for '%s' success. Sending SS_LOCATION %s %d", filename, ss_ip, ss_port);
                    snprintf(response_buffer, sizeof(response_buffer), "SS_LOCATION %s %d\n", ss_ip, ss_port);
                }
                write(client_socket, response_buffer, strlen(response_buffer));
            
            } else if (strncmp(buffer, "STREAM", 6) == 0) {
                char filename[256];
                if (sscanf(buffer, "STREAM %s", filename) != 1) {
                    snprintf(err_msg, sizeof(err_msg), "ERROR: Invalid STREAM format. Use: STREAM <filename>\n");
                    write(client_socket, err_msg, strlen(err_msg));
                    continue;
                }
                printf("Client requested STREAM for '%s'\n", filename);
                log_to_file(client_addr_str, username, "REQ: STREAM for '%s'", filename);

                char ss_ip[INET_ADDRSTRLEN];
                int ss_port;
                int permitted = 0; 
                FileLocation* file = NULL;

                pthread_mutex_lock(&g_system_mutex);
                
                file = cache_get_unsafe(filename); // Check cache first
                if (file == NULL) {
                    file = hash_map_find_unsafe(filename); // Check main map
                    if (file != NULL) {
                        cache_put_unsafe(filename, file); // Add to cache
                    }
                }
                
                if (file != NULL) {
                    if (check_permission(file, username, 'R')) {
                        permitted = 1;
                        strncpy(ss_ip, file->ss_ip_addr, INET_ADDRSTRLEN);
                        ss_port = file->ss_client_port;
                        get_current_timestamp(file->last_accessed_ts, 128);
                        strncpy(file->last_accessed_by, username, 255);
                    }
                }
                pthread_mutex_unlock(&g_system_mutex);

                char response_buffer[BUFFER_SIZE];
                if (file == NULL) {
                    printf("  File not found.\n");
                    log_to_file(client_addr_str, username, "RES: STREAM for '%s' failed: File not found.", filename);
                    snprintf(response_buffer, sizeof(response_buffer), "ERROR %d: File not found.\n", ERROR_FILE_NOT_FOUND);
                } else if (!permitted) {
                    printf("  Access Denied for user '%s'.\n", username);
                    log_to_file(client_addr_str, username, "RES: STREAM for '%s' failed: Access Denied.", filename);
                    snprintf(response_buffer, sizeof(response_buffer), "ERROR %d: Access Denied.\n", ERROR_ACCESS_DENIED);
                } else {
                    printf("  Access Granted. Sending location to client: %s %d\n", ss_ip, ss_port);
                    log_to_file(client_addr_str, username, "RES: STREAM for '%s' success. Sending SS_LOCATION %s %d", filename, ss_ip, ss_port);
                    snprintf(response_buffer, sizeof(response_buffer), "SS_LOCATION %s %d\n", ss_ip, ss_port);
                }
                write(client_socket, response_buffer, strlen(response_buffer));

            } else if (strncmp(buffer, "CREATE", 6) == 0) {
                char filename[256];
                if (sscanf(buffer, "CREATE %s", filename) != 1) {
                    snprintf(err_msg, sizeof(err_msg), "ERROR: Invalid CREATE format. Use: CREATE <filename>\n");
                    write(client_socket, err_msg, strlen(err_msg));
                    continue;
                }
                printf("Client requested CREATE for '%s'\n", filename);
                log_to_file(client_addr_str, username, "REQ: CREATE for '%s'", filename);

                pthread_mutex_lock(&g_system_mutex);
                
                FileLocation* file = hash_map_find_unsafe(filename);
                if (file != NULL) {
                    pthread_mutex_unlock(&g_system_mutex);
                    log_to_file(client_addr_str, username, "RES: CREATE for '%s' failed: File already exists.", filename);
                    snprintf(err_msg, sizeof(err_msg), "ERROR %d: File already exists.\n", ERROR_FILE_EXISTS);
                    write(client_socket, err_msg, strlen(err_msg));
                    continue;
                }

                if (g_num_servers == 0) {
                    pthread_mutex_unlock(&g_system_mutex);
                    log_to_file(client_addr_str, username, "RES: CREATE for '%s' failed: No Storage Servers available.", filename);
                    snprintf(err_msg, sizeof(err_msg), "ERROR %d: No Storage Servers available.\n", ERROR_SERVER_ERROR);
                    write(client_socket, err_msg, strlen(err_msg));
                    continue;
                }

                char ss_ip[INET_ADDRSTRLEN];
                int ss_nm_port = server_list[0].ss_nm_port;
                int ss_client_port = server_list[0].ss_client_port;
                strncpy(ss_ip, server_list[0].ss_ip_addr, INET_ADDRSTRLEN);
                
                pthread_mutex_unlock(&g_system_mutex);
                int result = forward_create_to_ss(ss_ip, ss_nm_port, filename);

                if (result == 0) {
                    FileLocation new_file;
                    strncpy(new_file.filename, filename, 255);
                    new_file.ss_client_port = ss_client_port;
                    strncpy(new_file.ss_ip_addr, ss_ip, INET_ADDRSTRLEN);
                    strncpy(new_file.owner_username, username, 255); 
                    new_file.num_permissions = 0; 
                    strncpy(new_file.last_accessed_by, "N/A", 255);
                    strncpy(new_file.last_accessed_ts, "N/A", 127);
                    
                    pthread_mutex_lock(&g_system_mutex);
                    hash_map_insert_unsafe(new_file);
                    save_registry_to_disk_unsafe(); // Save persistence
                    pthread_mutex_unlock(&g_system_mutex);

                    printf("  Successfully registered new file '%s' for owner '%s'\n", filename, username);
                    log_to_file(client_addr_str, username, "RES: CREATE for '%s' success.", filename);
                    write(client_socket, "File created successfully.\n", sizeof("File created successfully.\n") - 1);
                } else {
                    log_to_file(client_addr_str, username, "RES: CREATE for '%s' failed: SS failed to create file.", filename);
                    snprintf(err_msg, sizeof(err_msg), "ERROR %d: Storage Server failed to create file.\n", ERROR_SERVER_ERROR);
                    write(client_socket, err_msg, strlen(err_msg));
                }
            
            } else if (strncmp(buffer, "DELETE", 6) == 0) {
                char filename[256];
                if (sscanf(buffer, "DELETE %s", filename) != 1) {
                    snprintf(err_msg, sizeof(err_msg), "ERROR: Invalid DELETE format. Use: DELETE <filename>\n");
                    write(client_socket, err_msg, strlen(err_msg));
                    continue;
                }
                printf("Client requested DELETE for '%s'\n", filename);
                log_to_file(client_addr_str, username, "REQ: DELETE for '%s'", filename);

                char ss_ip[INET_ADDRSTRLEN];
                int ss_nm_port = -1;
                int permitted = 0; 
                FileLocation* file = NULL;

                pthread_mutex_lock(&g_system_mutex);
                
                file = hash_map_find_unsafe(filename); // No cache needed, we're deleting
                
                if (file != NULL) {
                    if (strcmp(file->owner_username, username) == 0) {
                        permitted = 1;
                        for (int j = 0; j < g_num_servers; j++) {
                            if (strcmp(server_list[j].ss_ip_addr, file->ss_ip_addr) == 0 &&
                                server_list[j].ss_client_port == file->ss_client_port) 
                            {
                                strncpy(ss_ip, server_list[j].ss_ip_addr, INET_ADDRSTRLEN);
                                ss_nm_port = server_list[j].ss_nm_port;
                                break;
                            }
                        }
                    }
                }
                pthread_mutex_unlock(&g_system_mutex);

                if (file == NULL) {
                    log_to_file(client_addr_str, username, "RES: DELETE for '%s' failed: File not found.", filename);
                    snprintf(err_msg, sizeof(err_msg), "ERROR %d: File not found.\n", ERROR_FILE_NOT_FOUND);
                    write(client_socket, err_msg, strlen(err_msg));
                    continue;
                }
                
                if (!permitted) {
                    printf("  Access Denied for user '%s'. Not owner.\n", username);
                    log_to_file(client_addr_str, username, "RES: DELETE for '%s' failed: Access Denied (not owner).", filename);
                    snprintf(err_msg, sizeof(err_msg), "ERROR %d: Access Denied. Only the owner can delete a file.\n", ERROR_ACCESS_DENIED);
                    write(client_socket, err_msg, strlen(err_msg));
                    continue;
                }

                if (ss_nm_port == -1) {
                    log_to_file(client_addr_str, username, "RES: DELETE for '%s' failed: SS not found (directory out of sync).", filename);
                    snprintf(err_msg, sizeof(err_msg), "ERROR %d: Could not find SS for file. Directory out of sync.\n", ERROR_SERVER_ERROR);
                    write(client_socket, err_msg, strlen(err_msg));
                    continue;
                }

                int result = forward_delete_to_ss(ss_ip, ss_nm_port, filename);

                if (result == 0) {
                    pthread_mutex_lock(&g_system_mutex);
                    if (hash_map_delete_unsafe(filename)) {
                        cache_delete_unsafe(filename); // Evict from cache
                        save_registry_to_disk_unsafe(); // Save persistence
                        printf("  Successfully deleted file '%s' from directory.\n", filename);
                        log_to_file(client_addr_str, username, "RES: DELETE for '%s' success.", filename);
                        write(client_socket, "File deleted successfully.\n", sizeof("File deleted successfully.\n") - 1);
                    } else {
                        printf("  Error: File was not found in hash map during delete.\n");
                        log_to_file(client_addr_str, username, "ERROR: DELETE for '%s' failed: File not found in map post-check.", filename);
                    }
                    pthread_mutex_unlock(&g_system_mutex);
                } else {
                    log_to_file(client_addr_str, username, "RES: DELETE for '%s' failed: SS failed to delete file.", filename);
                    snprintf(err_msg, sizeof(err_msg), "ERROR %d: Storage Server failed to delete file.\n", ERROR_SERVER_ERROR);
                    write(client_socket, err_msg, strlen(err_msg));
                }

            } else if (strncmp(buffer, "LIST", 4) == 0) {
                printf("Client requested LIST\n");
                log_to_file(client_addr_str, username, "REQ: LIST");
                
                char response_buffer[BUFFER_SIZE] = {0};
                int offset = 0;

                pthread_mutex_lock(&g_system_mutex);
                
                if (g_num_clients == 0) {
                    offset += sprintf(response_buffer + offset, "No users connected.\n");
                } else {
                    offset += sprintf(response_buffer + offset, "Connected Users:\n");
                    for (int i = 0; i < g_num_clients; i++) {
                        offset += sprintf(response_buffer + offset, "-> %s\n", client_list[i].username);
                    }
                }
                pthread_mutex_unlock(&g_system_mutex);
                
                write(client_socket, response_buffer, strlen(response_buffer));
                log_to_file(client_addr_str, username, "RES: LIST success.");

            } else if (strncmp(buffer, "ADDACCESS", 9) == 0) {
                char perm_char_str[2], target_filename[256], target_username[256];
                if (sscanf(buffer, "ADDACCESS %1s %s %s", perm_char_str, target_filename, target_username) != 3) {
                    snprintf(err_msg, sizeof(err_msg), "ERROR: Invalid format. Use: ADDACCESS <R|W> <filename> <username>\n");
                    write(client_socket, err_msg, strlen(err_msg));
                    continue;
                }
                char perm = perm_char_str[0];
                if (perm != 'R' && perm != 'W') {
                    snprintf(err_msg, sizeof(err_msg), "ERROR: Permission must be 'R' or 'W'.\n");
                    write(client_socket, err_msg, strlen(err_msg));
                    continue;
                }
                printf("User '%s' requested ADDACCESS %c for '%s' to user '%s'\n", username, perm, target_filename, target_username);
                log_to_file(client_addr_str, username, "REQ: ADDACCESS %c for '%s' to user '%s'", perm, target_filename, target_username);

                pthread_mutex_lock(&g_system_mutex);
                
                FileLocation* file = cache_get_unsafe(target_filename);
                if (file == NULL) {
                    file = hash_map_find_unsafe(target_filename);
                    if (file != NULL) cache_put_unsafe(target_filename, file);
                }
                
                if (file == NULL) {
                    pthread_mutex_unlock(&g_system_mutex);
                    log_to_file(client_addr_str, username, "RES: ADDACCESS for '%s' failed: File not found.", target_filename);
                    snprintf(err_msg, sizeof(err_msg), "ERROR %d: File not found.\n", ERROR_FILE_NOT_FOUND);
                    write(client_socket, err_msg, strlen(err_msg));
                    continue;
                }

                if (strcmp(file->owner_username, username) != 0) {
                    pthread_mutex_unlock(&g_system_mutex);
                    log_to_file(client_addr_str, username, "RES: ADDACCESS for '%s' failed: Not owner.", target_filename);
                    snprintf(err_msg, sizeof(err_msg), "ERROR %d: You are not the owner of this file.\n", ERROR_ACCESS_DENIED);
                    write(client_socket, err_msg, strlen(err_msg));
                    continue;
                }

                if (file->num_permissions < MAX_PERMISSIONS) {
                    AccessControl* new_perm = &file->acl[file->num_permissions++];
                    strncpy(new_perm->username, target_username, 255);
                    new_perm->permission = perm;
                    
                    save_registry_to_disk_unsafe(); // Save persistence
                    printf("  Access granted.\n");
                    log_to_file(client_addr_str, username, "RES: ADDACCESS for '%s' to user '%s' success.", target_filename, target_username);
                    write(client_socket, "Access granted successfully.\n", sizeof("Access granted successfully.\n") - 1);
                } else {
                    log_to_file(client_addr_str, username, "RES: ADDACCESS for '%s' failed: Max permissions reached.", target_filename);
                    write(client_socket, "ERROR: File has reached its maximum permission entries.\n", 56);
                }
                pthread_mutex_unlock(&g_system_mutex);

            } else if (strncmp(buffer, "REMACCESS", 9) == 0) {
                char target_filename[256], target_username[256];
                if (sscanf(buffer, "REMACCESS %s %s", target_filename, target_username) != 2) {
                    snprintf(err_msg, sizeof(err_msg), "ERROR: Invalid format. Use: REMACCESS <filename> <username>\n");
                    write(client_socket, err_msg, strlen(err_msg));
                    continue;
                }
                printf("User '%s' requested REMACCESS for '%s' from user '%s'\n", username, target_filename, target_username);
                log_to_file(client_addr_str, username, "REQ: REMACCESS for '%s' from user '%s'", target_filename, target_username);

                pthread_mutex_lock(&g_system_mutex);
                
                FileLocation* file = cache_get_unsafe(target_filename);
                if (file == NULL) {
                    file = hash_map_find_unsafe(target_filename);
                    if (file != NULL) cache_put_unsafe(target_filename, file);
                }

                if (file == NULL) {
                    pthread_mutex_unlock(&g_system_mutex);
                    log_to_file(client_addr_str, username, "RES: REMACCESS for '%s' failed: File not found.", target_filename);
                    snprintf(err_msg, sizeof(err_msg), "ERROR %d: File not found.\n", ERROR_FILE_NOT_FOUND);
                    write(client_socket, err_msg, strlen(err_msg));
                    continue;
                }

                if (strcmp(file->owner_username, username) != 0) {
                    pthread_mutex_unlock(&g_system_mutex);
                    log_to_file(client_addr_str, username, "RES: REMACCESS for '%s' failed: Not owner.", target_filename);
                    snprintf(err_msg, sizeof(err_msg), "ERROR %d: You are not the owner of this file.\n", ERROR_ACCESS_DENIED);
                    write(client_socket, err_msg, strlen(err_msg));
                    continue;
                }

                int perm_index = -1;
                for (int i = 0; i < file->num_permissions; i++) {
                    if (strcmp(file->acl[i].username, target_username) == 0) {
                        perm_index = i;
                        break;
                    }
                }

                if (perm_index == -1) {
                    pthread_mutex_unlock(&g_system_mutex);
                    log_to_file(client_addr_str, username, "RES: REMACCESS for '%s' failed: User '%s' has no permissions.", target_filename, target_username);
                    snprintf(err_msg, sizeof(err_msg), "ERROR: That user does not have special permissions on this file.\n");
                    write(client_socket, err_msg, strlen(err_msg));
                    continue;
                }

                for (int i = perm_index; i < file->num_permissions - 1; i++) {
                    file->acl[i] = file->acl[i + 1];
                }
                file->num_permissions--;
                
                save_registry_to_disk_unsafe(); // Save persistence
                printf("  Access removed.\n");
                log_to_file(client_addr_str, username, "RES: REMACCESS for '%s' from user '%s' success.", target_filename, target_username);
                write(client_socket, "Access removed successfully.\n", sizeof("Access removed successfully.\n") - 1);
                pthread_mutex_unlock(&g_system_mutex);
            
            } else if (strncmp(buffer, "INFO", 4) == 0) {
                char filename[256];
                if (sscanf(buffer, "INFO %s", filename) != 1) {
                    snprintf(err_msg, sizeof(err_msg), "ERROR: Invalid INFO format. Use: INFO <filename>\n");
                    write(client_socket, err_msg, strlen(err_msg));
                    continue;
                }
                printf("Client requested INFO for '%s'\n", filename);
                log_to_file(client_addr_str, username, "REQ: INFO for '%s'", filename);

                char response_buffer[BUFFER_SIZE] = {0};
                int offset = 0;
                FileLocation file_copy;
                StorageServer* ss = NULL;
                
                pthread_mutex_lock(&g_system_mutex);

                FileLocation* file = cache_get_unsafe(filename);
                if (file == NULL) {
                    file = hash_map_find_unsafe(filename);
                    if (file != NULL) cache_put_unsafe(filename, file);
                }

                if (file == NULL) {
                    pthread_mutex_unlock(&g_system_mutex);
                    log_to_file(client_addr_str, username, "RES: INFO for '%s' failed: File not found.", filename);
                    snprintf(err_msg, sizeof(err_msg), "ERROR %d: File not found.\n", ERROR_FILE_NOT_FOUND);
                    write(client_socket, err_msg, strlen(err_msg));
                    continue;
                }

                if (!check_permission(file, username, 'R')) {
                    pthread_mutex_unlock(&g_system_mutex);
                    printf("  Access Denied for user '%s'.\n", username);
                    log_to_file(client_addr_str, username, "RES: INFO for '%s' failed: Access Denied.", filename);
                    snprintf(err_msg, sizeof(err_msg), "ERROR %d: Access Denied.\n", ERROR_ACCESS_DENIED);
                    write(client_socket, err_msg, strlen(err_msg));
                    continue;
                }

                file_copy = *file; // Make a copy
                
                for (int j = 0; j < g_num_servers; j++) {
                    if (strcmp(server_list[j].ss_ip_addr, file_copy.ss_ip_addr) == 0 &&
                        server_list[j].ss_client_port == file_copy.ss_client_port) {
                        ss = &server_list[j];
                        break;
                    }
                }
                
                if (ss == NULL) {
                    pthread_mutex_unlock(&g_system_mutex);
                    log_to_file(client_addr_str, username, "RES: INFO for '%s' failed: SS not found (directory out of sync).", filename);
                    snprintf(err_msg, sizeof(err_msg), "ERROR %d: Could not find SS for file. Directory out of sync.\n", ERROR_SERVER_ERROR);
                    write(client_socket, err_msg, strlen(err_msg));
                    continue;
                }
                
                char ss_ip[INET_ADDRSTRLEN];
                strncpy(ss_ip, ss->ss_ip_addr, INET_ADDRSTRLEN);
                int ss_nm_port = ss->ss_nm_port;
                
                pthread_mutex_unlock(&g_system_mutex);
                
                int words = 0, chars = 0;
                char created_ts[128], modified_ts[128]; 

                if (get_metadata_from_ss(ss_ip, ss_nm_port, file_copy.filename, &words, &chars, created_ts, modified_ts, 128) != 0) {
                    log_to_file(client_addr_str, username, "RES: INFO for '%s' failed: Failed to get metadata from SS.", filename);
                    snprintf(err_msg, sizeof(err_msg), "ERROR %d: Failed to retrieve file metadata from Storage Server.\n", ERROR_SERVER_ERROR);
                    write(client_socket, err_msg, strlen(err_msg));
                    continue;
                }

                offset += sprintf(response_buffer + offset, "--> File: %s\n", file_copy.filename);
                offset += sprintf(response_buffer + offset, "--> Owner: %s\n", file_copy.owner_username);
                offset += sprintf(response_buffer + offset, "--> Created: %s\n", created_ts);
                offset += sprintf(response_buffer + offset, "--> Last Modified: %s\n", modified_ts);
                offset += sprintf(response_buffer + offset, "--> Size: %d bytes\n", chars); 
                offset += sprintf(response_buffer + offset, "--> Access: %s (RW)\n", file_copy.owner_username);
                for (int i = 0; i < file_copy.num_permissions; i++) {
                    offset += sprintf(response_buffer + offset, "-->          %s (%s)\n", 
                                      file_copy.acl[i].username, 
                                      file_copy.acl[i].permission == 'W' ? "RW" : "R"); 
                }
                offset += sprintf(response_buffer + offset, "--> Last Accessed: %s by %s\n", file_copy.last_accessed_ts, file_copy.last_accessed_by); 

                write(client_socket, response_buffer, offset);
                log_to_file(client_addr_str, username, "RES: INFO for '%s' success.", filename);
            
            } else if (strncmp(buffer, "EXEC", 4) == 0) {
                char filename[256];
                if (sscanf(buffer, "EXEC %s", filename) != 1) {
                    snprintf(err_msg, sizeof(err_msg), "ERROR: Invalid EXEC format. Use: EXEC <filename>\n");
                    write(client_socket, err_msg, strlen(err_msg));
                    continue;
                }
                printf("Client requested EXEC for '%s'\n", filename);
                log_to_file(client_addr_str, username, "REQ: EXEC for '%s'", filename);

                int permitted = 0;
                FileLocation file_copy;
                StorageServer* ss = NULL;
                FileLocation* file = NULL;

                pthread_mutex_lock(&g_system_mutex);
                
                file = cache_get_unsafe(filename);
                if (file == NULL) {
                    file = hash_map_find_unsafe(filename);
                    if (file != NULL) cache_put_unsafe(filename, file);
                }
                
                if (file != NULL) {
                    if (check_permission(file, username, 'R')) {
                        permitted = 1;
                        file_copy = *file; 
                        for (int j = 0; j < g_num_servers; j++) {
                            if (strcmp(server_list[j].ss_ip_addr, file_copy.ss_ip_addr) == 0 &&
                                server_list[j].ss_client_port == file_copy.ss_client_port) {
                                ss = &server_list[j];
                                break;
                            }
                        }
                    }
                }
                pthread_mutex_unlock(&g_system_mutex);

                if (file == NULL) {
                    log_to_file(client_addr_str, username, "RES: EXEC for '%s' failed: File not found.", filename);
                    snprintf(err_msg, sizeof(err_msg), "ERROR %d: File not found.\n", ERROR_FILE_NOT_FOUND);
                    write(client_socket, err_msg, strlen(err_msg));
                    continue;
                }
                if (!permitted) {
                    log_to_file(client_addr_str, username, "RES: EXEC for '%s' failed: Access Denied.", filename);
                    snprintf(err_msg, sizeof(err_msg), "ERROR %d: Access Denied.\n", ERROR_ACCESS_DENIED);
                    write(client_socket, err_msg, strlen(err_msg));
                    continue;
                }
                if (ss == NULL) {
                    log_to_file(client_addr_str, username, "RES: EXEC for '%s' failed: SS not found (directory out of sync).", filename);
                    snprintf(err_msg, sizeof(err_msg), "ERROR %d: Could not find SS for file. Directory out of sync.\n", ERROR_SERVER_ERROR);
                    write(client_socket, err_msg, strlen(err_msg));
                    continue;
                }

                char* file_content = malloc(EXEC_OUTPUT_BUFFER_SIZE);
                if (file_content == NULL) {
                    log_to_file(client_addr_str, username, "CRITICAL: NM failed to allocate memory for exec.");
                    snprintf(err_msg, sizeof(err_msg), "ERROR %d: NM failed to allocate memory for exec.\n", ERROR_SERVER_ERROR);
                    write(client_socket, err_msg, strlen(err_msg));
                    continue;
                }
                
                if (get_file_content_from_ss(ss->ss_ip_addr, ss->ss_client_port, file_copy.filename, file_content, EXEC_OUTPUT_BUFFER_SIZE) != 0) {
                    log_to_file(client_addr_str, username, "ERROR: NM failed to fetch file '%s' from SS for EXEC.", filename);
                    snprintf(err_msg, sizeof(err_msg), "ERROR %d: NM failed to fetch file from SS.\n", ERROR_SERVER_ERROR);
                    write(client_socket, err_msg, strlen(err_msg));
                    free(file_content);
                    continue;
                }
                
                printf("  Executing content:\n%s\n", file_content);
                log_to_file(client_addr_str, username, "INFO: Executing content for '%s'...", filename);
                FILE* pipe = popen(file_content, "r");
                free(file_content); 

                if (pipe == NULL) {
                    log_to_file(client_addr_str, username, "ERROR: NM failed to popen() for exec.");
                    snprintf(err_msg, sizeof(err_msg), "ERROR %d: NM failed to execute command.\n", ERROR_SERVER_ERROR);
                    write(client_socket, err_msg, strlen(err_msg));
                    continue;
                }

                char* output_buffer = malloc(EXEC_OUTPUT_BUFFER_SIZE);
                if (output_buffer == NULL) {
                    log_to_file(client_addr_str, username, "CRITICAL: NM failed to allocate memory for exec output.");
                    snprintf(err_msg, sizeof(err_msg), "ERROR %d: NM failed to allocate memory for output.\n", ERROR_SERVER_ERROR);
                    write(client_socket, err_msg, strlen(err_msg));
                    pclose(pipe);
                    continue;
                }
                
                ssize_t output_size = fread(output_buffer, 1, EXEC_OUTPUT_BUFFER_SIZE - 1, pipe);
                output_buffer[output_size] = '\0';
                pclose(pipe);

                printf("  Sending output to client:\n%s\n", output_buffer);
                log_to_file(client_addr_str, username, "RES: Sending exec output for '%s' to client.", filename);
                write(client_socket, output_buffer, output_size);
                free(output_buffer);

            } else if (strncmp(buffer, "UNDO", 4) == 0) {
                char filename[256];
                if (sscanf(buffer, "UNDO %s", filename) != 1) {
                    snprintf(err_msg, sizeof(err_msg), "ERROR: Invalid UNDO format. Use: UNDO <filename>\n");
                    write(client_socket, err_msg, strlen(err_msg));
                    continue;
                }
                printf("Client requested UNDO for '%s'\n", filename);
                log_to_file(client_addr_str, username, "REQ: UNDO for '%s'", filename);

                char ss_ip[INET_ADDRSTRLEN];
                int ss_nm_port = -1;
                int permitted = 0; 
                FileLocation* file = NULL;

                pthread_mutex_lock(&g_system_mutex);
                
                file = cache_get_unsafe(filename);
                if (file == NULL) {
                    file = hash_map_find_unsafe(filename);
                    if (file != NULL) cache_put_unsafe(filename, file);
                }
                
                if (file != NULL) {
                    if (check_permission(file, username, 'W')) {
                        permitted = 1;
                        for (int j = 0; j < g_num_servers; j++) {
                            if (strcmp(server_list[j].ss_ip_addr, file->ss_ip_addr) == 0 &&
                                server_list[j].ss_client_port == file->ss_client_port) 
                            {
                                strncpy(ss_ip, server_list[j].ss_ip_addr, INET_ADDRSTRLEN);
                                ss_nm_port = server_list[j].ss_nm_port;
                                break;
                            }
                        }
                    }
                }
                pthread_mutex_unlock(&g_system_mutex);

                if (file == NULL) {
                    log_to_file(client_addr_str, username, "RES: UNDO for '%s' failed: File not found.", filename);
                    snprintf(err_msg, sizeof(err_msg), "ERROR %d: File not found.\n", ERROR_FILE_NOT_FOUND);
                    write(client_socket, err_msg, strlen(err_msg));
                    continue;
                }
                if (!permitted) {
                    log_to_file(client_addr_str, username, "RES: UNDO for '%s' failed: Access Denied (Write permission required).", filename);
                    snprintf(err_msg, sizeof(err_msg), "ERROR %d: Access Denied (Write permission required to undo).\n", ERROR_ACCESS_DENIED);
                    write(client_socket, err_msg, strlen(err_msg));
                    continue;
                }
                if (ss_nm_port == -1) {
                    log_to_file(client_addr_str, username, "RES: UNDO for '%s' failed: SS not found (directory out of sync).", filename);
                    snprintf(err_msg, sizeof(err_msg), "ERROR %d: Could not find SS for file. Directory out of sync.\n", ERROR_SERVER_ERROR);
                    write(client_socket, err_msg, strlen(err_msg));
                    continue;
                }

                int result = forward_undo_to_ss(ss_ip, ss_nm_port, filename);

                if (result == 0) {
                    log_to_file(client_addr_str, username, "RES: UNDO for '%s' success.", filename);
                    write(client_socket, "Undo Successful!\n", 17);
                } else if (result == -2) {
                    log_to_file(client_addr_str, username, "RES: UNDO for '%s' failed: No undo history found.", filename);
                    snprintf(err_msg, sizeof(err_msg), "ERROR %d: No undo history found for this file.\n", ERROR_NO_UNDO_HISTORY);
                    write(client_socket, err_msg, strlen(err_msg));
                } else {
                    log_to_file(client_addr_str, username, "RES: UNDO for '%s' failed: SS failed to undo file.", filename);
                    snprintf(err_msg, sizeof(err_msg), "ERROR %d: Storage Server failed to undo file.\n", ERROR_SERVER_ERROR);
                    write(client_socket, err_msg, strlen(err_msg));
                }
            
            } else if (strncmp(buffer, "WRITE", 5) == 0) {
                char filename[256];
                int sentence_num;
                if (sscanf(buffer, "WRITE %s %d", filename, &sentence_num) != 2) {
                    snprintf(err_msg, sizeof(err_msg), "ERROR: Invalid WRITE format. Use: WRITE <filename> <sentence_num>\n");
                    write(client_socket, err_msg, strlen(err_msg));
                    continue;
                }
                printf("Client requested WRITE for '%s', sentence %d\n", filename, sentence_num);
                log_to_file(client_addr_str, username, "REQ: WRITE for '%s', sentence %d.", filename, sentence_num);

                char ss_ip[INET_ADDRSTRLEN];
                int ss_port;
                int permitted = 0;
                int already_locked = 0;
                char locking_user[256];
                FileLocation* file = NULL;

                pthread_mutex_lock(&g_system_mutex);

                file = cache_get_unsafe(filename);
                if (file == NULL) {
                    file = hash_map_find_unsafe(filename);
                    if (file != NULL) cache_put_unsafe(filename, file);
                }
                
                if (file != NULL) {
                    if (check_permission(file, username, 'W')) {
                        permitted = 1;
                        
                        for (int j = 0; j < g_num_locks; j++) {
                            if (strcmp(g_file_locks[j].filename, filename) == 0 && g_file_locks[j].sentence_num == sentence_num) {
                                already_locked = 1;
                                strncpy(locking_user, g_file_locks[j].username, 255);
                                break;
                            }
                        }

                        if (!already_locked && g_num_locks < MAX_LOCKS) {
                            FileLock* new_lock = &g_file_locks[g_num_locks++];
                            strncpy(new_lock->filename, filename, 255);
                            strncpy(new_lock->username, username, 255);
                            new_lock->sentence_num = sentence_num;
                            
                            strncpy(ss_ip, file->ss_ip_addr, INET_ADDRSTRLEN);
                            ss_port = file->ss_client_port;
                            printf("  Lock granted to '%s'\n", username);
                            log_to_file(client_addr_str, username, "INFO: Lock granted for '%s' (sent %d).", filename, sentence_num);
                        } else if (already_locked) {
                            // Lock is held
                        } else {
                            already_locked = -1; // Signal max locks
                        }
                    }
                }
                pthread_mutex_unlock(&g_system_mutex);

                char response_buffer[BUFFER_SIZE];
                if (file == NULL) {
                    log_to_file(client_addr_str, username, "RES: WRITE for '%s' failed: File not found.", filename);
                    snprintf(response_buffer, sizeof(response_buffer), "ERROR %d: File not found.\n", ERROR_FILE_NOT_FOUND);
                } else if (!permitted) {
                    log_to_file(client_addr_str, username, "RES: WRITE for '%s' failed: Access Denied (Write permission required).", filename);
                    snprintf(response_buffer, sizeof(response_buffer), "ERROR %d: Access Denied (Write permission required).\n", ERROR_ACCESS_DENIED);
                } else if (already_locked == 1) {
                    log_to_file(client_addr_str, username, "RES: WRITE for '%s' failed: Sentence %d is locked by '%s'.", filename, sentence_num, locking_user);
                    snprintf(response_buffer, sizeof(response_buffer), "ERROR %d: Sentence is currently locked by '%s'.\n", ERROR_FILE_LOCKED, locking_user);
                } else if (already_locked == -1) {
                    log_to_file(client_addr_str, username, "RES: WRITE for '%s' failed: System is at maximum lock capacity.", filename);
                    snprintf(response_buffer, sizeof(response_buffer), "ERROR %d: System is at maximum lock capacity. Try again later.\n", ERROR_MAX_LOCKS);
                } else {
                    log_to_file(client_addr_str, username, "RES: WRITE for '%s' success. Sending SS_LOCATION %s %d", filename, ss_ip, ss_port);
                    snprintf(response_buffer, sizeof(response_buffer), "SS_LOCATION %s %d\n", ss_ip, ss_port);
                }
                write(client_socket, response_buffer, strlen(response_buffer));
            
            } else if (strncmp(buffer, "RELEASE_LOCK", 12) == 0) {
                char filename[256];
                int sentence_num;
                if (sscanf(buffer, "RELEASE_LOCK %s %d", filename, &sentence_num) != 2) {
                    snprintf(err_msg, sizeof(err_msg), "ERROR: Invalid RELEASE_LOCK format.\n");
                    write(client_socket, err_msg, strlen(err_msg));
                    continue;
                }
                printf("Client requested RELEASE_LOCK for '%s', sentence %d\n", filename, sentence_num);
                log_to_file(client_addr_str, username, "REQ: RELEASE_LOCK for '%s' (sent %d).", filename, sentence_num);

                pthread_mutex_lock(&g_system_mutex);
                int lock_index = -1;
                for (int i = 0; i < g_num_locks; i++) {
                    if (strcmp(g_file_locks[i].filename, filename) == 0 &&
                        g_file_locks[i].sentence_num == sentence_num &&
                        strcmp(g_file_locks[i].username, username) == 0)
                    {
                        lock_index = i;
                        break;
                    }
                }

                if (lock_index != -1) {
                    for (int i = lock_index; i < g_num_locks - 1; i++) {
                        g_file_locks[i] = g_file_locks[i + 1];
                    }
                    g_num_locks--;
                    printf("  Lock released.\n");
                    log_to_file(client_addr_str, username, "RES: Lock released for '%s' (sent %d).", filename, sentence_num);
                    write(client_socket, "ACK_LOCK_RELEASED\n", 18);
                } else {
                    printf("  Invalid lock release request.\n");
                    log_to_file(client_addr_str, username, "RES: RELEASE_LOCK failed: Lock not held by user.");
                    snprintf(err_msg, sizeof(err_msg), "ERROR %d: You do not hold that lock.\n", ERROR_ACCESS_DENIED);
                    write(client_socket, err_msg, strlen(err_msg));
                }
                pthread_mutex_unlock(&g_system_mutex);

            } else {
                printf("Client sent unknown command: %s\n", buffer);
                log_to_file(client_addr_str, username, "WARN: Unknown command: %s", buffer);
                snprintf(err_msg, sizeof(err_msg), "ERROR: Unknown command.\n");
                write(client_socket, err_msg, strlen(err_msg));
            }
        }
    } else {
        printf("Unknown connection type. Closing.\n");
        log_to_file(client_addr_str, "Unknown", "WARN: Unknown connection type. First line: %s", buffer);
    }

    // --- (Client Disconnect Logic) ---
    pthread_mutex_lock(&g_system_mutex);
    int client_index = -1;
    char disconnected_username[256] = "Unknown";
    for (int i = 0; i < g_num_clients; i++) {
        if (client_list[i].client_socket_fd == client_socket) {
            client_index = i;
            strncpy(disconnected_username, client_list[i].username, 255);
            break;
        }
    }

    if (client_index != -1) {
        printf("User '%s' disconnected. Removing from list.\n", disconnected_username);
        log_to_file(client_addr_str, disconnected_username, "INFO: Client disconnected. Removing from list.");
        
        for (int i = g_num_locks - 1; i >= 0; i--) { // Iterate backwards
            if (strcmp(g_file_locks[i].username, disconnected_username) == 0) {
                printf("  User disconnected, releasing lock on %s (sent %d)\n", g_file_locks[i].filename, g_file_locks[i].sentence_num);
                log_to_file(client_addr_str, disconnected_username, "INFO: Auto-releasing lock on %s (sent %d).", g_file_locks[i].filename, g_file_locks[i].sentence_num);
                for (int j = i; j < g_num_locks - 1; j++) {
                    g_file_locks[j] = g_file_locks[j + 1];
                }
                g_num_locks--;
            }
        }
        
        for (int i = client_index; i < g_num_clients - 1; i++) {
            client_list[i] = client_list[i + 1];
        }
        g_num_clients--;
    }
    pthread_mutex_unlock(&g_system_mutex);

    close(client_socket);
    printf("Connection from %s closed.\n", client_addr_str);
    log_to_file(client_addr_str, disconnected_username, "INFO: Connection closed.");
    return NULL;
}

/*
 * --- Main Server Function ---
 */
int main() {
    // Initialize hash map buckets to NULL
    for (int i = 0; i < HASH_MAP_SIZE; i++) {
        g_file_hash_map[i] = NULL;
    }
    
    // Initialize cache map buckets to NULL
    for (int i = 0; i < CACHE_MAP_SIZE; i++) {
        g_cache_map[i] = NULL;
    }

    load_registry_from_disk(); // Load persistent data
    
    int server_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket failed");
        log_to_file("Internal", "NameServer", "CRITICAL: socket() failed: %m");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt");
        log_to_file("Internal", "NameServer", "CRITICAL: setsockopt() failed: %m");
        exit(EXIT_FAILURE);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY; 
    server_addr.sin_port = htons(NAME_SERVER_PORT);

    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind failed");
        log_to_file("Internal", "NameServer", "CRITICAL: bind() failed: %m");
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 10) < 0) { 
        perror("listen failed");
        log_to_file("Internal", "NameServer", "CRITICAL: listen() failed: %m");
        exit(EXIT_FAILURE);
    }

    printf("Name Server listening on port %d\n", NAME_SERVER_PORT);
    log_to_file("Internal", "NameServer", "INFO: Name Server listening on port %d", NAME_SERVER_PORT);

    while (1) {
        int client_socket = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_socket < 0) {
            perror("accept failed");
            log_to_file("Internal", "NameServer", "ERROR: accept() failed: %m");
            continue; 
        }

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        printf("Accepted new connection from %s:%d\n", client_ip, ntohs(client_addr.sin_port));
        // Note: We log the connection *inside* handle_connection once we know the type (SS or Client)

        pthread_t thread_id;
        int* p_client_socket = malloc(sizeof(int));
        *p_client_socket = client_socket;

        if (pthread_create(&thread_id, NULL, handle_connection, (void*)p_client_socket) != 0) {
            perror("pthread_create failed");
            log_to_file("Internal", "NameServer", "ERROR: pthread_create failed: %m");
            free(p_client_socket);
            close(client_socket);
        }
    }

    close(server_fd);
    return 0;
}