#ifndef PROTO_PROTO_H
#define PROTO_PROTO_H

#include <stdbool.h>
#include <stdint.h>

#include "chess/board.h"
#include "chess/rules.h"

#define PORT 5555

// board + color + castling + en passant square + halfmove + fullmove
#define STATE_SIZE (64 + 1 + 4 + 1 + 1 + 2)

// The largest payload any message carries.
#define MAX_PAYLOAD STATE_SIZE
_Static_assert(MAX_PAYLOAD <= 255, "spec lengths are uint8_t");

#define PROTO_VERSION 1
#define ROOM_ID_LEN 6
#define TOKEN_SIZE 16
#define MAX_PASSWORD_LEN 32

// Payload sizes
#define HELLO_SIZE 2      // u16 version
#define HELLO_OK_SIZE 5   // version + u8 spectators + u16 grace
#define CREATE_ROOM_MIN 2 // flags + pw_len
#define CREATE_ROOM_MAX (CREATE_ROOM_MIN + MAX_PASSWORD_LEN)
#define JOIN_ROOM_MIN (ROOM_ID_LEN + 2) // id + flags + pw_len
#define JOIN_ROOM_MAX (JOIN_ROOM_MIN + MAX_PASSWORD_LEN + TOKEN_SIZE)
#define JOINED_SIZE (ROOM_ID_LEN + 1 + TOKEN_SIZE + 1) // id + role + token + flags
#define PEER_SIZE 1                                    // role
#define PEER_DISC_SIZE 3                               // role + u16 grace
#define MOVE_SIZE 3                                    // from + to + promotion
#define GAME_OVER_SIZE 2                               // result + reason
#define ERROR_SIZE 2                                   // code + context

// Room flags
// Unknown bits are masked off rather than rejected, so a newer client can set one harmlessly.
#define ROOM_ALLOW_SPECTATORS (1u << 0)
#define ROOM_PREFER_SHIFT 1
#define ROOM_PREFER_MASK (3u << ROOM_PREFER_SHIFT)
#define ROOM_FLAG_MASK (ROOM_ALLOW_SPECTATORS | ROOM_PREFER_MASK)
#define ROOM_PREFER(flags) (((flags) & ROOM_PREFER_MASK) >> ROOM_PREFER_SHIFT)

enum {
  PREFER_RANDOM = 0,
  PREFER_WHITE = 1,
  PREFER_BLACK = 2
};

#define ROOM_ID_BUF_LEN (ROOM_ID_LEN + 1)
#define PASSWORD_BUF_LEN (MAX_PASSWORD_LEN + 1)

// JOIN_ROOM flags.
#define JOIN_AS_SPECTATOR (1u << 0)
#define JOIN_HAS_TOKEN (1u << 1)
#define JOIN_FLAG_MASK (JOIN_AS_SPECTATOR | JOIN_HAS_TOKEN)

typedef enum {
  NEEDS_NOTHING = 0, // HELLO itself
  NEEDS_HELLO,       // version negotiated
  NEEDS_ROOM,        // in a room, any role
  NEEDS_SEAT,        // in a room as a player
  NEEDS_LIVE,        // seated, game started and not over
} MsgNeeds;

typedef enum {
  DIR_C2S = 0,
  DIR_S2C = 1
} MsgDir;

typedef struct {
  const char *name;
  uint8_t dir;
  uint8_t min_len;
  uint8_t max_len;
  uint8_t needs;
} MsgSpec;

// NULL for an unknown or retired message type.
const MsgSpec *msg_spec(uint8_t msg_type);
bool proto_self_check(void);

// id, enum name, direction, min, max, requirements
#define MSG_TABLE(X)                                                                   \
  X(1, MSG_HELLO, DIR_C2S, HELLO_SIZE, HELLO_SIZE, NEEDS_NOTHING)                      \
  X(2, MSG_CREATE_ROOM, DIR_C2S, CREATE_ROOM_MIN, CREATE_ROOM_MAX, NEEDS_HELLO)        \
  X(3, MSG_JOIN_ROOM, DIR_C2S, JOIN_ROOM_MIN, JOIN_ROOM_MAX, NEEDS_HELLO)              \
  X(4, MSG_LEAVE_ROOM, DIR_C2S, 0, 0, NEEDS_ROOM)                                      \
  X(5, MSG_MOVE, DIR_C2S, MOVE_SIZE, MOVE_SIZE, NEEDS_LIVE)                            \
  X(6, MSG_RESIGN, DIR_C2S, 0, 0, NEEDS_LIVE)                                          \
  X(7, MSG_HELLO_OK, DIR_S2C, HELLO_OK_SIZE, HELLO_OK_SIZE, NEEDS_NOTHING)             \
  X(8, MSG_JOINED, DIR_S2C, JOINED_SIZE, JOINED_SIZE, NEEDS_NOTHING)                   \
  X(9, MSG_PEER_JOINED, DIR_S2C, PEER_SIZE, PEER_SIZE, NEEDS_NOTHING)                  \
  X(10, MSG_PEER_LEFT, DIR_S2C, PEER_SIZE, PEER_SIZE, NEEDS_NOTHING)                   \
  X(11, MSG_PEER_DISCONNECTED, DIR_S2C, PEER_DISC_SIZE, PEER_DISC_SIZE, NEEDS_NOTHING) \
  X(12, MSG_PEER_REJOINED, DIR_S2C, PEER_SIZE, PEER_SIZE, NEEDS_NOTHING)               \
  X(13, MSG_STATE, DIR_S2C, STATE_SIZE, STATE_SIZE, NEEDS_NOTHING)                     \
  X(14, MSG_GAME_OVER, DIR_S2C, GAME_OVER_SIZE, GAME_OVER_SIZE, NEEDS_NOTHING)         \
  X(15, MSG_ERROR, DIR_S2C, ERROR_SIZE, ERROR_SIZE, NEEDS_NOTHING)

// id, enum name, message shown to user
#define ERROR_TABLE(X)                                       \
  /* framing and handshake */                                \
  X(1, ERR_UNKNOWN_MESSAGE, "unsupported message")           \
  X(2, ERR_MALFORMED, "malformed message")                   \
  X(3, ERR_VERSION_MISMATCH, "client out of date")           \
  X(4, ERR_NEED_HELLO, "handshake not complete")             \
  /* rooms */                                                \
  X(5, ERR_ALREADY_IN_ROOM, "already in a room")             \
  X(6, ERR_NOT_IN_ROOM, "not in a room")                     \
  X(7, ERR_ROOM_NOT_FOUND, "no such room")                   \
  X(8, ERR_BAD_PASSWORD, "wrong password")                   \
  X(9, ERR_BAD_TOKEN, "session expired")                     \
  X(10, ERR_ROOM_FULL, "room is full")                       \
  X(11, ERR_NO_SPECTATORS, "room does not allow spectators") \
  X(12, ERR_TOO_MANY_ROOMS, "server has too many rooms")     \
  X(13, ERR_SERVER_FULL, "server is full")                   \
  X(14, ERR_RATE_LIMITED, "too many attempts, slow down")    \
  /* game */                                                 \
  X(15, ERR_NOT_A_PLAYER, "spectators cannot play")          \
  X(16, ERR_GAME_NOT_STARTED, "game has not started")        \
  X(17, ERR_GAME_FINISHED, "game is over")                   \
  X(18, ERR_NOT_YOUR_TURN, "not your turn")                  \
  X(19, ERR_ILLEGAL_MOVE, "illegal move")                    \
  /* server */                                               \
  X(20, ERR_INTERNAL, "server error")

enum {
#define X(id, name, ...) name = id,
  MSG_TABLE(X)
#undef X
};

typedef enum {
  ERR_NONE = 0,
#define X(id, name, ...) name = id,
  ERROR_TABLE(X)
#undef X
} ErrorCode;

typedef enum {
  ROLE_WHITE = 0,
  ROLE_BLACK = 1,
  ROLE_SPECTATOR = 2,
} Role;

#define ROLE_COLOR(c) ((c) == ROLE_WHITE ? COLOR_WHITE : COLOR_BLACK)

const char *msg_type_name(uint8_t msg_type);

// Both return NULL for an unknown or retired code
const char *error_name(uint8_t code);    // for logs
const char *error_message(uint8_t code); // for the user

// receive_msg fails on a payload larger than max_msg_len
bool send_msg(int fd, uint8_t msg_type, const void *msg_buf, uint16_t msg_len);
bool receive_msg(int fd, uint8_t *msg_type, uint8_t *msg_buf, uint16_t *msg_len,
                 uint16_t max_msg_len);

// server -> client, one recipient
bool send_hello_ok(int fd, uint8_t max_spectators, uint16_t grace_secs);
bool send_joined(int fd, const char *room_id, Role role, const uint8_t *token, uint8_t flags);
bool send_state(int fd, const Game *g);
bool send_game_over(int fd, GameResult r);
bool send_error(int fd, ErrorCode code, uint8_t context);

// client -> server
bool send_hello(int fd);
bool send_create_room(int fd, uint8_t flags, const char *password);
bool send_join_room(int fd, const char *room_id, uint8_t flags, const char *password,
                    const uint8_t *token);
bool send_leave_room(int fd);
bool send_move(int fd, Move m);
bool send_resign(int fd);

// encoders
uint16_t encode_state(uint8_t *buf, const Game *g);
uint16_t encode_game_over(uint8_t *buf, GameResult r);
uint16_t encode_peer(uint8_t *buf, Role role);
uint16_t encode_peer_disconnected(uint8_t *buf, Role role, uint16_t grace_secs);

// decoders
bool decode_hello(const uint8_t *buf, uint16_t len, uint16_t *version);
bool decode_hello_ok(const uint8_t *buf, uint16_t len, uint16_t *version, uint8_t *max_spectators,
                     uint16_t *grace_secs);
bool decode_create_room(const uint8_t *buf, uint16_t len, uint8_t *flags, char *password);
bool decode_join_room(const uint8_t *buf, uint16_t len, char *room_id, uint8_t *flags,
                      char *password, uint8_t *token);
bool decode_joined(const uint8_t *buf, uint16_t len, char *room_id, Role *role, uint8_t *token,
                   uint8_t *flags);
bool decode_move(const uint8_t *buf, uint16_t len, Move *m);
bool decode_state(const uint8_t *buf, uint16_t len, Game *g);
bool decode_game_over(const uint8_t *buf, uint16_t len, GameResult *r);
bool decode_error(const uint8_t *buf, uint16_t len, uint8_t *code, uint8_t *context);
bool decode_peer(const uint8_t *buf, uint16_t len, Role *role);
bool decode_peer_disconnected(const uint8_t *buf, uint16_t len, Role *role, uint16_t *grace_secs);

#endif
