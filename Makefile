CC = gcc
CFLAGS = -Wall -g -Iinclude
LDFLAGS =

all: folders dserver dclient

dserver: bin/dserver

dclient: bin/dclient

folders:
	@mkdir -p src include obj bin tmp

bin/dserver: obj/dserver.o
	$(CC) $(LDFLAGS) $^ -o $@

bin/dclient: obj/dclient.o
	$(CC) $(LDFLAGS) $^ -o $@

obj/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@
	
clean:
	rm -f obj/* tmp/* bin/dclient bin/dserver