CC = gcc
CFLAGS += -Wall -g -pedantic
INCLUDES = -I ./includes

TARGETS = ./bin/Server ./bin/Client

.PHONY: all clean cleanall

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
	\rm -f objs/*.o *~ libs/*.a libs/*.so tmp/*

test1:
	echo FILE_SPACE 10000 > ./tmp/test1.txt && echo MEM_SPACE 128MB >> ./tmp/test1.txt && echo N_WORKERS 1 >> ./tmp/test1.txt && echo SOCKET_NAME tmp/socket.sk >> ./tmp/test1.txt
	./script/test1.sh

test2:
	echo FILE_SPACE 10 > ./tmp/test2.txt && echo MEM_SPACE 1MB >> ./tmp/test2.txt && echo N_WORKERS 4 >> ./tmp/test2.txt && echo SOCKET_NAME socket.sk >> ./tmp/test2.txt
	./bin/Server ./tmp/test2.txt
	./script/test2.sh