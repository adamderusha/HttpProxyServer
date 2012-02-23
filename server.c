#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#define MAXLINE 1024
#define MAXLISTEN 10

void serve(int);

int main( int argc, char *argv[] )
{
  int listenfd, connfd;
  pid_t pid;
  short port;
  socklen_t len;
  struct sockaddr_in servaddr, cliaddr;

  if( argc < 2 )
  {
    fprintf(stderr, "Usage: %s portNumber domainsToFilter\n", argv[0]);
    return 1;
  }

  port = atoi(argv[1]);
  if( port < 1024 )
  {
    fprintf(stderr, "ERROR! portNumber should be a number and greater than 1023\n");
    return 1;
  }

  if( (listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0 )
  {
    fprintf(stderr, "ERROR! Could not create listener socket\n");
    return 1;
  }

  bzero(&servaddr, sizeof(servaddr));
  servaddr.sin_family = AF_INET;
  servaddr.sin_addr.s_addr = htonl(INADDR_ANY); /* Might need to change this IP */
  servaddr.sin_port = htons(port);

  if( bind(listenfd, (struct sockaddr *) &servaddr, sizeof(servaddr)) < 0 )
  {
    fprintf(stderr, "ERROR! Could not bind to port %hd\n", port);
    return 1;
  }

  if( listen(listenfd, MAXLISTEN) < 0 )
  {
    fprintf(stderr, "ERROR! Problem in the listen function\n");
    return 1;
  }

  while( 1 )
  {
    len = sizeof(cliaddr);
    if( (connfd = accept(listenfd, (struct sockaddr *) &cliaddr, &len)) < 0 )
    {
      perror("Accept()");
      //fprintf(stderr, "ERROR! Problem in accept()\n");
      return -1;
    }

    if( (pid = fork()) == 0 ) /* Child */
    {
      printf("Child %d: Serving a client\n", getpid());
      serve( connfd );
      close( connfd );
      printf("Child %d: done serving a client\n", getpid());
    }
    close( connfd );
  }
}

void serve(int connfd)
{
  char buff[MAXLINE];
  int count;
  while( (count = read( connfd, buff, MAXLINE-1 )) > 0 )
  {
    buff[count] = '\0';
    printf("CHILD %d: My client said \"%s\"!\n", getpid(), buff);
    if( write(connfd, buff, strlen(buff)) < 0 )
    {
      fprintf(stderr, "CHILD %d: Failed my write call\n", getpid());
      return;
    }
  }
}
