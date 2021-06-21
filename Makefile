CC = gcc
CFLAGS += -Wall -g -pedantic
INCLUDES = -I ./includes

TARGETS = ./bin/Server ./bin/Client

.PHONY: all

./bin/Server: ./objs/Server.o ./libs/dataStructures.so
	$(CC) $(CFLAGS) $(INCLUDES) $< -o $@ -Wl,-rpath,./libs -L ./libs -ldataStructures -lpthread

./bin/Client: ./objs/Client.o ./libs/comunicationAPI.so ./libs/dataStructures.so
	$(CC) $(CFLAGS) $(INCLUDES) $< -o $@ -Wl,-rpath,./libs -L ./libs -lcomunicationAPI -ldataStructures

./objs/Client.o: ./src/Client.c
	$(CC) $(CFLAGS) $< -c -o $@

./objs/Server.o: ./src/Server.c
	$(CC) $(CFLAGS) $< -c -o $@

./libs/dataStructures.so: ./objs/icl_hash.o ./objs/list.o
	$(CC) $(CFLAGS) -shared -o ./libs/libdataStructures.so $^

./libs/comunicationAPI.so: ./objs/comunicationProtocol.o
	$(CC) $(CFLAGS) -shared -o ./libs/libcomunicationAPI.so $^

./objs/%.o: ./src/%.c
	$(CC) $(CFLAGS) $< -c -fPIC -o $@

all: $(TARGETS)

clean:
	rm -f $(TARGETS)

cleanall: clean
	\rm -f objs/*.o *~ libs/*.a libs/*.so