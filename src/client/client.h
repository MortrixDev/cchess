#ifndef CLIENT_CLIENT_H
#define CLIENT_CLIENT_H

#include "chess/board.h"
#include "chess/rules.h"

#include <pthread.h>
#include <stdbool.h>

typedef struct View {
  bool connected;
  bool joined;
  bool spectator;
  bool has_state;
  bool game_over;
  PieceColor my_color;
  GameResult result;
  Game game;
} View;

typedef struct ClientState {
  pthread_mutex_t lock;
  int fd;
  View view;
} ClientState;

#endif
