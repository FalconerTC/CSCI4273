#include <sys/errno.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>


/* Structs */
struct Config {
	char	server_names[8][16];
	char	server_addrs[8][128];
	int		server_count;
	char	username[32];
	char	password[32];
} config;

/* Prototypes */
int			errexit(const char *format, ...);
void		parse_conf(const char* conffile);

/*
 * main - DFS client loop
 */
int main(int argc, char *argv[]) {
	char *conffile;

    switch(argc) {
    	case 2:
    		conffile = argv[1];
    		break;
    	case 1:
    		/* Fall through */
    	default:
    		fprintf(stderr, "Usage: %s [conf file]\n", argv[0]);
    		exit(1);
    }

    parse_conf(conffile);



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
				strcpy(config.server_addrs[server_count++], tail);
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
 * errexit - print an error message and exit
 */
int errexit(const char *format, ...) {
        va_list args;

        va_start(args, format);
        vfprintf(stderr, format, args);
        va_end(args);
        exit(1);
}