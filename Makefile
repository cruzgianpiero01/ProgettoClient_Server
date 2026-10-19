CC=gcc
CFLAGS=-Wall -Wextra -pthread

all: server client

server: server.o protocol.o
	$(CC) $(CFLAGS) -o server server.o protocol.o

client: client.o protocol.o
	$(CC) $(CFLAGS) -o client client.o protocol.o

server.o: server.c protocol.h
	$(CC) $(CFLAGS) -c server.c

client.o: client.c protocol.h
	$(CC) $(CFLAGS) -c client.c

protocol.o: protocol.c protocol.h
	$(CC) $(CFLAGS) -c protocol.c

clean:
	rm -f *.o server client
