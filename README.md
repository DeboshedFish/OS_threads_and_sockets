# OS_threads_and_sockets


Pour l'instant on a un code img-search.c et client.c
Ils communiquent entre eux avec chacun leur socket. Le code client envoie une image que le serveur (du nom de img-search.c) reçoit.

Là où ça va se compliquer, c'est faire en sorte que le serveur compare l'image reçue avec les images de la base de données. Cela nécessite forcément 3 threads au niveau du serveur.

De plus, le serveur doit être capable d'accueillir plusieurs clients (100), et chaque client peut envoyer plus d'une image. 
