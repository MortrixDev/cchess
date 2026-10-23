#ifndef PROTO_PROTO_H
#define PROTO_PROTO_H

#include "chess/board.h"
#include "chess/rules.h"

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

// Framing. receive_msg fails on a payload larger than max_msg_len, so callers
// pass a MAX_PAYLOAD buffer and let the decoders judge the contents.
bool send_msg(int fd, uint8_t msg_type, const void *msg_buf, uint16_t msg_len);
bool receive_msg(int fd, uint8_t *msg_type, uint8_t *msg_buf, uint16_t *msg_len, uint16_t max_msg_len);

// server -> client
bool send_join(int fd, Role role);
bool send_state(int fd, const Game *g);
bool send_rejected(int fd, uint8_t msg_type);
bool send_game_over(int fd, GameResult r);

// client -> server
bool send_move(int fd, Move m);
bool send_resign(int fd);

// Encode a payload into buf (at least MAX_PAYLOAD bytes) and return its
// length, for messages the server sends to every client at once.
uint16_t encode_state(uint8_t *buf, const Game *g);
uint16_t encode_game_over(uint8_t *buf, GameResult r);

// Decoders check the shape of a payload, not the legality of its contents:
// a move that decodes still has to pass is_legal_move.
bool decode_join(const uint8_t *buf, uint16_t len, Role *role, PieceColor *color);
bool decode_move(const uint8_t *buf, uint16_t len, Move *m);
bool decode_state(const uint8_t *buf, uint16_t len, Game *g);
bool decode_rejected(const uint8_t *buf, uint16_t len, uint8_t *msg_type);
bool decode_game_over(const uint8_t *buf, uint16_t len, GameResult *r);

#endif
