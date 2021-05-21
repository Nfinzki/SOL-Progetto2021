CC = gcc
CFLAGS += -Wall -g -pedantic

TARGETS = ./bin/Server ./bin/Client

.PHONY: all

./bin/Server: ./objs/Server.o
	$(CC) $(CFLAGS) $< -lpthread -o $@

./objs/Server.o: ./src/Server.c ./includes/
	$(CC) $(CFLAGS) $< -I ./includes -c -o $@

./bin/Client: ./objs/Client.o ./objs/comunicationProtocol.o
	$(CC) $(CFLAGS) ./objs/comunicationProtocol.o $< -o $@

./objs/Client.o: ./src/Client.c ./includes/
	$(CC) $(CFLAGS) $< -I ./includes -c -o $@

./objs/comunicationProtocol.o: ./src/comunicationProtocol.c ./includes/comunicationProtocol.h
	$(CC) $(CFLAGS) $< -I ./includes -c -o $@

all: $(TARGETS)