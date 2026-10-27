#ifndef SERVER_ROOM_H
#define SERVER_ROOM_H

#include <stdbool.h>
#include <stdint.h>

#include "chess/rules.h"
#include "proto/proto.h"

#define MAX_ROOMS 64
#define MAX_SPECTATORS 10
#define GRACE_SECS 60

typedef struct {
  bool claimed;
  uint8_t token[TOKEN_SIZE];
  int64_t vacated_ms;
} Seat;

typedef struct {
  bool used;
  char id[ROOM_ID_BUF_LEN];
  char password[PASSWORD_BUF_LEN];
  uint8_t flags;
  Game game;
  bool started;
  bool over;
  GameResult result;
  Seat seats[2];
} Room;

typedef struct {
  Room rooms[MAX_ROOMS];
} RoomTable;

void room_init(Room *r, const char *id, const char *password, uint8_t flags);
bool room_start(Room *r);
bool room_finish(Room *r, GameResult result);
Room *room_find(RoomTable *t, const char *id);
void room_free(Room *r);
Room *room_create(RoomTable *t, const char *password, uint8_t flags);
bool room_password_ok(const Room *r, const char *attempt);
bool room_coin_flip(void);
bool room_claim_seat(Room *r, Role role);
int64_t room_table_next_expiry(const RoomTable *t);
bool room_seat_expired(const Room *r, Role role, int64_t now);
void room_release_seat(Room *r, Role role);
void room_vacate_seat(Room *r, Role role, int64_t now);
int room_seat_by_token(const Room *r, const uint8_t *token);

#endif
