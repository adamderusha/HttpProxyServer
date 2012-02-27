all: server

server: server.o
	gcc -Wall -o server server.o

server.o: server.c
	gcc -Wall -c server.c

clean:
	rm *.o
