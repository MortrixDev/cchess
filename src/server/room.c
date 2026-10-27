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

bool room_claim_seat(Room *r, Role role) {
  Seat *seat = &r->seats[role];
  if (getrandom(seat->token, TOKEN_SIZE, 0) != TOKEN_SIZE) return false;

  seat->claimed = true;
  seat->vacated_ms = 0;
  return true;
}

bool room_password_ok(const Room *r, const char *attempt) {
  return r->password[0] == '\0' || strcmp(r->password, attempt) == 0;
}

bool room_coin_flip(void) {
  uint8_t b;
  if (getrandom(&b, 1, 0) != 1) return false;
  return b & 1;
}

static bool seat_waiting(const Seat *seat) {
  return seat->claimed && seat->vacated_ms != 0;
}

bool room_seat_expired(const Room *r, Role role, int64_t now) {
  const Seat *seat = &r->seats[role];
  return seat_waiting(seat) && now - seat->vacated_ms >= GRACE_SECS * 1000;
}

int64_t room_table_next_expiry(const RoomTable *t) {
  int64_t soonest = -1;
  for (int i = 0; i < MAX_ROOMS; i++) {
    const Room *r = &t->rooms[i];
    if (!r->used) continue;

    for (int role = ROLE_WHITE; role <= ROLE_BLACK; role++) {
      if (!seat_waiting(&r->seats[role])) continue;

      int64_t at = r->seats[role].vacated_ms + GRACE_SECS * 1000;
      if (soonest < 0 || at < soonest) soonest = at;
    }
  }
  return soonest;
}

void room_release_seat(Room *r, Role role) {
  r->seats[role] = (Seat){0};
}

void room_vacate_seat(Room *r, Role role, int64_t now) {
  r->seats[role].vacated_ms = now;
}

// -1 when no claimed seat holds this token.
int room_seat_by_token(const Room *r, const uint8_t *token) {
  for (int role = ROLE_WHITE; role <= ROLE_BLACK; role++) {
    const Seat *seat = &r->seats[role];
    if (seat->claimed && memcmp(seat->token, token, TOKEN_SIZE) == 0) return role;
  }
  return -1;
}
