#ifndef NET_NET_H
#define NET_NET_H

#include <stdbool.h>
#include <stddef.h>

bool read_full(int fd, void *buf, size_t n);
bool write_full(int fd, const void *buf, size_t n);

// Both return -1 on failure, after reporting the cause.
int connect_to(const char *host, int port);
int listen_on(int port, int backlog);

// Accepts one pending connection, or -1 if none could be accepted.
int accept_conn(int listen_fd);

#endif
