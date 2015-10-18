#include <sys/errno.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>

#define QLEN 32		/* Maximum connections */
#define	BUFSIZE	4096

const char *CONF = "./server/dfs.conf";

/* Structs */
struct Config {
	char	file_dir[128];
	char	server_users[8][32];
	char	server_passwords[8][32];
	int		user_count;
} config;

/* Prototypes */
int			interpret(int fd);
int			errexit(const char *format, ...);
int 		connectsock(const char* portnum, int qlen);


/*
 * main - DFS server loop
 */
int main(int argc, char *argv[]) {
	char *directory;
	char *port;
	struct sockaddr_in c_addr; 		/* From address of client */
	int sock;						/* Server listening socket */
	fd_set	rfds;					/* read file descriptor set	*/
	fd_set	afds;					/* active file descriptor set */
	unsigned int alen;				/* From address length */
	int fd, nfds;

	switch(argc) {
		case 3:
			directory = argv[1];
			port = argv[2];
			break;
		default:
			fprintf(stderr, "Usage: %s [dir-name] [port]\n", argv[0]);
			exit(1);
	}

	//parse_conf(CONF);

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
 * Intrepret - Read, execute, and respond to command from socket
 */
int interpret(int fd) {
	char	buf[BUFSIZ];
	int	cc;

	cc = read(fd, buf, sizeof buf);
	buf[cc] = '\0';

	if (cc < 0)
		errexit("echo read: %s\n", strerror(errno));
	if (cc && write(fd, buf, cc) >= 0)
		printf("Echo: %s",buf);
	else
		errexit("echo write: %s\n", strerror(errno));
	return cc;
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
	            int socklen = sizeof(sockin);

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
