CC = gcc
CFLAGS = -Wall -Wextra -std=c99

# Detect OS (Linux, macOS/Darwin, Windows/MinGW)
UNAME_S := $(shell uname -s 2>/dev/null)

ifneq ($(findstring MINGW,$(UNAME_S)),)
    IS_WINDOWS = 1
else ifeq ($(OS),Windows_NT)
    IS_WINDOWS = 1
endif

ifeq ($(UNAME_S),Darwin)
    # macOS (Apple Clang / GCC)
    CFLAGS += -pthread
    LIBS = -lpthread
    SERVER_BIN = server/server
    CLIENT_BIN = client/client
else ifeq ($(UNAME_S),Linux)
    # Linux (GCC / Clang)
    CFLAGS += -pthread
    LIBS = -pthread
    SERVER_BIN = server/server
    CLIENT_BIN = client/client
else ifeq ($(IS_WINDOWS),1)
    # Windows (MinGW)
    LIBS = -lws2_32
    SERVER_BIN = server/server.exe
    CLIENT_BIN = client/client.exe
else
    # Default POSIX Linux Fallback
    CFLAGS += -pthread
    LIBS = -pthread
    SERVER_BIN = server/server
    CLIENT_BIN = client/client
endif

.PHONY: all server client clean

all: server client

server: $(SERVER_BIN)

client: $(CLIENT_BIN)

$(SERVER_BIN): server/server.c server/game.c server/game.h server/protocol.h server/thread_compat.h
	$(CC) $(CFLAGS) server/server.c server/game.c -o $(SERVER_BIN) $(LIBS)

$(CLIENT_BIN): client/client.c client/ui.c client/ui.h client/protocol.h client/thread_compat.h
	$(CC) $(CFLAGS) client/client.c client/ui.c -o $(CLIENT_BIN) $(LIBS)

clean:
ifeq ($(IS_WINDOWS),1)
	-del /f /q server\server.exe client\client.exe *.o 2>NUL
else
	-rm -f server/server client/client *.o
endif
