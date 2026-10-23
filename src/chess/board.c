#include "chess/board.h"

#include <stdlib.h>

void init_board(Game *g) {
  static const Piece back_rank[8] = { ROOK, KNIGHT, BISHOP, QUEEN, KING, BISHOP, KNIGHT, ROOK };
  *g = (Game){0};

  for (int file = 0; file < 8; file++) {
    g->board[file] = back_rank[file] | COLOR_WHITE;
    g->board[8 + file] = PAWN | COLOR_WHITE;
    g->board[48 + file] = PAWN | COLOR_BLACK;
    g->board[56 + file] = back_rank[file] | COLOR_BLACK;
  }

  g->turn = COLOR_WHITE;
  g->can_castle[0][0] = true;
  g->can_castle[1][0] = true;
  g->can_castle[0][1] = true;
  g->can_castle[1][1] = true;
  g->en_passant_square = NO_SQUARE;
}

static void revoke_rook_right(Game *g, int sq) {
  switch (sq) {
    case 0:  g->can_castle[COLOR_INDEX(COLOR_WHITE)][CASTLE_QUEEN] = false; break;
    case 7:  g->can_castle[COLOR_INDEX(COLOR_WHITE)][CASTLE_KING]  = false; break;
    case 56: g->can_castle[COLOR_INDEX(COLOR_BLACK)][CASTLE_QUEEN] = false; break;
    case 63: g->can_castle[COLOR_INDEX(COLOR_BLACK)][CASTLE_KING]  = false; break;
  }
}

void apply_move(Game *g, Move m) {
  Piece p = g->board[m.from];
  PieceColor color = COLOR(p);
  int type = TYPE(p);
  bool capture = !IS_EMPTY(g->board[m.to]);

  if (type == PAWN && m.to == g->en_passant_square && file_of(m.to) != file_of(m.from)) {
    g->board[square_of(rank_of(m.to) - pawn_dir(color), file_of(m.to))] = EMPTY;
    capture = true;
  }

  g->board[m.to] = p;
  g->board[m.from] = EMPTY;

  if (type == PAWN && rank_of(m.to) == promotion_rank(color)) {
    int promo = TYPE(m.promotion);
    if (promo != QUEEN && promo != ROOK && promo != BISHOP && promo != KNIGHT) promo = QUEEN;
    g->board[m.to] = promo | color;
  }

  if (type == KING && abs(file_of(m.to) - file_of(m.from)) == 2) {
    int home = rank_of(m.from);
    bool kingside = file_of(m.to) == 6;
    int rook_from = square_of(home, kingside ? 7 : 0);
    int rook_to   = square_of(home, kingside ? 5 : 3);
    g->board[rook_to] = g->board[rook_from];
    g->board[rook_from] = EMPTY;
  }

  if (type == KING) {
    g->can_castle[COLOR_INDEX(color)][CASTLE_KING] = false;
    g->can_castle[COLOR_INDEX(color)][CASTLE_QUEEN] = false;
  }
  revoke_rook_right(g, m.from);
  revoke_rook_right(g, m.to);

  g->en_passant_square = NO_SQUARE;
  if (type == PAWN && abs(rank_of(m.to) - rank_of(m.from)) == 2)
    g->en_passant_square = square_of(rank_of(m.from) + pawn_dir(color), file_of(m.from));

  if (type == PAWN || capture) g->halfmove_clock = 0;
  else g->halfmove_clock++;

  if (color == COLOR_BLACK) g->fullmove_number++;

  g->turn = OTHER_COLOR(g->turn);
}
