#include <sys/errno.h>
#include <netinet/in.h>

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define PORT 8679
#define QLEN 32

int errexit(const char *format, ...);
int connectsock(int qlen);

int main(int argc, char *argv[]) {
  fprintf(stdout, "Initializing socket...\n");
  connectsock(QLEN);
  fprintf(stdout, "Socket connected!\n");

  return 0;
}


/* Print given error and exit
 *
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

int connectsock(int qlen) {

  struct sockaddr_in sockin;
  int sock;

  // Zero out sockin
  memset(&sockin, 0, sizeof(sockin));

  // Set family to internet
  sockin.sin_family = AF_INET;
  // Set address to any
  sockin.sin_addr.s_addr = INADDR_ANY;
  // Set port to netowrk short conversion of given port
  sockin.sin_port = htons((unsigned short)PORT);

  if (sockin.sin_port == 0)
    errexit("Unable to get port number: \"%s\"\n", PORT);

  // Create internet TCP socket
  sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);

  if (sock < 0)
    errexit("Unable to create socket: %s\n", strerror(errno));

  // Bind the socket to our parameters 
  if (bind(sock, (struct sockaddr *)&sockin, sizeof(sockin)) < 0) {
    errexit("Unable to bind to port: %d\n", ntohs(sockin.sin_port));
  }

  // Start listening on socket
  if (listen(sock, qlen) < 0) {
    errexit("Unable to listen on %s port %s\n", PORT, strerror(errno));
  }

  return sock;

}


