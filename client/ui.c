#include "ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void clear_screen(void) {
#ifdef _WIN32
    system("cls");
#else
    printf("\033[H\033[2J\033[3J");
    fflush(stdout);
#endif
}

void print_banner(void) {
    printf("\n");
    printf("%s┌──────────────────────────────────────────────────────┐%s\n", COLOR_CYAN, COLOR_RESET);
    printf("%s│%s            🔴 🟡   F O R Z A   4   🟡 🔴             %s│%s\n", COLOR_CYAN, COLOR_BOLD, COLOR_CYAN, COLOR_RESET);
    printf("%s└──────────────────────────────────────────────────────┘%s\n", COLOR_CYAN, COLOR_RESET);
    printf("\n");
}

void print_grid(char grid[ROWS][COLS]) {
    printf("\n");
    /* Intestazione numeri colonne */
    printf("%s    1    2    3    4    5    6    7%s\n", COLOR_BOLD, COLOR_RESET);
    
    /* Bordo superiore */
    printf("%s ┌────┬────┬────┬────┬────┬────┬────┐%s\n", COLOR_GRAY, COLOR_RESET);

    for (int r = 0; r < ROWS; r++) {
        printf("%s │%s", COLOR_GRAY, COLOR_RESET);
        for (int c = 0; c < COLS; c++) {
            char cell = grid[r][c];
            if (cell == CELL_X) {
                printf(" %s🔴%s ", COLOR_RED, COLOR_RESET);
            } else if (cell == CELL_O) {
                printf(" %s🟡%s ", COLOR_YELLOW, COLOR_RESET);
            } else {
                printf("    ");
            }
            printf("%s│%s", COLOR_GRAY, COLOR_RESET);
        }
        printf("\n");

        if (r < ROWS - 1) {
            /* Bordo intermedio */
            printf("%s ├────┼────┼────┼────┼────┼────┼────┤%s\n", COLOR_GRAY, COLOR_RESET);
        } else {
            /* Bordo inferiore */
            printf("%s └────┴────┴────┴────┴────┴────┴────┘%s\n", COLOR_GRAY, COLOR_RESET);
        }
    }
    printf("\n");
}

void print_match_list(MatchInfo *matches, int count) {
    printf("%s┌──────┬─────────────────┬─────────────────┬──────────────────────┐%s\n", COLOR_GRAY, COLOR_RESET);
    printf("%s│%s                   ELENCO PARTITE SUL SERVER                     %s│%s\n", COLOR_GRAY, COLOR_BOLD, COLOR_GRAY, COLOR_RESET);
    printf("%s├──────┼─────────────────┼─────────────────┼──────────────────────┤%s\n", COLOR_GRAY, COLOR_RESET);
    printf("%s│  ID  │ Creatore (Host) │ Sfidante (Guest)│        Stato         │%s\n", COLOR_GRAY, COLOR_RESET);
    printf("%s├──────┼─────────────────┼─────────────────┼──────────────────────┤%s\n", COLOR_GRAY, COLOR_RESET);

    if (count == 0) {
        printf("%s│%s Nessuna partita disponibile al momento.                             %s│%s\n", COLOR_GRAY, COLOR_DIM, COLOR_GRAY, COLOR_RESET);
    } else {
        for (int i = 0; i < count; i++) {
            const char *color_code = COLOR_RESET;
            const char *raw_state = "Sconosciuto";
            int visible_len = 11;

            switch (matches[i].state) {
                case MATCH_STATE_WAITING:
                    color_code = COLOR_GREEN;
                    raw_state = "IN ATTESA";
                    visible_len = 9;
                    break;
                case MATCH_STATE_JOIN_PENDING:
                    color_code = COLOR_YELLOW;
                    raw_state = "RICHIESTA IN CORSO";
                    visible_len = 18;
                    break;
                case MATCH_STATE_IN_PROGRESS:
                    color_code = COLOR_RED;
                    raw_state = "IN CORSO";
                    visible_len = 8;
                    break;
                case MATCH_STATE_TERMINATED:
                    color_code = COLOR_MAGENTA;
                    raw_state = "TERMINATA";
                    visible_len = 9;
                    break;
                default:
                    break;
            }

            const char *g_nick = (matches[i].guest_nick[0] != '\0') ? matches[i].guest_nick : "-";
            int pad_spaces = 20 - visible_len;
            if (pad_spaces < 0) pad_spaces = 0;

            printf("%s│%s #%-3d %s│%s %-15s %s│%s %-15s %s│%s %s%s%s%*s %s│%s\n",
                   COLOR_GRAY, COLOR_BOLD, matches[i].match_id, COLOR_GRAY,
                   COLOR_RESET, matches[i].host_nick, COLOR_GRAY,
                   COLOR_RESET, g_nick, COLOR_GRAY,
                   COLOR_RESET, color_code, raw_state, COLOR_RESET, pad_spaces, "",
                   COLOR_GRAY, COLOR_RESET);
        }
    }
    printf("%s└──────┴─────────────────┴─────────────────┴──────────────────────┘%s\n\n", COLOR_GRAY, COLOR_RESET);
}

void print_menu(const char *nickname) {
    printf("%s┌──────────────────────────────────────────────────────┐%s\n", COLOR_GRAY, COLOR_RESET);
    printf("%s│%s  Giocatore: %s%-41s%s%s│%s\n", COLOR_GRAY, COLOR_BOLD, COLOR_CYAN, nickname, COLOR_RESET, COLOR_GRAY, COLOR_RESET);
    printf("%s├──────────────────────────────────────────────────────┤%s\n", COLOR_GRAY, COLOR_RESET);
    printf("%s│%s  [1]  Crea una nuova partita                         %s│%s\n", COLOR_GRAY, COLOR_RESET, COLOR_GRAY, COLOR_RESET);
    printf("%s│%s  [2]  Mostra elenco partite disponibili              %s│%s\n", COLOR_GRAY, COLOR_RESET, COLOR_GRAY, COLOR_RESET);
    printf("%s│%s  [3]  Unisciti a una partita                         %s│%s\n", COLOR_GRAY, COLOR_RESET, COLOR_GRAY, COLOR_RESET);
    printf("%s│%s  [4]  Esci dal gioco                                 %s│%s\n", COLOR_GRAY, COLOR_RESET, COLOR_GRAY, COLOR_RESET);
    printf("%s└──────────────────────────────────────────────────────┘%s\n", COLOR_GRAY, COLOR_RESET);
    printf("\n%sScegli un'opzione (1-4): %s", COLOR_BOLD, COLOR_RESET);
    fflush(stdout);
}
