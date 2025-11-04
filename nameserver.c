#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>
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


#define MAX_FILES 100
#define MAX_SERVERS 10
#define MAX_CLIENTS 50 

FileLocation file_directory[MAX_FILES];
StorageServer server_list[MAX_SERVERS];
ClientInfo client_list[MAX_CLIENTS]; 
int g_num_files = 0;
int g_num_servers = 0;
int g_num_clients = 0; 

pthread_mutex_t g_system_mutex = PTHREAD_MUTEX_INITIALIZER;


// --- HELPER FUNCTIONS FOR SS COMMUNICATION ---
// (forward_create_to_ss and forward_delete_to_ss functions are unchanged)

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

/*
 * --- NEW HELPER: Get Metadata from SS ---
 */
int get_metadata_from_ss(const char* ss_ip, int ss_nm_port, const char* filename, char* out_buffer, int out_len) {
    int ss_sock;
    struct sockaddr_in ss_addr;
    char command[BUFFER_SIZE];

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
    
    snprintf(command, sizeof(command), "GET_METADATA %s\n", filename);
    if (write(ss_sock, command, strlen(command)) < 0) {
        perror("NM (SS-Client): write to SS failed");
        close(ss_sock);
        return -1;
    }
    
    ssize_t bytes_read = read(ss_sock, out_buffer, out_len - 1);
    close(ss_sock); 
    
    if (bytes_read <= 0) {
        printf("NM (SS-Client): No response from SS for metadata.\n");
        return -1;
    }
    
    out_buffer[bytes_read] = '\0';
    
    if (strncmp(out_buffer, "METADATA_RESPONSE", 17) == 0) {
        return 0; // Success
    } else {
        return -1; // Failure
    }
}


// --- HELPER FUNCTION FOR PERMISSION CHECKING ---
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


// ---------------------------------------

/*
 * This is the function that each thread will run.
 */
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

    char buffer[BUFFER_SIZE];
    ssize_t bytes_read;

    bytes_read = read(client_socket, buffer, sizeof(buffer) - 1);
    if (bytes_read <= 0) {
        printf("Connection closed before identification.\n");
        close(client_socket);
        return NULL;
    }
    buffer[bytes_read] = '\0'; 

    // 3. Identify the connection type
    if (strncmp(buffer, "REGISTER_SS", 11) == 0) {
        // (This section is unchanged)
        printf("New connection from a Storage Server (%s).\n", peer_ip);
        
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
        }

        while ((token = strtok_r(NULL, " \n", &rest)) != NULL) {
            if (g_num_files < MAX_FILES) {
                FileLocation* new_file = &file_directory[g_num_files++];
                strncpy(new_file->filename, token, 255);
                new_file->filename[255] = '\0';
                new_file->ss_client_port = ss_client_port;
                strncpy(new_file->ss_ip_addr, peer_ip, INET_ADDRSTRLEN);
                
                strncpy(new_file->owner_username, "system", 255); 
                new_file->num_permissions = 0; 
                
                printf("  Registered file: %s\n", new_file->filename);
            }
        }
        
        pthread_mutex_unlock(&g_system_mutex);
        
        close(client_socket);
        printf("Storage Server registration complete. Connection closed.\n");
        return NULL; 
        
    } else if (strncmp(buffer, "REGISTER_CLIENT", 15) == 0) {
        // (This section is unchanged)
        printf("New connection from a Client (%s).\n", peer_ip);
        
        char username[256]; 
        if (sscanf(buffer, "REGISTER_CLIENT %s", username) != 1) {
            printf("  Failed to parse username. Closing connection.\n");
            close(client_socket);
            return NULL;
        }
        username[255] = '\0'; 

        pthread_mutex_lock(&g_system_mutex);
        if (g_num_clients < MAX_CLIENTS) {
            ClientInfo* new_client = &client_list[g_num_clients++];
            strncpy(new_client->username, username, 255);
            new_client->username[255] = '\0';
            strncpy(new_client->ip_addr, peer_ip, INET_ADDRSTRLEN);
            new_client->client_socket_fd = client_socket;
            printf("  User '%s' registered from %s.\n", username, peer_ip);
        } else {
            printf("  Max clients reached. Rejecting %s.\n", username);
        }
        pthread_mutex_unlock(&g_system_mutex);


        // 4. Enter a loop to handle commands from *this* client
        while ((bytes_read = read(client_socket, buffer, sizeof(buffer) - 1)) > 0) {
            buffer[bytes_read] = '\0';
            buffer[strcspn(buffer, "\n")] = 0; // Remove trailing newline

            if (strncmp(buffer, "VIEW", 4) == 0) {
                // --- **** NEW UPDATED VIEW LOGIC **** ---
                printf("Client requested VIEW\n");
                
                // Parse flags with correct precedence
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

                for (int i = 0; i < g_num_files; i++) {
                    int has_permission = check_permission(&file_directory[i], username, 'R');
                    
                    if (all_flag || has_permission) {
                        if (long_flag) {
                            char metadata_response[BUFFER_SIZE];
                            int words = 0, chars = 0;
                            char date[64] = "N/A", time[64] = "N/A";
                            
                            StorageServer* ss = NULL;
                            for (int j = 0; j < g_num_servers; j++) {
                                if (strcmp(server_list[j].ss_ip_addr, file_directory[i].ss_ip_addr) == 0 &&
                                    server_list[j].ss_client_port == file_directory[i].ss_client_port) {
                                    ss = &server_list[j];
                                    break;
                                }
                            }
                            
                            if (ss != NULL) {
                                pthread_mutex_unlock(&g_system_mutex);
                                int res = get_metadata_from_ss(ss->ss_ip_addr, ss->ss_nm_port, file_directory[i].filename, metadata_response, sizeof(metadata_response));
                                pthread_mutex_lock(&g_system_mutex);

                                if (res == 0) {
                                    sscanf(metadata_response, "METADATA_RESPONSE %d %d %63s %63s", &words, &chars, date, time);
                                }
                            }

                            char timestamp[128];
                            snprintf(timestamp, sizeof(timestamp), "%s %s", date, time);
                            
                            offset += sprintf(response_buffer + offset,
                                "| %-20s | %5d | %5d | %-16s | %-10s |\n",
                                file_directory[i].filename, words, chars, timestamp, file_directory[i].owner_username);
                            
                        } else {
                            // Simple listing
                            offset += sprintf(response_buffer + offset, "%s\n", file_directory[i].filename);
                        }
                        files_shown++;
                    }

                    if (offset > BUFFER_SIZE - 256) {
                        write(client_socket, response_buffer, offset);
                        offset = 0; 
                    }
                }
                pthread_mutex_unlock(&g_system_mutex);
                
                if (files_shown == 0 && offset == 0) {
                    offset += sprintf(response_buffer + offset, "No files to display.\n");
                }
                
                if (offset > 0) {
                    write(client_socket, response_buffer, offset);
                }
                // --- **** END OF UPDATED VIEW LOGIC **** ---
            
            } else if (strncmp(buffer, "READ", 4) == 0) {
                // (This section is unchanged)
                char filename[256];
                if (sscanf(buffer, "READ %s", filename) != 1) {
                    const char* err_msg = "ERROR: Invalid READ format. Use: READ <filename>\n";
                    write(client_socket, err_msg, strlen(err_msg));
                    continue;
                }
                
                printf("Client requested READ for '%s'\n", filename);

                int found = 0;
                char ss_ip[INET_ADDRSTRLEN];
                int ss_port;
                int permitted = 0; 

                pthread_mutex_lock(&g_system_mutex);
                for (int i = 0; i < g_num_files; i++) {
                    if (strcmp(file_directory[i].filename, filename) == 0) {
                        found = 1;
                        if (check_permission(&file_directory[i], username, 'R')) {
                            permitted = 1;
                            strncpy(ss_ip, file_directory[i].ss_ip_addr, INET_ADDRSTRLEN);
                            ss_port = file_directory[i].ss_client_port;
                        }
                        break;
                    }
                }
                pthread_mutex_unlock(&g_system_mutex);

                char response_buffer[BUFFER_SIZE];
                if (!found) {
                    snprintf(response_buffer, sizeof(response_buffer), "ERROR: File not found.\n");
                    printf("  File not found.\n");
                } else if (!permitted) {
                    snprintf(response_buffer, sizeof(response_buffer), "ERROR: Access Denied.\n");
                    printf("  Access Denied for user '%s'.\n", username);
                } else {
                    snprintf(response_buffer, sizeof(response_buffer), "SS_LOCATION %s %d\n", ss_ip, ss_port);
                    printf("  Access Granted. Sending location to client: %s %d\n", ss_ip, ss_port);
                }
                write(client_socket, response_buffer, strlen(response_buffer));
            
            } else if (strncmp(buffer, "STREAM", 6) == 0) {
                // (This section is unchanged)
                char filename[256];
                if (sscanf(buffer, "STREAM %s", filename) != 1) {
                    const char* err_msg = "ERROR: Invalid STREAM format. Use: STREAM <filename>\n";
                    write(client_socket, err_msg, strlen(err_msg));
                    continue;
                }
                
                printf("Client requested STREAM for '%s'\n", filename);

                int found = 0;
                char ss_ip[INET_ADDRSTRLEN];
                int ss_port;
                int permitted = 0; 

                pthread_mutex_lock(&g_system_mutex);
                for (int i = 0; i < g_num_files; i++) {
                    if (strcmp(file_directory[i].filename, filename) == 0) {
                        found = 1;
                        if (check_permission(&file_directory[i], username, 'R')) {
                            permitted = 1;
                            strncpy(ss_ip, file_directory[i].ss_ip_addr, INET_ADDRSTRLEN);
                            ss_port = file_directory[i].ss_client_port;
                        }
                        break;
                    }
                }
                pthread_mutex_unlock(&g_system_mutex);

                char response_buffer[BUFFER_SIZE];
                if (!found) {
                    snprintf(response_buffer, sizeof(response_buffer), "ERROR: File not found.\n");
                    printf("  File not found.\n");
                } else if (!permitted) {
                    snprintf(response_buffer, sizeof(response_buffer), "ERROR: Access Denied.\n");
                    printf("  Access Denied for user '%s'.\n", username);
                } else {
                    snprintf(response_buffer, sizeof(response_buffer), "SS_LOCATION %s %d\n", ss_ip, ss_port);
                    printf("  Access Granted. Sending location to client: %s %d\n", ss_ip, ss_port);
                }
                write(client_socket, response_buffer, strlen(response_buffer));

            } else if (strncmp(buffer, "CREATE", 6) == 0) {
                // (This section is unchanged)
                char filename[256];
                if (sscanf(buffer, "CREATE %s", filename) != 1) {
                    const char* err_msg = "ERROR: Invalid CREATE format. Use: CREATE <filename>\n";
                    write(client_socket, err_msg, strlen(err_msg));
                    continue;
                }
                printf("Client requested CREATE for '%s'\n", filename);

                int found = 0;
                pthread_mutex_lock(&g_system_mutex);
                for (int i = 0; i < g_num_files; i++) {
                    if (strcmp(file_directory[i].filename, filename) == 0) {
                        found = 1;
                        break;
                    }
                }
                if (found) {
                    pthread_mutex_unlock(&g_system_mutex);
                    const char* err_msg = "ERROR: File already exists.\n";
                    write(client_socket, err_msg, strlen(err_msg));
                    continue;
                }

                if (g_num_servers == 0) {
                    pthread_mutex_unlock(&g_system_mutex);
                    const char* err_msg = "ERROR: No Storage Servers available.\n";
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
                    pthread_mutex_lock(&g_system_mutex);
                    if (g_num_files < MAX_FILES) {
                        FileLocation* new_file = &file_directory[g_num_files++];
                        strncpy(new_file->filename, filename, 255);
                        new_file->ss_client_port = ss_client_port;
                        strncpy(new_file->ss_ip_addr, ss_ip, INET_ADDRSTRLEN);

                        strncpy(new_file->owner_username, username, 255); 
                        new_file->num_permissions = 0; 
                        
                        printf("  Successfully registered new file '%s' for owner '%s'\n", filename, username);
                        write(client_socket, "File created successfully.\n", sizeof("File created successfully.\n") - 1);
                    } else {
                        write(client_socket, "ERROR: NM file directory is full.\n", sizeof("ERROR: NM file directory is full.\n") - 1);
                    }
                    pthread_mutex_unlock(&g_system_mutex);
                } else {
                    write(client_socket, "ERROR: Storage Server failed to create file.\n", sizeof("ERROR: Storage Server failed to create file.\n") - 1);
                }
            
            } else if (strncmp(buffer, "DELETE", 6) == 0) {
                // (This section is unchanged)
                char filename[256];
                if (sscanf(buffer, "DELETE %s", filename) != 1) {
                    const char* err_msg = "ERROR: Invalid DELETE format. Use: DELETE <filename>\n";
                    write(client_socket, err_msg, strlen(err_msg));
                    continue;
                }
                printf("Client requested DELETE for '%s'\n", filename);

                int found_index = -1;
                char ss_ip[INET_ADDRSTRLEN];
                int ss_nm_port = -1;
                int permitted = 0; 

                pthread_mutex_lock(&g_system_mutex);
                for (int i = 0; i < g_num_files; i++) {
                    if (strcmp(file_directory[i].filename, filename) == 0) {
                        found_index = i;
                        if (strcmp(file_directory[i].owner_username, username) == 0) {
                            permitted = 1;
                            for (int j = 0; j < g_num_servers; j++) {
                                if (strcmp(server_list[j].ss_ip_addr, file_directory[i].ss_ip_addr) == 0 &&
                                    server_list[j].ss_client_port == file_directory[i].ss_client_port) 
                                {
                                    strncpy(ss_ip, server_list[j].ss_ip_addr, INET_ADDRSTRLEN);
                                    ss_nm_port = server_list[j].ss_nm_port;
                                    break;
                                }
                            }
                        }
                        break;
                    }
                }
                pthread_mutex_unlock(&g_system_mutex);

                if (found_index == -1) {
                    const char* err_msg = "ERROR: File not found.\n";
                    write(client_socket, err_msg, strlen(err_msg));
                    continue;
                }
                
                if (!permitted) {
                    const char* err_msg = "ERROR: Access Denied. Only the owner can delete a file.\n";
                    write(client_socket, err_msg, strlen(err_msg));
                    printf("  Access Denied for user '%s'. Not owner.\n", username);
                    continue;
                }

                if (ss_nm_port == -1) {
                    const char* err_msg = "ERROR: Could not find SS for file. Directory out of sync.\n";
                    write(client_socket, err_msg, strlen(err_msg));
                    continue;
                }

                int result = forward_delete_to_ss(ss_ip, ss_nm_port, filename);

                if (result == 0) {
                    pthread_mutex_lock(&g_system_mutex);
                    printf("  Deleting file at index %d\n", found_index);
                    for (int i = found_index; i < g_num_files - 1; i++) {
                        file_directory[i] = file_directory[i + 1];
                    }
                    g_num_files--;
                    
                    printf("  Successfully deleted file '%s' from directory.\n", filename);
                    write(client_socket, "File deleted successfully.\n", sizeof("File deleted successfully.\n") - 1);
                    pthread_mutex_unlock(&g_system_mutex);
                } else {
                    write(client_socket, "ERROR: Storage Server failed to delete file.\n", sizeof("ERROR: Storage Server failed to delete file.\n") - 1);
                }

            } else if (strncmp(buffer, "LIST", 4) == 0) {
                // (This section is unchanged)
                printf("Client requested LIST\n");
                
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

            } else if (strncmp(buffer, "ADDACCESS", 9) == 0) {
                // (This section is unchanged)
                char perm_char_str[2], target_filename[256], target_username[256];
                if (sscanf(buffer, "ADDACCESS %1s %s %s", perm_char_str, target_filename, target_username) != 3) {
                    const char* err_msg = "ERROR: Invalid format. Use: ADDACCESS <R|W> <filename> <username>\n";
                    write(client_socket, err_msg, strlen(err_msg));
                    continue;
                }
                char perm = perm_char_str[0];
                if (perm != 'R' && perm != 'W') {
                    const char* err_msg = "ERROR: Permission must be 'R' or 'W'.\n";
                    write(client_socket, err_msg, strlen(err_msg));
                    continue;
                }

                printf("User '%s' requested ADDACCESS %c for '%s' to user '%s'\n", username, perm, target_filename, target_username);

                pthread_mutex_lock(&g_system_mutex);
                int found_index = -1;
                for(int i=0; i < g_num_files; i++) {
                    if (strcmp(file_directory[i].filename, target_filename) == 0) {
                        found_index = i;
                        break;
                    }
                }

                if (found_index == -1) {
                    pthread_mutex_unlock(&g_system_mutex);
                    const char* err_msg = "ERROR: File not found.\n";
                    write(client_socket, err_msg, strlen(err_msg));
                    continue;
                }

                if (strcmp(file_directory[found_index].owner_username, username) != 0) {
                    pthread_mutex_unlock(&g_system_mutex);
                    const char* err_msg = "ERROR: You are not the owner of this file.\n";
                    write(client_socket, err_msg, strlen(err_msg));
                    continue;
                }

                if (file_directory[found_index].num_permissions < MAX_PERMISSIONS) {
                    AccessControl* new_perm = &file_directory[found_index].acl[file_directory[found_index].num_permissions++];
                    strncpy(new_perm->username, target_username, 255);
                    new_perm->permission = perm;
                    printf("  Access granted.\n");
                    write(client_socket, "Access granted successfully.\n", sizeof("Access granted successfully.\n") - 1);
                } else {
                    const char* err_msg = "ERROR: File has reached its maximum permission entries.\n";
                    write(client_socket, err_msg, strlen(err_msg));
                }
                pthread_mutex_unlock(&g_system_mutex);

            } else if (strncmp(buffer, "REMACCESS", 9) == 0) {
                // (This section is unchanged)
                char target_filename[256], target_username[256];
                if (sscanf(buffer, "REMACCESS %s %s", target_filename, target_username) != 2) {
                    const char* err_msg = "ERROR: Invalid format. Use: REMACCESS <filename> <username>\n";
                    write(client_socket, err_msg, strlen(err_msg));
                    continue;
                }

                printf("User '%s' requested REMACCESS for '%s' from user '%s'\n", username, target_filename, target_username);

                pthread_mutex_lock(&g_system_mutex);
                int file_index = -1;
                for(int i=0; i < g_num_files; i++) {
                    if (strcmp(file_directory[i].filename, target_filename) == 0) {
                        file_index = i;
                        break;
                    }
                }

                if (file_index == -1) {
                    pthread_mutex_unlock(&g_system_mutex);
                    const char* err_msg = "ERROR: File not found.\n";
                    write(client_socket, err_msg, strlen(err_msg));
                    continue;
                }

                if (strcmp(file_directory[file_index].owner_username, username) != 0) {
                    pthread_mutex_unlock(&g_system_mutex);
                    const char* err_msg = "ERROR: You are not the owner of this file.\n";
                    write(client_socket, err_msg, strlen(err_msg));
                    continue;
                }

                int perm_index = -1;
                for (int i = 0; i < file_directory[file_index].num_permissions; i++) {
                    if (strcmp(file_directory[file_index].acl[i].username, target_username) == 0) {
                        perm_index = i;
                        break;
                    }
                }

                if (perm_index == -1) {
                    pthread_mutex_unlock(&g_system_mutex);
                    const char* err_msg = "ERROR: That user does not have special permissions on this file.\n";
                    write(client_socket, err_msg, strlen(err_msg));
                    continue;
                }

                for (int i = perm_index; i < file_directory[file_index].num_permissions - 1; i++) {
                    file_directory[file_index].acl[i] = file_directory[file_index].acl[i + 1];
                }
                file_directory[file_index].num_permissions--;
                
                printf("  Access removed.\n");
                write(client_socket, "Access removed successfully.\n", sizeof("Access removed successfully.\n") - 1);
                pthread_mutex_unlock(&g_system_mutex);

            } else {
                const char* ack = "ERROR: Unknown command.\n";
                write(client_socket, ack, strlen(ack));
            }
        }
    } else {
        printf("Unknown connection type. Closing.\n");
    }

    // --- (Unchanged: Remove client on disconnect) ---
    pthread_mutex_lock(&g_system_mutex);
    int client_index = -1;
    for (int i = 0; i < g_num_clients; i++) {
        if (client_list[i].client_socket_fd == client_socket) {
            client_index = i;
            break;
        }
    }

    if (client_index != -1) {
        printf("User '%s' disconnected. Removing from list.\n", client_list[client_index].username);
        for (int i = client_index; i < g_num_clients - 1; i++) {
            client_list[i] = client_list[i + 1];
        }
        g_num_clients--;
    }
    pthread_mutex_unlock(&g_system_mutex);

    // 5. Clean up
    close(client_socket);
    printf("Connection closed.\n");
    return NULL;
}


/*
 * --- Main Server Function ---
 */
int main() {
    // (Unchanged)
    int server_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY; 
    server_addr.sin_port = htons(NAME_SERVER_PORT);

    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 10) < 0) { 
        perror("listen failed");
        exit(EXIT_FAILURE);
    }

    printf("Name Server listening on port %d\n", NAME_SERVER_PORT);

    while (1) {
        int client_socket = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_socket < 0) {
            perror("accept failed");
            continue; 
        }

        printf("Accepted new connection from %s:%d\n",
               inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

        pthread_t thread_id;
        int* p_client_socket = malloc(sizeof(int));
        *p_client_socket = client_socket;

        if (pthread_create(&thread_id, NULL, handle_connection, (void*)p_client_socket) != 0) {
            perror("pthread_create failed");
            free(p_client_socket);
            close(client_socket);
        }
    }

    close(server_fd);
    return 0;
}


