#ifndef SERVER_ROOM_H
#define SERVER_ROOM_H

#include "proto/proto.h"
#include "chess/rules.h"

#include <stdbool.h>
#include <stdint.h>

#define MAX_ROOMS      64
#define MAX_SPECTATORS 10

typedef struct {
  bool used;
  char id[ROOM_ID_BUF_LEN];
  char password[PASSWORD_BUF_LEN];
  uint8_t flags;
  Game game;
  bool started;
  bool over;
  GameResult result;
} Room;

typedef struct { Room rooms[MAX_ROOMS]; } RoomTable;

void room_init(Room *r, const char *id, const char *password, uint8_t flags);
bool room_start(Room *r);
bool room_finish(Room *r, GameResult result);

#endif
