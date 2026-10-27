#include "client/client.h"
#include "client/ui.h"
#include "net/net.h"
#include "proto/proto.h"

#include <ctype.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
      case MSG_HELLO_OK: {
        uint16_t version, grace_secs;
        uint8_t max_spectators;
        if (!decode_hello_ok(msg_buf, msg_len, &version, &max_spectators, &grace_secs))
          break;

        // The server rejects a client that is too old; this is the mirror of
        // that check, for a client that is too new.
        if (version != PROTO_VERSION) {
          cs->view.error_code = ERR_VERSION_MISMATCH;
          cs->view.error_context = MSG_HELLO_OK;
          break;
        }
        cs->view.grace_secs = grace_secs;

        if (cs->intent == INTENT_CREATE)
          send_create_room(cs->fd, ROOM_ALLOW_SPECTATORS, cs->password);
        else
          send_join_room(cs->fd, cs->room_id, cs->spectate ? JOIN_AS_SPECTATOR : 0,
                         cs->password, NULL);
        break;
      }
      case MSG_JOINED: {
        Role role;
        uint8_t flags;
        if (decode_joined(msg_buf, msg_len, cs->view.room_id, &role, cs->token, &flags)) {
          cs->view.spectator = role == ROLE_SPECTATOR;
          if (!cs->view.spectator) cs->view.my_color = ROLE_COLOR(role);
          cs->view.joined = true;
          cs->view.error_code = ERR_NONE;
        }
        break;
      }
      case MSG_STATE:
        if (decode_state(msg_buf, msg_len, &cs->view.game)) {
          cs->view.has_state = true;
          cs->view.game_over = false;
          cs->view.error_code = ERR_NONE;
        }
        break;
      case MSG_ERROR: {
          uint8_t code, context;
          if (decode_error(msg_buf, msg_len, &code, &context)) {
            cs->view.error_code = code;
            cs->view.error_context = context;
          }
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

static void usage(const char *argv0) {
  fprintf(stderr,
          "usage: %s --create [password]\n"
          "       %s --join CODE [password] [--spectate]\n",
          argv0, argv0);
  exit(EXIT_FAILURE);
}

// Room codes are generated uppercase and the decoder accepts nothing else,
// so a code typed in lower case is corrected rather than refused.
static void copy_room_id(char *dst, const char *src) {
  size_t i = 0;
  for (; src[i] != '\0' && i < ROOM_ID_LEN; i++) dst[i] = (char)toupper((unsigned char)src[i]);
  dst[i] = '\0';
}

static void parse_args(ClientState *cs, int argc, char **argv) {
  int next = 2;

  if (argc < 2) usage(argv[0]);

  if (strcmp(argv[1], "--create") == 0) {
    cs->intent = INTENT_CREATE;
  } else if (strcmp(argv[1], "--join") == 0) {
    cs->intent = INTENT_JOIN;
    if (argc < 3) usage(argv[0]);
    if (strlen(argv[2]) != ROOM_ID_LEN) usage(argv[0]);
    copy_room_id(cs->room_id, argv[2]);
    next = 3;
  } else {
    usage(argv[0]);
  }

  for (; next < argc; next++) {
    if (strcmp(argv[next], "--spectate") == 0) {
      cs->spectate = true;
    } else if (cs->password[0] == '\0') {
      if (strlen(argv[next]) > MAX_PASSWORD_LEN) usage(argv[0]);
      snprintf(cs->password, sizeof cs->password, "%s", argv[next]);
    } else {
      usage(argv[0]);
    }
  }
}

int main(int argc, char **argv) {
  if (!proto_self_check()) exit(EXIT_FAILURE);
  signal(SIGPIPE, SIG_IGN);

  ClientState cs = {0};
  parse_args(&cs, argc, argv);
  pthread_mutex_init(&cs.lock, NULL);
  
  cs.fd = connect_to(SERVER_HOST, PORT);
  if (cs.fd < 0) {
    fprintf(stderr, "could not connect to %s:%d\n", SERVER_HOST, PORT);
    exit(EXIT_FAILURE);
  }
  cs.view.connected = true;
  send_hello(cs.fd);

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
