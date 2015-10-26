#include <sys/errno.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/sendfile.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>

#include <time.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <openssl/md5.h>

#define	LINELEN			128
#define REQ_TIMEOUT		1
#define	BUFSIZE			4096

const char *FILE_DIR = "./upload";
const char *RETRIEVE_DIR = "./retrieval";

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
int			process_list();
int 		process_get(char *file_name);
int			process_put(char *file_name);
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
			process_list();
		} else if (!strncasecmp(command, "GET", 3)) {
			if (strlen(line) <= 4)
				printf("GET needs an argument\n");
			else
				process_get(arg);
		} else if (!strncasecmp(command, "PUT", 3)) {
			if (strlen(line) <= 4)
				printf("PUT needs an argument\n");
			else
				process_put(arg);
		} else if (!strncasecmp(command, "EXIT", 4)) {
			status = 0;
		}
	}
	printf("Shutting down...\n");
}


/*
 * process_get - Process send and receive for GET command
 * GET retrieves chunks for a file and saves the rebuilt file
 */
int process_get(char* file_name) {
	FILE *file;
	int count = config.server_count;
	char req[128];
	char chunk[count][BUFSIZE];
	char chunk_buf1[BUFSIZE];
	char chunk_buf2[BUFSIZE];
	int indexes[count];
	char file_buf[count*BUFSIZE];
	int ret = 0;
	int pieces = 0;

	sprintf(req, "GET .%s.", file_name);

	/* Generate random starting primary server */
	srand(time(NULL));
	int start = ((rand() % 2) * 2);

	/* Send buf pointers as char array to get past va_args limitations */
	sprintf(chunk_buf1, "%p %p", &(chunk[0]), &(chunk[1]));
	sprintf(chunk_buf2, "%p %p", &(chunk[2]), &(chunk[3]));

	/* Request first primary server */
	if ((ret = send_request(start, req, chunk_buf1)) < 0) {

		/* Request first secondary server */
		if ((ret = send_request(start+1, req, chunk_buf1)) < 0) {
			printf("File is incomplete\n");
			return 1;
		} else {
			/* Read indexes from return code */
			indexes[0] = ret % 10;
			indexes[1] = ret / 10;
			pieces +=2;
		}

		/* Request second secondary server */
		if ((ret = send_request((start+3)%count, req, chunk_buf2)) < 0) {
			printf("File is incomplete\n");
			return 1;
		} else {
			/* Read indexes from return code */
			indexes[2] = ret % 10;
			indexes[3] = ret / 10;
			pieces +=2;
		}

	} else {
		/* Read indexes from return code */
		indexes[0] = ret % 10;
		indexes[1] = ret / 10;
		pieces +=2;
	}

	if (pieces < count) {
		/* Request second primary server */
		if ((ret = send_request((start+2)%count, req, chunk_buf2)) < 0) {

			/* Request first secondary server */
			if ((ret = send_request(start+1, req, chunk_buf1)) < 0) {
				printf("File is incomplete\n");
				return 1;
			} else {
				/* Read indexes from return code */
				indexes[0] = ret % 10;
				indexes[1] = ret / 10;
				pieces +=2;
			}

			/* Request second secondary server */
			if ((ret = send_request((start+3)%count, req, chunk_buf2)) < 0) {
				printf("File is incomplete\n");
				return 1;
			} else {
				/* Read indexes from return code */
				indexes[2] = ret % 10;
				indexes[3] = ret / 10;
				pieces +=2;
			}
		} else {
			/* Read indexes from return code */
			indexes[2] = ret % 10;
			indexes[3] = ret / 10;
			pieces +=2;
		}
	}

	/* Build string buffer */
	strcpy(file_buf, "\0");
	int piece = 1;
	int i;
	for(i = 0; piece < count+1; i++) {
		if (indexes[i] == piece) {
			strcat(file_buf, chunk[i]);
			i = -1;
			piece++;
		}
		if (i == count) 
			break;
	}

	/* Format save file string */
	char file_loc[BUFSIZE];
	sprintf(file_loc, "%s/%s", RETRIEVE_DIR, file_name);

	/* Open file for writing */
	if ((file = fopen(file_loc, "w")) < 0)
		errexit("Failed to open file at: '%s' %s\n", file_loc, strerror(errno)); 

	fwrite(file_buf, sizeof(char), strlen(file_buf), file);

	printf("File saved to: %s\n", file_loc);

	fclose(file);
}

/*
 * process_put - Process send and receive for PUT command
 * PUT chunks a file between servers and sends the chunks
 * Reference - http://stackoverflow.com/questions/10324611/how-to-calculate-the-md5-hash-of-a-large-file-in-c
 */
int process_put(char* file_name) {
	char req[128];
	char file_loc[128];
	int fd;
	struct stat file_stat;
	long size;						/* File size */
	int rv;							/* Holds number of bytes read */
	char buf[BUFSIZE];				/* Holds bytes read from file */
	MD5_CTX mdContext;				/* MD5 object */
	char byte[1];
	char sum[MD5_DIGEST_LENGTH];	/* Holds MD5 sum */
	int order;						/* Defines the order of file splitting between server */
	int order_mat[config.server_count][2];
	int count = config.server_count;
	//int count = 4;
	int i;

	sprintf(file_loc, "%s/%s", FILE_DIR, file_name);

	/* Open file for reading */
	if ((fd = open(file_loc, O_RDONLY)) < 0)
		errexit("Failed to open file at: '%s' %s\n", file_loc, strerror(errno)); 

	/* Get file attributes */
	if (fstat(fd, &file_stat) < 0)
		errexit("Error fstat file at: '%s' %s\n", file_loc, strerror(errno));
	size = file_stat.st_size;

	/* Get MD5sum of file to select server order */
	MD5_Init(&mdContext);
	while ((rv = read(fd, &buf, BUFSIZE)) != 0)
		MD5_Update(&mdContext, buf, rv);
	MD5_Final(sum, &mdContext);
	/* Use modulus on first byte of MD5 hash */
	sprintf(byte, "%02x", sum[0]);
	order = (strtol(byte, NULL, 16) % count);

	close(fd);

	/* Calculate order matrix
	 * (4 X 2) Piece number X Server number
	 */
	 for (i = 1; i < count + 1; i++) {
	 	order_mat[((i + count - order) % count)][0] = i-1;
	 	order_mat[((i + count - order) % count)][1] = (i % count);
	 }
	//printf("Size: %ld Using: %ld\n", size, size / count);

	/* Format new filename */
	sprintf(req, "PUT .%s.", file_name);

	/* Send requests 
	 * Include additional paramaters: file size, file location, offset
	 */
	int offset = 0;
	char piece_name[128];

	for (i = 0; i < count - 1; i++) {
		int chunk_size = (size/count);
		if (chunk_size % 2 == 1)
			chunk_size--;
		sprintf(piece_name, "%s%d", req, i+1);

		send_request(order_mat[i][0], piece_name, chunk_size, file_loc, offset);
		send_request(order_mat[i][1], piece_name, chunk_size, file_loc, offset);
		offset += chunk_size;
	}
	sprintf(piece_name, "%s%d", req, count);

	/* Send remaining data in last chunk */
	send_request(order_mat[count-1][0], piece_name, (size - offset), file_loc, offset);
	send_request(order_mat[count-1][1], piece_name, (size - offset), file_loc, offset);

	printf("Request to save file sent\n");
	
}

/*
 * process_list - Process send and receive for LIST command
 * LIST queries servers for file parts and shows complete files
 */
int process_list(char* req) {
	int count = config.server_count;
	char files[64][64];
	int file_parts[64][count];
	int file_count = 0;
	char buf[BUFSIZE];
	char *token;
	int server;
	int i, j;

	/* Initialize file parts */
	for (i = 0; i < 64; i++)
		for (j = 0; j < count; j++)
			file_parts[i][j] = 0;

	/* Request part information from all servers */
	for (server = 0; server < 4; server++) {

		if (send_request(server, req, buf) < 0)
			continue;

		/* Parse file list */
		token = strtok(buf, "\n");
		while (token != NULL) {
			char name[32];
			int piece;
			int new = 1;

			/* Read and clean name and piece number */
			sscanf(token, ".%s", name);
			piece = name[strlen(name)-1] - '0';
			name[strlen(name)-2] = '\0';

			/* Check if file is already tracked */
			for (i = 0; i < file_count; i++) {
				if (!strcmp(name, files[i])) {
					file_parts[i][piece-1] = 1;
					new = 0;
					break;
				}
			}

			/* Insert new file */
			if (new == 1) {
				strcpy(files[file_count], name);
				file_parts[file_count][piece-1] = 1;
				file_count++;
			}

			token = strtok(NULL, "\n");
		}

	}

	printf("\n");

	/* Display compiled file information */
	for (i = 0; i < file_count; i++) {
		char name[32];
		int value = 0;

		/* Check if file can be completed */
		for (j = 0; j < count; j++) {
			value += (pow(10, j)*file_parts[i][j]);
		}

		strcpy(name, files[i]);

		if (value < 1111)
			printf("%s [incomplete]\n", name);
		else
			printf("%s\n", name);
	}

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

	if (write(sock, auth, strlen(auth)) < 0) {
		printf("Connection to server %s unavailable\n", 
				config.server_names[server_num]);
		return -1;
	}

	/* Wait 100ms between requests */
	nanosleep(&tim, NULL);

	/* Send request */
	if (write(sock, req, strlen(req)) < 0)
			errexit("Echo write: %s\n", strerror(errno));

	/* Listen for response */
	if ((rv = select(sock+1, &set, NULL, NULL, &timeout)) > 0) {
		len = recv(sock, &resp, BUFSIZE, 0);
		resp[len] = '\0';

		//printf("Found: %s\n", resp);

		/* Process PUT */
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
            offset = va_arg(args, int);

			//printf("size: %s remaining: %d %s\n", file_size, remaining, file_loc);

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

			//printf("offset %ld\n", offset);
			/* Send file chunk */
			while (((sent = sendfile(sock, fd, &offset, remaining)) >= 0) && (remaining > 0)) {
				remaining -= sent;
				//printf("%d bytes sent. %d bytes remaining\n", sent, remaining);
			}
			return 0;
		}
		/* Process GET */
		else if (!strncmp(resp, "Authenticated. Sending files.", 29)) {
			char	buf[BUFSIZE];
			int		file_count = 0;
			int 	files_recv = 0;
			int 	remaining;
			char 	*chunks[2];
			int 	ret = 0;			/* Hold return value */

			char *ptrs = va_arg(args, char *);
			sscanf(ptrs, "%p %p", &chunks[0], &chunks[1]);

			/* Receive file count */
			if ((rv = recv(sock, buf, BUFSIZE, 0)) < 0) 
				errexit("Failed to receive file size: %s\n", strerror(errno));

			sscanf(buf, "Files: %d", &file_count);

			while (files_recv < file_count) {
				int file_size = 0;
				int index;

				/* Receive file size */
				if ((rv = recv(sock, buf, BUFSIZE, 0)) < 0)
					errexit("Failed to receive file: %s\n", strerror(errno));
				sscanf(buf, "%d %d", &file_size, &index);

				remaining = file_size;
				char chunk_buf[file_size];

				/* Receive file chunk*/
				while ((remaining > 0) && ((len = recv(sock, chunk_buf, BUFSIZE, 0)) > 0)) {
					remaining -= len;
				}
				/* Clean and copy data */
				chunk_buf[file_size] = '\0';
				strcpy(chunks[files_recv], chunk_buf);
				ret += (pow(10, files_recv)*index);

				files_recv++;
			}
			va_end(args);
			return ret;
		}
		/* Process LIST */
		else if (!strncmp(resp, "Authenticated. Listing files.", 29)) {
			char	buf[BUFSIZE];
			char 	*resp = va_arg(args, char *);

			/* Receive file list */
			if ((rv = recv(sock, buf, BUFSIZE, 0)) < 0)
				errexit("Failed to receive file: %s\n", strerror(errno));

			strcpy(resp, buf);
			return rv;

		}

		return -1;
	}
	if (rv < 0)
		errexit("Error in select: %s\n", strerror(errno));
	else if (rv == 0) /* Timeout */
		printf("Connection to server %s timed-out after %d seconds\n", 
				config.server_names[server_num], REQ_TIMEOUT);

	va_end(args);
	return -1;
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
