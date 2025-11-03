#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include "common.h"

/*
 * This function connects to the Storage Server, requests the file,
 * and prints the content to stdout.
 */
void fetch_file_from_ss(const char* ss_ip, int ss_port, const char* filename) {
    int ss_sock;
    struct sockaddr_in ss_addr;

    printf("--- Connecting to Storage Server at %s:%d...\n", ss_ip, ss_port);

    // 1. Create socket
    if ((ss_sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Client (SS): socket creation failed");
        return;
    }

    // 2. Set up SS address
    ss_addr.sin_family = AF_INET;
    ss_addr.sin_port = htons(ss_port);
    if (inet_pton(AF_INET, ss_ip, &ss_addr.sin_addr) <= 0) {
        perror("Client (SS): invalid address");
        close(ss_sock);
        return;
    }

    // 3. Connect to SS
    if (connect(ss_sock, (struct sockaddr*)&ss_addr, sizeof(ss_addr)) < 0) {
        perror("Client (SS): connection to Storage Server failed");
        close(ss_sock);
        return;
    }

    // 4. Send the file request to the SS
    char request_msg[BUFFER_SIZE];
    snprintf(request_msg, sizeof(request_msg), "READ_FILE %s\n", filename);
    if (write(ss_sock, request_msg, strlen(request_msg)) < 0) {
        perror("Client (SS): failed to send file request");
        close(ss_sock);
        return;
    }

    // 5. Read the file content back and print to stdout
    char file_buffer[BUFFER_SIZE];
    ssize_t bytes_read;
    printf("--- File Content ---\n");
    while ((bytes_read = read(ss_sock, file_buffer, sizeof(file_buffer) - 1)) > 0) {
        file_buffer[bytes_read] = '\0';
        printf("%s", file_buffer); // Print the chunk
    }
    printf("\n--- End of File ---\n");

    if (bytes_read < 0) {
        perror("Client (SS): read from SS failed");
    }

    close(ss_sock);
}


/*
 * Connects to the Name Server and returns the socket descriptor.
 */
int connect_to_name_server() {
    int sock;
    struct sockaddr_in nm_addr;

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Client: socket creation failed");
        return -1;
    }

    nm_addr.sin_family = AF_INET;
    nm_addr.sin_port = htons(NAME_SERVER_PORT);
    if (inet_pton(AF_INET, "127.0.0.1", &nm_addr.sin_addr) <= 0) {
        perror("Client: invalid address");
        close(sock);
        return -1;
    }

    if (connect(sock, (struct sockaddr*)&nm_addr, sizeof(nm_addr)) < 0) {
        perror("Client: connection to Name Server failed");
        close(sock);
        return -1;
    }

    printf("Connected to Name Server!\n");
    return sock;
}

/*
 * Main loop to handle user commands.
 */
void command_loop(int nm_socket) {
    char command_buffer[BUFFER_SIZE];
    char response_buffer[BUFFER_SIZE];

    while (1) {
        printf("Docs++ > ");
        
        if (fgets(command_buffer, sizeof(command_buffer), stdin) == NULL) {
            printf("Error reading input or EOF reached. Exiting.\n");
            break;
        }

        // --- NEW LOGIC: Parse command *before* sending ---
        char command[64], arg1[256];
        int is_read_cmd = 0;
        
        // Check if it's a READ command
        if (sscanf(command_buffer, "%s %s", command, arg1) == 2) {
            if (strcmp(command, "READ") == 0) {
                is_read_cmd = 1;
            }
        }

        // 3. Send the command to the Name Server
        if (write(nm_socket, command_buffer, strlen(command_buffer)) < 0) {
            perror("Failed to send command to NM");
            break;
        }

        // 4. Read the response from the Name Server
        ssize_t bytes_read = read(nm_socket, response_buffer, sizeof(response_buffer) - 1);
        if (bytes_read <= 0) {
            printf("Name Server closed the connection. Exiting.\n");
            break;
        }
        response_buffer[bytes_read] = '\0'; // Null-terminate

        // 5. Check response and act
        if (is_read_cmd && strncmp(response_buffer, "SS_LOCATION", 11) == 0) {
            // This is the special response for our READ command
            char ss_ip[INET_ADDRSTRLEN];
            int ss_port;
            if (sscanf(response_buffer, "SS_LOCATION %s %d", ss_ip, &ss_port) == 2) {
                // We got the location, now fetch the file
                fetch_file_from_ss(ss_ip, ss_port, arg1); // arg1 is the filename
            } else {
                printf("Error: Could not parse SS_LOCATION response.\n");
            }
        } else {
            // This is a normal response (VIEW, ERROR, etc.)
            printf("%s", response_buffer);
        }
    }
}

int main() {
    char username[256];

    printf("Enter your username: ");
    if (fgets(username, sizeof(username), stdin) == NULL) {
        perror("Failed to read username");
        exit(EXIT_FAILURE);
    }
    username[strcspn(username, "\n")] = 0; // Remove trailing newline

    int nm_socket = connect_to_name_server();
    if (nm_socket < 0) {
        exit(EXIT_FAILURE);
    }

    char reg_msg[BUFFER_SIZE];
    snprintf(reg_msg, sizeof(reg_msg), "REGISTER_CLIENT %s\n", username);
    
    if (write(nm_socket, reg_msg, strlen(reg_msg)) < 0) {
        perror("Failed to send client registration");
        close(nm_socket);
        exit(EXIT_FAILURE);
    }

    command_loop(nm_socket);

    close(nm_socket);
    return 0;
}