#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>

#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>

#define MAXLINE 1024
#define MAXLISTEN 10

/* HTTP Types */
#define HTTP1_1 1
#define HTTP1_0 0

/* Method Types */
#define GET 0
#define POST 1
#define HEAD 2

struct request
{
  int method;
  int type;
  char *resource;
};

struct headers
{
  char *header;
  struct headers *next;
};

void serveClient(int);
char *readRequest( int connfd );
char *readResponse( int connfd );
void splitHeaders( char *message, struct headers **output );
void freeHeaders( struct headers *input );
int getHTTPversion( char * );
int getMethodType( char * );
int establishConnection( int *sock, char *hostname );
char *findValue( struct headers *head, char *key );
int transfer( int clientfd, int serverfd, char *request_line );

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
  //sigaction( SIGINT, &sigact, NULL);  /* To ignore keyboard interupts */

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
      if( errno == EINTR ) /* May not need this if... */
      {
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

void serveClient( int connfd )
{
  char badResponse[] = "HTTP/1.1 403 Forbidden\r\nContent-Length: 22\r\nContent-Type: text/html\r\n\r\n<h1>403 Forbidden</h1>\r\n";
  char *input = NULL;
  int method;
  struct headers *headers = NULL;
  int i;

  printf("Child %d: reading from client\n", getpid());
  if( (input = readResponse(connfd)) == NULL )
  {
    printf("Read error\n");
    return;
  }

  printf("CLIENT BROWSWER SAID:\n");
  printf("%s\n",input);
  
  splitHeaders(input, &headers);

  if( (method = getMethodType(headers->header)) < 0 )
  {
    printf("ERROR: Could not get HTTP method\n");
    freeHeaders(headers);
    free(input);
    return;
  }

  int sock;
  char *hostname = findValue(headers, "Host");
  if( establishConnection( &sock, hostname ) < 0 )
  {
    printf("Couldn't not establish connection to %s\n", hostname);
    freeHeaders(headers); 
    free(input);
    free(hostname);
    return;
  }
  free(hostname);

  i = transfer(connfd, sock, input);
  freeHeaders(headers);
  free(input);

  return;
}


int getRequestInfo( char *message, struct request *info )
{
  char *start = message;
  while( *start == ' ' ) /* Skip any leading whitespace */
    ++start;
  return 0;

}

int getHTTPversion( char *message )
{
  char *start = message;
  while( *start == ' ' ) /* Skip any leading whitespace */
    ++start;

  if( strncmp(start, "HTTP/1.1", 8) == 0 )
  {
    return HTTP1_1;
  }
  else if( strncmp(start, "HTTP/1.0", 8) == 0 )
  {
    return HTTP1_0;
  }
  else
  {
    return -1;
  }
}

int getMethodType( char *message )
{
  char *start = message;
  while( *start == ' ' ) /* Skip any leading whitespace */
    ++start;

  if( strncmp(start, "GET", 3) == 0 )
    return GET;
  else if( strncmp(start, "POST", 4) == 0 )
    return POST;
  else if( strncmp(start, "HEAD", 4) == 0 )
    return HEAD;
  else
    return -1;
}

void splitHeaders( char *message, struct headers **output )
{
  struct headers *head;
  char *messageCopy = strdup(message);
  char *header = strtok(messageCopy, "\r\n");

  (*output) = (struct headers *) malloc(sizeof(struct headers));
  (*output)->header = strdup(header);
  (*output)->next = NULL;

  head = *output;

  while( (header = strtok(NULL, "\r\n")) != NULL )
  {
    (*output)->next = (struct headers *) malloc( sizeof(struct headers) );
    *output = (*output)->next;
    (*output)->header = strdup(header);
    (*output)->next = NULL;
  }
  free(messageCopy);
  *output = head;
}

void freeHeaders( struct headers *input )
{
  struct headers *p;
  while( input != NULL )
  {
    p = input;
    input = input->next;
    free(p->header);
    free(p);
  }
  return;
}

int establishConnection( int *sock, char *hostname )
{
  struct sockaddr_in server;
  struct hostent *hp;
  int port = 80;
  if( (*sock = socket( PF_INET, SOCK_STREAM, 0 )) < 0 )
  {
    return -1;
  }

  server.sin_family = PF_INET;
  hp = gethostbyname(hostname);
  if( hp == NULL )
  {
    return -1;
  }

  bcopy( (char *)hp->h_addr, (char *) &server.sin_addr, hp->h_length );
  server.sin_port = htons(port);
  if( connect( *sock, (struct sockaddr *) &server, sizeof( server ))< 0)
  {
    return -1;
  }
  return 0; /* Successful Connection */
}

char *findValue( struct headers *head, char *key )
{
  struct headers *p = head;
  while( p != NULL )
  {
    if( strncmp(key, p->header, strlen(key)) == 0 )
    {
      char *start = strstr(p->header, ":");
      if( start == NULL )
        return NULL;
      
      ++start;
      while( *start == ' ' )
        ++start;

      return strdup(start);
    }
    p = p->next;
  }
  return NULL;
}


char *readResponse( int connfd )
{
  char *response = NULL;
  int read_count = 0;
  char buffer[MAXLINE];
  struct headers *p = NULL;
  int content_length = -1;

  response = (char *) malloc(1);
  response[0] = '\0';
  while( (read_count = read(connfd, buffer, MAXLINE-1)) > 0 )
  {
    /* Build new string */
    int old_length = strlen(response);
    char *temp = (char *) malloc(old_length+read_count+1);
    memcpy(temp,response,old_length);
    memcpy(temp+old_length, buffer, read_count);
    temp[old_length+read_count] = '\0';
    free(response);
    response = temp;


    /* Check for all the headers read */
    temp = strstr(response, "\r\n\r\n");
    if( temp != NULL && p == NULL )
    {
      char *headers = (char *) malloc(temp-response);
      char *value = NULL;
      memcpy(headers, response, temp-response-1);
      headers[temp-response] = '\0';
      splitHeaders(headers, &p);
      free(headers);

      value = findValue(p, "Content-Length");
      if( value == NULL )
        break;
      content_length = atoi(value);
      free(value);
    }

    if( temp != NULL && content_length > 0 )
    {
      int content_size = -1;
      temp += 4;
      content_size = strlen(temp);
      
      if( content_length == content_size )
        break;
    }
  }
  freeHeaders(p);
  return response;
}

int transfer( int clientfd, int serverfd, char *request_line )
{
  char buffer[MAXLINE];
  int count;

  write( serverfd, request_line, strlen(request_line) );
  while( (count = recv(serverfd, buffer, MAXLINE, MSG_WAITALL)) > 0 )
  {
    write(clientfd, buffer, count);
    if(count < MAXLINE)
    {
      break;
    }
  }
  return 1;
}
