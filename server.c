// Server


#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#define PORT 8101
#define MAX_SIZE 20480 // 20KB, limite de taille image
#define NB_CLIENTS 5


void receiveFile(int csock) {
    FILE *file = fopen("received_image.bmp", "wb");
    // prévoir erreur d'ouverture de fichier
    
    //L'image va être envoyée par morceaux. On appelle ces morceaux des "buffer".
    char buffer[MAX_SIZE];
    ssize_t bytesRead;
    
    while ((bytesRead = recv(csock, buffer, sizeof(buffer), 0)) > 0) {
        fwrite(buffer, 1, bytesRead, file);
    }

    fclose(file);
    printf("Fichier bmp reçu et and enregistré comme received_image.bmp\n");
    
}

int main(void)
{
    int sock, csock;
    struct sockaddr_in sin, csin;
    socklen_t recsize = sizeof(sin);
    socklen_t crecsize = sizeof(csin);
    int sock_err;

    /* Création d'une socket */
    sock = socket(AF_INET, SOCK_STREAM, 0);

    /* Si la socket est valide */
    if (sock != -1)
    {
        printf("La socket %d est maintenant ouverte en mode TCP/IP\n", sock);

        /* Configuration */
        sin.sin_addr.s_addr = htonl(INADDR_ANY); /* Adresse IP automatique */
        sin.sin_family = AF_INET;                /* Protocole familial (IP) */
        sin.sin_port = htons(PORT);              /* Listage du port */
        sock_err = bind(sock, (struct sockaddr *)&sin, recsize);

        /* Si la socket fonctionne */
        if (sock_err != -1)
        {
            /* Démarrage du listage (mode server) */
            sock_err = listen(sock, NB_CLIENTS);
            printf("Listage du port %d...\n", PORT);

            /* Si la socket fonctionne */
            if (sock_err != -1)
            {
                /* Attente pendant laquelle le client se connecte */
                printf("Patientez pendant que le client se connecte sur le port %d...\n", PORT);
                csock = accept(sock, (struct sockaddr *)&csin, &crecsize);
                printf("Un client se connecte avec la socket %d de %s:%d\n", csock, inet_ntoa(csin.sin_addr), htons(csin.sin_port));
                
                // !!! Receive file
                receiveFile(csock);
                
                
                // shutdown(csock)?
            }
            else
                perror("listen");
        }
        else
            perror("bind");

        /* Fermeture de la socket client et de la socket serveur */
        printf("Fermeture de la socket client\n");
        close(csock);
        printf("Fermeture de la socket serveur\n");
        close(sock);
        printf("Fermeture du serveur terminée\n");
    }
    else
        perror("socket");

    return EXIT_SUCCESS;
}