#ifndef CHESS_RULES_H
#define CHESS_RULES_H

#include "chess/board.h"

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

GameResult game_status(const Game *g);

#endif
