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
  char 	version[16];
	char 	host[128];
  int  port;
	int 	keep_alive;
  char  full_req[BUFSIZE];
};

static const struct HTTP_Request EmptyRequest;

/* Prototypes */
int			interpret(int client);
int     send_request(int client, const struct HTTP_Request req);
int			errexit(const char *format, ...);
int 		connectsock(const char *host, int portnum);
int     bindsock(const char *portnum, int qlen);

int main(int argc, char *argv[]) {
  char *port;                     /* Port to host proxy at */
  struct sockaddr_in c_addr;      /* From address of client */
  int sock;                       /* Server listening socket */
  int connection;					        /* Connection socket */
  unsigned int alen;				      /* From address length */
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
        if (interpret(connection) == 0)
          close(connection);
        exit(0);
      }
      close(connection);
    }
  }


/*
 * Intrepret - Read and pass GET request for client
 */
int interpret(int client) {
  char	*req_400_1 =	"HTTP/1.1 400 Bad Request: Invalid Method: ";
  char	*req_400_2 =	"HTTP/1.1 400 Bad Request: Invalid HTTP-Version: ";
  char	*req_500 =	"HTTP/1.1 500 Internal Server Error: ";

  struct 	HTTP_Request req;
  char buf[BUFSIZE];
  char current[BUFSIZE];
  char 	resp[BUFSIZE];

  int read_len = 0;
  int len = 0;
  int rv;

  /* Holds the state of a request
   * 0 means no request received
   * 1 means request initiated, waiting on paramaters */
  int 	req_state = 0;
  /* Maintains whether a request has been parsed for a single token
   * 0 means no completed request
   * 1 means a complete request has been parsed and will be sent */
  int 	req_complete = 0;
  /* Maintains whether a request has been sent. Used for building segmented requests.
   * 0 means no request has been sent
   * 1 means a request was sent during the last batch */
  int		req_sent = 0;

  fd_set set;
  FD_ZERO(&set);
  FD_SET(client, &set);

  while ((rv = select(client + 1, &set, NULL, NULL, NULL)) > 0) {
    /* Read input from user */
    read_len = recv(client, &buf[len], (BUFSIZE-len), 0);
    /* Finish request if buf is empty */
    if (strlen(buf) == 0) {
      printf("BUF IS EMPTY\n");
      break;
    }

    /* Copy last received line */
    strncpy(current, &buf[len], read_len);
    len += read_len;

    /* Overwrite trailing new line */
    current[read_len-1] = '\0';
    /* Parse currrent patch by line */
    char *current_ptr = &current[0];
    char *token = strsep(&current_ptr, "\n");

    for(token; token != '\0'; token = strsep(&current_ptr, "\n")) {
      /* Remove trailing '\r' */
      if (token[strlen(token)-1] == '\r')
				token[strlen(token) - 1] = '\0';

      /* Double new line signifies the end of a request */
      if (strlen(token) == 0) {
        if (req_state == 1) {
          req_complete = 1;
        }
        /* Exit loop if not currently building a request */
        else {
          break;
        }
      }
      /* Parse request */
      else {
        switch(req_state) {
          /* No request started */
          case 0:
            if (!strncmp(token, "GET ", 4)) {
              char site_buf[128];
  						sscanf(token, "GET %s %s %*s",
                  site_buf, req.version);

              /* Read in port from host */
              int port = 0;
              sscanf(site_buf, "http://%99[^:]:%99d/", req.site, &port);
              /* Removing trailing slash */
              if (req.site[strlen(req.site) - 1] == '/')
                req.site[strlen(req.site) - 1] = '\0';
              req.port = (port > 0 ? port : 80);

  						req_state = 1;
  						/* Unsuported version */
  						if (strncmp(req.version, "HTTP/1.0", 8) &&
  							strncmp(req.version, "HTTP/1.1", 8)) {
  							strcpy(resp, req_400_2);
                strcat(resp, req.version);
                /* Send error */
    						if (send(client, resp, strlen(resp), 0) < 0)
    							errexit("echo write: %s\n", strerror(errno));
  					  }
            }
            /* Invalid request received */
            else {
  						len = 0;
  						strcpy(resp, req_400_1);
  						strcat(resp, token);
              /* Send error */
  						if (send(client, resp, strlen(resp), 0) < 0)
  							errexit("echo write: %s\n", strerror(errno));
  					}
            break;
          /* Receiving request parameters */
          case 1:
            /* Read in host */
            if (!strncmp(token, "Host:", 5))
              sscanf(token, "%*s %s %*s", req.host);
            /* Read in keep-alive */
            if (!strcmp(token, "Connection: keep-alive"))
              req.keep_alive = 1;
            break;
        }
      }

      /* HTTP request was captured */
			if (req_complete == 1) {
        strcpy(req.full_req, buf);
				if (send_request(client, req) < 0) {
					sprintf(resp, "%s", req_500);
				 	/* Send server error */
					if (write(client, resp, strlen(resp)) < 0)
							errexit("echo write: %s\n", strerror(errno));
				}

				/* Restart request */
				req = EmptyRequest;
				req_state = 0;
				req_complete = 0;
				req_sent = 1;
				len = 0;
      }
    }

    // Kill connection
    if (req.keep_alive == 0 && req_sent == 1)
      return 0;
    req_complete = 0;
    req_sent = 0;

  }
  printf("Exiting %d\n", rv);
  if (rv < 0)
		errexit("Error in select: %s\n", strerror(errno));
	/* Timeout */
	return 0;
}

/* send HTTP Request and send back response */
int send_request(int client, const struct HTTP_Request req) {
  char buf[BUFSIZE];
  int server;         /* socket descriptor to server */
  int len;


  /* Keep track of file descriptor */
  fd_set set;
  FD_ZERO(&set);
  FD_SET(server, &set);

  server = connectsock(req.host, req.port);

  /* Forward request */
  if (send(server, req.full_req, strlen(req.full_req), 0) < 0) {
    printf("Connection to site %s unavailable\n",
        req.site);
    return -1;
  }

  printf("Request sent, awaiting response\n");

  /* Listen for response */
  while((len = recv(server, buf, BUFSIZE, 0)) > 0) {
    buf[len] = '\0';
    /* Forward to client*/
    if (send(client, buf, len, 0) < 0) {
      errexit("Failed write: %s\n", strerror(errno));
      return -1;
    }

  }
  return -1;
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

  printf("Socket %d connected on port %d\n", sock, ntohs(sockin.sin_port));
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
