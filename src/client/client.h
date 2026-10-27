#ifndef CLIENT_CLIENT_H
#define CLIENT_CLIENT_H

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

#include "chess/board.h"
#include "chess/rules.h"
#include "proto/proto.h"

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

  bool reconnecting;
  int64_t reconnect_started_ms;

  bool peer_present[2];
  int64_t peer_vacated_ms[2];
  int spectators;
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
  bool has_token;

  Intent intent;
  char room_id[ROOM_ID_BUF_LEN];
  char password[PASSWORD_BUF_LEN];
  bool spectate;

  Move pending_move;
  bool has_pending_move;
  bool pending_resign;

  // Terminal: the seat cannot be reclaimed, so stop reconnecting.
  bool stopped;
  bool quit;
  View view;
} ClientState;

#endif
