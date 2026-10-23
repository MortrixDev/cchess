#include "chess/board.h"
#include "chess/movegen.h"
#include "chess/rules.h"
#include "net/net.h"
#include "proto/proto.h"

#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define MAX_SPECTATORS 10
#define MAX_CLIENTS (2 + MAX_SPECTATORS)

typedef struct {
  int fd;
  Role role;
  bool active;
} Client;

typedef struct {
  int listen_fd;
  Client clients[MAX_CLIENTS];
  Game game;
  bool started;
  bool over;
  GameResult result;
} Server;

static bool has_role(const Server *s, Role role) {
  for (int i = 0; i < MAX_CLIENTS; i++) {
    if (s->clients[i].active && s->clients[i].role == role) {
      return true;
    }
  }
  return false;
}

static int count_role(const Server *s, Role role) {
  int n = 0;
  for (int i = 0; i < MAX_CLIENTS; i++) {
    if (s->clients[i].active && s->clients[i].role == role) {
      n++;
    }
  }
  return n;
}

static const char *role_name(Role role) {
  switch (role) {
    case ROLE_WHITE: return "white";
    case ROLE_BLACK: return "black";
    case ROLE_SPECTATOR: return "spectator";
  }
  return "unknown";
}

static const char *reason_name(GameResult r) {
  switch (r.reason) {
    case REASON_CHECKMATE:             return "checkmate";
    case REASON_STALEMATE:             return "stalemate";
    case REASON_RESIGN:                return "resignation";
    case REASON_DISCONNECT:            return "disconnection";
    case REASON_FIFTY_MOVES:           return "fifty-move rule";
    case REASON_INSUFFICIENT_MATERIAL: return "insufficient material";
  }
  return "an unknown reason";
}

static const char *winner_name(GameResult r) {
  switch (r.result) {
    case RESULT_WHITE_WINS: return "white wins";
    case RESULT_BLACK_WINS: return "black wins";
    case RESULT_DRAW:       return "draw";
  }
  return "game over";
}

static void broadcast(Server *s, uint8_t msg_type, const void *msg_buf, uint16_t msg_len) {
  for (int i = 0; i < MAX_CLIENTS; i++) {
    if (s->clients[i].active) {
      send_msg(s->clients[i].fd, msg_type, msg_buf, msg_len);
    }
  }
}

static void broadcast_state(Server *s) {
  uint8_t buf[MAX_PAYLOAD];
  broadcast(s, MSG_STATE, buf, encode_state(buf, &s->game));
}

static void maybe_start(Server *s) {
  if (s->started || !has_role(s, ROLE_WHITE) || !has_role(s, ROLE_BLACK)) return;
  init_board(&s->game);
  s->started = true;
  broadcast_state(s);
}

static void remove_client(Server *s, int i) {
  close(s->clients[i].fd);
  s->clients[i].active = false;
}

static void accept_client(Server *s) {
  int fd = accept_conn(s->listen_fd);
  if (fd < 0) return;

  Role role;
  if (!has_role(s, ROLE_WHITE))
    role = ROLE_WHITE;
  else if (!has_role(s, ROLE_BLACK))
    role = ROLE_BLACK;
  else if (count_role(s, ROLE_SPECTATOR) < MAX_SPECTATORS){
    role = ROLE_SPECTATOR;
  } else {
    printf("connection rejected: server full\n");
    close(fd);
    return;
  }

  int slot = -1;
  for (int i = 0; i < MAX_CLIENTS && slot < 0; i++) {
    if (!s->clients[i].active) slot = i;
  }
  if (slot < 0) {
    close(fd);
    return;
  }
  s->clients[slot] = (Client){ fd, role, true };
  printf("[%s] connected\n", role_name(role));

  send_join(fd, role);

  if (s->started) {
    send_state(fd, &s->game);
    if (s->over) send_game_over(fd, s->result);
  } else {
    maybe_start(s);
  }
}

enum { NEEDS_PLAYER = 1 << 0, NEEDS_GAME = 1 << 1, NEEDS_LIVE = 1 << 2 };

static const uint8_t msg_requirements[] = {
  [MSG_MOVE]   = NEEDS_PLAYER | NEEDS_GAME | NEEDS_LIVE,
  [MSG_RESIGN] = NEEDS_PLAYER | NEEDS_GAME | NEEDS_LIVE,
};

static uint8_t requirements_for(uint8_t msg_type) {
  if (msg_type >= sizeof msg_requirements) return 0; // unknown: default case ignores it
  return msg_requirements[msg_type];
}

static GameResult reject(Client *c, uint8_t msg_type) {
  send_rejected(c->fd, msg_type);
  return NO_RESULT;
}

static GameResult handle_move(Server *s, Client *c, const uint8_t *msg_buf, uint16_t msg_len) {
  Move m;
  if (!decode_move(msg_buf, msg_len, &m)) return reject(c, MSG_MOVE);

  if (ROLE_COLOR(c->role) != s->game.turn || !is_legal_move(&s->game, m))
    return reject(c, MSG_MOVE);

  apply_move(&s->game, m);
  broadcast_state(s);

  return game_status(&s->game);
}

static GameResult handle_resign(Server *s, Client *c) {
  (void)s;
  return (GameResult){ WIN_FOR(OTHER_COLOR(ROLE_COLOR(c->role))), REASON_RESIGN };
}

static GameResult handle_disconnect(Server *s, int i) {
  Role role = s->clients[i].role;
  remove_client(s, i);
  printf("[%s] disconnected\n", role_name(role));

  if (s->started && role != ROLE_SPECTATOR)
    return (GameResult){ WIN_FOR(OTHER_COLOR(ROLE_COLOR(role))), REASON_DISCONNECT };

  return NO_RESULT;
}

static GameResult handle_msg(Server *s, int i) {
  Client *c = &s->clients[i];
  uint8_t msg_type, msg_buf[MAX_PAYLOAD];
  uint16_t msg_len;

  if (!receive_msg(c->fd, &msg_type, msg_buf, &msg_len, sizeof msg_buf)) {
    return handle_disconnect(s, i);
  }

  uint8_t need = requirements_for(msg_type);
  if (((need & NEEDS_PLAYER) && c->role == ROLE_SPECTATOR) ||
      ((need & NEEDS_GAME) && !s->started) ||
      ((need & NEEDS_LIVE) && s->over)) {
    return reject(c, msg_type);
  }

  switch (msg_type) {
    case MSG_MOVE:   return handle_move(s, c, msg_buf, msg_len);
    case MSG_RESIGN: return handle_resign(s, c);
    default:         return NO_RESULT;
  }
}

// Records the first result the game produces and announces it. Later results
// are ignored: the finished position stands.
static void finish_game(Server *s, GameResult r) {
  if (r.result == RESULT_NONE || s->over) return;

  s->over = true;
  s->result = r;

  uint8_t buf[MAX_PAYLOAD];
  broadcast(s, MSG_GAME_OVER, buf, encode_game_over(buf, r));
  printf("%s by %s\n", winner_name(r), reason_name(r));
}

void run_server(Server *s) {
  for (;;) {
    struct pollfd fds[1 + MAX_CLIENTS];
    int owner[1 + MAX_CLIENTS];
    int n = 0;

    // Build array of active clients.
    // Entry 0 is the listening socket, with owner = -1.
    fds[n] = (struct pollfd){
      .fd = s->listen_fd,
      .events = POLLIN
    };
    owner[n++] = -1;
    for (int i = 0; i < MAX_CLIENTS; i++) {
      if (s->clients[i].active) {
        fds[n] = (struct pollfd){
          .fd = s->clients[i].fd,
          .events = POLLIN
        };
        owner[n++] = i;
      }
    }

    // Block until at least one socket is ready
    if (poll(fds, n, -1) < 0) continue;

    // Handle events
    for (int k = 0; k < n; k++) {
      if (!(fds[k].revents & (POLLIN | POLLHUP | POLLERR))) continue;

      if (owner[k] < 0) accept_client(s);
      else if (s->clients[owner[k]].active) finish_game(s, handle_msg(s, owner[k]));
    }
  }
}

int main(void) {
  Server s = {0};

  // Prevents the program to crash when a player disconnects
  signal(SIGPIPE, SIG_IGN);

  s.listen_fd = listen_on(PORT, MAX_CLIENTS);
  if (s.listen_fd < 0) exit(EXIT_FAILURE);

  printf("Server running...\n");
  run_server(&s);

  close(s.listen_fd);
  return 0;
}
