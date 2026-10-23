#include "common.h"
#include "rules.h"
 
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <signal.h>
#include <poll.h>
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

static void init_game(Game *g) {
  static const Piece back_rank[8] = { ROOK, KNIGHT, BISHOP, QUEEN, KING, BISHOP, KNIGHT, ROOK };
  *g = (Game){0};

  for (int file = 0; file < 8; file++) {
    g->board[file] = back_rank[file] | COLOR_WHITE;
    g->board[8 + file] = PAWN | COLOR_WHITE;
    g->board[48 + file] = PAWN | COLOR_BLACK;
    g->board[56 + file] = back_rank[file] | COLOR_BLACK;
  }

  g->turn = COLOR_WHITE;
  g->can_castle[0][0] = true;
  g->can_castle[1][0] = true;
  g->can_castle[0][1] = true;
  g->can_castle[1][1] = true;
  g->en_passant_square = NO_SQUARE;
}

static void broadcast(Server *s, uint8_t msg_type, const void *msg_buf, uint16_t msg_len) {
  for (int i = 0; i < MAX_CLIENTS; i++) {
    if (s->clients[i].active) {
      send_msg(s->clients[i].fd, msg_type, msg_buf, msg_len);
    }
  }
}

static void send_state(int fd, const Game *g) {
  uint8_t state[STATE_SIZE];
  pack_state(g, state);
  send_msg(fd, MSG_STATE, state, STATE_SIZE);
}

static void broadcast_state(Server *s) {
  uint8_t state[STATE_SIZE];
  pack_state(&s->game, state);
  broadcast(s, MSG_STATE, state, STATE_SIZE);
}

static void maybe_start(Server *s) {
  if (s->started || !has_role(s, ROLE_WHITE) || !has_role(s, ROLE_BLACK)) return;
  init_game(&s->game);
  s->started = true;
  broadcast_state(s);
}

static void remove_client(Server *s, int i) {
  close(s->clients[i].fd);
  s->clients[i].active = false;
}

static void accept_client(Server *s) {
  int fd = accept(s->listen_fd, NULL, NULL);
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

  if (role == ROLE_SPECTATOR) {
    send_msg(fd, MSG_JOIN, NULL, 0);
  } else {
    PieceColor color = ROLE_COLOR(role);
    send_msg(fd, MSG_JOIN, &color, 1);
  }

  if (s->started) {
    send_state(fd, &s->game);
    if (s->over) {
      uint8_t msg_buf[2] = { s->result.result, s->result.reason };
      send_msg(fd, MSG_GAME_OVER, msg_buf, 2);
    }
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

static GameResult send_reject(Client *c, uint8_t msg_type) {
  send_msg(c->fd, MSG_REJECTED, &msg_type, 1);
  return NO_RESULT;
}

static GameResult handle_move(Server *s, Client *c, const uint8_t *msg_buf, uint16_t msg_len) {
  if (msg_len != 3) return send_reject(c, MSG_MOVE);

  Move m = { msg_buf[0], msg_buf[1], msg_buf[2] };
  if (ROLE_COLOR(c->role) != s->game.turn || !is_legal_move(&s->game, m))
    return send_reject(c, MSG_MOVE);

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
  uint8_t msg_type, msg_buf[16];
  uint16_t msg_len;

  if (!receive_msg(c->fd, &msg_type, msg_buf, &msg_len, sizeof msg_buf)) {
    return handle_disconnect(s, i);
  }

  uint8_t need = requirements_for(msg_type);
  if (((need & NEEDS_PLAYER) && c->role == ROLE_SPECTATOR) ||
      ((need & NEEDS_GAME) && !s->started) ||
      ((need & NEEDS_LIVE) && s->over)) {
    return send_reject(c, msg_type);
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

  uint8_t msg_buf[2] = { r.result, r.reason };
  broadcast(s, MSG_GAME_OVER, msg_buf, 2);
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
  int opt = 1;
  struct sockaddr_in listen_address = {0};

  // Prevents the program to crash when a player disconnects
  signal(SIGPIPE, SIG_IGN);

  if ((s.listen_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    perror("socket failed");
    exit(EXIT_FAILURE);
  }

  if (setsockopt(s.listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
    perror("setsockopt");
    exit(EXIT_FAILURE);
  }

  listen_address.sin_family = AF_INET;
  listen_address.sin_addr.s_addr = INADDR_ANY;
  listen_address.sin_port = htons(PORT);

  if (bind(s.listen_fd, (struct sockaddr *)&listen_address, sizeof(listen_address)) < 0) {
    perror("bind failed");
    exit(EXIT_FAILURE);
  }

  if (listen(s.listen_fd, MAX_CLIENTS) < 0) {
    perror("listen");
    exit(EXIT_FAILURE);
  }

  printf("Server running...\n");
  run_server(&s);

  close(s.listen_fd);
  return 0;
}
