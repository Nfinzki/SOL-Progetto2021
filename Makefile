CC = gcc
CFLAGS += -Wall -g -pedantic
INCLUDES = -I ./includes

TARGETS = ./bin/Server ./bin/Client

.PHONY: all

./bin/Server: ./objs/Server.o ./objs/list.o ./objs/icl_hash.o 
	$(CC) $(CFLAGS) $(INCLUDES) $^ -o $@ -lpthread

./bin/Client: ./objs/Client.o ./objs/comunicationProtocol.o ./objs/list.o
	$(CC) $(CFLAGS) $(INCLUDES) $^ -o $@

./objs/%.o: ./src/%.c
	$(CC) $(CFLAGS) $(INCLUDES) $< -c -o $@

all: $(TARGETS)

clean:
	rm -f $(TARGETS)

cleanall: clean
	\rm -f objs/*.o *~ libs/*.a