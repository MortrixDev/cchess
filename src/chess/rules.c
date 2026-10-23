#include "chess/rules.h"
#include "chess/board.h"
#include "chess/movegen.h"

static bool is_insufficient_material(const Game *g) {
  int minors[2] = { 0, 0 };
  int bishop_shade[2] = { -1, -1 };
  int bishops = 0;

  for (int sq = 0; sq < 64; sq++) {
    Piece p = g->board[sq];
    if (IS_EMPTY(p) || TYPE(p) == KING) continue;

    int type = TYPE(p);
    if (type == PAWN || type == ROOK || type == QUEEN) return false;

    int ci = COLOR_INDEX(COLOR(p));
    minors[ci]++;
    if (type == BISHOP) {
      bishops++;
      bishop_shade[ci] = (rank_of(sq) + file_of(sq)) % 2;
    }
  }

  int total = minors[0] + minors[1];
  if (total <= 1) return true;

  return total == 2 && bishops == 2 &&
         minors[0] == 1 && minors[1] == 1 &&
         bishop_shade[0] == bishop_shade[1];
}

GameResult game_status(const Game *g) {
  Move moves[MAX_LEGAL_MOVES];

  if (generate_legal_moves(g, moves) == 0) {
    if (in_check(g, g->turn))
      return (GameResult){ WIN_FOR(OTHER_COLOR(g->turn)), REASON_CHECKMATE };
    return (GameResult){ RESULT_DRAW, REASON_STALEMATE };
  }

  if (g->halfmove_clock >= 100)
    return (GameResult){ RESULT_DRAW, REASON_FIFTY_MOVES };

  if (is_insufficient_material(g))
    return (GameResult){ RESULT_DRAW, REASON_INSUFFICIENT_MATERIAL };

  return NO_RESULT;
}
