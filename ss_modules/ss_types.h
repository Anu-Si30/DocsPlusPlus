#ifndef SS_TYPES_H
#define SS_TYPES_H

#include "common.h"

/*
 * Data structures for Storage Server
 */

// Word/Sentence linked list structure
typedef struct WordNode {
    char* word;
    struct WordNode* next_word;
    struct WordNode* next_sentence;
} WordNode;

// Storage Server Configuration
#define SS_NM_PORT 9001       // Port for NM to connect to
#define SS_CLIENT_PORT 9002   // Port for Clients to connect to
#define SS_STORAGE_DIR "ss_storage/"
#define SS_LOG_FILE "storageserver.log"

#endif // SS_TYPES_H
