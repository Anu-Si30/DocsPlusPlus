#include "ss_logging.h"
#include "ss_types.h"
#include <stdio.h>
#include <time.h>
#include <stdarg.h>

pthread_mutex_t g_log_mutex = PTHREAD_MUTEX_INITIALIZER;

/*
 * Writes a formatted, detailed message ONLY to the log file.
 * This function is thread-safe.
 */
void log_to_file(const char* client_addr, const char* client_type, const char* format, ...) {
    time_t now = time(NULL);
    char time_buf[64];
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", localtime(&now));

    va_list args;
    va_start(args, format);

    pthread_mutex_lock(&g_log_mutex);
    
    FILE* log_file = fopen(SS_LOG_FILE, "a");
    if (log_file) {
        // [Timestamp] [Client: IP:Port] [Type: ClientType] Message
        fprintf(log_file, "[%s] [Client: %s] [Type: %s] ", time_buf, client_addr, client_type);
        vfprintf(log_file, format, args);
        fprintf(log_file, "\n");
        fclose(log_file);
    }
    
    pthread_mutex_unlock(&g_log_mutex);
    va_end(args);
}

/*
 * Writes a formatted message to the log file AND the terminal.
 * This function is thread-safe.
 */
void log_message(const char* client_addr, const char* client_type, const char* format, ...) {
    time_t now = time(NULL);
    char time_buf[64];
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", localtime(&now));

    va_list args_for_file;
    va_start(args_for_file, format);
    
    va_list args_for_stdout;
    va_copy(args_for_stdout, args_for_file); // Copy the argument list

    pthread_mutex_lock(&g_log_mutex);
    
    // --- 1. Log to terminal (human-readable status) ---
    // [Timestamp] [ClientType@IP:Port] Message
    printf("[%s] [%s@%s] ", time_buf, client_type, client_addr);
    vprintf(format, args_for_stdout);
    printf("\n");
    fflush(stdout); // Ensure it prints immediately

    // --- 2. Log to file (detailed record) ---
    FILE* log_file = fopen(SS_LOG_FILE, "a");
    if (log_file) {
        // [Timestamp] [Client: IP:Port] [Type: ClientType] Message
        fprintf(log_file, "[%s] [Client: %s] [Type: %s] ", time_buf, client_addr, client_type);
        vfprintf(log_file, format, args_for_file);
        fprintf(log_file, "\n");
        fclose(log_file);
    }
    
    pthread_mutex_unlock(&g_log_mutex);

    va_end(args_for_file);
    va_end(args_for_stdout);
}
