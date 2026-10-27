#ifndef CHESS_BOARD_H
#define CHESS_BOARD_H

#include <stdbool.h>
#include <stdint.h>

typedef uint8_t Piece;
typedef uint8_t PieceColor;

enum {
  EMPTY = 0,
  PAWN = 1,
  KNIGHT = 2,
  BISHOP = 3,
  ROOK = 4,
  QUEEN = 5,
  KING = 6
};

enum {
  COLOR_WHITE = 0,
  COLOR_BLACK = 8
};

#define TYPE_MASK 0b0111
#define COLOR_MASK 0b1000

#define TYPE(p) ((p) & TYPE_MASK)
#define COLOR(p) ((p) & COLOR_MASK)
#define IS_EMPTY(p) ((p) == EMPTY)
#define IS_COLOR(p, c) (!IS_EMPTY(p) && COLOR(p) == (c))
#define OTHER_COLOR(c) ((c) ^ COLOR_BLACK)
#define COLOR_INDEX(c) ((c) >> 3)

#define NO_SQUARE 255

// can_castle[color][side]
enum {
  CASTLE_KING = 0,
  CASTLE_QUEEN = 1
};

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

typedef struct {
  int dir_rank, dir_file;
} Dir;

static inline int rank_of(int sq) {
  return sq >> 3;
}

static inline int file_of(int sq) {
  return sq & 7;
}

static inline bool on_board(int sq) {
  return (unsigned)sq < 64;
}

static inline int square_of(int rank, int file) {
  return (rank * 8) + file;
}

// One step from sq in direction d, or -1 if that leaves the board.
static inline int step(int sq, Dir d) {
  int rank = rank_of(sq) + d.dir_rank;
  int file = file_of(sq) + d.dir_file;
  if ((unsigned)rank >= 8 || (unsigned)file >= 8) {
    return -1;
  }
  return square_of(rank, file);
}

// Rank delta of a single pawn push for color.
static inline int pawn_dir(PieceColor color) {
  return color == COLOR_WHITE ? 1 : -1;
}

// Rank a pawn starts on, where a double push is still available.
static inline int pawn_start_rank(PieceColor color) {
  return color == COLOR_WHITE ? 1 : 6;
}

// Rank a pawn promotes on.
static inline int promotion_rank(PieceColor color) {
  return color == COLOR_WHITE ? 7 : 0;
}

void init_board(Game *g);

// assumes m is legal
void apply_move(Game *g, Move m);

#endif
