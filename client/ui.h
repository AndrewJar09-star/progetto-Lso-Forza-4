#ifndef UI_H
#define UI_H

#include "protocol.h"

/* ANSI Color codes */
#define COLOR_RESET        "\033[0m"
#define COLOR_RED          "\033[1;31m"
#define COLOR_YELLOW       "\033[1;33m"
#define COLOR_CYAN         "\033[1;36m"
#define COLOR_GREEN        "\033[1;32m"
#define COLOR_MAGENTA      "\033[1;35m"
#define COLOR_BLUE         "\033[1;34m"
#define COLOR_GRAY         "\033[90m"
#define COLOR_BRIGHT_WHITE "\033[97m"
#define COLOR_BOLD         "\033[1m"
#define COLOR_DIM          "\033[2m"

void clear_screen(void);
void print_banner(void);
void print_grid(char grid[ROWS][COLS]);
void print_match_list(MatchInfo *matches, int count);
void print_menu(const char *nickname);

#endif /* UI_H */
