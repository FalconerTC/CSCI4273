#include <sys/errno.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#define QLEN 32

extern int	errno;
int 		errexit(const char *format, ...);
int 		connectsock(const char* portnum, int qlen);
int			interpret(int fd);
int			parse_headers(const char* req, int len);
int 		process_request(void * resp);

int main(int argc, char *argv[]) {
	
	char *portnum = "8679";
	struct sockaddr_in c_addr; 		/* From address of client */
	int sock;						/* Server socket */
	int connection;
	fd_set master_fds; 				/* active file descriptor set */
	fd_set read_fds;				/* read file descriptor set */
	unsigned int alen;				/* From address length */
	int nfds;						/* Number of file descriptors */
	int fdmax;						/* Highest file descriptor */
	int fd;

	char remoteIP[INET6_ADDRSTRLEN];

  	sock = connectsock(portnum, QLEN);

	nfds = getdtablesize();
	FD_ZERO(&master_fds);
	// Add socket to master_fds
	FD_SET(sock, &master_fds);
	// Keep track of the highest file descriptor
	fdmax = sock;

	// Primary loop
	while(1) {
		// Initialize read_fds
		memcpy(&read_fds, &master_fds, sizeof(read_fds));

		// Block until read_fds is ready to be read
		if (select(fdmax+1, &read_fds, NULL, NULL, NULL) < 0)
			errexit("select: %s\n", strerror(errno));

		// Search connections for data to read
		for (fd = 0; fd <= fdmax; fd++) {
			if (FD_ISSET(fd, &read_fds)) {
				if (fd == sock) {
					// Handle new connection
					alen = sizeof(c_addr);
					connection = accept(sock, (struct sockaddr *)&c_addr, &alen);
					if (connection < 0) 
						errexit("accept: %s\n", strerror(errno));
					// Add accepted connection
					FD_SET(connection, &master_fds);
					// Keep track of highest connection
					if (connection > fdmax)
						fdmax = connection;
					printf("New connection from %s on socket %d\n",
						inet_ntop(c_addr.sin_family, 
							&(c_addr.sin_addr), 
							remoteIP, INET6_ADDRSTRLEN), 
						connection);
				} else {
					// Interpret data from client
					if (interpret(fd) == 0) {
						close(fd);
						FD_CLR(fd, &master_fds);
					}
				}
			}
		}
	}
}

int interpret(int fd){
	char 	http1_0[] = "GET / HTTP/1.0";
	char 	http1_1[] = "GET / HTTP/1.1";

	char	buf[BUFSIZ];
	char	current[BUFSIZ];
	int 	encounters = 0;
	char 	req[BUFSIZ];
	char 	resp[BUFSIZ];

	char	*http_req;

	int	read_len = 0;
	int 	len = 0;
	/* Holds the state of a request 
	 * 0 means no request received
	 * 1 means request initiated, waiting on paramaters
	*/
	int 	req_state = 0; 


	/*cc = read(fd, buf, sizeof buf);
	memcpy(&req, &buf, sizeof buf);

	if (cc < 0)
		errexit("echo read: %s\n", strerror(errno)); */

	// Read input from user
	while((read_len = read(fd, &buf[len], (BUFSIZ-len))) > 0) {
		char current[read_len];
		// Copy last received line
		strncpy(current, &buf[len], sizeof(current));
		current[read_len-1] = '\0';

		printf("%s\n", buf);

		if(req_state == 0) {	// Not currently receiving headers
			// Valid HTTP header received
			if (!strcmp(current, http1_0) || !strcmp(current, http1_1)) {
				printf("Headers received: \n");
				req_state = 1;
				len += read_len;
				continue;
			}

		}  else if (req_state == 1) {	// Receiving request headers
			// Empty line indicates the end of a request
			if (read_len == 1) {
				printf("OVER\n");
				req_state = 0;
				parse_headers(buf, len);
				// Restart buf
				len = 0;
			}

		}
		//printf("%s\n", buf);
	}

	//printf("%s\n", buf);


	return 1;

	int cc;
	// Remove the new line at the end
	req[ strlen(req) - 1 ] = '\0';

	http_req = strtok(buf, "\n\r");

	printf("%s\n", buf);

	/*while (http_req != NULL) {
		printf("%s\n", http_req);
		http_req = strtok(NULL, "\n\r");
	}*/

	return cc;

	
	if (!strcmp(req, http1_0) || !strcmp(req, http1_1)) {
		printf("HTTP request made\n");
		process_request(&resp);
	}
	

	if (cc && write(fd, resp, cc) >= 0)
		printf("Echo: %s", resp);
	else
		errexit("echo write: %s\n", strerror(errno));
	return cc;
}

int parse_headers(const char* req, int len) {

}

int process_request(void * resp) {
	strcpy(resp, "HTTP/1.1 200 OK\n");
	//printf("Response: %s\n", (char*)resp);

	return 0;
}


/* Print given error and exit
 */
int errexit(const char *format, ...) {
        va_list args;

        va_start(args, format);
        vfprintf(stderr, format, args);
        va_end(args);
        exit(1);
}

/* Establish socket connection and sets it to listen for connections
 * Returns: Socket created
 */

int connectsock(const char* portnum, int qlen) {

  struct sockaddr_in sockin;
  int sock;

  // Zero out sockin
  memset(&sockin, 0, sizeof(sockin));

  // Set family to internet
  sockin.sin_family = AF_INET;
  // Set address to any
  sockin.sin_addr.s_addr = INADDR_ANY;
  // Set port to netowrk short conversion of given port
  sockin.sin_port = htons((unsigned short)atoi(portnum));

  if (sockin.sin_port == 0)
    errexit("Unable to get port number: \"%s\"\n", portnum);

  // Create internet TCP socket
  sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);

  if (sock < 0)
    errexit("Unable to create socket: %s\n", strerror(errno));

  // Bind the socket to our parameters 
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

  // Start listening on socket
  if (listen(sock, qlen) < 0) {
    errexit("Unable to listen on %s port %s\n", portnum, strerror(errno));
  }

  printf("Socket %d connected on port %d\n", sock, ntohs(sockin.sin_port));
  return sock;

}


