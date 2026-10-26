#include "server/room.h"
#include "chess/board.h"
#include "proto/proto.h"
#include <stdio.h>
#include <string.h>

void room_init(Room *r, const char *id, const char *password, uint8_t flags) {
  *r = (Room){0};
  r->used = true;
  memcpy(r->id, id, ROOM_ID_LEN);
  r->id[ROOM_ID_LEN] = '\0';
  snprintf(r->password, sizeof r->password, "%s", password ? password : "");
  r->flags = flags & ROOM_FLAG_MASK;
}

bool room_start(Room *r) {
  if (r->started) return false;
  
  init_board(&r->game);
  r->started = true; 
  return true;
}

bool room_finish(Room *r, GameResult result) {
  if (result.result == RESULT_NONE || r->over) return false;

  r->over = true;
  r->result = result;
  return true;
}
