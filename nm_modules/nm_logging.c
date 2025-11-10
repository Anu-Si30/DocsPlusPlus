#include "nm_logging.h"
#include "nm_types.h"
#include <stdio.h>
#include <time.h>
#include <stdarg.h>

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
