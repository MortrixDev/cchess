CC = gcc
WARNINGS = -Wall -Wextra
STD = -std=gnu99
CFLAGS = $(WARNINGS) $(STD) -O2
CLIENT_LIBS = -pthread $(shell pkg-config --libs raylib)
CLIENT_CFLAGS = $(shell pkg-config --cflags raylib)

SRC = src

SERVER_SRCS = $(SRC)/server.c $(SRC)/rules.c $(SRC)/common.c
SERVER_HDRS = $(SRC)/common.h $(SRC)/rules.h

CLIENT_SRCS = $(SRC)/client.c $(SRC)/gui.c $(SRC)/rules.c $(SRC)/common.c
CLIENT_HDRS = $(SRC)/client.h $(SRC)/gui.h $(SRC)/common.h $(SRC)/rules.h

all: server client

server: $(SERVER_SRCS) $(SERVER_HDRS)
	$(CC) $(CFLAGS) -o $@ $(SERVER_SRCS)

client: $(CLIENT_SRCS) $(CLIENT_HDRS)
	$(CC) $(CFLAGS) $(CLIENT_CFLAGS) -o $@ $(CLIENT_SRCS) $(CLIENT_LIBS)

debug:
	$(MAKE) clean
	$(MAKE) CFLAGS="$(WARNINGS) $(STD) -g -O0" all

clean:
	rm -f server client

.PHONY: all debug clean
