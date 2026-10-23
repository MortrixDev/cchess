#include "common.h"
#include "gui.h"
 
#include <netdb.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>
#include <signal.h>

#define SERVER_HOST "127.0.0.1"

int connect_to(const char *host, int port) {
  char port_str[8];
  snprintf(port_str, sizeof port_str, "%d", port);

  struct addrinfo hints = {0}, *res, *rp;
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  if (getaddrinfo(host, port_str, &hints, &res) != 0) return -1;

  int fd = -1;
  for (rp = res; rp; rp = rp->ai_next) {
    fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
    if (fd < 0) continue;
    if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) break;
    close(fd);
    fd = -1;
  }
  freeaddrinfo(res);
  return fd;
}

bool send_move(int fd, Move m) {
  uint8_t payload[3] = { m.from, m.to, m.promotion };
  return send_msg(fd, MSG_MOVE, payload, 3);
}

bool send_resign(int fd) {
  return send_msg(fd, MSG_RESIGN, NULL, 0);
}

void *network_thread(void *arg) {
  ClientState *cs = arg;
  uint8_t msg_type, msg_buf[STATE_SIZE];
  uint16_t msg_len;

  while (receive_msg(cs->fd, &msg_type, msg_buf, &msg_len, sizeof msg_buf)) {
    pthread_mutex_lock(&cs->lock);
    switch (msg_type) {
      case MSG_JOIN:
        if (msg_len == 1) cs->view.my_color = msg_buf[0];
        else if (msg_len == 0) cs->view.spectator = true;
        cs->view.joined = true;
        break;
      case MSG_STATE:
        if (msg_len == STATE_SIZE) {
          unpack_state(&cs->view.game, msg_buf);
          cs->view.has_state = true;
          cs->view.game_over = false;
        }
        break;
      case MSG_REJECTED:
        fprintf(stderr, "server rejected %s\n",
                msg_len == 1 ? msg_type_name(msg_buf[0]) : "a message");
        break;
      case MSG_GAME_OVER:
        if (msg_len == 2) {
          cs->view.result = (GameResult){ msg_buf[0], msg_buf[1] };
          cs->view.game_over = true;
        }
        break;
    }
    pthread_mutex_unlock(&cs->lock);
  }

  pthread_mutex_lock(&cs->lock);
  cs->view.connected = false;
  pthread_mutex_unlock(&cs->lock);
  
  return NULL;
}

int main(void) {
  signal(SIGPIPE, SIG_IGN);
  
  ClientState cs = {0};
  pthread_mutex_init(&cs.lock, NULL);
  
  cs.fd = connect_to(SERVER_HOST, PORT);
  if (cs.fd < 0) {
    fprintf(stderr, "could not connect to %s:%d\n", SERVER_HOST, PORT);
    exit(EXIT_FAILURE);
  }
  cs.view.connected = true;

  pthread_t net;
  if (pthread_create(&net, NULL, network_thread, &cs) != 0) {
    perror("pthread_create");
    exit(EXIT_FAILURE);
  }

  run_gui(&cs);

  shutdown(cs.fd, SHUT_RDWR);
  pthread_join(net, NULL);
  close(cs.fd);
  pthread_mutex_destroy(&cs.lock);
  return 0;
}
