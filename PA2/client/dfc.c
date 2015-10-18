#include <sys/errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>

#define	LINELEN			128
#define REQ_TIMEOUT		1
#define	BUFSIZE	4096

/* Structs */
struct Config {
	char	server_names[8][16];
	char	server_addrs[8][64];
	char	server_ports[8][8];
	int		server_count;
	char	username[32];
	char	password[32];
} config;

/* Prototypes */
int			errexit(const char *format, ...);
void		parse_conf(const char *conffile);
void		shell_loop();
int			send_request(const int server_num, char *req);
int 		connectsock(const char *host, const char *portnum);

/*
 * main - DFS client loop
 */
int main(int argc, char *argv[]) {
	char *conffile;
	int sock;						/* Server listening socket */
    char *port = "10001";

    switch(argc) {
    	case 2:
    		conffile = argv[1];
    		break;
    	default:
    		fprintf(stderr, "Usage: %s [conf file]\n", argv[0]);
    		exit(1);
    }

    parse_conf(conffile);


    //send_request("127.0.0.1", port);

    shell_loop();

    return(0);
}

/*
 * parse_conf - Read in user supplied config file
 */
void parse_conf(const char* conffile){
	FILE *cfile;			/* Config file */
	char* line;				/* Current line */
	char* token;			/* Current token */
	int read_len = 0;		/* Length read per line */
	size_t len = 0;
	char head[64], middle[64], tail[64];
	int server_count = 0;

	// Open config file
	if ((cfile = fopen(conffile, "r")) == NULL)
		errexit("Failed opening config at: '%s' %s\n", conffile, strerror(errno));
	// Iterate by line
	while((read_len = getline(&line, &len, cfile)) != -1) {
		line[read_len-1] = '\0';
		// Ignore comments
		if (line[0] == '#')
			continue;
		sscanf(line, "%s %s %s", head, middle, tail);

		/* Parse server list */
		if (!strncmp(head, "Server", 6)) {
			if (server_count < 16) {
				strcpy(config.server_names[server_count], middle);
				/* Split address and port */
				token = strtok(tail, ":");
				strcpy(config.server_addrs[server_count], token);
				token = strtok(NULL, ":");
				strcpy(config.server_ports[server_count++], token);
			}
		}
		/* Parse username */
		if (!strncmp(head, "Username:", 9) || !strncmp(head, "Username", 8)) {
			strcpy(config.username, middle);
		}
		/* Parse password */
		if (!strncmp(head, "Password:", 9) || !strncmp(head, "Password", 8)) {
			strcpy(config.password, middle);
		}
	}
	config.server_count = server_count;
}

/*
 * shell_loop - Client shell for receiving commands
 * Referene: http://stephen-brennan.com/2015/01/16/write-a-shell-in-c/
 */
void shell_loop() {
	char *line = NULL;		/* Line read from STDIN */
	char *token;
	ssize_t len;
	char command[8], arg[64];
	int status = 1;

	printf("Servers: %d\n", config.server_count);

	while (status) {
		printf("%s@DFC> ", config.username);
		getline(&line, &len, stdin);

		sscanf(line, "%s %s", command, arg);
		if (!strncasecmp(command, "LIST", 4)) {
			send_request(0, line);

		}
		if (!strncasecmp(command, "GET", 3)) {

		}
		if (!strncasecmp(command, "PUT", 3)) {

		}
		if (!strncasecmp(command, "EXIT", 4)) {
			status = 0;
		}
		//printf("%s\n", command);
	}
	printf("Shutting down...\n");
}

/*
 * send_command - Send the request to the specified server 
 * and return the response
 */
int send_request(const int server_num, char* req) {
	int		sock, n;			/* socket descriptor, read count*/
	char 	resp[BUFSIZE];
	int		outchars, inchars;	/* characters sent and received	*/
	char 	*host = config.server_addrs[server_num];
	char 	*port = config.server_ports[server_num];

	struct timeval timeout;
	timeout.tv_sec = REQ_TIMEOUT;
	int rv, len;

	/* Make new TCP connection */
    sock = connectsock(host, port);

    fd_set set;
    FD_ZERO(&set);
    FD_SET(sock, &set);

	//outchars = strlen(req);

	/* Send request */
	if (write(sock, req, strlen(req)) < 0)
			errexit("Echo write: %s\n", strerror(errno));

	if ((rv = select(sock+1, &set, NULL, NULL, &timeout)) > 0) {
		len = recv(sock, &resp, BUFSIZE, 0);
		resp[len-1] = '\0';

		printf("Found: %s\n", resp);
		return 0;
	}
	if (rv < 0)
		errexit("Error in select: %s\n", strerror(errno));
	/* Timeout */
	printf("Connection to server %s timed-out after %d seconds\n", 
			config.server_names[server_num], REQ_TIMEOUT);
	return 1;

	/* read it back */
	/*for (inchars = 0; inchars < outchars; inchars+=n ) {
		n = read(sock, &req[inchars], outchars - inchars);
	}
	fputs(req, stdout);*/
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
 * connectsock - Allocate and connect a socket using TCP 
 */
int connectsock(const char *host, const char *portnum) {
	struct hostent  *phe;   		/* pointer to host information entry */
	struct sockaddr_in sockin;		/* an Internet endpoint address */
	int sock;              			/* socket descriptor */

	/* Zero out sockin */
	memset(&sockin, 0, sizeof(sockin));

	sockin.sin_family = AF_INET;

	/* Convert and set port */
	sockin.sin_port = htons((unsigned short)atoi(portnum));
	if (sockin.sin_port == 0)
		errexit("Unable to get port number: \"%s\"\n", portnum);

	/* Map host name to IP address, allowing for dotted decimal */
	if ( phe = gethostbyname(host) )
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
