httpserver: server.o
	gcc  -pthread -o httpserver server.o

server.o: server.c
	gcc  -c -Wall -Wextra -Wpedantic -Wshadow -O2 server.c

clean:
	rm -f httpserver server.o
