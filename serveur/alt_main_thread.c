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
#include <errno.h>

#include "imgdist.h"

#define PORT 5555
#define MAX_SIZE 20480 // 20KB, limit of image size
#define BACKLOG 5
#define MAX_FILENAME_LENGTH 256
#define DISTANCE_THRESHOLD 999
#define NUM_THREADS 3
#define NUM_DB_IMG 101

// Structure to pass information to the thread
typedef struct {
    int csock;
    struct sockaddr_in client_addr;
    int thread_id;
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

/*
bool readImage(const char *filename, char **imageData, size_t *imageSize) {
    // ... (unchanged)
}
*/

void freeImage(char *imageData) {
    free(imageData);
}

void *compareImages(void *arg) {
	ThreadArgs *threadArgs = (ThreadArgs *)arg;

    int threadId = threadArgs->thread_id;
    int numThreads = NUM_THREADS;

    // Calculate the range of images for this thread
    int startImage = threadId * (NUM_DB_IMG / numThreads);
    int endImage = (threadId + 1) * (NUM_DB_IMG / numThreads);

    char buffer[MAX_SIZE];
    ssize_t bytesRead;
    if ((bytesRead = recv(threadArgs->csock, buffer, sizeof(buffer), 0)) > 0) {
		printf("Thread %d: Received %zd bytes\n", threadId, bytesRead);

        // buffer contains image data

        uint64_t raw_hash;
        if (!PHashRaw(buffer, bytesRead, &raw_hash)) {
			fprintf(stderr, "Error hashing raw image\n");
			printf("Thread %d: Exiting thread due to hashing error\n", threadId);
			pthread_exit(NULL);
        }
        
        DIR *dir;
        struct dirent *ent;
        
        int final_result = DISTANCE_THRESHOLD;
        char best_name[MAX_FILENAME_LENGTH];
        
        if ((dir = opendir("img/")) != NULL) {
        int imageCounter = 0;  // Counter for images processed by this thread

        while ((ent = readdir(dir)) != NULL && imageCounter < endImage) {
            if (ent->d_type == DT_REG) {
                if (imageCounter >= startImage) {  // Process images within the assigned range
                    char dbimg_name[MAX_FILENAME_LENGTH];
                    snprintf(dbimg_name, sizeof(dbimg_name), "img/%s", ent->d_name);

                    uint64_t db_hash;
                    if (PHash(dbimg_name, &db_hash)) {
                        int distance = DistancePHash(raw_hash, db_hash);

                        if (final_result > distance) {
                            final_result = distance;
                            strcpy(best_name, dbimg_name);
                            printf("Thread %d: The closest image so far is: %s with a distance of %d\n", threadId, best_name, distance);
                        }
                    } else {
                        printf("Thread %d: Error hashing database image", threadId);
                    }
                }

                imageCounter++;
            }
        }
        
        if (ent == NULL) {
			printf("Thread %d: Reached end of directory\n", threadId);
		} else {
			printf("Thread %d: Loop terminated. imageCounter = %d, endImage = %d\n", threadId, imageCounter, endImage);
		}

		// Check errno after readdir
		if (errno) {
			perror("Thread %d: Error reading directory");
		}

		printf("Thread %d: CLOSING DIRECTORY\n", threadId);
		closedir(dir);
		printf("Thread %d: DIRECTORY CLOSED\n", threadId);

		ThreadResult result;
		result.distance = final_result;
		strcpy(result.best_name, best_name);

		// Synchronize access to shared memory
		sem_wait(&sem);
		sharedResults[threadId] = result;
		printf("Thread %d: SYNCHRONIZED MEMORY\n", threadId);
		sem_post(&sem);

        } else {
               printf("Thread %d: Error in recv. Errno: %d\n", threadId, errno);
				close(threadArgs->csock);
				free(threadArgs);
				pthread_exit(NULL);
		}
		}
		else if (bytesRead == 0) {
			// Connection closed by the sender
			printf("Thread %d: Connection closed by the sender\n", threadId);
			close(threadArgs->csock);
			free(threadArgs);
			pthread_exit(NULL);
		} else {
			perror("Thread %d: Error in recv");
			close(threadArgs->csock);
			free(threadArgs);
			pthread_exit(NULL);
	}
    
    printf("1");
    close(threadArgs->csock);
    printf("2");
    free(threadArgs);
    printf("3");
    pthread_exit(NULL);
    printf("4\n");
}

void *handleClient(void *arg) {
    ThreadArgs *threadArgs = (ThreadArgs *)arg;

    while (1) {
        printf("Thread %d: Before main loop iteration\n", threadArgs->thread_id);

        pthread_t threads[NUM_THREADS];
        ThreadArgs argsForThread[NUM_THREADS];

        // Create threads
        for (int i = 0; i < NUM_THREADS; ++i) {
            // Allocate memory for the current thread's arguments
            ThreadArgs *argsForThread = (ThreadArgs *)malloc(sizeof(ThreadArgs));
            if (argsForThread == NULL) {
                perror("Error allocating memory for ThreadArgs");
                exit(EXIT_FAILURE);  // Handle the error appropriately
            }

            *argsForThread = *threadArgs;  // Copy the original threadArgs
            argsForThread->thread_id = i;   // Assign a unique threadId for each thread

            printf("Thread %d: Before thread creation\n", argsForThread->thread_id);
            if (pthread_create(&threads[i], NULL, compareImages, (void *)argsForThread) != 0) {
                perror("Error creating thread");
                free(argsForThread);  // Free allocated memory in case of an error
                exit(EXIT_FAILURE);
            }
            printf("Thread %d: Thread created\n", argsForThread->thread_id);
        }
        printf("Threads created\n");

        // Wait for threads to finish
        for (int i = 0; i < NUM_THREADS; ++i) {
            printf("Thread %d: Before thread join\n", i);
            pthread_join(threads[i], NULL);
            printf("Thread %d: After thread join\n", i);
        }

        printf("Threads joined\n");

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

        printf("BEST RESULT COLLECTED. Min Distance : %d\n", minDistance);

        if (minDistance < DISTANCE_THRESHOLD) {
            printf("FOUND A GOOD MINDISTANCE\n");
            char resultMessage[MAX_SIZE];
            snprintf(resultMessage, sizeof(resultMessage), "Most similar image found: '%s' with a distance of %d.\n", bestName, minDistance);
            send(threadArgs->csock, resultMessage, strlen(resultMessage), 0);
        } else {
            const char *noSimilarImageMessage = "No similar image found (no comparison could be performed successfully).\n";
            send(threadArgs->csock, noSimilarImageMessage, strlen(noSimilarImageMessage), 0);
        }

        printf("Thread %d: Exiting loop\n", threadArgs->thread_id);
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
