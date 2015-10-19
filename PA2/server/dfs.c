#include <sys/errno.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>

#define QLEN 32		/* Maximum connections */
#define	BUFSIZE	4096

const char *CONF_FILE = "./server/dfs.conf";

/* Structs */
struct Config {
	char	file_dir[128];
	char	server_users[8][32];
	char	server_passwords[8][32];
	int		user_count;
} config;

/* Prototypes */
void		parse_conf(const char *conffile);
int			interpret(int fd);
int 		process_put(int fd, char* user, char* file_name);
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

	parse_conf(CONF_FILE);

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
 * parse_conf - Read in user supplied config file
 */
void parse_conf(const char* conffile){
	FILE *cfile;			/* Config file */
	char* line;				/* Current line */
	char* token;			/* Current token */
	int read_len = 0;		/* Length read per line */
	size_t len = 0;
	char head[64], tail[64];
	int user_count = 0;

	// Open config file
	if ((cfile = fopen(conffile, "r")) == NULL)
		errexit("Failed opening config at: '%s' %s\n", conffile, strerror(errno));
	// Iterate by line
	while((read_len = getline(&line, &len, cfile)) != -1) {
		line[read_len-1] = '\0';
		// Ignore comments
		if (line[0] == '#')
			continue;
		sscanf(line, "%s %s", head, tail);
		/* Parse FileDirectory */
		if (!strncmp(head, "FileDirectory", 13)) {
			strcpy(config.file_dir, tail);
		} 
		/* Parse user list */
		else {
			if (user_count < 8) {
				strcpy(config.server_users[user_count], head);
				strcpy(config.server_passwords[user_count++], tail);
			}
		}

	}
	config.user_count = user_count;
}

/*
 * Intrepret - Read, execute, and respond to command from socket
 */
int interpret(int fd) {
	char	buf[BUFSIZE];
	char 	command[8], arg[64];
	char	username[32], password[32];
	int		rv = 0;
	int 	len = 0;

	while ((rv = recv(fd, &buf[len], (BUFSIZE-len), 0)) > 0) {
		char 	current[rv];
		/* Retrive the last line sent */
		strncpy(current, &buf[len], sizeof(current));
		len += rv;
		/* Zero end of array */
		current[rv] = '\0';

		printf("Found:  %s\n", current);

		/* First request (auth request) */
		if (len == rv) {
			sscanf(current, "Username:%s Password:%s", username, password);
			// TODO interpret credentials
		} 
		/* Second request (command request) */
		else if (len > rv) { 
			sscanf(current, "%s %s", command, arg);

			if (!strncasecmp(command, "LIST", 4)) {

			} else if (!strncasecmp(command, "GET", 3)) {

			} else if (!strncasecmp(command, "PUT", 3)) {
				process_put(fd, username, arg);
			}

		}

		//if (write(fd, current, strlen(current)) < 0)
		//	errexit("Failed to write: %s\n", strerror(errno));
	}

	if (username != NULL)
		printf("User: %s disconnected\n", username);
	else
		printf("No more\n");
	
	if (rv < 0)
		errexit("echo read: %s\n", strerror(errno));
		
	return rv;
}

/*
 * process_put - Receive and save file chunk
 * Refernce - http://stackoverflow.com/questions/11952898/c-send-and-receive-file
 */
int process_put(int fd, char* user, char* file_name) {
	printf("User: %s\n", user);
	char buf[BUFSIZE];
	int file_size;
	int remaining;
	int len;
	char file_loc[128];
	FILE *file;

	char *auth = "Authenticated. Clear for transfer.";
	sprintf(file_loc, "%s/%s", config.file_dir, user);

	/* Create directory for user */
	if (access(file_loc, F_OK) == -1)
		mkdir(file_loc, 0700);

	sprintf(file_loc, "%s/%s", file_loc, file_name);
	//strcat(file_loc, file_name);

	printf("Sending auth %s\n", file_loc);

	/* Send authentication reply */
	if (write(fd, auth, strlen(auth)) < 0)
		errexit("Failed to write: %s\n", strerror(errno));

	/* Receive file size */
	int rv;
	if ((rv = recv(fd, buf, BUFSIZE, 0)) < 0) 
		errexit("Failed to receive file size: %s\n", strerror(errno));
	file_size = atoi(buf);

	printf("\nFile size : %d\n", file_size);

	/* Open file for writing */
	if ((file = fopen(file_loc, "w")) < 0)
		errexit("Failed to open file at: '%s' %s\n", file_loc, strerror(errno)); 

	remaining = file_size;

	/* Receive and save file chunk */
	while (((len = recv(fd, buf, BUFSIZE, 0)) > 0) && (remaining > 0)) {
		fwrite(buf, sizeof(char), len, file);
		remaining -= len;
		fprintf(stdout, "Received %d bytes\n", len);
	}

	 fclose(file);


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
