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
    if (word == NULL) {
        return NULL;
    }
    WordNode* new_node = (WordNode*)malloc(sizeof(WordNode));
    if (new_node == NULL) {
        return NULL;
    }
    new_node->word = strdup(word);
    if (new_node->word == NULL) {
        free(new_node);
        return NULL;
    }
    new_node->next_word = NULL;
    new_node->next_sentence = NULL;
    return new_node;
}

/*
 * Split a string with consecutive delimiters into multiple sentence nodes
 * E.g., "..." becomes three sentences, each ending with "."
 */
WordNode* create_delimiter_sentences(const char* delimiters) {
    if (delimiters == NULL || delimiters[0] == '\0') return NULL;
    
    WordNode* first_sentence = NULL;
    WordNode* prev_sentence = NULL;
    
    for (int i = 0; delimiters[i] != '\0'; i++) {
        if (is_delimiter(delimiters[i])) {
            char single_delim[2] = {delimiters[i], '\0'};
            WordNode* new_sentence = create_word_node(single_delim);
            
            if (first_sentence == NULL) {
                first_sentence = new_sentence;
            } else {
                prev_sentence->next_sentence = new_sentence;
            }
            prev_sentence = new_sentence;
        }
    }
    
    return first_sentence;
}


/*
 * Check if a character is a sentence delimiter
 */
int is_delimiter(char c) {
    return (c == '.' || c == '!' || c == '?');
}

/*
 * Check if a string contains only delimiter characters
 */
int is_all_delimiters(const char* str) {
    if (str == NULL || str[0] == '\0') {
        return 0;
    }
    for (int i = 0; str[i] != '\0'; i++) {
        if (!is_delimiter(str[i])) {
            return 0;
        }
    }
    return 1;
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
            } else if (is_delimiter(c)) {
                // Standalone delimiter (e.g., space followed by period)
                char delim_str[2] = {c, '\0'};
                WordNode* new_node = create_word_node(delim_str);
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
            // Write the word, stripping trailing delimiters from non-final words
            char* word_to_write = current_word->word;
            
            // Safety check
            if (word_to_write == NULL) {
                current_word = current_word->next_word;
                continue;
            }
            
            // Write the word exactly as-is (preserve all delimiters, don't auto-add anything)
            fprintf(file, "%s", word_to_write);
            
            if (current_word->next_word != NULL) {
                // Check if next word is a standalone delimiter (single delimiter character)
                // If so, don't add space before it
                WordNode* next = current_word->next_word;
                int is_standalone_delimiter = 0;
                if (next->word != NULL && 
                    strlen(next->word) == 1 && 
                    is_delimiter(next->word[0])) {
                    is_standalone_delimiter = 1;
                }
                
                if (!is_standalone_delimiter) {
                    fprintf(file, " ");
                }
            }
            current_word = current_word->next_word;
        }
        current_sentence = current_sentence->next_sentence;
        if (current_sentence != NULL) {
            // Check if next sentence is a standalone delimiter (single character that is a delimiter)
            // If so, don't add space before it
            int is_standalone_delimiter = 0;
            if (current_sentence->word != NULL && 
                strlen(current_sentence->word) == 1 && 
                is_delimiter(current_sentence->word[0]) &&
                current_sentence->next_word == NULL) {
                is_standalone_delimiter = 1;
            }
            
            if (!is_standalone_delimiter) {
                fprintf(file, " ");
            }
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
    
    // Special case: if content is all delimiters (e.g., "...", "?!."), split into multiple sentences
    if (is_all_delimiters(content) && word_index == 0) {
        printf("SS (Write): Detected delimiter-only content '%s', creating multiple sentences\n", content);
        
        // Create multiple sentences from the delimiters
        WordNode* delimiter_sentences = create_delimiter_sentences(content);
        if (delimiter_sentences == NULL) {
            return NULL;
        }
        
        // Find the last sentence created
        WordNode* last_delim_sentence = delimiter_sentences;
        while (last_delim_sentence->next_sentence != NULL) {
            last_delim_sentence = last_delim_sentence->next_sentence;
        }
        
        // Handle empty document
        if (doc_head == NULL && sentence_index == 0) {
            return delimiter_sentences;
        }
        
        // Get the target sentence where we want to insert
        WordNode* sentence_head = get_sentence(doc_head, sentence_index);
        
        if (sentence_head == NULL && sentence_index > 0) {
            // Appending to end of document
            WordNode* prev_sentence = get_sentence(doc_head, sentence_index - 1);
            if (prev_sentence != NULL) {
                prev_sentence->next_sentence = delimiter_sentences;
                return doc_head;
            } else {
                printf("SS (Write): ERROR: Invalid sentence index %d (non-contiguous)\n", sentence_index);
                // Clean up
                free_document(delimiter_sentences);
                return NULL;
            }
        }
        
        if (sentence_head == NULL) {
            printf("SS (Write): ERROR: Invalid sentence index %d\n", sentence_index);
            free_document(delimiter_sentences);
            return NULL;
        }
        
        // Insert the delimiter sentences at the target position
        last_delim_sentence->next_sentence = sentence_head->next_sentence;
        
        if (sentence_index == 0) {
            delimiter_sentences->next_word = sentence_head;
            return delimiter_sentences;
        } else {
            WordNode* prev_sentence = get_sentence(doc_head, sentence_index - 1);
            prev_sentence->next_sentence = delimiter_sentences;
            delimiter_sentences->next_word = sentence_head;
            return doc_head;
        }
    }
    
    // Regular word insertion (existing logic)
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
