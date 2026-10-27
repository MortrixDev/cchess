#include "server/server.h"

#include "chess/board.h"
#include "chess/movegen.h"
#include "chess/rules.h"
#include "net/net.h"
#include "proto/proto.h"
#include "server/room.h"

#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

static const uint8_t NO_TOKEN[TOKEN_SIZE] = {0};

static Room *client_room(Server *s, const Client *c) {
  return &s->room_table.rooms[c->room];
}

static int room_index(const Server *s, const Room *r) {
  return (int)(r - s->room_table.rooms);
}

static bool room_empty(const Server *s, int room) {
  for (int i = 0; i < MAX_CLIENTS; i++) {
    if (s->clients[i].active && s->clients[i].room == room) return false;
  }
  return true;
}

static bool has_role(const Server *s, int room, Role role) {
  for (int i = 0; i < MAX_CLIENTS; i++) {
    if (s->clients[i].active && s->clients[i].room == room && s->clients[i].role == role) {
      return true;
    }
  }
  return false;
}

static int count_role(const Server *s, int room, Role role) {
  int n = 0;
  for (int i = 0; i < MAX_CLIENTS; i++) {
    if (s->clients[i].active && s->clients[i].room == room && s->clients[i].role == role) {
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

static GameResult fail(Client *c, ErrorCode code, uint8_t context) {
  send_error(c->fd, code, context);
  return NO_RESULT;
}

static void seat_creator(Client *c, const Room *r, int room) {
  int prefer = ROOM_PREFER(r->flags);
  if (prefer == PREFER_RANDOM) prefer = room_coin_flip() ? PREFER_WHITE : PREFER_BLACK;

  c->role = prefer == PREFER_BLACK ? ROLE_BLACK : ROLE_WHITE;
  c->reached = NEEDS_SEAT;
  c->room = room;
}

static ErrorCode seat_joiner(Server *s, Client *c, const Room *r, int room, bool as_spectator) {
  bool white_free = !has_role(s, room, ROLE_WHITE);
  bool black_free = !has_role(s, room, ROLE_BLACK);

  if (!as_spectator && (white_free || black_free)) {
    c->role = white_free ? ROLE_WHITE : ROLE_BLACK;
    c->reached = NEEDS_SEAT;
  } else {
    if (!(r->flags & ROOM_ALLOW_SPECTATORS))
      return (white_free || black_free) ? ERR_NO_SPECTATORS : ERR_ROOM_FULL;
    if (count_role(s, room, ROLE_SPECTATOR) >= MAX_SPECTATORS) return ERR_ROOM_FULL;

    c->role = ROLE_SPECTATOR;
    c->reached = NEEDS_ROOM;
  }
  c->room = room;
  return ERR_NONE;
}

static void broadcast(Server *s, int room, uint8_t msg_type, const void *msg_buf, uint16_t msg_len) {
  for (int i = 0; i < MAX_CLIENTS; i++) {
    if (s->clients[i].active && s->clients[i].room == room) {
      send_msg(s->clients[i].fd, msg_type, msg_buf, msg_len);
    }
  }
}

static void broadcast_except(Server *s, int room, const Client *skip,
                             uint8_t msg_type, const void *msg_buf, uint16_t msg_len) {
  for (int i = 0; i < MAX_CLIENTS; i++) {
    Client *c = &s->clients[i];
    if (c->active && c->room == room && c != skip)
      send_msg(c->fd, msg_type, msg_buf, msg_len);
  }
}

static void broadcast_state(Server *s, int room) {
  uint8_t buf[MAX_PAYLOAD];
  broadcast(s, room, MSG_STATE, buf, encode_state(buf, &s->room_table.rooms[room].game));
}

static void maybe_start(Server *s, int room) {
  if (!has_role(s, room, ROLE_WHITE) || !has_role(s, room, ROLE_BLACK)) return;
  if (!room_start(&s->room_table.rooms[room])) return;
  broadcast_state(s, room);
}


static GameResult handle_create_room(Server *s, Client *c, const uint8_t *msg_buf, uint16_t msg_len) {
  if (c->room >= 0) return fail(c, ERR_ALREADY_IN_ROOM, MSG_CREATE_ROOM);

  uint8_t flags;
  char password[PASSWORD_BUF_LEN];
  if (!decode_create_room(msg_buf, msg_len, &flags, password))
    return fail(c, ERR_MALFORMED, MSG_CREATE_ROOM);

  Room *r = room_create(&s->room_table, password, flags);
  if (r == NULL) return fail(c, ERR_TOO_MANY_ROOMS, MSG_CREATE_ROOM);

  seat_creator(c, r, room_index(s, r));
  send_joined(c->fd, r->id, c->role, NO_TOKEN, r->flags);
  printf("[%s] created, %s\n", r->id, role_name(c->role));
  return NO_RESULT;
}

static GameResult handle_join_room(Server *s, Client *c, const uint8_t *msg_buf, uint16_t msg_len) {
  if (c->room >= 0)  return fail(c, ERR_ALREADY_IN_ROOM, MSG_JOIN_ROOM);

  char id[ROOM_ID_BUF_LEN], password[PASSWORD_BUF_LEN];
  uint8_t flags, token[TOKEN_SIZE];
  if (!decode_join_room(msg_buf, msg_len, id, &flags, password, token))
    return fail(c, ERR_MALFORMED, MSG_JOIN_ROOM);

  Room *r = room_find(&s->room_table, id);
  if (r == NULL) return fail(c, ERR_ROOM_NOT_FOUND, MSG_JOIN_ROOM);
  if (!room_password_ok(r, password)) return fail(c, ERR_BAD_PASSWORD, MSG_JOIN_ROOM);

  int room = room_index(s, r);
  ErrorCode err = seat_joiner(s, c, r, room, flags & JOIN_AS_SPECTATOR);
  if (err != ERR_NONE) return fail(c, err, MSG_JOIN_ROOM);

  send_joined(c->fd, r->id, c->role, NO_TOKEN, r->flags);

  uint8_t buf[MAX_PAYLOAD];
  for (int i = 0; i < MAX_CLIENTS; i++) {
    Client *other = &s->clients[i];
    if (other->active && other->room == room && other != c)
      send_msg(c->fd, MSG_PEER_JOINED, buf, encode_peer(buf, other->role));
  }
  broadcast_except(s, room, c, MSG_PEER_JOINED, buf, encode_peer(buf, c->role));

  if (r->started) {
    send_state(c->fd, &r->game);
    if (r->over) send_game_over(c->fd, r->result);
  } else {
    maybe_start(s, room);
  }
  printf("[%s] %s joined\n", r->id, role_name(c->role));
  return NO_RESULT;
}

static GameResult leave_room(Server *s, Client *c) {
  int room = c->room;
  Room *r = &s->room_table.rooms[room];
  Role role = c->role;
  bool was_player = role != ROLE_SPECTATOR;
  bool live = r->started && !r->over;

  c->room = -1;
  c->reached = NEEDS_HELLO;

  uint8_t buf[MAX_PAYLOAD];
  broadcast(s, room, MSG_PEER_LEFT, buf, encode_peer(buf, role));

  if (room_empty(s, room)) {
    printf("[%s] closed\n", r->id);
    room_free(r);
    return NO_RESULT;
  }

  if (live && was_player)
    return (GameResult){ WIN_FOR(OTHER_COLOR(ROLE_COLOR(role))), REASON_DISCONNECT };

  return NO_RESULT;
}

static void remove_client(Server *s, int i) {
  close(s->clients[i].fd);
  s->clients[i].active = false;
}

static void accept_client(Server *s) {
  int fd = accept_conn(s->listen_fd);
  if (fd < 0) return;

  int slot = -1;
  for (int i = 0; i < MAX_CLIENTS && slot < 0; i++) {
    if (!s->clients[i].active) slot = i;
  }
  if (slot < 0) {
    send_error(fd, ERR_SERVER_FULL, 0);
    close(fd);
    return;
  }

  s->clients[slot] = (Client){
    .fd = fd,
    .active = true,
    .reached = NEEDS_NOTHING,
    .room = -1
  };
  printf("connected\n");
}

static ErrorCode check_msg(const Server *s, const Client *c, const MsgSpec *spec, uint16_t len) {
  if (spec == NULL || spec->dir != DIR_C2S) return ERR_UNKNOWN_MESSAGE;
  if (len < spec->min_len || len > spec->max_len) return ERR_MALFORMED;

  // NEEDS_LIVE is about the game, so a connection can climb no higher than NEEDS_SEAT
  MsgNeeds conn_level = spec->needs > NEEDS_SEAT ? NEEDS_SEAT : spec->needs;
  if (conn_level > c->reached) {
    switch (c->reached) {
      case NEEDS_NOTHING: return ERR_NEED_HELLO;
      case NEEDS_HELLO:  return ERR_NOT_IN_ROOM;
      default:          return ERR_NOT_A_PLAYER;
    }
  }

  if (spec->needs == NEEDS_LIVE) {
    const Room *r = &s->room_table.rooms[c->room];
    if (!r->started) return ERR_GAME_NOT_STARTED;
    if (r->over)     return ERR_GAME_FINISHED;
  }

  return ERR_NONE;
}

static GameResult handle_hello(Server *s, Client *c, const uint8_t *msg_buf, uint16_t msg_len) {
  uint16_t version;
  if (!decode_hello(msg_buf, msg_len, &version)) return fail(c, ERR_MALFORMED, MSG_HELLO);
  if (version != PROTO_VERSION) return fail(c, ERR_VERSION_MISMATCH, MSG_HELLO);

  c->reached = NEEDS_HELLO;
  send_hello_ok(c->fd, MAX_SPECTATORS, 0);
  return NO_RESULT;
}

static GameResult handle_move(Server *s, Client *c, const uint8_t *msg_buf, uint16_t msg_len) {
  Move move;
  if (!decode_move(msg_buf, msg_len, &move)) return fail(c, ERR_MALFORMED, MSG_MOVE);

  Room *r = client_room(s, c);
  if (ROLE_COLOR(c->role) != r->game.turn)   return fail(c, ERR_NOT_YOUR_TURN, MSG_MOVE);
  if (!is_legal_move(&r->game, move))        return fail(c, ERR_ILLEGAL_MOVE, MSG_MOVE);

  apply_move(&r->game, move);
  broadcast_state(s, c->room);
  return game_status(&r->game);
}

static GameResult handle_resign(Server *s, Client *c) {
  (void)s;
  return (GameResult){ WIN_FOR(OTHER_COLOR(ROLE_COLOR(c->role))), REASON_RESIGN };
}

static GameResult handle_disconnect(Server *s, int i) {
  Client *c = &s->clients[i];
  GameResult result = (c->room >= 0) ? leave_room(s, c) : NO_RESULT;

  remove_client(s, i);
  printf("disconnected\n");
  return result;
}

static GameResult handle_msg(Server *s, int i) {
  Client *c = &s->clients[i];
  uint8_t msg_type, msg_buf[MAX_PAYLOAD];
  uint16_t msg_len;

  if (!receive_msg(c->fd, &msg_type, msg_buf, &msg_len, sizeof msg_buf)) {
    return handle_disconnect(s, i);
  }

  const MsgSpec *spec = msg_spec(msg_type);
  ErrorCode err = check_msg(s, c, spec, msg_len);
  if (err != ERR_NONE) return fail(c, err, msg_type);
  
  switch (msg_type) {
    case MSG_HELLO:  return handle_hello(s, c, msg_buf, msg_len);
    case MSG_MOVE:   return handle_move(s, c, msg_buf, msg_len);
    case MSG_RESIGN: return handle_resign(s, c);
    case MSG_CREATE_ROOM: return handle_create_room(s, c, msg_buf, msg_len);
    case MSG_JOIN_ROOM:   return handle_join_room(s, c, msg_buf, msg_len);
    case MSG_LEAVE_ROOM:  return leave_room(s, c);
    default:         return fail(c, ERR_UNKNOWN_MESSAGE, msg_type);
  }
}

// Records the first result the game produces and announces it. Later results
// are ignored: the finished position stands.
static void finish_game(Server *s, int room, GameResult result) {
  Room *r = &s->room_table.rooms[room];
  if (!room_finish(r, result)) return;

  uint8_t buf[MAX_PAYLOAD];
  broadcast(s, room, MSG_GAME_OVER, buf, encode_game_over(buf, result));
  printf("[%s] %s by %s\n", r->id, winner_name(result), reason_name(result));
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
      else if (s->clients[owner[k]].active) {
        int i = owner[k];
        int room = s->clients[i].room;
        GameResult result = handle_msg(s, i);
        if (room >= 0) finish_game(s, room, result); 
      }
    }
  }
}
