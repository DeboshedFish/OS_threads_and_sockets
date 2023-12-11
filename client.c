// client


#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stddef.h>

#define PORT 8101
#define MAX_SIZE 20480 // 20KB, limite de taille image


void sendFile(int sock) {
	
	FILE *file = fopen("image.bmp", "rb"); //?
	// prévoir erreur au cas où il ne s'ouvre pas
	
	char buffer[MAX_SIZE];
	int bytesRead;
	
	while ((bytesRead = fread(buffer, 1, sizeof(buffer), file)) > 0) {
		send(sock, buffer, bytesRead, 0);
    }
    
    
    fclose(file);
}

int main(void)
{
    int sock;
    struct sockaddr_in sin;

    /* Création de la socket */
    sock = socket(AF_INET, SOCK_STREAM, 0);

    /* Configuration de la connexion */
    sin.sin_addr.s_addr = inet_addr("127.0.0.1");
    sin.sin_family = AF_INET;
    sin.sin_port = htons(PORT);

    // Si le client arrive à se connecter
    if (connect(sock, (struct sockaddr *)&sin, sizeof(sin)) != -1){
        printf("Connexion à %s sur le port %d\n", inet_ntoa(sin.sin_addr), htons(sin.sin_port));
        
        // !!! Send file
        sendFile(sock);
	}

    else
        printf("Impossible de se connecter\n");

    // On ferme la socket précédemment ouverte
    close(sock);

    return EXIT_SUCCESS;
}
