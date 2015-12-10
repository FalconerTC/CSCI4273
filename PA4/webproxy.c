#include <dirent.h>
#include <fcntl.h>
#include <netdb.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <sys/errno.h>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <netinet/in.h>

#define QLEN 32		/* Maximum connections */
#define	BUFSIZE	4096

/* Structs */
struct HTTP_Request {
char 	site[128];
int	port;
char	full_req[BUFSIZE];
};

static const struct HTTP_Request EmptyRequest;

/* Prototypes */
int		interpret(int client);
int		send_request(int client, const struct HTTP_Request req);
int		errexit(const char *format, ...);
int		connectsock(const char *host, int portnum);
int		bindsock(const char *portnum, int qlen);

int main(int argc, char *argv[]) {
  char *port;                     	/* Port to host proxy at */
  struct sockaddr_in c_addr;      	/* From address of client */
  int sock;                       	/* Server listening socket */
  int connection;			/* Connection socket */
  unsigned int alen;			/* From address length */
  char remoteIP[INET6_ADDRSTRLEN];

  switch(argc) {
    case 2:
      port = argv[1];
      break;
    default:
      fprintf(stderr, "Usage: %s [port]\n", argv[0]);
      exit(1);
  }

  // Create and connect listening socket
  sock = bindsock(port, QLEN);

  while(1) {
      // Handle new connection
      alen = sizeof(c_addr);
      connection = accept(sock, (struct sockaddr *)&c_addr, &alen);
      if (connection < 0)
        errexit("accept: %s\n", strerror(errno));

      printf("New connection from %s on socket %d\n",
        inet_ntop(c_addr.sin_family,
          &(c_addr.sin_addr),
          remoteIP, INET6_ADDRSTRLEN),
        connection);

      // Start receiving data from connection
      if (!fork()) {
        close(sock);
        if (interpret(connection) == 0) {
	  printf("closing connection\n");
          close(connection);
	}
        exit(0);
      }
      close(connection);
    }
  }


/*
 * Interpret - Read and pass GET request for client
 */
int interpret(int client) {
  struct HTTP_Request req;
  char buf[BUFSIZE];
  char current[BUFSIZE];
  char 	resp[BUFSIZE];

  int read_len = 0;
  int len = 0;
  int rv;
  
  fd_set set;
  FD_ZERO(&set);
  FD_SET(client, &set);
  
  struct timeval timeout;
  timeout.tv_sec = 10;
  
    while ((rv = select(client + 1, &set, NULL, NULL, &timeout)) > 0) {
      /* Read input from user */
      read_len = recv(client, &buf[len], (BUFSIZE-len), 0);
      /* Finish request if buf is empty */
      if (strlen(buf) == 0) {
	  printf("BUF IS EMPTY\n");
	  break;
      }
    
    /* Overwrite trailing new line */
    buf[read_len-1] = '\0';
          
    strcpy(req.site, "192.168.2.1");
    printf("'%s'\n", buf);
    strcpy(req.full_req, buf);
    req.port = 22;
	if (send_request(client, req) < 0) {
	    errexit("echo write: %s\n", strerror(errno));
	}
    
    }
  
  printf("Exiting\n");

  return 0;
}

/* send HTTP Request and send back response */
int send_request(int client, const struct HTTP_Request req) {
  char buf[BUFSIZE];
  int server;         /* socket descriptor to server */
  int len;
  int rv;

  printf("Processing request to %s\n", req.site);
  printf("'%s'\n", req.full_req);

  
  server = connectsock(req.site, req.port);

  /* Keep track of file descriptors */
  fd_set set;
  FD_ZERO(&set);
  FD_SET(server, &set);
  FD_SET(client, &set);
  int smax = (client > server ? client : server) + 1;
  
  struct timeval timeout;
  timeout.tv_sec = 10;

  /* Forward response */
  if (send(server, req.full_req, strlen(req.full_req), 0) < 0) {
    printf("Connection to site %s unavailable\n",
        req.site);
    return -1;
  }
  
  printf("Message forwarded to server\n");
  
  while ((rv = select(smax, &set, NULL, NULL, NULL)) > 0) {
    
    printf("Message received: ");
    len = 0;
    /* Check client for requests */
    if ((FD_ISSET(client, &set) > 0)) {
      printf("Client sent message\n");
      
      /* Read input from client */
      len = recv(client, &buf[len], (BUFSIZE-len), 0);
      buf[len-1] = '\0';
      printf("'%d'\n", strlen(buf));
      /* Forward to server*/
      if (write(server, buf, len) < 0) {
	errexit("Failed write: %s\n", strerror(errno));
	return -1;
      }
      printf("Message forwarded to server\n");
    
    }
    /* Check server for requests */
    if ((FD_ISSET(server, &set) > 0)) {
      printf("Server sent message\n");
      
      /* Read input from server */
      len = recv(server, &buf[len], (BUFSIZE-len), 0);
      buf[len-1] = '\0';
      printf("'%d'\n", strlen(buf));
      /* Forward to server*/
      if (write(client, buf, len) < 0) {
	errexit("Failed write: %s\n", strerror(errno));
	return -1;
      }
      printf("Message forwarded to client\n");
    }
    
  }
  
  printf("Exiting function\n");
  
  return 0;
}

/*
 * errexit - Print an error message and exit
 */
int errexit(const char *format, ...) {
        va_list args;

        va_start(args, format);
        vfprintf(stderr, format, args);
        va_end(args);
        exit(1);
}

/*
 * connectsock - Allocate and connect a socket using TCP
 */
int connectsock(const char* host, int portnum) {
	struct hostent  *phe;   		/* pointer to host information entry */
	struct sockaddr_in sockin;		/* an Internet endpoint address */
	int sock;              			/* socket descriptor */

	/* Zero out sockin */
	memset(&sockin, 0, sizeof(sockin));

	sockin.sin_family = AF_INET;

	/* Convert and set port */
	sockin.sin_port = htons((unsigned short)portnum);
	if (sockin.sin_port == 0)
		errexit("Unable to get port number: \"%s\"\n", portnum);

	/* Map host name to IP address, allowing for dotted decimal */
	if ((phe = gethostbyname(host)))
		memcpy(&sockin.sin_addr, phe->h_addr, phe->h_length);
	else if ( (sockin.sin_addr.s_addr = inet_addr(host)) == INADDR_NONE )
		errexit("Can't get \"%s\" host entry\n", host);

	/* Allocate a socket */
	sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sock < 0)
		errexit("Unable to create socket: %s\n", strerror(errno));

	/* Connect the socket */
	if (connect(sock, (struct sockaddr *)&sockin, sizeof(sockin)) < 0)
		return -1; /* Connection failed */

  //printf("Socket %d connected on port %d\n", sock, ntohs(sockin.sin_port));
	return sock;
}

/* Establish socket connection and sets it to listen for connections
 * Returns: Socket created
 */

int bindsock(const char* portnum, int qlen) {

	struct sockaddr_in sockin;
	int sock;

	// Zero out sockin
	memset(&sockin, 0, sizeof(sockin));

	// Set family to internet
	sockin.sin_family = AF_INET;
	// Set address to any
	sockin.sin_addr.s_addr = INADDR_ANY;
	// Set port to network short conversion of given port
	sockin.sin_port = htons((unsigned short)atoi(portnum));

	if (sockin.sin_port == 0)
	errexit("Unable to get port number: \"%s\"\n", portnum);

	// Create internet TCP socket
	sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);

	if (sock < 0)
	errexit("Unable to create socket: %s\n", strerror(errno));

	/* Bind the socket */
	    if (bind(sock, (struct sockaddr *)&sockin, sizeof(sockin)) < 0) {
	        fprintf(stderr, "can't bind to %s port: %s; Trying other port\n",
	            portnum, strerror(errno));
	        sockin.sin_port=htons(0); /* request a port number to be allocated
	                               by bind */
	        if (bind(sock, (struct sockaddr *)&sockin, sizeof(sockin)) < 0)
	            errexit("can't bind: %s\n", strerror(errno));
	        else {
	            socklen_t socklen = sizeof(sockin);

	            if (getsockname(sock, (struct sockaddr *)&sockin, &socklen) < 0)
	                    errexit("getsockname: %s\n", strerror(errno));
	            printf("New server port number is %d\n", ntohs(sockin.sin_port));
	        }
	    }

	// Start listening on socket
	if (listen(sock, qlen) < 0) {
	errexit("Unable to listen on %s port %s\n", portnum, strerror(errno));
	}

	printf("Socket %d connected on port %d\n", sock, ntohs(sockin.sin_port));
	return sock;

}
