#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>

#define MAXLINE 512
#define MAXLISTEN 10

void serveClient(int);
char *readClientfd(int);

int main( int argc, char *argv[] )
{
  int listenfd, connfd;
  pid_t pid;
  short port;
  socklen_t len;
  struct sockaddr_in servaddr, cliaddr;
  struct sigaction sigact;

  if( argc < 2 )
  {
    fprintf(stderr, "Usage: %s portNumber domainsToFilter\n", argv[0]);
    return EXIT_FAILURE;
  }

  port = atoi(argv[1]);
  if( port < 1024 )
  {
    fprintf(stderr, "ERROR! portNumber should be a number and greater than 1023\n");
    return EXIT_FAILURE;
  }

  sigemptyset( &sigact.sa_mask );
  sigact.sa_flags = 0 | SA_NODEFER | SA_RESETHAND;
  sigact.sa_handler = SIG_IGN;

  sigaction( SIGCHLD, &sigact, NULL); /* To prevent zombie processes */
  /*sigaction( SIGINT, &sigact, NULL);  /* To ignore keyboard interupts */

  if( (listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0 )
  {
    fprintf(stderr, "ERROR! Could not create listener socket\n");
    return EXIT_FAILURE;
  }

  bzero(&servaddr, sizeof(servaddr));
  servaddr.sin_family = AF_INET;
  servaddr.sin_addr.s_addr = htonl(INADDR_ANY); /* Might need to change this IP */
  servaddr.sin_port = htons(port);

  if( bind(listenfd, (struct sockaddr *) &servaddr, sizeof(servaddr)) < 0 )
  {
    fprintf(stderr, "ERROR! Could not bind to port %hd\n", port);
    return EXIT_FAILURE;
  }

  if( listen(listenfd, MAXLISTEN) < 0 )
  {
    fprintf(stderr, "ERROR! Problem in the listen function\n");
    return EXIT_FAILURE;
  }

  /* Create children */

  while( 1 )
  {
    len = sizeof(cliaddr);
    if( (connfd = accept(listenfd, (struct sockaddr *) &cliaddr, &len)) < 0 )
    {
      if( errno == EINTR )
      {
        printf("INTERRUPTS CANNOT STOP MEEE!\n");
        continue;
      }
      perror("Accept()");
      return EXIT_FAILURE;
    }

    if( (pid = fork()) == 0 ) /* Child */
    {
      printf("Child %d: Serving a client\n", getpid());
      serveClient( connfd );
      close( connfd );
      printf("Child %d: done serving a client\n", getpid());
      exit(0);
    }
    close( connfd );
  }

  return EXIT_SUCCESS;
}

//*
void serveClient( int connfd )
{
  char badResponse[] = "HTTP/1.1 403 Forbidden\r\nContent-Length: 22\r\nContent-Type: text/html\r\n\r\n<h1>403 Forbidden</h1>\r\n";
  char *input = NULL;

  printf("Child %d: reading from client\n", getpid());
  if( (input = readClientfd(connfd )) == NULL )
  {
    printf("Read error\n");
    return;
  }

  printf("%s\n", input);
  free(input);
  printf("Child %d: writing to client\n", getpid());
  write( connfd, badResponse, strlen(badResponse) );
  return;
}
/**/

char *readClientfd(int connfd)
{
  char *finalMessage = NULL;
  int sizeOfMessage = 0;
  int count;
  char buffer[MAXLINE];
  while( (count = read( connfd, buffer, MAXLINE-1)) > 0 )
  {
    char *temp = (char *) malloc( count + sizeOfMessage + 1 );
    if( finalMessage != NULL )
    {
      memcpy(temp, finalMessage, sizeOfMessage);
      memcpy(temp+sizeOfMessage, buffer, count);
      free(finalMessage);
    }
    else
    {
      memcpy(temp, buffer, count);
    }
    sizeOfMessage += count;
    finalMessage = temp;
  
    temp = strstr(finalMessage, "\r\n\r\n");
    if( temp != NULL )
    {
      *temp = '\0';
      return finalMessage;
    }
  }
  return NULL;
}
