#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>

#define PORT 5555
#define MAX_SIZE 20480 // 20KB, limit of image size

int sock; // Declare the socket as a global variable for signal handling

// Function to handle Ctrl+C signal
void handleSignal(int signum) {
    if (signum == SIGINT) {
        printf("\nCtrl+C pressed. Closing the connection.\n");
        close(sock);
        exit(EXIT_SUCCESS);
    }
}

// Function to receive a message from the server
void receiveMessage(int sock) {
    char message[1024];
    ssize_t bytesRead = recv(sock, message, sizeof(message), 0);

    if (bytesRead > 0) {
        message[bytesRead] = '\0';
        printf("%s", message);
    } else {
        perror("Error receiving message");
        exit(EXIT_FAILURE);
    }
}

// Function to send the file to the server
void sendFile(int sock, const char *image_name) {
    FILE *file = fopen(image_name, "rb");
    if (file == NULL) {
        perror("Error opening file for reading");
        return;
    }

    char buffer[MAX_SIZE];
    int bytesReadFile;

    while ((bytesReadFile = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        if (send(sock, buffer, bytesReadFile, 0) == -1) {
            perror("Error sending file");
            fclose(file);
            return;
        }
    }

    fclose(file);

    // Receive a success message from the server
    receiveMessage(sock);
}

// Function to close the connection gracefully
void closeConnection() {
    printf("Closing the connection.\n");
    close(sock);
    exit(EXIT_SUCCESS);
}

int main(void) {
    struct sockaddr_in sin;
    signal(SIGINT, handleSignal); // Register the signal handler

    // Create a socket
    sock = socket(AF_INET, SOCK_STREAM, 0);

    if (sock == -1) {
        perror("Error creating socket");
        exit(EXIT_FAILURE);
    }

    // Configure the connection
    sin.sin_addr.s_addr = inet_addr("127.0.0.1");
    sin.sin_family = AF_INET;
    sin.sin_port = htons(PORT);

    // Connect to the server
    if (connect(sock, (struct sockaddr *)&sin, sizeof(sin)) != -1) {
        // Welcome from server
        //receiveMessage(sock);

        while (1) {
            char image_name[255];
            printf("Enter the name of the image to send (or 'exit' to close): ");
            fgets(image_name, sizeof(image_name), stdin);
            image_name[strlen(image_name) - 1] = '\0'; // Remove the '\n' character at the end

            if (strcmp(image_name, "exit") == 0) {
                closeConnection();
            }

            // Check if the file exists
            if (access(image_name, F_OK) == -1) {
                printf("File '%s' does not exist. Please enter a valid file.\n", image_name);
                continue;
            }

            // Send the image to the server
            sendFile(sock, image_name);
        }

        // Close the connection
        closeConnection();
    } else {
        perror("Error connecting to the server");
        exit(EXIT_FAILURE);
    }

    return EXIT_SUCCESS;
}

