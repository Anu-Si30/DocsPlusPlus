#include "ss_document.h"
#include "ss_logging.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include <time.h>

/*
 * Create a new word node
 */
WordNode* create_word_node(const char* word) {
    WordNode* new_node = (WordNode*)malloc(sizeof(WordNode));
    new_node->word = strdup(word);
    new_node->next_word = NULL;
    new_node->next_sentence = NULL;
    return new_node;
}

/*
 * Check if a character is a sentence delimiter
 */
int is_delimiter(char c) {
    return (c == '.' || c == '!' || c == '?');
}

/*
 * Free an entire document structure
 */
void free_document(WordNode* doc_head) {
    WordNode* current_sentence = doc_head;
    while (current_sentence != NULL) {
        WordNode* current_word = current_sentence;
        WordNode* next_sentence = current_sentence->next_sentence;
        while (current_word != NULL) {
            WordNode* next_word = current_word->next_word;
            free(current_word->word);
            free(current_word);
            current_word = next_word;
        }
        current_sentence = next_sentence;
    }
}

/*
 * Parse a file into a linked list structure
 */
WordNode* parse_file_to_list(FILE* file) {
    WordNode* doc_head = NULL;
    WordNode* current_sentence_head = NULL;
    WordNode* current_word_node = NULL; 
    char buffer[256];
    int buffer_idx = 0;
    char c;
    while ((c = fgetc(file)) != EOF) {
        if (isspace(c) || is_delimiter(c)) {
            if (buffer_idx > 0) {
                if (is_delimiter(c)) { 
                    buffer[buffer_idx++] = c;
                }
                buffer[buffer_idx] = '\0';
                WordNode* new_node = create_word_node(buffer);
                if (current_sentence_head == NULL) {
                    current_sentence_head = new_node;
                    if (doc_head == NULL) {
                        doc_head = current_sentence_head;
                    } else {
                        WordNode* temp_sent = doc_head;
                        while(temp_sent->next_sentence != NULL) {
                            temp_sent = temp_sent->next_sentence;
                        }
                        temp_sent->next_sentence = current_sentence_head;
                    }
                } else {
                    current_word_node->next_word = new_node;
                }
                current_word_node = new_node;
                buffer_idx = 0; 
            }
            if (is_delimiter(c)) {
                current_sentence_head = NULL;
                current_word_node = NULL;
            }
        } else {
            if (buffer_idx < 255) {
                buffer[buffer_idx++] = c;
            }
        }
    }
    if (buffer_idx > 0) {
        buffer[buffer_idx] = '\0';
        WordNode* new_node = create_word_node(buffer);
        if (current_sentence_head == NULL) {
            current_sentence_head = new_node;
            if (doc_head == NULL) doc_head = current_sentence_head;
        } else {
            current_word_node->next_word = new_node;
        }
    }
    return doc_head;
}

/*
 * Flatten a document structure back to a file
 */
void flatten_list_to_file(WordNode* doc_head, FILE* file) {
    WordNode* current_sentence = doc_head;
    while (current_sentence != NULL) {
        WordNode* current_word = current_sentence;
        while (current_word != NULL) {
            fprintf(file, "%s", current_word->word);
            if (current_word->next_word != NULL) {
                fprintf(file, " ");
            }
            current_word = current_word->next_word;
        }
        current_sentence = current_sentence->next_sentence;
        if (current_sentence != NULL) {
            fprintf(file, " ");
        }
    }
}

/*
 * Get a specific sentence from the document
 */
WordNode* get_sentence(WordNode* doc_head, int sentence_index) {
    WordNode* current_sentence = doc_head;
    for (int i = 0; i < sentence_index && current_sentence != NULL; i++) {
        current_sentence = current_sentence->next_sentence;
    }
    return current_sentence;
}

/*
 * Insert a word at a specific position
 */
WordNode* insert_word_at(WordNode* doc_head, int sentence_index, int word_index, const char* content) {
    
    if (doc_head == NULL && sentence_index == 0 && word_index == 0) {
        printf("SS (Write): Inserting into empty doc\n"); 
        WordNode* new_word_node = create_word_node(content);
        doc_head = new_word_node;
        return doc_head;
    }
    
    WordNode* sentence_head = get_sentence(doc_head, sentence_index);
    
    if (sentence_head == NULL && sentence_index > 0 && word_index == 0) {
        WordNode* prev_sentence = get_sentence(doc_head, sentence_index - 1);
        if (prev_sentence != NULL) {
             printf("SS (Write): Appending new sentence\n");
             WordNode* new_word_node = create_word_node(content);
             prev_sentence->next_sentence = new_word_node;
             return doc_head;
        } else {
             printf("SS (Write): ERROR: Invalid sentence index %d (non-contiguous)\n", sentence_index);
             return NULL; 
        }
    }
    
    if (sentence_head == NULL) {
        printf("SS (Write): ERROR: Invalid sentence index %d\n", sentence_index);
        return NULL; 
    }
    
    WordNode* new_word_node = create_word_node(content);

    if (word_index == 0) {
        new_word_node->next_word = sentence_head;
        new_word_node->next_sentence = sentence_head->next_sentence; 
        sentence_head->next_sentence = NULL; 
        
        if (sentence_index == 0) {
            doc_head = new_word_node;
        } else {
            WordNode* prev_sentence = get_sentence(doc_head, sentence_index - 1);
            prev_sentence->next_sentence = new_word_node;
        }
        return doc_head;
    }

    WordNode* current_word = sentence_head;
    int i = 0;
    for (i = 0; i < word_index - 1; i++) {
        if (current_word->next_word == NULL) {
            break;
        }
        current_word = current_word->next_word;
    }

    if (i != word_index - 1) {
        printf("SS (Write): ERROR: Invalid word index %d (too large for sentence).\n", word_index);
        free(new_word_node->word);
        free(new_word_node);
        return NULL; 
    }
    
    new_word_node->next_word = current_word->next_word;
    current_word->next_word = new_word_node;
    
    return doc_head;
}

/*
 * Get file metadata (word count, char count, timestamps)
 */
int get_file_metadata(const char* file_path, int* word_count, int* char_count, 
                      char* created_ts, char* modified_ts, int ts_len) 
{
    struct stat file_stat;
    if (stat(file_path, &file_stat) != 0) {
        perror("SS: stat failed");
        log_message("Internal", "System", "ERROR: stat failed for %s: %m", file_path);
        return -1;
    }
    *char_count = (int)file_stat.st_size; 

    FILE* file = fopen(file_path, "r");
    if (file == NULL) {
        perror("SS: fopen for word count failed");
        log_message("Internal", "System", "ERROR: fopen for word count failed for %s: %m", file_path);
        return -1;
    }
    *word_count = 0;
    int in_word = 0;
    char c;
    while ((c = fgetc(file)) != EOF) {
        if (isspace(c)) {
            in_word = 0;
        } else if (in_word == 0) {
            in_word = 1;
            (*word_count)++;
        }
    }
    fclose(file);

    struct tm *tm_info;
    tm_info = localtime(&file_stat.st_ctime);
    strftime(created_ts, ts_len, "%Y-%m-%d %H:%M", tm_info);
    tm_info = localtime(&file_stat.st_mtime);
    strftime(modified_ts, ts_len, "%Y-%m-%d %H:%M", tm_info);

    return 0;
}
