#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>

/*
 * C program to manage the build and concurrent execution of a distributed system.
 *
 * This program performs three main tasks:
 * 1. Build the project using 'make clean' and 'make all'.
 * 2. Launch the three executables in separate terminal windows concurrently.
 * 3. Use 'xterm' (lightweight, widely available) to open new windows.
 * The '-hold' option keeps the terminal open after the program finishes.
 *
 * NOTE: This version uses xterm to avoid snap package conflicts with gnome-terminal.
 * If xterm is not installed, run: sudo apt install xterm
 * 
 * ALTERNATIVES:
 * - For gnome-terminal (if not snap): Change TERMINAL_CMD to "gnome-terminal --"
 * - For konsole: Change to "konsole -e"
 * - For xfce4-terminal: Change to "xfce4-terminal -e"
 */

// Define the terminal command and target programs
// Using xterm for better compatibility (avoids snap conflicts)
#define TERMINAL_CMD "xterm"
#define NAMESERVER_TITLE "1. Name Server"
#define STORAGESERVER_TITLE "2. Storage Server"
#define CLIENT_TITLE "3. Client"
#define NAMESERVER_EXEC "./nameserver"
#define STORAGESERVER_EXEC "./storageserver"
#define CLIENT_EXEC "./client"

// Function to safely execute a system command and check the result
int run_command(const char *cmd) {
    printf("Executing: %s\n", cmd);
    int status = system(cmd);
    if (status == -1) {
        perror("Error executing command");
        return 1;
    } else if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
        printf("Command failed with exit code: %d\n", WEXITSTATUS(status));
        return 1;
    }
    return 0;
}

// Function to construct and launch a program in a new terminal window
void launch_terminal(const char *title, const char *executable) {
    // Buffer to hold the full command string
    char command[512]; 

    // Using xterm for maximum compatibility (avoids snap gnome-terminal issues)
    // xterm -hold keeps window open after program exits
    // -T sets the title, -e executes the command
    // & at the end runs it in background so we can launch multiple terminals
    
    snprintf(command, sizeof(command),
             "%s -T \"%s\" -hold -e bash -c 'echo \"=== %s ===\"; echo; %s; echo; echo \"--- Program finished (exit code: $?) ---\"' &",
             TERMINAL_CMD, title, title, executable);

    printf("Launching %s in new terminal...\n", title);

    // Use system() to run the terminal command asynchronously
    if (system(command) == -1) {
        perror("Could not launch terminal command");
    }
}


int main() {
    printf("--- Project Build and Execution Manager ---\n");

    // 1. Build Phase: Execute make clean and make all sequentially
    if (run_command("make clean") != 0) {
        fprintf(stderr, "Build failed at 'make clean'. Aborting.\n");
        return 1;
    }
    
    if (run_command("make all") != 0) {
        fprintf(stderr, "Build failed at 'make all'. Aborting.\n");
        return 1;
    }

    printf("\nBuild successful. Starting services...\n");
    sleep(1); // Small pause for better terminal display flow

    // 2. Execution Phase: Launch services concurrently in new windows

    // Start Name Server (often the first component)
    launch_terminal(NAMESERVER_TITLE, NAMESERVER_EXEC);
    sleep(2); // Wait a moment for the server to initialize (critical for networking)

    // Start Storage Server
    launch_terminal(STORAGESERVER_TITLE, STORAGESERVER_EXEC);
    sleep(2); // Wait a moment for the server to initialize and potentially connect

    // Start Client
    launch_terminal(CLIENT_TITLE, CLIENT_EXEC);
    
    printf("\nAll services launched concurrently in separate windows.\n");
    
    // 3. Interactive Menu: Allow launching additional clients
    printf("\n=== Interactive Control Menu ===\n");
    printf("Commands:\n");
    printf("  new      - Launch a new client terminal\n");
    printf("  client   - Launch a new client terminal\n");
    printf("  status   - Show running processes\n");
    printf("  help     - Show this menu\n");
    printf("  quit     - Exit this manager (services will keep running)\n");
    printf("  exit     - Exit this manager (services will keep running)\n");
    printf("\nType a command: ");
    
    char input[256];
    int client_count = 1; // We already launched one client
    
    while (1) {
        if (fgets(input, sizeof(input), stdin) == NULL) {
            break; // EOF or error
        }
        
        // Remove newline
        input[strcspn(input, "\n")] = 0;
        
        // Convert to lowercase for comparison
        for (int i = 0; input[i]; i++) {
            input[i] = tolower(input[i]);
        }
        
        if (strcmp(input, "new") == 0 || strcmp(input, "client") == 0) {
            client_count++;
            char title[128];
            snprintf(title, sizeof(title), "%d. Client #%d", 2 + client_count, client_count);
            launch_terminal(title, CLIENT_EXEC);
            printf("Launched new client terminal (#%d)\n", client_count);
            printf("\nType a command: ");
            
        } else if (strcmp(input, "status") == 0) {
            printf("\nChecking running processes...\n");
            system("ps aux | grep -E 'xterm.*nameserver|xterm.*storageserver|xterm.*client|./nameserver|./storageserver|./client' | grep -v grep | grep -v 'project_runner'");
            printf("\nType a command: ");
            
        } else if (strcmp(input, "help") == 0) {
            printf("\nCommands:\n");
            printf("  new/client - Launch a new client terminal\n");
            printf("  status     - Show running processes\n");
            printf("  help       - Show this menu\n");
            printf("  quit/exit  - Exit this manager\n");
            printf("\nType a command: ");
            
        } else if (strcmp(input, "quit") == 0 || strcmp(input, "exit") == 0) {
            printf("\nExiting manager. Services will continue running in their terminals.\n");
            printf("Close individual terminal windows to stop services.\n");
            break;
            
        } else if (strlen(input) > 0) {
            printf("Unknown command: '%s'. Type 'help' for available commands.\n", input);
            printf("\nType a command: ");
        } else {
            printf("\nType a command: ");
        }
    }

    return 0;
}