#include "proto/proto.h"
#include "net/net.h"

#include <string.h>

const char *msg_type_name(uint8_t msg_type) {
  static const char *names[] = {
    [MSG_JOIN]      = "JOIN",
    [MSG_MOVE]      = "MOVE",
    [MSG_STATE]     = "STATE",
    [MSG_REJECTED]  = "REJECTED",
    [MSG_GAME_OVER] = "GAME_OVER",
    [MSG_RESIGN]    = "RESIGN",
  };
  if (msg_type >= sizeof names / sizeof names[0] || names[msg_type] == NULL)
    return "UNKNOWN";
  return names[msg_type];
}

bool send_msg(int fd, uint8_t msg_type, const void *msg_buf, uint16_t msg_len) {
  uint8_t header[3] = { msg_type, msg_len >> 8, msg_len & 0xFF };
  return write_full(fd, header, 3) && write_full(fd, msg_buf, msg_len);
}

bool receive_msg(int fd, uint8_t *msg_type, uint8_t *msg_buf, uint16_t *msg_len, uint16_t max_msg_len) {
  uint8_t header[3];
  if (!read_full(fd, header, 3))
    return false;
  *msg_type = header[0];
  *msg_len = (header[1] << 8) | header[2];
  if (*msg_len > max_msg_len)
    return false;
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

bool send_join(int fd, Role role) {
  if (role == ROLE_SPECTATOR) return send_msg(fd, MSG_JOIN, NULL, 0);

  uint8_t color = ROLE_COLOR(role);
  return send_msg(fd, MSG_JOIN, &color, 1);
}

bool send_state(int fd, const Game *g) {
  uint8_t buf[MAX_PAYLOAD];
  return send_msg(fd, MSG_STATE, buf, encode_state(buf, g));
}

bool send_rejected(int fd, uint8_t msg_type) {
  return send_msg(fd, MSG_REJECTED, &msg_type, 1);
}

bool send_game_over(int fd, GameResult r) {
  uint8_t buf[MAX_PAYLOAD];
  return send_msg(fd, MSG_GAME_OVER, buf, encode_game_over(buf, r));
}

bool send_move(int fd, Move m) {
  uint8_t buf[3] = { m.from, m.to, m.promotion };
  return send_msg(fd, MSG_MOVE, buf, sizeof buf);
}

bool send_resign(int fd) {
  return send_msg(fd, MSG_RESIGN, NULL, 0);
}

bool decode_join(const uint8_t *buf, uint16_t len, Role *role, PieceColor *color) {
  if (len == 0) {
    *role = ROLE_SPECTATOR;
    return true;
  }
  if (len != 1) return false;
  if (buf[0] != COLOR_WHITE && buf[0] != COLOR_BLACK) return false;

  *color = buf[0];
  *role = buf[0] == COLOR_WHITE ? ROLE_WHITE : ROLE_BLACK;
  return true;
}

bool decode_move(const uint8_t *buf, uint16_t len, Move *m) {
  if (len != 3) return false;
  if (buf[0] >= 64 || buf[1] >= 64) return false;

  *m = (Move){ buf[0], buf[1], buf[2] };
  return true;
}

bool decode_state(const uint8_t *buf, uint16_t len, Game *g) {
  if (len != STATE_SIZE) return false;

  unpack_state(g, buf);
  return true;
}

bool decode_rejected(const uint8_t *buf, uint16_t len, uint8_t *msg_type) {
  if (len != 1) return false;

  *msg_type = buf[0];
  return true;
}

bool decode_game_over(const uint8_t *buf, uint16_t len, GameResult *r) {
  if (len != 2) return false;

  *r = (GameResult){ buf[0], buf[1] };
  return true;
}
