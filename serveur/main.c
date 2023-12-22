#include <sys/types.h>
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
#define THREAD_COUNT 3

pthread_mutex_t memoLock = PTHREAD_MUTEX_INITIALIZER;
int bestDistance = DISTANCE_THRESHOLD;
char bestImage[MAX_FILENAME_LENGTH];

// Structure to pass information to the thread
typedef struct {
    int threadIndex;
    int totalImages;
    uint64_t rawHash;
    int csock;
} ThreadArgs;

void handleSignal(int signum) {
    if (signum == SIGINT) {
        printf("\nCtrl+C pressed. Closing the server.\n");
        exit(EXIT_SUCCESS);
    }
}

void *imageProcessingThread(void *arg) {
    ThreadArgs *threadArgs = (ThreadArgs *)arg;
    int startIndex = threadArgs->threadIndex * threadArgs->totalImages;
    int endIndex = startIndex + threadArgs->totalImages - 1;
    uint64_t raw_hash = threadArgs->rawHash;
    int csock = threadArgs->csock;

    int localBestDistance = DISTANCE_THRESHOLD;
    char localBestImage[MAX_FILENAME_LENGTH];
    
    printf("Entering db for loop");
    for (int i = startIndex; i <= endIndex; ++i) {
        char dbimg_name[MAX_FILENAME_LENGTH];
        snprintf(dbimg_name, sizeof(dbimg_name), "img/%d.bmp", i);

        uint64_t db_hash;
        if (PHash(dbimg_name, &db_hash)) {
            int distance = DistancePHash(raw_hash, db_hash);
            // printf("image number %d has a distance of %d from raw image\n", i, distance);
            if (localBestDistance > distance) {
				printf("Current closest image is number %d at a distance of %d from raw image\n", i, localBestDistance);
                localBestDistance = distance;
                strcpy(localBestImage, dbimg_name);
            } 
        } else {
            perror("Error hashing database image");
        }
    }

    // Update the global best result
    pthread_mutex_lock(&memoLock);
    if (localBestDistance < bestDistance) {
        bestDistance = localBestDistance;
        strcpy(bestImage, localBestImage);
        
    }
    pthread_mutex_unlock(&memoLock);
        
	if (bestDistance < DISTANCE_THRESHOLD) {
		printf("Min distance found\n");
		char resultMessage[MAX_SIZE];
		snprintf(resultMessage, sizeof(resultMessage), "Most similar image found: '%s' with a distance of %d.\n", bestImage, bestDistance);
		send(csock, resultMessage, strlen(resultMessage), 0);
	} else {
		const char *noSimilarImageMessage = "No similar image found (no comparison could be performed successfully).\n";
		send(csock, noSimilarImageMessage, strlen(noSimilarImageMessage), 0);
	}


    pthread_exit(NULL);
}

void compareAndSendResult(int csock, const char *raw_image_data, size_t raw_image_size, int totalImages) {
    uint64_t raw_hash;
    
    if (!PHashRaw(raw_image_data, raw_image_size, &raw_hash)) {
        fprintf(stderr, "Error hashing raw image\n");
        return;
    }
    printf("Raw hash value: %ld\n", raw_hash);

    int imagesPerThread = totalImages / THREAD_COUNT;
    int remainingImages = totalImages % THREAD_COUNT;
    

    pthread_t processThreads[THREAD_COUNT];
    ThreadArgs threadArgs[THREAD_COUNT];

    for (int i = 0; i < THREAD_COUNT; ++i) {
		threadArgs[i].rawHash = raw_hash;
        threadArgs[i].threadIndex = i;
        threadArgs[i].totalImages = imagesPerThread + (i < remainingImages ? 1 : 0);
        threadArgs[i].csock = csock;

        pthread_create(&processThreads[i], NULL, imageProcessingThread, &threadArgs[i]);
    }

    for (int i = 0; i < THREAD_COUNT; ++i) {
        pthread_join(processThreads[i], NULL);
        printf("Process thread %d joined\n", i);
    }

    // Send the best result to the client
    printf("Sending result to client\n"); 

}


// Fonction pour compter le nombre d'images dans le répertoire
int countImagesInDirectory(const char *directory) {
    int count = 0;
    DIR *dir;
    struct dirent *ent;

    if ((dir = opendir(directory)) != NULL) {
        while ((ent = readdir(dir)) != NULL) {
            if (ent->d_type == DT_REG) {
                count++;
            }
        }
        closedir(dir);
    } else {
        perror("Error opening directory");
    }

    return count;
}

// Fonction pour traiter chaque client
void *clientHandler(void *arg) {
    int csock = *((int *)arg);

    // Receive the image from the client
    char buffer[MAX_SIZE];
    ssize_t bytesRead = recv(csock, buffer, sizeof(buffer), 0);
    if (bytesRead > 0) {
        buffer[bytesRead] = '\0';
        // Perform image comparison and send result to the client
        printf("Comparing and sending result\n");
        compareAndSendResult(csock, buffer, bytesRead, countImagesInDirectory("img/"));
    }

    // Close the client socket
    close(csock);

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

        // Créer un thread pour gérer le client
        pthread_t clientThread;
        int *clientSockPtr = malloc(sizeof(int));
        *clientSockPtr = csock;
        
        printf("ABOUT TO CREATE CLIENT THREAD\n");
        if (pthread_create(&clientThread, NULL, clientHandler, (void *)clientSockPtr) != 0) {
            perror("Error creating client thread");
            close(csock);
            free(clientSockPtr);
        } else {
			printf("THREAD CREATED \n");
            pthread_detach(clientThread);
        }
    }

    // Le programme ne devrait jamais atteindre cette ligne
    printf("Closing the server socket\n");
    close(sock);

    return EXIT_SUCCESS;
}
