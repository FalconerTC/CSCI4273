#include <sys/errno.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/sendfile.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>

#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#define	LINELEN			128
#define REQ_TIMEOUT		1
#define	BUFSIZE			4096

const char *FILE_DIR = "./upload";

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
void		parse_conf(const char *conffile);
void		shell_loop();
int			process_list(char* req);
int			process_put(char* req);
int			send_request(const int server_num, char *req, ...);
int			errexit(const char *format, ...);
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

	/* Open config file */
	if ((cfile = fopen(conffile, "r")) == NULL)
		errexit("Failed opening config at: '%s' %s\n", conffile, strerror(errno));
	/* Iterate by line */
	while((read_len = getline(&line, &len, cfile)) != -1) {
		line[read_len-1] = '\0';
		/* Ignore comments */
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
	ssize_t len = 0;
	ssize_t read;
	char command[8], arg[64];
	int status = 1;

	printf("Servers: %d\n", config.server_count);

	while (status) {
		printf("%s@DFC> ", config.username);
		read = getline(&line, &len, stdin);
		line[read-1] = '\0';

		sscanf(line, "%s %s", command, arg);


		if (!strncasecmp(command, "LIST", 4)) {


		} else if (!strncasecmp(command, "GET", 3)) {

		} else if (!strncasecmp(command, "PUT", 3)) {
			//TODO error handling
			if (strlen(line) == 4)
				printf("PUT needs an argument\n");
			else
				process_put(line);
		} else if (!strncasecmp(command, "EXIT", 4)) {
			status = 0;
		}
		//printf("%s\n", command);
	}
	printf("Shutting down...\n");
}

/*
 * process_list - Process send and receive for LIST command
 */
int process_list(char* req) {

	//send_request(0, req);

}

/*
 * process_put - Process send and receive for PUT command
 */
int process_put(char* req) {
	char command[8], arg[64];
	char file_loc[128];
	int fd;
	struct stat file_stat;

	sscanf(req, "%s %s", command, arg);
	sprintf(file_loc, "%s/%s", FILE_DIR, arg);

	/* Open file for reading */
	if ((fd = open(file_loc, O_RDONLY)) < 0)
		errexit("Failed to open file at: '%s' %s\n", file_loc, strerror(errno)); 

	/* Get file attributes */
	if (fstat(fd, &file_stat) < 0)
		errexit("Error fstat file at: '%s' %s\n", file_loc, strerror(errno));

	close(fd);

	printf("Size: %ld\n", file_stat.st_size);

	/* Send additional paramaters: file size, file location, offset */
	send_request(0, req, file_stat.st_size, file_loc, 0);
}

/*
 * send_command - Communicate request and response to the specified server
 */
int send_request(const int server_num, char* req, ...) {
	int		sock, n;			/* socket descriptor, read count*/
	char 	resp[BUFSIZE];
	char	auth[BUFSIZE];
	int rv, len;
	va_list args;
	struct timeval timeout;

	struct timespec tim;
	tim.tv_sec = 0;
	tim.tv_nsec = 100000000L; /* 0.1 seconds */

	/* Make new TCP connection */
    sock = connectsock(config.server_addrs[server_num], config.server_ports[server_num]);

    /* Keep track of file descriptor */
    fd_set set;
    FD_ZERO(&set);
    FD_SET(sock, &set);
	timeout.tv_sec = REQ_TIMEOUT;
	va_start(args, req);

	/* Send credentials */
	int auth_len = 0;
	auth_len = sprintf(auth, "Username: %s Password: %s", config.username, config.password);

	if (write(sock, auth, strlen(auth)) < 0)
			errexit("Echo write: %s\n", strerror(errno));

	/* Wait 100ms between requests */
	nanosleep(&tim, NULL);

	/* Send request */
	if (write(sock, req, strlen(req)) < 0)
			errexit("Echo write: %s\n", strerror(errno));

	/* Listen for response */
	if ((rv = select(sock+1, &set, NULL, NULL, &timeout)) > 0) {
		len = recv(sock, &resp, BUFSIZE, 0);
		resp[len] = '\0';

		printf("Found: %s\n", resp);

		/* Send file chunk */
		if (!strncmp(resp, "Authenticated. Clear for transfer.", 34)) {
			char file_size[256];		/* Amount of bytes to take from file */
			char *file_loc;				/* Location of file */
			off_t offset;				/* Offset in file for chunk */
			int sent = 0;				/* Bytes sent */
			int fd;	
			int remaining = va_arg(args, off_t);
			struct stat file_stat;

			/* Read in additional params */
			sprintf(file_size, "%d", remaining);
			file_loc = va_arg(args, char *);
            //offset = va_arg(args, int);
            offset = 0;

			printf("%s %d %s\n", file_size, remaining, file_loc);

			/* Send file size */
			if (write(sock, file_size, sizeof(file_size)) < 0)
				errexit("Echo write: %s\n", strerror(errno));

			/* Wait 100ms between requests */
			nanosleep(&tim, NULL); 

			/* Open file for reading */
			if ((fd = open(file_loc, O_RDONLY)) < 0)
				errexit("Failed to open file at: '%s' %s\n", file_loc, strerror(errno)); 

			/* Get file attributes */
			if (fstat(fd, &file_stat) < 0)
				errexit("Error fstat file at: '%s' %s\n", file_loc, strerror(errno));

			while (((sent = sendfile(sock, fd, &offset, BUFSIZE)) >= 0) && (remaining > 0)) {
				remaining -= sent;
				printf("%d bytes sent. %d bytes remaining\n", sent, remaining);
			}


		}


		return 0;
	}
	if (rv < 0)
		errexit("Error in select: %s\n", strerror(errno));
	else if (rv == 0) /* Timeout */
		printf("Connection to server %s timed-out after %d seconds\n", 
				config.server_names[server_num], REQ_TIMEOUT);

	va_end(args);
	return 1;
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
