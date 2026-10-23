#include "chess/movegen.h"
#include "chess/board.h"

#include <string.h>

#define COUNT(a) ((int)(sizeof (a) / sizeof (a)[0]))

static const Piece PROMO_PIECES[4] = { QUEEN, ROOK, BISHOP, KNIGHT };


static const Dir KNIGHT_DIRS[8] = {
  { 2, 1 }, { 2, -1 }, { -2, 1 }, { -2, -1 },
  { 1, 2 }, { 1, -2 }, { -1, 2 }, { -1, -2 },
};

static const Dir ALL_DIRS[8] = {
  { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 },
  { 1, 1 }, { 1, -1 }, { -1, 1 }, { -1, -1 },
};

static const Dir ROOK_DIRS[4]   = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
static const Dir BISHOP_DIRS[4] = { { 1, 1 }, { 1, -1 }, { -1, 1 }, { -1, -1 } };

static inline int find_king(const Game *g, PieceColor color) {
  for (int sq = 0; sq < 64; sq++) {
    if (g->board[sq] == (KING | color)) return sq;
  }
  return -1;
}

bool is_legal_move(const Game *g, Move m) {
  if (!on_board(m.from) || !on_board(m.to)) return false;

  Move moves[MAX_LEGAL_MOVES];
  int n = generate_legal_moves(g, moves);

  for (int i = 0; i < n; i++) {
    if (moves[i].from != m.from || moves[i].to != m.to) continue;
    if (TYPE(moves[i].promotion) == TYPE(m.promotion)) return true;
  }

  return false;
}

bool in_check(const Game *g, PieceColor color) {
  int king = find_king(g, color);
  return on_board(king) && is_square_attacked(g, king, OTHER_COLOR(color));
}

bool is_square_attacked(const Game *g, int sq, PieceColor by_color) {
  // knight
  for (int i = 0; i < COUNT(KNIGHT_DIRS); i++) {
    int t = step(sq, KNIGHT_DIRS[i]);
    if (on_board(t) && g->board[t] == (KNIGHT | by_color)) return true;
  }

  // king
  for (int i = 0; i < COUNT(ALL_DIRS); i++) {
    int t = step(sq, ALL_DIRS[i]);
    if (on_board(t) && g->board[t] == (KING | by_color)) return true;
  }

  // rook / queen
  for (int i = 0; i < COUNT(ROOK_DIRS); i++) {
    for (int t = step(sq, ROOK_DIRS[i]); on_board(t); t = step(t, ROOK_DIRS[i])) {
      if (IS_EMPTY(g->board[t])) continue;
      if (g->board[t] == (ROOK | by_color) || g->board[t] == (QUEEN | by_color)) return true;
      break;
    }
  }
  
  // bishop / queen
  for (int i = 0; i < COUNT(BISHOP_DIRS); i++) {
    for (int t = step(sq, BISHOP_DIRS[i]); on_board(t); t = step(t, BISHOP_DIRS[i])) {
      if (IS_EMPTY(g->board[t])) continue;
      if (g->board[t] == (BISHOP | by_color) || g->board[t] == (QUEEN | by_color)) return true;
      break;
    }
  }
  
  // pawn
  int back = -pawn_dir(by_color);
  for (int df = -1; df <= 1; df += 2) {
    int t = step(sq, (Dir){ back, df });
    if (on_board(t) && g->board[t] == (PAWN | by_color)) return true;
  }

  return false;
}

static int emit_pawn_move(int from, int to, PieceColor color, Move *out) {
  if (rank_of(to) != promotion_rank(color)) {
    out[0] = (Move){ from, to, EMPTY };
    return 1;
  }
  for (int i = 0; i < COUNT(PROMO_PIECES); i++)
    out[i] = (Move){ from, to, PROMO_PIECES[i] };
  return COUNT(PROMO_PIECES);
}

static int gen_pawn(const Game *g, int from, Move *out) {
  PieceColor color = g->turn;
  int dir = pawn_dir(color);
  int n = 0;

  int one = step(from, (Dir){ dir, 0 });
  if (on_board(one) && IS_EMPTY(g->board[one])) {
    n += emit_pawn_move(from, one, color, out + n);

    int two = step(one, (Dir){ dir, 0 });
    if (rank_of(from) == pawn_start_rank(color) && on_board(two) && IS_EMPTY(g->board[two]))
      out[n++] = (Move){ from, two, EMPTY };
  }

  for (int df = -1; df <= 1; df += 2) {
    int t = step(from, (Dir){ dir, df });
    if (!on_board(t)) continue;
    if (IS_COLOR(g->board[t], OTHER_COLOR(color)))
      n += emit_pawn_move(from, t, color, out + n);
    else if (t == g->en_passant_square)
      out[n++] = (Move){ from, t, EMPTY };
  }

  return n;
}

static int gen_steps(const Game *g, int from, const Dir *dirs, int count, Move *out) {
  int n = 0;
  for (int i = 0; i < count; i++) {
    int t = step(from, dirs[i]);
    if (on_board(t) && !IS_COLOR(g->board[t], g->turn))
      out[n++] = (Move){ from, t, EMPTY };
  }
  return n;
}

static int gen_rays(const Game *g, int from, const Dir *dirs, int count, Move *out) {
  int n = 0;
  for (int i = 0; i < count; i++) {
    for (int t = step(from, dirs[i]); on_board(t); t = step(t, dirs[i])) {
      if (IS_EMPTY(g->board[t])) {
        out[n++] = (Move){ from, t, EMPTY };
        continue;
      }
      if (!IS_COLOR(g->board[t], g->turn)) out[n++] = (Move){ from, t, EMPTY };
      break;
    }
  }
  return n;
}

static int gen_castling(const Game *g, Move *out) {
  PieceColor color = g->turn;
  PieceColor enemy = OTHER_COLOR(color);
  int home = color == COLOR_WHITE ? 0 : 7;
  int king = square_of(home, 4);
  int ci = COLOR_INDEX(color);
  int n = 0;

  if (g->board[king] != (KING | color)) return 0;
  if (is_square_attacked(g, king, enemy)) return 0;

  if (g->can_castle[ci][CASTLE_KING] &&
      g->board[square_of(home, 7)] == (ROOK | color) &&
      IS_EMPTY(g->board[square_of(home, 5)]) &&
      IS_EMPTY(g->board[square_of(home, 6)]) &&
      !is_square_attacked(g, square_of(home, 5), enemy) &&
      !is_square_attacked(g, square_of(home, 6), enemy))
    out[n++] = (Move){ king, square_of(home, 6), EMPTY };

  if (g->can_castle[ci][CASTLE_QUEEN] &&
      g->board[square_of(home, 0)] == (ROOK | color) &&
      IS_EMPTY(g->board[square_of(home, 1)]) &&
      IS_EMPTY(g->board[square_of(home, 2)]) &&
      IS_EMPTY(g->board[square_of(home, 3)]) &&
      !is_square_attacked(g, square_of(home, 3), enemy) &&
      !is_square_attacked(g, square_of(home, 2), enemy))
    out[n++] = (Move){ king, square_of(home, 2), EMPTY };

  return n;
}

static int generate_pseudo(const Game *g, Move *out) {
  int n = 0;

  for (int from = 0; from < 64; from++) {
    if (!IS_COLOR(g->board[from], g->turn)) continue;

    switch (TYPE(g->board[from])) {
      case PAWN:   n += gen_pawn(g, from, out + n); break;
      case KNIGHT: n += gen_steps(g, from, KNIGHT_DIRS, COUNT(KNIGHT_DIRS), out + n); break;
      case KING:   n += gen_steps(g, from, ALL_DIRS, COUNT(ALL_DIRS), out + n); break;
      case BISHOP: n += gen_rays(g, from, BISHOP_DIRS, COUNT(BISHOP_DIRS), out + n); break;
      case ROOK:   n += gen_rays(g, from, ROOK_DIRS, COUNT(ROOK_DIRS), out + n); break;
      case QUEEN:  n += gen_rays(g, from, ALL_DIRS, COUNT(ALL_DIRS), out + n); break;
    }
  }

  return n + gen_castling(g, out + n);
}

int generate_legal_moves(const Game *g, Move *out) {
  Move pseudo[MAX_LEGAL_MOVES];
  int count = generate_pseudo(g, pseudo);
  int n = 0;

  for (int i = 0; i < count; i++) {
    Game after = *g;
    apply_move(&after, pseudo[i]);
    if (!in_check(&after, g->turn)) out[n++] = pseudo[i];
  }

  return n;
}

int generate_legal_destinations(const Game *g, int from, bool targets[64]) {
  memset(targets, 0, 64 * sizeof *targets);
  if (!on_board(from)) return 0;

  Move moves[MAX_LEGAL_MOVES];
  int count = generate_legal_moves(g, moves);
  int n = 0;

  for (int i = 0; i < count; i++) {
    if (moves[i].from != from || targets[moves[i].to]) continue;
    targets[moves[i].to] = true;
    n++;
  }

  return n;
}
