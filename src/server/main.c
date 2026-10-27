#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "net/net.h"
#include "proto/proto.h"
#include "server/server.h"

int main(void) {
  if (!proto_self_check()) {
    exit(EXIT_FAILURE);
  }

  Server s = { 0 };

  // Prevents the program to crash when a player disconnects
  signal(SIGPIPE, SIG_IGN);

  s.listen_fd = listen_on(PORT, MAX_CLIENTS);
  if (s.listen_fd < 0) {
    exit(EXIT_FAILURE);
  }

  printf("Server running...\n");
  run_server(&s);

  close(s.listen_fd);
  return 0;
}
