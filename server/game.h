#ifndef GAME_H
#define GAME_H

#include "protocol.h"
#include "thread_compat.h"

typedef struct Match {
    int match_id;
    MatchState state;
    
    int host_socket;
    int guest_socket;
    char host_nick[MAX_NICKNAME];
    char guest_nick[MAX_NICKNAME];

    char grid[ROWS][COLS];
    int current_turn;        /* 1 = Host (CELL_X), 2 = Guest (CELL_O) */
    int winner;              /* 0 = None, 1 = Host, 2 = Guest, 3 = Draw */

    int host_rematch_choice;  /* -1 = pending, 1 = create/continue, 0 = leave */
    int guest_rematch_choice; /* -1 = pending, 1 = continue, 0 = leave */

    pthread_mutex_t mutex;
    pthread_cond_t cond;
} Match;

/* Function prototypes */
void init_grid(char grid[ROWS][COLS]);
int drop_piece(char grid[ROWS][COLS], int col, char symbol);
int check_win(char grid[ROWS][COLS], char symbol);
int check_draw(char grid[ROWS][COLS]);
void reset_match(Match *match);

#endif /* GAME_H */
