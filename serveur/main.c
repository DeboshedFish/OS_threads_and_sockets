#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <inttypes.h>
#include <dirent.h>
#include <signal.h>

#include "imgdist.h"

#define PORT 5555
#define MAX_SIZE 20480 // 20KB, limit of image size
#define BACKLOG 5
#define MAX_FILENAME_LENGTH 256
#define DISTANCE_THRESHOLD 999

// Structure to pass information to the thread
typedef struct {
    int csock;
    struct sockaddr_in client_addr;
} ThreadArgs;

void handleSignal(int signum) {
    if (signum == SIGINT) {
        printf("\nCtrl+C pressed. Closing the server.\n");
        exit(EXIT_SUCCESS);
    }
}

bool readImage(const char *filename, char **imageData, size_t *imageSize) {
    FILE *file = fopen(filename, "rb");
    if (file == NULL) {
        perror("Error opening file for reading");
        return false;
    }

    fseek(file, 0, SEEK_END);
    *imageSize = ftell(file);
    rewind(file);

    *imageData = (char *)malloc(*imageSize);
    if (*imageData == NULL) {
        perror("Error allocating memory for image data");
        fclose(file);
        return false;
    }

    size_t bytesRead = fread(*imageData, 1, *imageSize, file);
    if (bytesRead != *imageSize) {
        perror("Error reading image data");
        free(*imageData);
        fclose(file);
        return false;
    }

    fclose(file);
    return true;
}

void freeImage(char *imageData) {
    free(imageData);
}

void compareAndSendResult(int csock, const char *raw_image_data, size_t raw_image_size) {
    if (raw_image_data == NULL) {
        fprintf(stderr, "raw_image_data is NULL\n");
        return;
    }

    printf("Comparing images...\n");

    uint64_t raw_hash;

    if (!PHashRaw(raw_image_data, raw_image_size, &raw_hash)) {
        fprintf(stderr, "Error hashing raw image\n");
        return;
    }

    uint64_t db_result;
    int final_result = DISTANCE_THRESHOLD;
    char best_name[MAX_FILENAME_LENGTH];

    printf("Searching for the most similar image...\n");

    DIR *dir;
    struct dirent *ent;

    if ((dir = opendir("img/")) != NULL) {
        while ((ent = readdir(dir)) != NULL) {
            if (ent->d_type == DT_REG) {
                char dbimg_name[MAX_FILENAME_LENGTH];
                snprintf(dbimg_name, sizeof(dbimg_name), "img/%s", ent->d_name);

                uint64_t db_hash;
                if (PHash(dbimg_name, &db_hash)) {
                    db_result = db_hash;
                } else {
                    perror("Error hashing database image");
                    continue;
                }

                int distance = DistancePHash(raw_hash, db_result);
                if (final_result > distance) {
                    final_result = distance;
                    strcpy(best_name, dbimg_name);
                    printf("The closest image so far is: %s with a distance of %d\n", best_name, distance);
                }
            }
        }

        closedir(dir);

        if (best_name != NULL) {
            char resultMessage[MAX_SIZE];
            snprintf(resultMessage, sizeof(resultMessage), "Most similar image found: '%s' with a distance of %d.\n", best_name, final_result);
            send(csock, resultMessage, strlen(resultMessage), 0);
        } else {
            const char *noSimilarImageMessage = "No similar image found (no comparison could be performed successfully).\n";
            send(csock, noSimilarImageMessage, strlen(noSimilarImageMessage), 0);
        }
    } else {
        perror("Error opening img directory");
    }
}

void *handleClient(void *arg) {
    ThreadArgs *threadArgs = (ThreadArgs *)arg;

    //const char *welcomeMessage = "Welcome to the server! Please send an image.\n";
    //send(threadArgs->csock, welcomeMessage, strlen(welcomeMessage), 0);

    while (1) {
        char buffer[MAX_SIZE];
        ssize_t bytesRead;

        if ((bytesRead = recv(threadArgs->csock, buffer, sizeof(buffer), 0)) > 0) {
            // buffer contains image data

            // Perform image comparison and send result to the client
            compareAndSendResult(threadArgs->csock, buffer, bytesRead);

            //const char *successMessage = "Image received successfully! Send another image or type 'exit' to close the connection.\n";
            //send(threadArgs->csock, successMessage, strlen(successMessage), 0);

            char exitCommand[5];
            bytesRead = recv(threadArgs->csock, exitCommand, sizeof(exitCommand), 0);
            if (bytesRead > 0 && strncmp(exitCommand, "exit", 4) == 0) {
                break;  // Terminate the connection if the client sends "exit"
            }
        }
    }

    printf("Closing the client socket %d\n", threadArgs->csock);
    close(threadArgs->csock);
    free(threadArgs);
    pthread_exit(NULL);
}

int main(void) {
    signal(SIGINT, handleSignal);

    int sock, csock;
    struct sockaddr_in sin, csin;
    socklen_t recsize = sizeof(sin);
    socklen_t crecsize = sizeof(csin);
    int sock_err;

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        perror("Error creating socket");
        exit(EXIT_FAILURE);
    }

    sin.sin_addr.s_addr = htonl(INADDR_ANY);
    sin.sin_family = AF_INET;
    sin.sin_port = htons(PORT);

    sock_err = bind(sock, (struct sockaddr *)&sin, recsize);
    if (sock_err == -1) {
        perror("Error binding socket");
        close(sock);
        exit(EXIT_FAILURE);
    }

    sock_err = listen(sock, BACKLOG);
    if (sock_err == -1) {
        perror("Error listening for connections");
        close(sock);
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port %d...\n", PORT);

    while (1) {
        printf("Waiting for a client to connect...\n");
        csock = accept(sock, (struct sockaddr *)&csin, &crecsize);

        if (csock == -1) {
            perror("Error accepting connection");
            close(sock);
            exit(EXIT_FAILURE);
        }

        printf("Client connected with socket %d from %s:%d\n", csock, inet_ntoa(csin.sin_addr), ntohs(csin.sin_port));

        pthread_t thread;
        ThreadArgs *threadArgs = malloc(sizeof(*threadArgs));
        threadArgs->csock = csock;
        threadArgs->client_addr = csin;

        if (pthread_create(&thread, NULL, handleClient, (void *)threadArgs) != 0) {
            perror("Error creating thread");
            close(csock);
            free(threadArgs);
        } else {
            pthread_detach(thread);
        }
    }

    // The program should never reach this line
    printf("Closing the server socket\n");
    close(sock);

    return EXIT_SUCCESS;
}

