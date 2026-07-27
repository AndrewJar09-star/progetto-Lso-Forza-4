#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>

#define DEFAULT_PORT 8888
#define MAX_NICKNAME 32
#define MAX_MATCHES 50
#define MAX_CLIENTS 100
#define BUFFER_SIZE 2048

#define ROWS 6
#define COLS 7

#define CELL_EMPTY ' '
#define CELL_X     'X'
#define CELL_O     'O'

/* Message Types between Client and Server */
typedef enum {
    MSG_REGISTER = 1,          /* Client sends nickname */
    MSG_REGISTER_ACK,          /* Server confirms registration */
    MSG_LIST_MATCHES,          /* Client requests list of matches */
    MSG_MATCH_LIST_RESP,       /* Server sends list of matches */
    MSG_CREATE_MATCH,          /* Client creates a new match */
    MSG_CREATE_MATCH_ACK,      /* Server confirms match created */
    MSG_JOIN_REQ,              /* Guest requests to join match_id */
    MSG_JOIN_PROMPT,           /* Host receives prompt: accept/refuse guest */
    MSG_JOIN_RESP,             /* Host responds: 1=Accept, 0=Refuse */
    MSG_JOIN_RESULT,           /* Guest receives join result: 1=Accepted, 0=Refused */
    MSG_GAME_START,            /* Match starts (assigns symbols X and O) */
    MSG_MOVE_REQ,              /* Active player submits column move (0-6) */
    MSG_MOVE_RESP,             /* Move validity result */
    MSG_GRID_UPDATE,           /* Broadcast updated grid & turn info */
    MSG_GAME_OVER,             /* Personal result: 1=Win, 2=Loss, 3=Draw */
    MSG_POST_GAME_CHOICE,      /* Choice after game (rematch/new session or leave) */
    MSG_NOTIFICATION,          /* General broadcast to other clients */
    MSG_LEAVE_MATCH,           /* Client leaves match */
    MSG_DISCONNECT             /* Client disconnects */
} MessageType;

/* Match States */
typedef enum {
    MATCH_STATE_EMPTY = 0,
    MATCH_STATE_WAITING,       /* Created, open for join requests */
    MATCH_STATE_JOIN_PENDING,  /* Guest asked to join, host deciding */
    MATCH_STATE_IN_PROGRESS,   /* Active Connect 4 match */
    MATCH_STATE_TERMINATED     /* Match concluded */
} MatchState;

/* Info structure for sending match lists */
typedef struct {
    int32_t match_id;
    int32_t state;
    char host_nick[MAX_NICKNAME];  /* same size as MAX_NICKNAME to avoid overflow */
    char guest_nick[MAX_NICKNAME];
} MatchInfo;

/* Fixed-size network packet structure */
typedef struct {
    int32_t type;              /* MessageType */
    int32_t match_id;          /* Match ID if applicable */
    int32_t player_id;         /* Player ID or client socket identifier */
    int32_t status;            /* Generic status code or boolean choice / count */
    int32_t turn;              /* 1 = Host turn (X), 2 = Guest turn (O) */
    int32_t col;               /* Column selected for move (0-6) */
    char sender_nick[MAX_NICKNAME];
    char text[128];            /* Notification message or descriptive text */
    char grid[ROWS][COLS];     /* Connect 4 grid state */
    MatchInfo match_list[10];  /* Up to 10 active matches per list packet */
} Packet;

#endif /* PROTOCOL_H */
