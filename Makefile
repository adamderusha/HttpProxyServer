all: server

server: server.o
	gcc -Wall -g -o server server.o 

server.o: server.c
	gcc -Wall -g -c server.c

clean:
	rm *.o
