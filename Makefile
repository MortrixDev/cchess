CC = gcc
WARNINGS = -Wall -Wextra
STD = -std=gnu99
OPT = -O2
CFLAGS = $(WARNINGS) $(STD) $(OPT) -I$(SRC)
DEPFLAGS = -MMD -MP

RAYLIB_CFLAGS = $(shell pkg-config --cflags raylib)
RAYLIB_LIBS = $(shell pkg-config --libs raylib)

SRC = src
BUILD = build

SHARED_SRCS = \
	$(SRC)/chess/board.c \
	$(SRC)/chess/movegen.c \
	$(SRC)/chess/rules.c \
	$(SRC)/proto/proto.c \
	$(SRC)/net/net.c

SERVER_SRCS = $(SHARED_SRCS) $(SRC)/server/main.c
CLIENT_SRCS = $(SHARED_SRCS) $(SRC)/client/main.c $(SRC)/client/ui.c

SERVER_OBJS = $(SERVER_SRCS:$(SRC)/%.c=$(BUILD)/%.o)
CLIENT_OBJS = $(CLIENT_SRCS:$(SRC)/%.c=$(BUILD)/%.o)

all: server client

server: $(SERVER_OBJS)
	$(CC) $(CFLAGS) -o $@ $^

client: $(CLIENT_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ -pthread $(RAYLIB_LIBS)

# The client talks to raylib; everything else is plain C.
$(BUILD)/client/%.o: $(SRC)/client/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(RAYLIB_CFLAGS) $(DEPFLAGS) -pthread -c $< -o $@

$(BUILD)/%.o: $(SRC)/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

debug:
	$(MAKE) clean
	$(MAKE) OPT="-g -O0" all

clean:
	rm -rf $(BUILD) server client

-include $(shell find $(BUILD) -name '*.d' 2>/dev/null)

.PHONY: all debug clean
