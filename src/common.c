#include "common.h"

#include <errno.h>
#include <string.h>
#include <unistd.h>

static bool read_full(int fd, void *buf, size_t n) {
  uint8_t *p = buf;
  while (n > 0) {
    ssize_t r = read(fd, p, n);
    if (r < 0 && errno == EINTR)
        continue;
    if (r <= 0)
        return false;
    p += r;
    n -= r;
  }
  return true;
}

static bool write_full(int fd, const void *buf, size_t n) {
  const uint8_t *p = buf;
  while (n > 0) {
    ssize_t w = write(fd, p, n);
    if (w < 0 && errno == EINTR)
        continue;
    if (w <= 0)
        return false;
    p += w;
    n -= w;
  }
  return true;
}

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

void pack_state(const Game *g, uint8_t *out) {
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

void unpack_state(Game *g, const uint8_t *in) {
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
