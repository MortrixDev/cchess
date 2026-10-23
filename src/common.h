#ifndef COMMON_H
#define COMMON_H

#include <stdbool.h>
#include <stdint.h>

typedef uint8_t Piece;
typedef uint8_t PieceColor;

enum { EMPTY = 0, PAWN = 1, KNIGHT, BISHOP, ROOK, QUEEN, KING };
enum { COLOR_WHITE = 0, COLOR_BLACK = 8 };

#define TYPE_MASK  0b0111
#define COLOR_MASK 0b1000

#define TYPE(p)        ((p) & TYPE_MASK)
#define COLOR(p)       ((p) & COLOR_MASK)
#define IS_EMPTY(p)    ((p) == EMPTY)
#define IS_COLOR(p, c) (!IS_EMPTY(p) && COLOR(p) == (c))
#define OTHER_COLOR(c) ((c) ^ COLOR_BLACK)
#define COLOR_INDEX(c) ((c) >> 3)

// board + color + castling + en passant square + halfmove + fullmove
#define STATE_SIZE (64 + 1 + 4 + 1 + 1 + 2)

#define PORT       5555
#define NO_SQUARE  255

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

enum {
  RESULT_NONE,
  RESULT_WHITE_WINS,
  RESULT_BLACK_WINS,
  RESULT_DRAW,
};
 
enum {
  REASON_NONE,
  REASON_CHECKMATE,
  REASON_STALEMATE,
  REASON_RESIGN,
  REASON_DISCONNECT,
  REASON_FIFTY_MOVES,
  REASON_INSUFFICIENT_MATERIAL,
};

typedef struct {
  uint8_t result;
  uint8_t reason;
} GameResult;

#define NO_RESULT ((GameResult){ RESULT_NONE, REASON_NONE })
#define WIN_FOR(c) ((c) == COLOR_WHITE ? RESULT_WHITE_WINS : RESULT_BLACK_WINS)

typedef struct {
  Piece board[64];
  PieceColor turn;
  bool can_castle[2][2];
  uint8_t en_passant_square;
  uint8_t halfmove_clock;
  uint16_t fullmove_number;
} Game;

typedef struct {
  uint8_t from;
  uint8_t to;
  uint8_t promotion;
} Move;

const char *msg_type_name(uint8_t msg_type);

bool send_msg(int fd, uint8_t msg_type, const void *msg_buf, uint16_t msg_len);
bool receive_msg(int fd, uint8_t *msg_type, uint8_t *msg_buf, uint16_t *msg_len, uint16_t max_msg_len);
 
void pack_state(const Game *g, uint8_t *out);
void unpack_state(Game *g, const uint8_t *in);

#endif
