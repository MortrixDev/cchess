#include "net/net.h"

#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

bool read_full(int fd, void *buf, size_t n) {
  uint8_t *p = buf;
  while (n > 0) {
    ssize_t r = read(fd, p, n);
    if (r < 0 && errno == EINTR) {
      continue;
    }
    if (r <= 0) {
      return false;
    }
    p += r;
    n -= r;
  }
  return true;
}

bool write_full(int fd, const void *buf, size_t n) {
  const uint8_t *p = buf;
  while (n > 0) {
    ssize_t w = write(fd, p, n);
    if (w < 0 && errno == EINTR) {
      continue;
    }
    if (w <= 0) {
      return false;
    }
    p += w;
    n -= w;
  }
  return true;
}

int connect_to(const char *host, int port) {
  char port_str[8];
  snprintf(port_str, sizeof port_str, "%d", port);

  struct addrinfo hints = { 0 };
  struct addrinfo *res;
  struct addrinfo *rp;
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  if (getaddrinfo(host, port_str, &hints, &res) != 0) {
    return -1;
  }

  int fd = -1;
  for (rp = res; rp; rp = rp->ai_next) {
    fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
    if (fd < 0) {
      continue;
    }
    if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) {
      break;
    }
    close(fd);
    fd = -1;
  }
  freeaddrinfo(res);
  return fd;
}

int listen_on(int port, int backlog) {
  struct sockaddr_in listen_address = { 0 };
  int opt = 1;
  int fd = socket(AF_INET, SOCK_STREAM, 0);

  if (fd < 0) {
    perror("socket failed");
    return -1;
  }

  if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof opt)) {
    perror("setsockopt");
    close(fd);
    return -1;
  }

  listen_address.sin_family = AF_INET;
  listen_address.sin_addr.s_addr = INADDR_ANY;
  listen_address.sin_port = htons(port);

  if (bind(fd, (struct sockaddr *)&listen_address, sizeof listen_address) < 0) {
    perror("bind failed");
    close(fd);
    return -1;
  }

  if (listen(fd, backlog) < 0) {
    perror("listen");
    close(fd);
    return -1;
  }

  return fd;
}

int accept_conn(int listen_fd) {
  return accept(listen_fd, NULL, NULL);
}
