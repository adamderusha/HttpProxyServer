/******************************************/
/*             Adam DeRusha               */
/*            derusa@rpi.edu              */
/*              Project 2                 */
/*            Due: 3/12/2012              */
/******************************************/

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

#define MAXLINE 4096
#define MAXLISTEN 10

/* HTTP Types */
#define HTTP1_1 1
#define HTTP1_0 0

/* Method Types */
#define BAD_METHOD -1
#define GET 0
#define POST 1
#define HEAD 2

struct request
{
  int method;          /* GET, POST, HEAD */
  int type;            /* HTTP/1.1 or HTTP/1.0 */
  char *resource; 
  char *full_resource;
};

/* Linked list node for the headers */
struct headers
{
  char *header;
  struct headers *next;
};

void serveClient(int connfd, char *client_name, char *blacklist[], int blacklistsize );
char *readResponse( int connfd );
void splitHeaders( char *message, struct headers **output );
void freeHeaders( struct headers *input );
int getRequestInfo( char *message, struct request **info );
int establishConnection( int *sock, char *hostname );
char *findValue( struct headers *head, char *key );
int transfer( int clientfd, int serverfd, char *request_line );
int check_blacklist( char *word, char *blacklist[], int size );
void print_client_request( char *client_name, char *req );
void freeRequestInfo( struct request *r );

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

  /* Set up the signal handlers */
  sigemptyset( &sigact.sa_mask );
  sigact.sa_flags = 0 | SA_NODEFER | SA_RESETHAND;
  sigact.sa_handler = SIG_IGN;

  sigaction( SIGCHLD, &sigact, NULL); /* To prevent zombie processes */
  sigaction( SIGINT, &sigact, NULL);  /* To ignore keyboard interupts */

  /* Request a server socket */
  if( (listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0 )
  {
    fprintf(stderr, "ERROR! Could not create listener socket\n");
    return EXIT_FAILURE;
  }

  /* Fill out and bind the server socket */
  bzero(&servaddr, sizeof(servaddr));
  servaddr.sin_family = AF_INET;
  servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
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

  /* Start the server process */
  while( 1 )
  {
    /* Get a client */
    len = sizeof(cliaddr);
    if( (connfd = accept(listenfd, (struct sockaddr *) &cliaddr, &len)) < 0 )
    {
      if( errno == EINTR )
      {
        continue;
      }
      perror("Accept()");
      return EXIT_FAILURE;
    }

    /* Pass the client to a child process */
    if( (pid = fork()) == 0 ) /* Child */
    {
      /* Get the hostname */
      struct hostent *hp;
      hp = gethostbyaddr( (char *) &cliaddr.sin_addr.s_addr, sizeof(cliaddr.sin_addr.s_addr), AF_INET);
      
      serveClient( connfd, hp->h_name, argv+2, argc-2);
      close( connfd );
      exit(EXIT_SUCCESS);
    }
    close( connfd );
  }

  return EXIT_SUCCESS;
}

void serveClient(int connfd, char *client_name, char *blacklist[], int blacklistsize )
{
  char badResponse[] = "HTTP/1.1 403 Forbidden\r\nContent-Length: 22\r\nContent-Type: text/html\r\n\r\n<h1>403 Forbidden</h1>\r\n";
  char notImplemented[] = "HTTP/1.1 405 Method Not Allowed\r\nContent-Length: 31\r\nContent-Type: text/html\r\n\r\n<h1>405 Method Not Allowed</h1>\r\n";
  char badRequest[] = "HTTP/1.1 400 Bad Request\r\nContent-Length: 24\r\nContent-Type: text/html\r\n\r\n<h1>400 Bad Request</h1>\r\n";
  char *input = NULL;
  struct headers *headers = NULL;
  int sock;
  char *hostname = NULL;
  struct request *r;

  /* Read the request from the client */
  if( (input = readResponse(connfd)) == NULL )
  {
    return;
  }

  /* Split the headers into a linked list */
  splitHeaders(input, &headers);
  if( headers == NULL )
  {
    write( connfd, badRequest, strlen(badRequest) );
    print_client_request( client_name, headers->header );
    printf("\n");
    free(input);
    return;
  }

  /* Split the request line into type, resource, and HTTP version */
  getRequestInfo(headers->header, &r);
  if( r->method < 0 )
  {
    write(connfd, notImplemented, strlen(notImplemented));
    print_client_request( client_name, headers->header );
    printf("\n");
    freeRequestInfo(r);
    freeHeaders(headers);
    free(input);
    return;
  }

  /* Get the hostname (who to connect to) */
  if( r->type == HTTP1_1 )
  {
    free(r->resource);
    r->resource = NULL;
    hostname = findValue(headers, "Host");
    if( hostname == NULL )
    {
      write( connfd, badRequest, strlen(badRequest) );
      print_client_request( client_name, headers->header );
      printf("\n");
      freeRequestInfo(r);
      free(hostname);
      freeHeaders(headers);
      free(input);
      return;
    }
  }
  else if( r->type == HTTP1_0 )
  {
    hostname = r->resource;
    r->resource = NULL;
  }

  /* Print what the client requested */
  print_client_request( client_name, headers->header );
  freeRequestInfo(r);
 
  /* Check and make sure the client isn't trying to go somewhere bad... */
  if( check_blacklist( hostname, blacklist, blacklistsize ) )
  {
    printf("[FILTERED]\n");
    freeHeaders(headers); 
    free(input);
    free(hostname);
    write(connfd, badResponse, strlen(badResponse));
    return;
  }
  printf("\n");

  /* If client is ok, establish a connection to the server they want to reach */
  if( establishConnection( &sock, hostname ) < 0 )
  {
    write( connfd, badRequest, strlen(badRequest));
    freeHeaders(headers); 
    free(input);
    free(hostname);
    return;
  }
  free(hostname);

  /* Transfer the data between client and server */
  transfer(connfd, sock, input);
  freeHeaders(headers);
  free(input);

  return;
}

int getRequestInfo( char *message, struct request **info )
{
  char *start = strdup(message);
  char *piece = NULL;
  char *temp = NULL;
  while( *start == ' ' ) /* Skip any leading whitespace */
    ++start;

  /* Carve space for the request */
  *info = (struct request *) malloc( sizeof(struct request));
  (*info)->resource = NULL;
  (*info)->full_resource = NULL;

  if( (piece = strtok(start, " ")) == NULL )
  {
    free(start);
    return -1;
  }
 
  /* Figure out whether we're a POST, GET, HEAD, or other */
  if( strncmp(piece, "GET", 3) == 0 )
    (*info)->method = GET;
  else if( strncmp(piece, "POST", 4) == 0 )
    (*info)->method = POST;
  else if( strncmp(piece, "HEAD", 4) == 0 )
    (*info)->method = HEAD;
  else
    (*info)->method = BAD_METHOD;

  if( (piece = strtok(NULL, " ")) == NULL )
  {
    free(start);
    return -1;
  }

  /* Get the full resource name and then extract the host name */
  (*info)->full_resource = strdup(piece);
  if( (temp = strstr(piece, "://")) != NULL )
  {
    piece = temp+3;
  }
  if( (temp = strstr(piece, "/")) != NULL )
  {
    piece[temp-piece] = '\0';
  }
  (*info)->resource = strdup(piece);

  if( (piece = strtok(NULL, " ")) == NULL )
  {
    free(start);
    return -1;
  }

  /* Figure out whether it's HTTP/1.0 or HTTP/1.1 */
  if( strncmp(piece, "HTTP/1.1", 8) == 0 )
  {
    (*info)->type = HTTP1_1;
  }
  else if( strncmp(piece, "HTTP/1.0", 8) == 0 )
  {
    (*info)->type = HTTP1_0;
  }
  else
  {
    free(start);
    return -1;
  }
  free(start);
  
  return 0;
}

void splitHeaders( char *message, struct headers **output )
{
  struct headers *head;
  char *messageCopy = strdup(message);
  char *header = strtok(messageCopy, "\r\n");
  *output = NULL;
  if( header == NULL )
  {
    return;
  }
  
  /* Create a linked list node */
  (*output) = (struct headers *) malloc(sizeof(struct headers));
  (*output)->header = strdup(header);
  (*output)->next = NULL;

  head = *output;

  /* Now we just keep tokenizing the input and add links to the list */
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
    if( p->header != NULL )
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

/* Searches the linked list to find a key, returns the value */
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

/* Reads in the input from a file descriptor and returns what it read */
char *readResponse( int connfd )
{
  char *response = NULL;
  int read_count = 0;
  char buffer[MAXLINE];
  struct headers *p = NULL;

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
    if( temp != NULL )
    {
      break;
    }
  }
  freeHeaders(p);
  return response;
}

/* Reads what a server says and just forwards it to a client */
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

int check_blacklist( char *word, char *blacklist[], int size )
{
  int i;
  for( i = 0; i < size; ++i )
  {
    char *temp = NULL;
    /* Check the prefix */
    if(strncmp(word, blacklist[i], strlen(blacklist[i])) == 0)
    {
      return 1;
    }
    /* Check the suffix */
    if( (temp = strstr(word,blacklist[i])) != NULL && strcmp(temp, blacklist[i]) == 0 )
    {
      return 1;
    }
  }
  return 0;
}

/* Prints (most of) the required output */
void print_client_request( char *client_name, char *req )
{
  char *t;
  printf("%s: ", client_name);
  if( (t = strstr(req, "HTTP/")) != NULL )
  {
    int len = t-req;
    char *out = (char *) malloc( len + 1);
    memcpy(out, req, len);
    out[len] = '\0';
    printf("%s ", out);
    free(out);
  }
  else
  {
    printf("%s ", req);
  }
  return;
}

void freeRequestInfo( struct request *r )
{
  if( r->full_resource != NULL )
    free(r->full_resource);

  if( r->resource != NULL )
    free(r->resource);

  free(r);
  return;
}
