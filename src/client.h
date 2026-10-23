#ifndef CLIENT_H
#define CLIENT_H

#include "common.h"
#include <pthread.h>

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

bool send_move(int fd, Move m);
bool send_resign(int fd);

#endif
