#include "game.h"
#include <stdio.h>
#include <string.h>

void init_grid(char grid[ROWS][COLS]) {
    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            grid[r][c] = CELL_EMPTY;
        }
    }
}

int drop_piece(char grid[ROWS][COLS], int col, char symbol) {
    if (col < 0 || col >= COLS) {
        return -1;
    }
    /* Find lowest empty row in column (from bottom to top) */
    for (int r = ROWS - 1; r >= 0; r--) {
        if (grid[r][col] == CELL_EMPTY) {
            grid[r][col] = symbol;
            return r;
        }
    }
    return -1; /* Column is full */
}

int check_win(char grid[ROWS][COLS], char symbol) {
    /* 1. Horizontal check */
    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c <= COLS - 4; c++) {
            if (grid[r][c] == symbol &&
                grid[r][c+1] == symbol &&
                grid[r][c+2] == symbol &&
                grid[r][c+3] == symbol) {
                return 1;
            }
        }
    }

    /* 2. Vertical check */
    for (int r = 0; r <= ROWS - 4; r++) {
        for (int c = 0; c < COLS; c++) {
            if (grid[r][c] == symbol &&
                grid[r+1][c] == symbol &&
                grid[r+2][c] == symbol &&
                grid[r+3][c] == symbol) {
                return 1;
            }
        }
    }

    /* 3. Diagonal down-right (\) check */
    for (int r = 0; r <= ROWS - 4; r++) {
        for (int c = 0; c <= COLS - 4; c++) {
            if (grid[r][c] == symbol &&
                grid[r+1][c+1] == symbol &&
                grid[r+2][c+2] == symbol &&
                grid[r+3][c+3] == symbol) {
                return 1;
            }
        }
    }

    /* 4. Diagonal up-right (/) check */
    for (int r = 3; r < ROWS; r++) {
        for (int c = 0; c <= COLS - 4; c++) {
            if (grid[r][c] == symbol &&
                grid[r-1][c+1] == symbol &&
                grid[r-2][c+2] == symbol &&
                grid[r-3][c+3] == symbol) {
                return 1;
            }
        }
    }

    return 0;
}

int check_draw(char grid[ROWS][COLS]) {
    for (int c = 0; c < COLS; c++) {
        if (grid[0][c] == CELL_EMPTY) {
            return 0; /* At least one cell is available */
        }
    }
    return 1; /* All top cells full */
}

void reset_match(Match *match) {
    if (!match) return;
    init_grid(match->grid);
    match->current_turn = 1;
    match->winner = 0;
    match->host_rematch_choice = -1;
    match->guest_rematch_choice = -1;
}
