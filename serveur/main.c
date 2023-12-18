#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>

#define PORT 5555
#define MAX_SIZE 20480 // 20KB, limit of image size
#define BACKLOG 5

// Structure to pass information to the thread
typedef struct {
    int csock;
    struct sockaddr_in client_addr;
} ThreadArgs;

bool readImage(const char *filename, char **imageData, size_t *imageSize) {
    FILE *file = fopen(filename, "rb");
    if (file == NULL) {
        perror("Error opening file for reading");
        return false;
    }

    // Get file size
    fseek(file, 0, SEEK_END);
    *imageSize = ftell(file);
    rewind(file);

    // Allocate memory for image data
    *imageData = (char *)malloc(*imageSize);
    if (*imageData == NULL) {
        perror("Error allocating memory for image data");
        fclose(file);
        return false;
    }

    // Read image data from file
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
bool readImage(const char *filename, char **imageData, size_t *imageSize) {
    FILE *file = fopen(filename, "rb");
    if (file == NULL) {
        perror("Error opening file for reading");
        return false;
    }

    // Get file size
    fseek(file, 0, SEEK_END);
    *imageSize = ftell(file);
    rewind(file);

    // Allocate memory for image data
    *imageData = (char *)malloc(*imageSize);
    if (*imageData == NULL) {
        perror("Error allocating memory for image data");
        fclose(file);
        return false;
    }

    // Read image data from file
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

	
void* compare(const char* raw_image_name) {
    printf("On entre dans la fonction compare\n");

    if (raw_image_name != NULL) {
        printf("raw_image_name: %s\n", raw_image_name);

        char *imageData;
        size_t imageSize;

        if (readImage(raw_image_name, &imageData, &imageSize)) {
            uint64_t raw_hash;
            int raw_result;
            
            if (PHashRaw(imageData, imageSize, &raw_hash)) {
                raw_result = raw_hash;
                fprintf("raw_result: %i", raw_result);

                // Continue with the rest of the code...
            } else {
                fprintf(stderr, "Error couldn't hash raw image\n");
            }

            // Don't forget to free the allocated memory
            free(imageData);
        } else {
            fprintf(stderr, "Error reading image data\n");
        }
    } else {
        printf("raw_image_name is NULL\n");
    }
	
	/*
	 
    // increment it by three
    for (int dbimg_nb = 1; dbimg_nb < 102; dbimg_nb++) {
        char nb_to_str[4];
        sprintf(nb_to_str, "%d", dbimg_nb);

        char suffix[] = ".bmp";

        // concatenate to obtain nb.bmp
        char result[7];
        sprintf(dbimg_name, "%s%s", nb_to_str, suffix);

        if (PHash(dbimg_name, &db_hash)){
            db_result = db_hash;
        } else {
            perror("Error couldn't hash database image");
        }

        int distance = DistancePHash(raw_result, db_result);
        if (final_result < distance) {
            final_result = distance;
            best_name = dbimg_name;
        }
    }
    printf("Most similar image found: %s with a distance of %d.\n", best_name, final_result);
    printf("IT IS WORKING\n");
    
    */
    
    return NULL;
   
}


// Function to handle a connected client
void *handleClient(void *arg) {
    ThreadArgs *threadArgs = (ThreadArgs *)arg;

    // Access client socket: threadArgs->csock
    // Access client address: threadArgs->client_addr

    // Send a welcome message to the client
    const char *welcomeMessage = "Welcome to the server! Please send an image.\n";
    send(threadArgs->csock, welcomeMessage, strlen(welcomeMessage), 0);


    while (1) {
        // Receive image data from the client and save it based on the client socket
        char filename[255];
        sprintf(filename, "serveur/received_image_%d.bmp", threadArgs->csock);
        FILE *file = fopen(filename, "wb");

        if (file == NULL) {
            perror("Error opening file for writing");
            close(threadArgs->csock);
            free(threadArgs);
            pthread_exit(NULL);
        }
        

        char buffer[MAX_SIZE];
        ssize_t bytesRead;
        
		if ((bytesRead = recv(threadArgs->csock, buffer, sizeof(buffer), 0)) > 0) {
			fwrite(buffer, 1, bytesRead, file);
		}
	 

        fclose(file);
        printf("File received and saved as %s\n", filename);

	// !!! COMPARISON
        printf("BEFORE COMPARISON\n");
        compare(&filename);
        printf("AFTER COMPARISON");
		
        // Send a success message to the client
        const char *successMessage = "Image received successfully! Send another image or type 'exit' to close the connection.\n";
        send(threadArgs->csock, successMessage, strlen(successMessage), 0);
        
    
        // Check if the client wants to exit
        char exitCommand[5];
        bytesRead = recv(threadArgs->csock, exitCommand, sizeof(exitCommand), 0);
        if (bytesRead > 0 && strncmp(exitCommand, "exit", 4) == 0) {
            break;
        }
    }

    // Close the client socket
    printf("Closing the client socket %d\n", threadArgs->csock);
    close(threadArgs->csock);
    free(threadArgs);
    pthread_exit(NULL);
}

// prints the distance, and name of the image with the shortest distacne
// parity is meant to split the effort between different threads. We split it into three groups depending on the remainder modulo 3
// (raw_image_name, parity)


int main(void) {
    int sock, csock;
    struct sockaddr_in sin, csin;
    socklen_t recsize = sizeof(sin);
    socklen_t crecsize = sizeof(csin);
    int sock_err;

    // Create a socket
    sock = socket(AF_INET, SOCK_STREAM, 0);

    if (sock == -1) {
        perror("Error creating socket");
        exit(EXIT_FAILURE);
    }

    // Configure the connection
    sin.sin_addr.s_addr = htonl(INADDR_ANY);
    sin.sin_family = AF_INET;
    sin.sin_port = htons(PORT);

    // Bind the socket
    sock_err = bind(sock, (struct sockaddr *)&sin, recsize);

    if (sock_err == -1) {
        perror("Error binding socket");
        close(sock);
        exit(EXIT_FAILURE);
    }

    // Start listening
    sock_err = listen(sock, BACKLOG);

    if (sock_err == -1) {
        perror("Error listening for connections");
        close(sock);
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port %d...\n", PORT);

    while (1) {
        // Accept incoming connections
        printf("Waiting for a client to connect...\n");
        csock = accept(sock, (struct sockaddr *)&csin, &crecsize);

        if (csock == -1) {
            perror("Error accepting connection");
            close(sock);
            exit(EXIT_FAILURE);
        }

        printf("Client connected with socket %d from %s:%d\n", csock, inet_ntoa(csin.sin_addr), ntohs(csin.sin_port));
        
        
        // Create a thread to handle the client
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

    // Close the server socket (this part is never reached in this example)
    printf("Closing the server socket\n");
    close(sock);

    return EXIT_SUCCESS;
}


