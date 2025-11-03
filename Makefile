CC = gcc
CFLAGS = -Wall -Werror -g
LDFLAGS = -pthread

all: nameserver storageserver client

nameserver: nameserver.c
	$(CC) $(CFLAGS) nameserver.c -o nameserver $(LDFLAGS)

storageserver: storageserver.c
	$(CC) $(CFLAGS) storageserver.c -o storageserver $(LDFLAGS)

client: client.c
	$(CC) $(CFLAGS) client.c -o client $(LDFLAGS)

clean:
	rm -f nameserver storageserver client