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

struct HTTP_Request {
	char 	command[8];
	char 	request[256];
	char 	version[16];
	char 	host[128];
	int 	keep_alive;
};

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
int 		process_request(const struct HTTP_Request req, void * resp);

int main(int argc, char *argv[]) {
	char *conffile = "./sample-ws.conf";
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
	struct HTTP_Request req;

	char	*bad_req = "HTTP/1.1 400 Bad Request: Invalid Method: ";
	char	buf[BUFSIZ];
	char	current[BUFSIZ];
	int 	encounters = 0;
	char 	resp[BUFSIZ];

	char	*http_req;

	int		read_len = 0;
	int 	len = 0;

	/* Holds the state of a request 
	 * 0 means no request received
	 * 1 means request initiated, waiting on paramaters
	*/
	int 	req_state = 0; 
	int		split_request = 0;


	// Read input from user
	while((read_len = recv(fd, &buf[len], (BUFSIZ-len), 0)) > 0) {
		char 	current[read_len];
		int 	tokens_read = 0;
		// Copy last received line
		strncpy(current, &buf[len], sizeof(current));
		current[read_len-1] = '\0';

		len += read_len;
		buf[len] = '\0';
	
		// Split current buffer line endings
		char *token = strtok(current, "\n\r");
		do {
			if(req_state == 0) {	// Not currently receiving headers
				// Valid HTTP header received
				if (!strncmp(token, "GET", 3)) {
					sscanf(token, "%s %s %s %*s", 
						req.command, req.request, req.version);
					req_state = 1;
				} else { // Invalid request
					len = 0;
					strcpy(resp, bad_req);
					strcat(resp, token);
					if (write(fd, resp, strlen(resp)) < 0)
						errexit("echo write: %s\n", strerror(errno));
				}
			}  else if (req_state == 1) {	// Receiving request headers
				// NULL is the end of a split request			
				if (token == NULL)
					split_request = 1;
				// Read in host
				if (!strncmp(token, "Host:", 5)) 
					sscanf(token, "%*s %s %*s", req.host);
				// Read in keep-alive
				if (!strcmp(token, "Connection: keep-alive"))
					req.keep_alive = 1;

			}
			token = strtok(NULL, "\n\r");
			tokens_read++;
		} while(token != NULL);


		/* HTTP request was captured
		 * If requests were sent slowly
		*/
		if ((tokens_read > 1 && req_state == 1) || (split_request == 1)) {
			req_state = 0;
			process_request(req, resp);

			if (write(fd, resp, strlen(resp)) < 0)
				errexit("echo write: %s\n", strerror(errno));
			// Restart buf
			len = 0;
		}

	}

	return 0;
	
/*
	if (cc && write(fd, resp, cc) >= 0)
		printf("Echo: %s", resp);
	else
		errexit("echo write: %s\n", strerror(errno));
	return cc; */
}

/* Process HTTP_Request and build string response */
int process_request(const struct HTTP_Request req, void *resp) {
	//printf("Sending reply\n");
	char content_type[BUFSIZ];
	char full_path[strlen(config.root) + strlen(req.request)];
	int code = validate_request(req.request, full_path, content_type);
	printf("Received %s %d %s\n", content_type, code, full_path);
	strcpy(resp, "HTTP/1.1 200 OK\n");
	//printf("Response: %s\n", (char*)resp);

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

	//strcpy(content_type, full_path);
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


