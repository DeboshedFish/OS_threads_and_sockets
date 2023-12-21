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
#include <semaphore.h>

#include "imgdist.h"

#define PORT 5555
#define MAX_SIZE 20480 // 20KB, limit of image size
#define BACKLOG 5
#define MAX_FILENAME_LENGTH 256
#define DISTANCE_THRESHOLD 999
#define NUM_THREADS 3

// Structure to pass information to the thread
typedef struct {
    int csock;
    struct sockaddr_in client_addr;
} ThreadArgs;

typedef struct {
    int distance;
    char best_name[MAX_FILENAME_LENGTH];
} ThreadResult;

sem_t sem;  // Semaphore to synchronize access to shared memory

ThreadResult sharedResults[NUM_THREADS];

void handleSignal(int signum) {
    if (signum == SIGINT) {
        printf("\nCtrl+C pressed. Closing the server.\n");
        exit(EXIT_SUCCESS);
    }
}

bool readImage(const char *filename, char **imageData, size_t *imageSize) {
    // ... (unchanged)
}

void freeImage(char *imageData) {
    free(imageData);
}

void *compareImages(void *arg) {
    ThreadArgs *threadArgs = (ThreadArgs *)arg;

    char buffer[MAX_SIZE];
    ssize_t bytesRead;

    if ((bytesRead = recv(threadArgs->csock, buffer, sizeof(buffer), 0)) > 0) {
        // buffer contains image data

        uint64_t raw_hash;
        if (!PHashRaw(buffer, bytesRead, &raw_hash)) {
            fprintf(stderr, "Error hashing raw image\n");
            pthread_exit(NULL);
        }

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
                        int distance = DistancePHash(raw_hash, db_hash);

                        if (final_result > distance) {
                            final_result = distance;
                            strcpy(best_name, dbimg_name);
                            printf("The closest image so far is: %s with a distance of %d\n", best_name, distance);
                        }
                    } else {
                        perror("Error hashing database image");
                    }
                }
            }

            closedir(dir);

            ThreadResult result;
            result.distance = final_result;
            strcpy(result.best_name, best_name);

            // Synchronize access to shared memory
            sem_wait(&sem);
            sharedResults[threadArgs->csock % NUM_THREADS] = result;
            sem_post(&sem);

        } else {
            perror("Error opening img directory");
        }
    }

    close(threadArgs->csock);
    free(threadArgs);
    pthread_exit(NULL);
}

void *handleClient(void *arg) {
    ThreadArgs *threadArgs = (ThreadArgs *)arg;

    while (1) {
        pthread_t threads[NUM_THREADS];

        for (int i = 0; i < NUM_THREADS; ++i) {
            if (pthread_create(&threads[i], NULL, compareImages, (void *)threadArgs) != 0) {
                perror("Error creating thread");
                continue;
            }
        }

        // Wait for all threads to finish
        for (int i = 0; i < NUM_THREADS; ++i) {
            pthread_join(threads[i], NULL);
        }

        // Retrieve the result with the smallest distance from shared memory
        sem_wait(&sem);
        int minDistance = DISTANCE_THRESHOLD;
        char bestName[MAX_FILENAME_LENGTH];

        for (int i = 0; i < NUM_THREADS; ++i) {
            if (sharedResults[i].distance < minDistance) {
                minDistance = sharedResults[i].distance;
                strcpy(bestName, sharedResults[i].best_name);
            }
        }

        sem_post(&sem);

        if (minDistance < DISTANCE_THRESHOLD) {
            char resultMessage[MAX_SIZE];
            snprintf(resultMessage, sizeof(resultMessage), "Most similar image found: '%s' with a distance of %d.\n", bestName, minDistance);
            send(threadArgs->csock, resultMessage, strlen(resultMessage), 0);
        } else {
            const char *noSimilarImageMessage = "No similar image found (no comparison could be performed successfully).\n";
            send(threadArgs->csock, noSimilarImageMessage, strlen(noSimilarImageMessage), 0);
        }
    }
}

int main(void) {
    signal(SIGINT, handleSignal);

    sem_init(&sem, 0, 1);  // Initialize semaphore

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

