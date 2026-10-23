#include "client/client.h"
#include "client/ui.h"
#include "net/net.h"
#include "proto/proto.h"

#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

#define SERVER_HOST "127.0.0.1"

void *network_thread(void *arg) {
  ClientState *cs = arg;
  uint8_t msg_type, msg_buf[MAX_PAYLOAD];
  uint16_t msg_len;

  while (receive_msg(cs->fd, &msg_type, msg_buf, &msg_len, sizeof msg_buf)) {
    pthread_mutex_lock(&cs->lock);
    switch (msg_type) {
      case MSG_JOIN: {
        Role role;
        if (decode_join(msg_buf, msg_len, &role, &cs->view.my_color)) {
          cs->view.spectator = role == ROLE_SPECTATOR;
          cs->view.joined = true;
        }
        break;
      }
      case MSG_STATE:
        if (decode_state(msg_buf, msg_len, &cs->view.game)) {
          cs->view.has_state = true;
          cs->view.game_over = false;
        }
        break;
      case MSG_REJECTED: {
        uint8_t rejected;
        fprintf(stderr, "server rejected %s\n",
                decode_rejected(msg_buf, msg_len, &rejected) ? msg_type_name(rejected)
                                                             : "a message");
        break;
      }
      case MSG_GAME_OVER:
        if (decode_game_over(msg_buf, msg_len, &cs->view.result))
          cs->view.game_over = true;
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

  run_ui(&cs);

  shutdown(cs.fd, SHUT_RDWR);
  pthread_join(net, NULL);
  close(cs.fd);
  pthread_mutex_destroy(&cs.lock);
  return 0;
}
