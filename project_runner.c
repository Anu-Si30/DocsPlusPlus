#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

/*
 * C program to manage the build and concurrent execution of a distributed system.
 *
 * This program performs three main tasks:
 * 1. Build the project using 'make clean' and 'make all'.
 * 2. Launch the three executables in separate terminal windows concurrently.
 * 3. Use 'gnome-terminal' (common on Linux) to open new windows.
 * The '/bin/bash -c "..."' wrapper is used to ensure the terminal stays open
 * after the program runs, allowing you to view the output.
 *
 * NOTE: If you are not using a GNOME-based Linux distribution, you may need to
 * change 'gnome-terminal' to your system's preferred emulator (e.g., 'xterm', 'konsole', etc.)
 */

// Define the terminal command and target programs
#define TERMINAL_CMD "gnome-terminal"
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

    // Construct the command: 
    // gnome-terminal --title="Title" -- /bin/bash -c "./executable; exec bash"
    // The 'exec bash' keeps the window open after the executable finishes.
    snprintf(command, sizeof(command),
             "%s --title=\"%s\" -- /bin/bash -c \"%s; echo '--- Program finished ---'; exec bash\"",
             TERMINAL_CMD, title, executable);

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
    printf("You can close this main runner terminal now, but the other three must remain open.\n");

    return 0;
}