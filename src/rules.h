#ifndef RULES_H
#define RULES_H

#include "common.h"

// 218 is the worst case in a legal position
#define MAX_LEGAL_MOVES 256

int generate_legal_moves(const Game *g, Move *out);
int generate_legal_destinations(const Game *g, int from, bool targets[64]);

bool is_legal_move(const Game *g, Move m);
bool is_square_attacked(const Game *g, int sq, PieceColor by_color);
bool in_check(const Game *g, PieceColor color);
bool needs_promotion(const Game *g, Move m);

// assumes m is legal
void apply_move(Game *g, Move m);

GameResult game_status(const Game *g);

#endif
