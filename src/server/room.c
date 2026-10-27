#include "server/room.h"
#include "chess/board.h"
#include "proto/proto.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/random.h>

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

static const char ID_ALPHABET[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";

static bool room_id_generate(char out[ROOM_ID_BUF_LEN]) {
  uint8_t raw[ROOM_ID_LEN];
  if (getrandom(raw, sizeof raw, 0) != (ssize_t)sizeof raw) return false;

  for (int i = 0; i < ROOM_ID_LEN; i++) out[i] = ID_ALPHABET[raw[i] & 31];
  out[ROOM_ID_LEN] = '\0';
  return true;
}

static Room *room_alloc(RoomTable *t) {
  for (int i = 0; i < MAX_ROOMS; i++) {
    if (!t->rooms[i].used) return &t->rooms[i];
  }
  return NULL;
}

Room *room_find(RoomTable *t, const char *id) {
  for (int i = 0; i < MAX_ROOMS; i++) {
    if (t->rooms[i].used && strcmp(t->rooms[i].id, id) == 0) return &t->rooms[i];
  }
  return NULL;
}

void room_free(Room *r) {
  r->used = false;
}

Room *room_create(RoomTable *t, const char *password, uint8_t flags) {
  Room *r = room_alloc(t);
  if (r == NULL) return NULL;

  char id[ROOM_ID_BUF_LEN];
  for (int tries = 0; tries < 8; tries++) {
    if (!room_id_generate(id)) {
       fprintf(stderr, "room_create: no randomness available\n");
       return NULL;
    }
    if (room_find(t, id) != NULL) continue;

    room_init(r, id, password, flags);
    return r;
  }
  return NULL;
}

bool room_password_ok(const Room *r, const char *attempt) {
  return r->password[0] == '\0' || strcmp(r->password, attempt) == 0;
}

bool room_coin_flip(void) {
  uint8_t b;
  if (getrandom(&b, 1, 0) != 1) return false;
  return b & 1;
}
