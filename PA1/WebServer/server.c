#include <sys/errno.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/sendfile.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#define QLEN 32

struct HTTP_Request {
	char 	command[8];
	char 	request[256];
	char 	version[16];
	char 	host[128];
	int 	keep_alive;
};

static const struct HTTP_Request EmptyRequest;

struct Config {
	char 	portnum[8];
	char 	root[256];
	char 	indexes[10][16];
	int 	index_count;
	char 	content[16][64];
	int 	content_types;
} config;

extern int	errno;
int 		errexit(const char *format, ...);
int 		connectsock(const char* portnum, int qlen);
void		parse_conf(const char* conffile);
int			validate_request(const char* path, void *full_path, void *content_type);
int			interpret(int fd);
int 		process_request(int fd, const struct HTTP_Request req);

int main(int argc, char *argv[]) {
	char *conffile = "./ws.conf";
	struct sockaddr_in c_addr; 		/* From address of client */
	int sock;						/* Server listening socket */
	int connection;					/* Connection socket */
	unsigned int alen;				/* From address length */

	char remoteIP[INET6_ADDRSTRLEN];

	// Load config
	parse_conf(conffile);
	printf("Found: %s\n", config.root);
	// Create and connect listening socket
  	sock = connectsock(config.portnum, QLEN);

	// Primary loop
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

/* Get web server settings from config file */
void parse_conf(const char* conffile) {
	FILE *cfile;				/* Config file */
	char* line;					/* Current line */
	char* token;				/* Current token */
	int read_len = 0;			/* Length read per line */
	size_t len = 0;
	char head[64], tail[256];
	int content_types = 0;		/* Content types extracted */
	int index_count = 0;		/* Directory indexes extracted */

	// Open config file
	if ((cfile = fopen(conffile, "r")) == NULL)
		errexit("failed opening config at: '%s' %s\n", conffile, strerror(errno));
	// Iterate through file
	while((read_len = getline(&line, &len, cfile)) != -1) {
		// Remove endline character
		line[read_len-1] = '\0';
		// Ignore comments
		if (line[0] == '#')
			continue;
		sscanf(line, "%s %s", head, tail);
		// Read in portnum
		if (!strcmp(head, "Listen")) {
			strcpy(config.portnum, tail);
		} 
		// Read in root
		if (!strncmp(head, "DocumentRoot", 12)) {
			sscanf(line, "%*s \"%s", config.root);
			// Ignore trailing quote mark
			config.root[strlen(config.root)-1] = '\0';
		} 
		// Read in content
		if (head[0] == '.') {
			if (content_types < 16) {
				strcat(head, " ");
				strcat(head, tail);
				strcpy(config.content[content_types++], head);
			}
		}
		// Read in indexes
		if (!strcmp(head, "DirectoryIndex")) {
			// Parse line on spaces
			token = strtok(line, " ");
			// Skip first token
			token = strtok(NULL, " ");
			while(token != NULL) {
				strcpy(config.indexes[index_count++], token);

				token = strtok(NULL, " ");
			}  
		}
	}
	config.content_types = content_types;
	config.index_count = index_count;
}

/* Interpret input on a socket */
int interpret(int fd){
	char	*req_400_1 =	"HTTP/1.1 400 Bad Request: Invalid Method: ";
	char	*req_400_2 =	"HTTP/1.1 400 Bad Request: Invalid URI: ";
	char	*req_400_3 =	"HTTP/1.1 400 Bad Request: Invalid HTTP-Version: ";
	char	*req_500 =	"HTTP/1.1 500 Internal Server Error: ";

	struct 	HTTP_Request req;
	char	buf[BUFSIZ];
	char	current[BUFSIZ];
	char 	resp[2048];

	int	read_len = 0;
	int 	len = 0;

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
	FD_SET(fd, &set);

	struct timeval timeout;
	timeout.tv_sec = 10;
	int rv;

	while ((rv = select(fd + 1, &set, NULL, NULL, &timeout)) > 0) {
		// Read input from user
		read_len = recv(fd, &buf[len], (BUFSIZ-len), 0);
		char 	current[read_len];

		// Copy last received line
		strncpy(current, &buf[len], sizeof(current));

		len += read_len;

		// Zero end of array
		buf[len] = '\0';
		current[read_len-1] = '\0';

		// Split current buffer line endings
		char *current_ptr = &current[0];
		char *token = strsep(&current_ptr, "\n");

		for(token; token != '\0'; token = strsep(&current_ptr, "\n")) {
			// Remove trailing '\r'
			if (token[strlen(token)-1] == '\r')
				token[strlen(token) - 1] = '\0';

			//printf("'%s'\n", token);
			// Empty line is the end of a request if one has been started
			if (strlen(token) == 0) {
				if (req_state == 1)
					req_complete = 1;
				else {
					// Exit loop when no longer receiving input
					break;
				}
			} else {
				if(req_state == 0) {	// Not currently receiving headers
					// Valid HTTP header received
					if (!strncmp(token, "GET", 3)) {
						sscanf(token, "%s %s %s %*s", 
							req.command, req.request, req.version);
						req_state = 1;
						// Unsuported version
						if (strncmp(req.version, "HTTP/1.0", 8) &&
							strncmp(req.version, "HTTP/1.1", 8)) {
							int rlen = sprintf(resp, "%s", req_400_3);
							rlen += sprintf(resp + rlen, "%s\r\n", req.version); 
						}

					} else { // Invalid request
						len = 0;
						strcpy(resp, req_400_1);
						strcat(resp, token);
						if (send(fd, resp, strlen(resp), 0) < 0)
							errexit("echo write: %s\n", strerror(errno));
					}
				}  else if (req_state == 1) {	// Receiving request headers
					// Read in host
					if (!strncmp(token, "Host:", 5)) 
						sscanf(token, "%*s %s %*s", req.host);
					// Read in keep-alive
					if (!strcmp(token, "Connection: keep-alive"))
						req.keep_alive = 1;

				}
			}

			/* HTTP request was captured
			*/
			if (req_complete == 1) {
				if (process_request(fd, req) < 0) {
					sprintf(resp, "%s", req_500);
				 	// Send respnse
					if (write(fd, resp, strlen(resp)) < 0)
							errexit("echo write: %s\n", strerror(errno));
				}

				// Restart request
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
	if (rv < 0)
		errexit("echo in select: %s\n", strerror(errno));
	// Timeout
	return 0;
}

/* Process HTTP_Request and build string response */
int process_request(int fd, const struct HTTP_Request req) {
	char	*req_404 = "HTTP/1.1 404 Not Found: ";
	char	*req_501 = "HTTP/1.1 501 Not Implemented: ";

	char 	resp[2048];
	char 	content_type[BUFSIZ];
	char 	full_path[strlen(config.root) + strlen(req.request)];
	int 	len; 			/* Keep track of location in buffers */
	int 	filed;			/* File descriptor for open file */
	struct stat stat_buf;

	// Test path and retrive content_type
	int code = validate_request(req.request, full_path, content_type);
	printf("Received %d %s\n", code, full_path);
	if (code == 200) { //OK
		if ((filed = open(full_path, O_RDONLY)) < 0)
			errexit("echo opening file: %s\n", strerror(errno));
		// Read in file attributes
		fstat(filed, &stat_buf);

		// Build response
		len = sprintf(resp, "HTTP/1.1 200 OK\r\n");
		len += sprintf(resp + len, "Content-Type: %s\r\n", content_type);
		len += sprintf(resp + len, "Content-Length: %ld\r\n", stat_buf.st_size);
		if (req.keep_alive == 1)
			len += sprintf(resp + len, "Connection: Keep-alive\r\n\r\n");	
	}

	if (code == 404) { // Not found
		len = sprintf(resp, "%s", req_404);
		len += sprintf(resp + len, "%s\r\n", req.request); 
	}

	if (code == 501) { // Unsupported
		len = sprintf(resp, "%s", req_501);
		len += sprintf(resp + len, "%s\r\n", req.request); 
	}

	// Send respnse
	if (write(fd, resp, strlen(resp)) < 0)
			errexit("echo write: %s\n", strerror(errno));
	// Send requested file if request was valid
	if (code == 200)
		if ((code = sendfile(fd, filed, (off_t)0, stat_buf.st_size)) < 0)
			errexit("echo sending file: %s\n", strerror(errno));
	close(filed);

	return 0;
}

/* Test if requested file is valid 
 * Returns: HTTP response code for request
*/
int validate_request(const char* path, void * full_path, void *content_type) {
	char request [256];
	strcpy(request, path);
	// Build full path based on DocumentRoot
	strcpy(full_path, config.root);
	strcat(full_path, path);
	// Adjust full path for DirectoryIndex call
	int i, valid = 0;
	if (!strncmp(path, "/", 1) && strlen(path) == 1) {
		char test_path[strlen(full_path)+16];
		int len = config.index_count;
		// Test DirectoryIndexes to find one that exists
		for (i = 0; i < len; i++) {
			strcpy(test_path, full_path);
			strcat(test_path, config.indexes[i]);
			// File exists
			if (access(test_path, F_OK) != -1) {
				valid = 1;
				strcpy(full_path, test_path);
				strcpy(request, config.indexes[i]);
				break;
			}
		}
		memset(&test_path, 0, sizeof(test_path));
		// No valid DocumentIndex found
		if (!valid)
			return 404;
	}
	// Validate request extension
	char *ext = strrchr(request, '.');
	if (!ext)
		ext = ""; 
	valid = 0;
	int len = config.content_types;
	for (i = 0; i < len; i++) {
		char head[8], tail[55];
		sscanf(config.content[i], "%s %s", head, tail);
		// Check extension for match
		if (!strncmp(head, ext, strlen(ext))) {
			valid = 1;
			// Retrieve content type
			strcpy(content_type, tail);
		}
	}
	// Extension not supported
	if (!valid)
		return 501;
	// Test if files exist
	if (access(full_path, F_OK) == -1) {
		return 404;
	}
	// OK
	return 200;
}


/* Print given error and exit */
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
  // Set port to network short conversion of given port
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


