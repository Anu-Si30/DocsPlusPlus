#ifndef NM_LOGGING_H
#define NM_LOGGING_H

#include <pthread.h>

/*
 * Logging Functions for Name Server
 */

// Write a formatted message to the log file (thread-safe)
void log_to_file(const char* client_addr, const char* username, const char* format, ...);

// External reference to log mutex
extern pthread_mutex_t g_log_mutex;

#endif // NM_LOGGING_H
