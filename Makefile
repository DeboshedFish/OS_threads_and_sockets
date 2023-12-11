CC = gcc
CFLAGS = -Wall -Extra

all: server client

server: server.c
	$(CC) $(CFLAGS) -o server server.c

client: client.client
	$(CC) $(CFLAGS) -o client client.c

clean:
	rm -f server client