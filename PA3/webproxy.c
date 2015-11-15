#include <dirent.h>
#include <fcntl.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>

#include <sys/errno.h>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <netinet/in.h>

#define QLEN 32		/* Maximum connections */
#define	BUFSIZE	4096

/* Prototypes */
int			interpret(int fd);
int			errexit(const char *format, ...);
int 		connectsock(const char *portnum, int qlen);

int main(int argc, char *argv[]) {
  char *port;                     /* Port to host proxy at */
  struct sockaddr_in c_addr;      /* From address of client */
  int sock;                       /* Server listening socket */
  fd_set	rfds;                   /* read file descriptor set	*/
  fd_set	afds;                   /* active file descriptor set */
  unsigned int alen;				      /* From address length */
  int fd, nfds;

  switch(argc) {
    case 2:
      port = argv[1];
      break;
    default:
      fprintf(stderr, "Usage: %s [port]\n", argv[0]);
      exit(1);
  }

  sock = connectsock(port, QLEN);

  nfds = getdtablesize();
  FD_ZERO(&afds);
  FD_SET(sock, &afds);

  while (1) {
    /* Copy afds to rfds */
    memcpy(&rfds, &afds, sizeof(rfds));

    if (select(nfds, &rfds, NULL, NULL, NULL) < 0)
      errexit("select: %s\n", strerror(errno));
    if (FD_ISSET(sock, &rfds)) {
      int	connection;	/* Connection socket */

      alen = sizeof(c_addr);
      connection = accept(sock, (struct sockaddr *)&c_addr, &alen);
      if (connection < 0)
        errexit("Accept: %s\n",
          strerror(errno));
      FD_SET(connection, &afds);
    }
    for (fd=0; fd<nfds; ++fd)
      if (fd != sock && FD_ISSET(fd, &rfds))
        if (interpret(fd) == 0) {
          (void) close(fd);
          FD_CLR(fd, &afds);
        }
  }
}

/*
 * Intrepret - Read and pass GET request for client
 */
int interpret(int fd) {

  printf("Welcome!\n");

  return 0;
}

/*
 * errexit - print an error message and exit
 */
int errexit(const char *format, ...) {
        va_list args;

        va_start(args, format);
        vfprintf(stderr, format, args);
        va_end(args);
        exit(1);
}

/*
 * connectsock - Allocate and bind a server socket using TCP
 */
int connectsock(const char* portnum, int qlen) {
	struct sockaddr_in sockin;
	int sock;

	/* Zero out sockin */
	memset(&sockin, 0, sizeof(sockin));

	sockin.sin_family = AF_INET;
	sockin.sin_addr.s_addr = INADDR_ANY;

	/* Convert and set port */
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

	/* Start listening on socket */
	if (listen(sock, qlen) < 0) {
	errexit("Unable to listen on %s port %s\n", portnum, strerror(errno));
	}

	printf("Socket %d connected on port %d\n", sock, ntohs(sockin.sin_port));
	return sock;
}
