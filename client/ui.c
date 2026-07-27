#include "ui.h"
#include <stdio.h>
#include <stdlib.h>

void clear_screen(void) {
#ifdef _WIN32
    system("cls");
#else
    printf("\033[H\033[J");
#endif
}

void print_banner(void) {
    printf("%s", COLOR_CYAN);
    printf("====================================================\n");
    printf("     🔴🟡 FORZA 4 MULTI-PLAYER IN C (LSO) 🟡🔴    \n");
    printf("====================================================\n");
    printf("%s", COLOR_RESET);
}

void print_grid(char grid[ROWS][COLS]) {
    printf("\n");
    printf("%s   1   2   3   4   5   6   7 %s\n", COLOR_BOLD, COLOR_RESET);
    printf(" +---+---+---+---+---+---+---+\n");

    for (int r = 0; r < ROWS; r++) {
        printf(" |");
        for (int c = 0; c < COLS; c++) {
            char cell = grid[r][c];
            if (cell == CELL_X) {
                printf(" %sX%s |", COLOR_RED, COLOR_RESET);
            } else if (cell == CELL_O) {
                printf(" %sO%s |", COLOR_YELLOW, COLOR_RESET);
            } else {
                printf("   |");
            }
        }
        printf("\n +---+---+---+---+---+---+---+\n");
    }
    printf("\n");
}

void print_match_list(MatchInfo *matches, int count) {
    printf("\n%s=== ELENCO PARTITE SUL SERVER ===%s\n", COLOR_CYAN, COLOR_RESET);
    if (count == 0) {
        printf("Nessuna partita presente al momento.\n\n");
        return;
    }

    printf("%-5s | %-15s | %-15s | %-15s\n", "ID", "Creatore (Host)", "Sfidante (Guest)", "Stato");
    printf("-------------------------------------------------------------\n");

    for (int i = 0; i < count; i++) {
        const char *state_str = "Sconosciuto";
        switch (matches[i].state) {
            case MATCH_STATE_WAITING:
                state_str = COLOR_GREEN "IN ATTESA" COLOR_RESET;
                break;
            case MATCH_STATE_JOIN_PENDING:
                state_str = COLOR_YELLOW "RICHIESTA IN CORSO" COLOR_RESET;
                break;
            case MATCH_STATE_IN_PROGRESS:
                state_str = COLOR_RED "IN CORSO" COLOR_RESET;
                break;
            case MATCH_STATE_TERMINATED:
                state_str = COLOR_MAGENTA "TERMINATA" COLOR_RESET;
                break;
            default:
                break;
        }

        const char *g_nick = (matches[i].guest_nick[0] != '\0') ? matches[i].guest_nick : "-";
        printf("#%-4d | %-15s | %-15s | %s\n",
               matches[i].match_id, matches[i].host_nick, g_nick, state_str);
    }
    printf("-------------------------------------------------------------\n\n");
}

void print_menu(const char *nickname) {
    printf("%sUtente:%s %s%s%s\n", COLOR_BOLD, COLOR_RESET, COLOR_CYAN, nickname, COLOR_RESET);
    printf("1. Crea una nuova partita\n");
    printf("2. Mostra elenco partite disponibili\n");
    printf("3. Chiedi di unirti a una partita\n");
    printf("4. Esci dal gioco\n");
    printf("Scegli un'opzione (1-4): ");
    fflush(stdout);
}
