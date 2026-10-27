#include "proto/proto.h"

#include <stdio.h>
#include <string.h>

#include "net/net.h"
#include "util/util.h"

static const MsgSpec MSG_SPECS[] = {
#define X(id, name, dir, mn, mx, needs) [name] = { #name, dir, mn, mx, needs },
  MSG_TABLE(X)
#undef X
};

const MsgSpec *msg_spec(uint8_t msg_type) {
  if (msg_type >= COUNT(MSG_SPECS)) {
    return NULL;
  }
  const MsgSpec *spec = &MSG_SPECS[msg_type];
  return spec->name ? spec : NULL;
}

// Catches a message added to the table without MAX_PAYLOAD being raised to fit it
bool proto_self_check(void) {
  bool ok = true;
  for (int i = 0; i < COUNT(MSG_SPECS); i++) {
    const MsgSpec *spec = msg_spec((uint8_t)i);
    if (spec == NULL) {
      continue; // unused or retired id
    }

    if (spec->min_len > spec->max_len) {
      fprintf(stderr, "%s: min_len %u exceeds max_len %u\n", spec->name, spec->min_len,
              spec->max_len);
      ok = false;
    }
    if (spec->max_len > MAX_PAYLOAD) {
      fprintf(stderr, "%s: max_len %u exceeds MAX_PAYLOAD %u\n", spec->name, spec->max_len,
              (unsigned)MAX_PAYLOAD);
      ok = false;
    }
    if (spec->dir != DIR_C2S && spec->dir != DIR_S2C) {
      fprintf(stderr, "%s: bad direction %u\n", spec->name, spec->dir);
      ok = false;
    }
    if (spec->needs > NEEDS_LIVE) {
      fprintf(stderr, "%s: bad needs level %u\n", spec->name, spec->needs);
      ok = false;
    }
  }
  return ok;
}

const char *msg_type_name(uint8_t msg_type) {
  switch (msg_type) {
#define X(id, name, ...) \
  case name:             \
    return #name;
    MSG_TABLE(X)
#undef X
  }
  return "unknown";
}

const char *error_name(uint8_t code) {
  switch (code) {
#define X(id, name, ...) \
  case name:             \
    return #name;
    ERROR_TABLE(X)
#undef X
  }
  return NULL;
}

const char *error_message(uint8_t code) {
  switch (code) {
#define X(id, name, msg) \
  case name:             \
    return msg;
    ERROR_TABLE(X)
#undef X
  }
  return NULL;
}

bool send_msg(int fd, uint8_t msg_type, const void *msg_buf, uint16_t msg_len) {
  uint8_t header[3] = { msg_type, msg_len >> 8, msg_len & 0xFF };
  return write_full(fd, header, 3) && write_full(fd, msg_buf, msg_len);
}

bool receive_msg(int fd, uint8_t *msg_type, uint8_t *msg_buf, uint16_t *msg_len,
                 uint16_t max_msg_len) {
  uint8_t header[3];
  if (!read_full(fd, header, 3)) {
    return false;
  }
  *msg_type = header[0];
  *msg_len = (header[1] << 8) | header[2];
  if (*msg_len > max_msg_len) {
    return false;
  }
  return read_full(fd, msg_buf, *msg_len);
}

static void pack_state(const Game *g, uint8_t *out) {
  memcpy(out, g->board, 64);
  out[64] = g->turn;
  out[65] = g->can_castle[0][0];
  out[66] = g->can_castle[0][1];
  out[67] = g->can_castle[1][0];
  out[68] = g->can_castle[1][1];
  out[69] = g->en_passant_square;
  out[70] = g->halfmove_clock;
  out[71] = g->fullmove_number >> 8;
  out[72] = g->fullmove_number & 0xFF;
}

static void unpack_state(Game *g, const uint8_t *in) {
  memcpy(g->board, in, 64);
  g->turn = in[64];
  g->can_castle[0][0] = in[65];
  g->can_castle[0][1] = in[66];
  g->can_castle[1][0] = in[67];
  g->can_castle[1][1] = in[68];
  g->en_passant_square = in[69];
  g->halfmove_clock = in[70];
  g->fullmove_number = (in[71] << 8) | in[72];
}

uint16_t encode_state(uint8_t *buf, const Game *g) {
  pack_state(g, buf);
  return STATE_SIZE;
}

uint16_t encode_game_over(uint8_t *buf, GameResult r) {
  buf[0] = r.result;
  buf[1] = r.reason;
  return 2;
}

static bool valid_room_id(const uint8_t *in) {
  for (int i = 0; i < ROOM_ID_LEN; i++) {
    bool ok = (in[i] >= 'A' && in[i] <= 'Z') || (in[i] >= '0' && in[i] <= '9');
    if (!ok) {
      return false;
    }
  }
  return true;
}

static bool valid_password(const uint8_t *in, uint8_t len) {
  for (uint8_t i = 0; i < len; i++) {
    if (in[i] < 0x20 || in[i] > 0x7E) {
      return false;
    }
  }
  return true;
}

static bool valid_role(uint8_t role) {
  return role == ROLE_WHITE || role == ROLE_BLACK || role == ROLE_SPECTATOR;
}

static void put_u16(uint8_t *buf, uint16_t v) {
  buf[0] = v >> 8;
  buf[1] = v & 0xFF;
}

static uint16_t get_u16(const uint8_t *buf) {
  return (uint16_t)((buf[0] << 8) | buf[1]);
}

bool send_hello(int fd) {
  uint8_t buf[HELLO_SIZE];
  put_u16(buf, PROTO_VERSION);
  return send_msg(fd, MSG_HELLO, buf, sizeof buf);
}

bool send_hello_ok(int fd, uint8_t max_spectators, uint16_t grace_secs) {
  uint8_t buf[HELLO_OK_SIZE];
  put_u16(buf, PROTO_VERSION);
  buf[2] = max_spectators;
  put_u16(buf + 3, grace_secs);
  return send_msg(fd, MSG_HELLO_OK, buf, sizeof buf);
}

bool send_create_room(int fd, uint8_t flags, const char *password) {
  uint8_t buf[CREATE_ROOM_MAX];
  size_t password_len = password ? strlen(password) : 0;
  if (password_len > MAX_PASSWORD_LEN) {
    return false;
  }

  buf[0] = flags & ROOM_FLAG_MASK;
  buf[1] = (uint8_t)password_len;
  memcpy(buf + 2, password, password_len);
  return send_msg(fd, MSG_CREATE_ROOM, buf, (uint16_t)(2 + password_len));
}

bool send_join_room(int fd, const char *room_id, uint8_t flags, const char *password,
                    const uint8_t *token) {
  uint8_t buf[JOIN_ROOM_MAX];
  size_t password_len = password ? strlen(password) : 0;
  if (strlen(room_id) != ROOM_ID_LEN || password_len > MAX_PASSWORD_LEN) {
    return false;
  }
  if ((flags & JOIN_HAS_TOKEN) && token == NULL) {
    return false;
  }

  memcpy(buf, room_id, ROOM_ID_LEN);
  buf[ROOM_ID_LEN] = flags & JOIN_FLAG_MASK;
  buf[ROOM_ID_LEN + 1] = (uint8_t)password_len;

  uint16_t n = JOIN_ROOM_MIN + (uint16_t)password_len;
  memcpy(buf + JOIN_ROOM_MIN, password, password_len);
  if (flags & JOIN_HAS_TOKEN) {
    memcpy(buf + n, token, TOKEN_SIZE);
    n += TOKEN_SIZE;
  }
  return send_msg(fd, MSG_JOIN_ROOM, buf, n);
}

bool send_leave_room(int fd) {
  return send_msg(fd, MSG_LEAVE_ROOM, NULL, 0);
}

bool send_joined(int fd, const char *room_id, Role role, const uint8_t *token, uint8_t flags) {
  uint8_t buf[JOINED_SIZE];
  memcpy(buf, room_id, ROOM_ID_LEN);
  buf[ROOM_ID_LEN] = role;
  memcpy(buf + ROOM_ID_LEN + 1, token, TOKEN_SIZE);
  buf[ROOM_ID_LEN + 1 + TOKEN_SIZE] = flags & ROOM_FLAG_MASK;
  return send_msg(fd, MSG_JOINED, buf, sizeof buf);
}

bool send_state(int fd, const Game *g) {
  uint8_t buf[MAX_PAYLOAD];
  return send_msg(fd, MSG_STATE, buf, encode_state(buf, g));
}

bool send_game_over(int fd, GameResult r) {
  uint8_t buf[MAX_PAYLOAD];
  return send_msg(fd, MSG_GAME_OVER, buf, encode_game_over(buf, r));
}

bool send_error(int fd, ErrorCode code, uint8_t context) {
  uint8_t buf[ERROR_SIZE] = { (uint8_t)code, context };
  return send_msg(fd, MSG_ERROR, buf, sizeof buf);
}

bool send_move(int fd, Move move) {
  uint8_t buf[MOVE_SIZE] = { move.from, move.to, move.promotion };
  return send_msg(fd, MSG_MOVE, buf, sizeof buf);
}

bool send_resign(int fd) {
  return send_msg(fd, MSG_RESIGN, NULL, 0);
}

uint16_t encode_peer(uint8_t *buf, Role role) {
  buf[0] = role;
  return PEER_SIZE;
}

uint16_t encode_peer_disconnected(uint8_t *buf, Role role, uint16_t grace_secs) {
  buf[0] = role;
  put_u16(buf + 1, grace_secs);
  return PEER_DISC_SIZE;
}

bool decode_hello(const uint8_t *buf, uint16_t len, uint16_t *version) {
  if (len != HELLO_SIZE) {
    return false;
  }

  *version = get_u16(buf);
  return true;
}

bool decode_hello_ok(const uint8_t *buf, uint16_t len, uint16_t *version, uint8_t *max_spectators,
                     uint16_t *grace_secs) {
  if (len != HELLO_OK_SIZE) {
    return false;
  }

  *version = get_u16(buf);
  *max_spectators = buf[2];
  *grace_secs = get_u16(buf + 3);
  return true;
}

bool decode_create_room(const uint8_t *buf, uint16_t len, uint8_t *flags, char *password) {
  if (len < CREATE_ROOM_MIN || len > CREATE_ROOM_MAX) {
    return false;
  }

  uint8_t pw_len = buf[1];
  if (len != CREATE_ROOM_MIN + pw_len) {
    return false;
  }
  if (!valid_password(buf + 2, pw_len)) {
    return false;
  }

  *flags = buf[0] & ROOM_FLAG_MASK;
  if (ROOM_PREFER(*flags) > PREFER_BLACK) {
    return false;
  }
  memcpy(password, buf + 2, pw_len);
  password[pw_len] = '\0';
  return true;
}

bool decode_join_room(const uint8_t *buf, uint16_t len, char *room_id, uint8_t *flags,
                      char *password, uint8_t *token) {
  if (len < JOIN_ROOM_MIN || len > JOIN_ROOM_MAX) {
    return false;
  }
  if (!valid_room_id(buf)) {
    return false;
  }

  uint8_t f = buf[ROOM_ID_LEN] & JOIN_FLAG_MASK;
  uint8_t pw_len = buf[ROOM_ID_LEN + 1];
  uint16_t want = JOIN_ROOM_MIN + pw_len + ((f & JOIN_HAS_TOKEN) ? TOKEN_SIZE : 0);
  if (len != want) {
    return false;
  }
  if (!valid_password(buf + JOIN_ROOM_MIN, pw_len)) {
    return false;
  }

  memcpy(room_id, buf, ROOM_ID_LEN);
  room_id[ROOM_ID_LEN] = '\0';
  *flags = f;
  memcpy(password, buf + JOIN_ROOM_MIN, pw_len);
  password[pw_len] = '\0';
  if (f & JOIN_HAS_TOKEN) {
    memcpy(token, buf + JOIN_ROOM_MIN + pw_len, TOKEN_SIZE);
  }
  return true;
}

bool decode_joined(const uint8_t *buf, uint16_t len, char *room_id, Role *role, uint8_t *token,
                   uint8_t *flags) {
  if (len != JOINED_SIZE) {
    return false;
  }
  if (!valid_role(buf[ROOM_ID_LEN])) {
    return false;
  }

  memcpy(room_id, buf, ROOM_ID_LEN);
  room_id[ROOM_ID_LEN] = '\0';
  *role = buf[ROOM_ID_LEN];
  memcpy(token, buf + ROOM_ID_LEN + 1, TOKEN_SIZE);
  *flags = buf[ROOM_ID_LEN + 1 + TOKEN_SIZE] & ROOM_FLAG_MASK;
  return true;
}

bool decode_error(const uint8_t *buf, uint16_t len, uint8_t *code, uint8_t *context) {
  if (len != ERROR_SIZE) {
    return false;
  }

  *code = buf[0];
  *context = buf[1];
  return true;
}

bool decode_peer(const uint8_t *buf, uint16_t len, Role *role) {
  if (len != PEER_SIZE) {
    return false;
  }
  if (!valid_role(buf[0])) {
    return false;
  }

  *role = buf[0];
  return true;
}

bool decode_peer_disconnected(const uint8_t *buf, uint16_t len, Role *role, uint16_t *grace_secs) {
  if (len != PEER_DISC_SIZE) {
    return false;
  }
  if (!valid_role(buf[0])) {
    return false;
  }

  *role = buf[0];
  *grace_secs = get_u16(buf + 1);
  return true;
}

bool decode_move(const uint8_t *buf, uint16_t len, Move *m) {
  if (len != 3) {
    return false;
  }
  if (buf[0] >= 64 || buf[1] >= 64) {
    return false;
  }

  *m = (Move){ buf[0], buf[1], buf[2] };
  return true;
}

static bool valid_game(const Game *g) {
  for (int sq = 0; sq < 64; sq++) {
    Piece p = g->board[sq];
    if (p & ~(TYPE_MASK | COLOR_MASK)) {
      return false; // no bits outside type+color
    }
    if (TYPE(p) > KING) {
      return false; // 7 and 15 are unused
    }
  }
  if (g->turn != COLOR_WHITE && g->turn != COLOR_BLACK) {
    return false;
  }
  if (g->en_passant_square != NO_SQUARE && g->en_passant_square >= 64) {
    return false;
  }

  return true;
}

bool decode_state(const uint8_t *buf, uint16_t len, Game *g) {
  if (len != STATE_SIZE) {
    return false;
  }

  Game decoded;
  unpack_state(&decoded, buf);
  if (!valid_game(&decoded)) {
    return false;
  }

  *g = decoded;
  return true;
}

bool decode_game_over(const uint8_t *buf, uint16_t len, GameResult *r) {
  if (len != 2) {
    return false;
  }

  *r = (GameResult){ buf[0], buf[1] };
  return true;
}
