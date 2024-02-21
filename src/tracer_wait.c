#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>

#define EARGC 1
#define EBIND 2
#define EREAD 3

int main(int argc, char **argv) {
  if (argc < 2) {
    return EARGC;
  }

  int sock = socket(AF_UNIX, SOCK_DGRAM, 0);
  int triggers = atoi(argv[1]);

  struct sockaddr_un addr;
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, "start.sock", sizeof(addr.sun_path) - 1);

  if (bind(sock, (const struct sockaddr *) &addr, sizeof(addr)) == -1) {
    return EBIND;
  }

  char buffer[triggers];

  int read_bytes = 0;

  while (read_bytes < triggers) {
    int bytes = read(sock, buffer, triggers);

    if (bytes == -1) {
      return EREAD;
    }

    read_bytes += bytes;
  }

  return 0;
}
