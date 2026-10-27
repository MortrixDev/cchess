#include <ctype.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "chess/board.h"
#include "chess/rules.h"
#include "client/client.h"
#include "client/ui.h"
#include "net/net.h"
#include "proto/proto.h"
#include "util/util.h"

#define SERVER_HOST "127.0.0.1"

#define RECONNECT_MIN_MS 250
#define RECONNECT_MAX_MS 2000

static bool should_quit(ClientState *cs) {
  pthread_mutex_lock(&cs->lock);
  bool quit = cs->quit;
  pthread_mutex_unlock(&cs->lock);
  return quit;
}

static int next_backoff(int delay_ms) {
  if (delay_ms == 0) {
    return RECONNECT_MIN_MS;
  }

  int next = delay_ms * 2;
  return next > RECONNECT_MAX_MS ? RECONNECT_MAX_MS : next;
}

// Sleeps in slices so closing the window does not wait out the whole backoff.
static void backoff_sleep(ClientState *cs, int delay_ms) {
  for (int slept = 0; slept < delay_ms && !should_quit(cs); slept += 50) {
    usleep(50 * 1000);
  }
}

// Terminal: the server rejected the token, or the grace window has certainly
// passed, so there is no seat left to reclaim.
static bool stop_reconnecting(ClientState *cs) {
  pthread_mutex_lock(&cs->lock);
  if (!cs->stopped && cs->view.reconnect_started_ms != 0 &&
      now_ms() - cs->view.reconnect_started_ms > (int64_t)cs->view.grace_secs * 1000) {
    cs->stopped = true;
    cs->view.reconnecting = false;
    cs->view.error_code = ERR_BAD_TOKEN;
    cs->view.error_context = MSG_JOIN_ROOM;
  }
  bool stopped = cs->stopped;
  pthread_mutex_unlock(&cs->lock);
  return stopped;
}

static void mark_connected(ClientState *cs, int fd) {
  pthread_mutex_lock(&cs->lock);
  cs->fd = fd;
  cs->view.connected = true;
  pthread_mutex_unlock(&cs->lock);
}

static void mark_disconnected(ClientState *cs) {
  pthread_mutex_lock(&cs->lock);
  cs->fd = -1;
  cs->view.connected = false;
  cs->view.joined = false;
  cs->view.reconnecting = true;
  if (cs->view.reconnect_started_ms == 0) {
    cs->view.reconnect_started_ms = now_ms();
  }
  pthread_mutex_unlock(&cs->lock);
}

static bool flush_outbox(ClientState *cs, int fd) {
  pthread_mutex_lock(&cs->lock);
  bool move = cs->has_pending_move;
  Move m = cs->pending_move;
  bool resign = cs->pending_resign;
  cs->has_pending_move = false;
  cs->pending_resign = false;
  pthread_mutex_unlock(&cs->lock);

  if (move && !send_move(fd, m)) {
    return false;
  }
  if (resign && !send_resign(fd)) {
    return false;
  }
  return true;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void handle_server_msg(ClientState *cs, int fd, uint8_t msg_type, const uint8_t *msg_buf,
                              uint16_t msg_len) {
  switch (msg_type) {
  case MSG_HELLO_OK: {
    uint16_t version;
    uint16_t grace_secs;
    uint8_t max_spectators;
    if (!decode_hello_ok(msg_buf, msg_len, &version, &max_spectators, &grace_secs)) {
      return;
    }

    // The server rejects a client that is too old; this is the mirror of
    // that check, for a client that is too new.
    if (version != PROTO_VERSION) {
      pthread_mutex_lock(&cs->lock);
      cs->view.error_code = ERR_VERSION_MISMATCH;
      cs->view.error_context = MSG_HELLO_OK;
      pthread_mutex_unlock(&cs->lock);
      return;
    }

    // Copy the plan out so the send happens with the lock released.
    uint8_t token[TOKEN_SIZE];
    char room[ROOM_ID_BUF_LEN];
    char password[PASSWORD_BUF_LEN];

    pthread_mutex_lock(&cs->lock);
    cs->view.grace_secs = grace_secs;
    bool rejoin = cs->has_token;
    bool create = !rejoin && cs->intent == INTENT_CREATE;
    bool spectate = cs->spectate;
    memcpy(token, cs->token, TOKEN_SIZE);
    snprintf(room, sizeof room, "%s", cs->view.room_id[0] != '\0' ? cs->view.room_id : cs->room_id);
    snprintf(password, sizeof password, "%s", cs->password);
    pthread_mutex_unlock(&cs->lock);

    if (rejoin) {
      send_join_room(fd, room, JOIN_HAS_TOKEN, NULL, token);
    } else if (create) {
      send_create_room(fd, ROOM_ALLOW_SPECTATORS, password);
    } else {
      send_join_room(fd, room, spectate ? JOIN_AS_SPECTATOR : 0, password, NULL);
    }
    break;
  }

  case MSG_JOINED: {
    char room_id[ROOM_ID_BUF_LEN];
    uint8_t token[TOKEN_SIZE];
    uint8_t flags;
    Role role;
    if (!decode_joined(msg_buf, msg_len, room_id, &role, token, &flags)) {
      return;
    }

    bool seated = role != ROLE_SPECTATOR;

    pthread_mutex_lock(&cs->lock);
    memcpy(cs->token, token, TOKEN_SIZE);
    cs->has_token = seated;
    snprintf(cs->view.room_id, sizeof cs->view.room_id, "%s", room_id);
    cs->view.spectator = !seated;
    if (seated) {
      cs->view.my_color = ROLE_COLOR(role);
    }
    cs->view.joined = true;
    cs->view.reconnecting = false;
    cs->view.reconnect_started_ms = 0;
    cs->view.error_code = ERR_NONE;

    memset(cs->view.peer_present, 0, sizeof cs->view.peer_present);
    memset(cs->view.peer_vacated_ms, 0, sizeof cs->view.peer_vacated_ms);
    cs->view.spectators = 0;
    pthread_mutex_unlock(&cs->lock);
    break;
  }

  case MSG_STATE: {
    Game game;
    if (!decode_state(msg_buf, msg_len, &game)) {
      return;
    }

    pthread_mutex_lock(&cs->lock);
    cs->view.game = game;
    cs->view.has_state = true;
    cs->view.game_over = false;
    cs->view.error_code = ERR_NONE;
    pthread_mutex_unlock(&cs->lock);
    break;
  }

  case MSG_GAME_OVER: {
    GameResult result;
    if (!decode_game_over(msg_buf, msg_len, &result)) {
      return;
    }

    pthread_mutex_lock(&cs->lock);
    cs->view.result = result;
    cs->view.game_over = true;
    pthread_mutex_unlock(&cs->lock);
    break;
  }

  case MSG_ERROR: {
    uint8_t code;
    uint8_t context;
    if (!decode_error(msg_buf, msg_len, &code, &context)) {
      return;
    }

    pthread_mutex_lock(&cs->lock);
    cs->view.error_code = code;
    cs->view.error_context = context;
    // The seat is gone, so there is nothing left to reclaim.
    if (code == ERR_BAD_TOKEN) {
      cs->has_token = false;
      cs->stopped = true;
      cs->view.reconnecting = false;
    }
    pthread_mutex_unlock(&cs->lock);
    break;
  }

  case MSG_PEER_JOINED:
  case MSG_PEER_REJOINED: {
    Role role;
    if (!decode_peer(msg_buf, msg_len, &role)) {
      return;
    }

    pthread_mutex_lock(&cs->lock);
    if (role == ROLE_SPECTATOR) {
      cs->view.spectators++;
    } else {
      cs->view.peer_present[role] = true;
      cs->view.peer_vacated_ms[role] = 0;
    }
    pthread_mutex_unlock(&cs->lock);
    break;
  }

  case MSG_PEER_LEFT: {
    Role role;
    if (!decode_peer(msg_buf, msg_len, &role)) {
      return;
    }

    pthread_mutex_lock(&cs->lock);
    if (role == ROLE_SPECTATOR) {
      if (cs->view.spectators > 0) {
        cs->view.spectators--;
      }
    } else {
      cs->view.peer_present[role] = false;
      cs->view.peer_vacated_ms[role] = 0;
    }
    pthread_mutex_unlock(&cs->lock);
    break;
  }

  case MSG_PEER_DISCONNECTED: {
    Role role;
    uint16_t grace_secs;
    if (!decode_peer_disconnected(msg_buf, msg_len, &role, &grace_secs)) {
      return;
    }
    if (role == ROLE_SPECTATOR) {
      return;
    }

    pthread_mutex_lock(&cs->lock);
    cs->view.grace_secs = grace_secs;
    cs->view.peer_vacated_ms[role] = now_ms();
    pthread_mutex_unlock(&cs->lock);
    break;
  }

  default:
    break;
  }
}

// Returns when the socket dies or we are told to quit.
static void session(ClientState *cs, int fd) {
  for (;;) {
    if (should_quit(cs)) {
      return;
    }

    struct pollfd p = { .fd = fd, .events = POLLIN };
    int ready = poll(&p, 1, 50);
    if (ready < 0) {
      return;
    }

    if (ready > 0) {
      uint8_t msg_type;
      uint8_t msg_buf[MAX_PAYLOAD];
      uint16_t msg_len;
      if (!receive_msg(fd, &msg_type, msg_buf, &msg_len, sizeof msg_buf)) {
        return;
      }
      handle_server_msg(cs, fd, msg_type, msg_buf, msg_len);
    }
    if (!flush_outbox(cs, fd)) {
      return;
    }
  }
}

void *network_thread(void *arg) {
  ClientState *cs = arg;
  int delay_ms = 0;

  while (!should_quit(cs) && !stop_reconnecting(cs)) {
    backoff_sleep(cs, delay_ms);
    if (should_quit(cs)) {
      break;
    }

    int fd = connect_to(SERVER_HOST, PORT);
    if (fd < 0) {
      delay_ms = next_backoff(delay_ms);
      continue;
    }

    mark_connected(cs, fd);
    send_hello(fd);
    session(cs, fd);

    pthread_mutex_lock(&cs->lock);
    bool was_joined = cs->view.joined;
    pthread_mutex_unlock(&cs->lock);

    close(fd);
    mark_disconnected(cs);

    // A session that got as far as joining earns an immediate retry; one that
    // never did is probably hitting a server that is not there.
    delay_ms = was_joined ? 0 : next_backoff(delay_ms);
  }

  pthread_mutex_lock(&cs->lock);
  cs->fd = -1;
  cs->view.connected = false;
  cs->view.reconnecting = false;
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
  for (; src[i] != '\0' && i < ROOM_ID_LEN; i++) {
    dst[i] = (char)toupper((unsigned char)src[i]);
  }
  dst[i] = '\0';
}

static void parse_args(ClientState *cs, int argc, char **argv) {
  int next = 2;

  if (argc < 2) {
    usage(argv[0]);
  }

  if (strcmp(argv[1], "--create") == 0) {
    cs->intent = INTENT_CREATE;
  } else if (strcmp(argv[1], "--join") == 0) {
    cs->intent = INTENT_JOIN;
    if (argc < 3) {
      usage(argv[0]);
    }
    if (strlen(argv[2]) != ROOM_ID_LEN) {
      usage(argv[0]);
    }
    copy_room_id(cs->room_id, argv[2]);
    next = 3;
  } else {
    usage(argv[0]);
  }

  for (; next < argc; next++) {
    if (strcmp(argv[next], "--spectate") == 0) {
      cs->spectate = true;
    } else if (cs->password[0] == '\0') {
      if (strlen(argv[next]) > MAX_PASSWORD_LEN) {
        usage(argv[0]);
      }
      snprintf(cs->password, sizeof cs->password, "%s", argv[next]);
    } else {
      usage(argv[0]);
    }
  }
}

int main(int argc, char **argv) {
  if (!proto_self_check()) {
    exit(EXIT_FAILURE);
  }
  signal(SIGPIPE, SIG_IGN);

  ClientState cs = { 0 };
  parse_args(&cs, argc, argv);
  pthread_mutex_init(&cs.lock, NULL);

  // The network thread owns the socket, including the first connection.
  cs.fd = -1;

  pthread_t net;
  if (pthread_create(&net, NULL, network_thread, &cs) != 0) {
    perror("pthread_create");
    exit(EXIT_FAILURE);
  }

  run_ui(&cs);

  // The flag stops the reconnect loop; the shutdown unblocks the session so
  // the flag gets looked at.
  pthread_mutex_lock(&cs.lock);
  cs.quit = true;
  int fd = cs.fd;
  pthread_mutex_unlock(&cs.lock);
  if (fd >= 0) {
    shutdown(fd, SHUT_RDWR);
  }

  pthread_join(net, NULL);
  pthread_mutex_destroy(&cs.lock);
  return 0;
}
