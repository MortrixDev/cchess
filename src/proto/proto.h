#ifndef PROTO_PROTO_H
#define PROTO_PROTO_H

#include "chess/board.h"

#include <stdbool.h>
#include <stdint.h>

#define PORT 5555

// board + color + castling + en passant square + halfmove + fullmove
#define STATE_SIZE (64 + 1 + 4 + 1 + 1 + 2)

// The largest payload any message carries.
#define MAX_PAYLOAD STATE_SIZE

enum {
  MSG_JOIN,
  MSG_MOVE,
  MSG_STATE,
  MSG_REJECTED,
  MSG_GAME_OVER,
  MSG_RESIGN,
};

typedef enum {
  ROLE_WHITE = 0,
  ROLE_BLACK = 1,
  ROLE_SPECTATOR = 2,
} Role;

#define ROLE_COLOR(c) ((c) == ROLE_WHITE ? COLOR_WHITE : COLOR_BLACK)

const char *msg_type_name(uint8_t msg_type);

bool send_msg(int fd, uint8_t msg_type, const void *msg_buf, uint16_t msg_len);
bool receive_msg(int fd, uint8_t *msg_type, uint8_t *msg_buf, uint16_t *msg_len, uint16_t max_msg_len);

bool send_move(int fd, Move m);
bool send_resign(int fd);

void pack_state(const Game *g, uint8_t *out);
void unpack_state(Game *g, const uint8_t *in);

#endif
