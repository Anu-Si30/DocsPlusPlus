#ifndef SS_DOCUMENT_H
#define SS_DOCUMENT_H

#include "ss_types.h"
#include <stdio.h>

/*
 * Document Processing Functions for Storage Server
 * Handle parsing, manipulation, and flattening of document structure
 */

// Create a new word node
WordNode* create_word_node(const char* word);

// Check if a character is a sentence delimiter
int is_delimiter(char c);

// Free an entire document structure
void free_document(WordNode* doc_head);

// Parse a file into a linked list structure
WordNode* parse_file_to_list(FILE* file);

// Flatten a document structure back to a file
void flatten_list_to_file(WordNode* doc_head, FILE* file);

// Get a specific sentence from the document
WordNode* get_sentence(WordNode* doc_head, int sentence_index);

// Insert a word at a specific position
WordNode* insert_word_at(WordNode* doc_head, int sentence_index, int word_index, const char* content);

// Get file metadata (word count, char count, timestamps)
int get_file_metadata(const char* file_path, int* word_count, int* char_count, 
                      char* created_ts, char* modified_ts, int ts_len);

#endif // SS_DOCUMENT_H
