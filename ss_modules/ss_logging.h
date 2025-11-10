#ifndef SS_LOGGING_H
#define SS_LOGGING_H

#include <pthread.h>

/*
 * Logging Functions for Storage Server
 */

// Write a formatted message to the log file only (thread-safe)
void log_to_file(const char* client_addr, const char* client_type, const char* format, ...);

// Write a formatted message to both log file and terminal (thread-safe)
void log_message(const char* client_addr, const char* client_type, const char* format, ...);

// External reference to log mutex
extern pthread_mutex_t g_log_mutex;

#endif // SS_LOGGING_H
