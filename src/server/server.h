#ifndef SERVER_SERVER_H
#define SERVER_SERVER_H

#include "chess/board.h"
#include "chess/rules.h"
#include "proto/proto.h"
#include "server/room.h"

#include <stdbool.h>

#define MAX_CLIENTS (2 + MAX_SPECTATORS)

typedef struct {
  int fd;
  Role role;
  bool active;
  MsgNeeds reached;
  int room;
} Client;

typedef struct {
  int listen_fd;
  Client clients[MAX_CLIENTS];
  RoomTable room_table;
} Server;

void run_server(Server *s);

#endif
