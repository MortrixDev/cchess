#ifndef CLIENT_CLIENT_H
#define CLIENT_CLIENT_H

#include "chess/board.h"
#include "chess/rules.h"
#include "proto/proto.h"

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct View {
  bool connected;
  bool joined;
  bool spectator;
  bool has_state;
  bool game_over;
  PieceColor my_color;
  GameResult result;
  Game game;
  char room_id[ROOM_ID_BUF_LEN];
  uint16_t grace_secs;
  uint8_t error_code;
  uint8_t error_context;
} View;

// What this client does once the handshake completes.
typedef enum {
  INTENT_CREATE,
  INTENT_JOIN,
} Intent;

typedef struct ClientState {
  pthread_mutex_t lock;
  int fd;
  uint8_t token[TOKEN_SIZE];

  // Set from argv before the network thread starts, read-only thereafter.
  Intent intent;
  char room_id[ROOM_ID_BUF_LEN];
  char password[PASSWORD_BUF_LEN];
  bool spectate;

  View view;
} ClientState;

#endif
