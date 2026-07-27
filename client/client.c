/*
 * client.c - Client Forza 4 multi-player
 *
 * Architettura: single-threaded con select() su socket + stdin (Linux/macOS).
 * Questo elimina ogni race condition tra rete e tastiera.
 * Su Windows (Docker usa Linux, quindi questa parte non è necessaria per Docker).
 */

#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    typedef int socklen_t;
    #define close_socket(s) closesocket(s)
#else
    #include <sys/socket.h>
    #include <sys/select.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <netdb.h>
    #include <unistd.h>
    #define close_socket(s) close(s)
    #define SOCKET int
    #define INVALID_SOCKET -1
    #define SOCKET_ERROR -1
#endif

#include "protocol.h"
#include "ui.h"

/* ======================================================
 * Stato del client
 * ====================================================== */
typedef enum {
    CLI_STATE_LOBBY         = 0,
    CLI_STATE_WAITING_HOST  = 1,  /* attende sfidante */
    CLI_STATE_JOIN_PROMPT   = 2,  /* host deve accettare/rifiutare */
    CLI_STATE_JOIN_WAIT     = 3,  /* guest attende risposta host */
    CLI_STATE_AWAIT_JOIN_ID = 4,  /* attende ID partita da tastiera */
    CLI_STATE_IN_GAME       = 5,  /* partita in corso */
    CLI_STATE_POST_WINNER   = 6,
    CLI_STATE_POST_LOSER    = 7,
    CLI_STATE_POST_DRAW     = 8
} ClientState;


static SOCKET      g_sock        = INVALID_SOCKET;
static char        g_nickname[MAX_NICKNAME];
static ClientState g_state       = CLI_STATE_LOBBY;
static int         g_my_symbol   = 0;   /* 1=X host, 2=O guest */
static int         g_match_id    = -1;
static int         g_is_my_turn  = 0;
static int         g_join_mid    = -1;  /* match_id per cui host decide */
static char        g_join_gnick[MAX_NICKNAME];
static char        g_game_over_text[256];

/* ======================================================
 * Helpers rete
 * ====================================================== */
static int send_packet(const Packet *pkt) {
    int total = 0, left = (int)sizeof(Packet);
    const char *p = (const char *)pkt;
    while (total < (int)sizeof(Packet)) {
        int n = send(g_sock, p + total, left, 0);
        if (n <= 0) return -1;
        total += n; left -= n;
    }
    return total;
}

static int recv_packet_exact(Packet *pkt) {
    int total = 0, left = (int)sizeof(Packet);
    char *p = (char *)pkt;
    while (total < (int)sizeof(Packet)) {
        int n = recv(g_sock, p + total, left, 0);
        if (n <= 0) return -1;
        total += n; left -= n;
    }
    return total;
}

/* ======================================================
 * Gestione pacchetti in arrivo dal server
 * ====================================================== */
static void handle_server_packet(const Packet *pkt) {
    switch (pkt->type) {

    case MSG_NOTIFICATION:
        printf("\n%s%s%s\n", COLOR_MAGENTA, pkt->text, COLOR_RESET);
        if (g_state == CLI_STATE_LOBBY) {
            print_menu(g_nickname);
        }
        fflush(stdout);
        break;

    case MSG_MATCH_LIST_RESP:
        print_match_list((MatchInfo *)pkt->match_list, pkt->status);
        if (g_state == CLI_STATE_LOBBY) print_menu(g_nickname);
        fflush(stdout);
        break;

    case MSG_CREATE_MATCH_ACK:
        if (pkt->status == 1) {
            g_state = CLI_STATE_LOBBY;
            printf("\n%s%s%s\n", COLOR_GREEN, pkt->text, COLOR_RESET);
            printf("%s(Partita #%d creata ed in attesa. Puoi crearne altre dal menu.)%s\n",
                   COLOR_YELLOW, pkt->match_id, COLOR_RESET);
            print_menu(g_nickname);
        } else {
            printf("\n%s%s%s\n", COLOR_RED, pkt->text, COLOR_RESET);
            g_state = CLI_STATE_LOBBY;
            print_menu(g_nickname);
        }
        fflush(stdout);
        break;

    case MSG_JOIN_PROMPT:
        g_join_mid = pkt->match_id;
        strncpy(g_join_gnick, pkt->sender_nick, MAX_NICKNAME - 1);
        g_state = CLI_STATE_JOIN_PROMPT;
        printf("\n%s╔══════════════════════════════════════╗%s\n", COLOR_YELLOW, COLOR_RESET);
        printf("%s║    RICHIESTA DI SFIDA RICEVUTA!      ║%s\n", COLOR_YELLOW, COLOR_RESET);
        printf("%s╚══════════════════════════════════════╝%s\n", COLOR_YELLOW, COLOR_RESET);
        printf("L'utente %s%s%s vuole unirsi alla partita #%d!\n",
               COLOR_CYAN, g_join_gnick, COLOR_RESET, g_join_mid);
        printf("Inserisci %s1%s=ACCETTA  %s0%s=RIFIUTA: ",
               COLOR_GREEN, COLOR_RESET, COLOR_RED, COLOR_RESET);
        fflush(stdout);
        break;

    case MSG_JOIN_RESULT:
        if (pkt->status == 1) {
            g_match_id = pkt->match_id;
            g_state    = CLI_STATE_JOIN_WAIT;
            printf("\n%s%s%s\n", COLOR_GREEN, pkt->text, COLOR_RESET);
            printf("%s[Attesa avvio partita...]%s\n", COLOR_CYAN, COLOR_RESET);
        } else {
            g_state = CLI_STATE_LOBBY;
            printf("\n%s%s%s\n", COLOR_RED, pkt->text, COLOR_RESET);
            print_menu(g_nickname);
        }
        fflush(stdout);
        break;

    case MSG_GAME_START:
        g_state      = CLI_STATE_IN_GAME;
        g_my_symbol  = pkt->status;
        g_is_my_turn = 0;
        clear_screen();
        print_banner();
        printf("\n%s=====================================================%s\n", COLOR_CYAN, COLOR_RESET);
        printf("%s      LA PARTITA #%d E' INIZIATA!%s\n", COLOR_BOLD, pkt->match_id, COLOR_RESET);
        printf("Sfidante: %s%s%s | Il tuo Simbolo: %s%s%s\n",
               COLOR_CYAN, pkt->sender_nick, COLOR_RESET,
               (g_my_symbol == 1) ? COLOR_RED : COLOR_YELLOW,
               (g_my_symbol == 1) ? "X (Rosso)" : "O (Giallo)", COLOR_RESET);
        printf("%s=====================================================%s\n", COLOR_CYAN, COLOR_RESET);
        printf("%s[Aspetta il tuo turno...]%s\n", COLOR_YELLOW, COLOR_RESET);
        fflush(stdout);
        break;

    case MSG_GRID_UPDATE:
        clear_screen();
        print_banner();
        print_grid((char (*)[COLS])pkt->grid);
        printf("%s%s%s\n", COLOR_CYAN, pkt->text, COLOR_RESET);
        if (pkt->turn == g_my_symbol) {
            g_is_my_turn = 1;
            printf("\n%s>> E' IL TUO TURNO! Inserisci la colonna (1-7): %s",
                   COLOR_GREEN, COLOR_RESET);
        } else if (pkt->turn != 0) {
            g_is_my_turn = 0;
            printf("\n%s[In attesa della mossa dell'avversario...]%s\n",
                   COLOR_YELLOW, COLOR_RESET);
        }
        fflush(stdout);
        break;

    case MSG_MOVE_RESP:
        if (pkt->status == 0) {
            /* Mossa invalida: il turno e' ancora del giocatore */
            printf("\n%s[ERRORE] %s%s\n", COLOR_RED, pkt->text, COLOR_RESET);
            printf("%s>> Riprova - colonna (1-7): %s", COLOR_GREEN, COLOR_RESET);
            g_is_my_turn = 1;
            fflush(stdout);
        }
        break;

    case MSG_GAME_OVER:
        g_is_my_turn = 0;
        strncpy(g_game_over_text, pkt->text, sizeof(g_game_over_text) - 1);
        clear_screen();
        print_banner();
        if (pkt->status == 1) {
            g_state = CLI_STATE_POST_WINNER;
            printf("\n%s=====================================================%s\n", COLOR_GREEN, COLOR_RESET);
            printf("%s  🏆 %s 🏆%s\n", COLOR_GREEN, g_game_over_text, COLOR_RESET);
            printf("%s=====================================================%s\n", COLOR_GREEN, COLOR_RESET);
            printf("\n%sSei il Vincitore!%s Puoi aprire una nuova sessione.\n", COLOR_BOLD, COLOR_RESET);
            printf("Vuoi creare una nuova sessione? (%s1%s=Si  %s0%s=No, torna al menu): ",
                   COLOR_GREEN, COLOR_RESET, COLOR_RED, COLOR_RESET);
        } else if (pkt->status == 2) {
            g_state = CLI_STATE_POST_LOSER;
            printf("\n%s=====================================================%s\n", COLOR_RED, COLOR_RESET);
            printf("%s  ❌ %s ❌%s\n", COLOR_RED, g_game_over_text, COLOR_RESET);
            printf("%s=====================================================%s\n", COLOR_RED, COLOR_RESET);
            printf("\n%sIl perdente deve lasciare la partita.%s\n", COLOR_YELLOW, COLOR_RESET);
            printf("Premi INVIO per tornare alla lobby...");
        } else {
            g_state = CLI_STATE_POST_DRAW;
            printf("\n%s=====================================================%s\n", COLOR_YELLOW, COLOR_RESET);
            printf("%s  🤝 %s 🤝%s\n", COLOR_YELLOW, g_game_over_text, COLOR_RESET);
            printf("%s=====================================================%s\n", COLOR_YELLOW, COLOR_RESET);
            printf("\nEntrambi dovete scegliere se fare una nuova partita.\n");
            printf("Vuoi fare rivincita? (%s1%s=Si  %s0%s=No): ",
                   COLOR_GREEN, COLOR_RESET, COLOR_RED, COLOR_RESET);
        }
        fflush(stdout);
        break;

    default:
        break;
    }
}

/* ======================================================
 * Gestione riga di testo inserita dall'utente
 * ====================================================== */
static void handle_user_line(char *line) {
    /* Trim newline/CR */
    size_t len = strlen(line);
    while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r' || line[len-1] == ' '))
        line[--len] = '\0';

    /* Strip '#' prefix e spazi iniziali */
    char *p = line;
    while (*p == ' ') p++;
    if (*p == '#') p++;

    int val = (*p != '\0') ? atoi(p) : -1;

    switch (g_state) {

    case CLI_STATE_LOBBY:
        if (val == 1) {
            Packet pkt; memset(&pkt, 0, sizeof(pkt));
            pkt.type = MSG_CREATE_MATCH;
            strncpy(pkt.sender_nick, g_nickname, MAX_NICKNAME - 1);
            send_packet(&pkt);
        } else if (val == 2) {
            Packet pkt; memset(&pkt, 0, sizeof(pkt));
            pkt.type = MSG_LIST_MATCHES;
            send_packet(&pkt);
        } else if (val == 3) {
            g_state = CLI_STATE_AWAIT_JOIN_ID;
            printf("Inserisci l'ID della partita (es. 1 o #1): ");
            fflush(stdout);
        } else if (val == 4) {
            Packet pkt; memset(&pkt, 0, sizeof(pkt));
            pkt.type = MSG_DISCONNECT;
            send_packet(&pkt);
            printf("Arrivederci!\n");
            close_socket(g_sock);
            exit(0);
        } else {
            print_menu(g_nickname);
            fflush(stdout);
        }
        break;

    case CLI_STATE_AWAIT_JOIN_ID:
        if (val > 0) {
            Packet pkt; memset(&pkt, 0, sizeof(pkt));
            pkt.type     = MSG_JOIN_REQ;
            pkt.match_id = val;
            strncpy(pkt.sender_nick, g_nickname, MAX_NICKNAME - 1);
            send_packet(&pkt);
            printf("%s[Richiesta di unirsi alla partita #%d inviata, attendo risposta...]%s\n",
                   COLOR_CYAN, val, COLOR_RESET);
            g_state = CLI_STATE_LOBBY; /* torna in lobby e aspetta MSG_JOIN_RESULT */
            fflush(stdout);
        } else {
            printf("ID non valido. Inserisci un numero positivo: ");
            fflush(stdout);
        }
        break;

    case CLI_STATE_JOIN_PROMPT: {
        int accept = (val == 1) ? 1 : 0;
        Packet resp; memset(&resp, 0, sizeof(resp));
        resp.type     = MSG_JOIN_RESP;
        resp.match_id = g_join_mid;
        resp.status   = accept;
        send_packet(&resp);
        if (accept) {
            printf("%s[Richiesta accettata! Attendo avvio partita...]%s\n", COLOR_GREEN, COLOR_RESET);
            g_match_id = g_join_mid;
            g_state    = CLI_STATE_JOIN_WAIT;
        } else {
            printf("%s[Richiesta rifiutata.]%s\n", COLOR_RED, COLOR_RESET);
            g_state = CLI_STATE_LOBBY;
            print_menu(g_nickname);
        }
        fflush(stdout);
        break;
    }

    case CLI_STATE_IN_GAME:
        if (!g_is_my_turn) {
            /* Ignora input: non e' il tuo turno */
            break;
        }
        if (val >= 1 && val <= 7) {
            Packet pkt; memset(&pkt, 0, sizeof(pkt));
            pkt.type     = MSG_MOVE_REQ;
            pkt.match_id = g_match_id;
            pkt.col      = val - 1;
            send_packet(&pkt);
            g_is_my_turn = 0;
            printf("%s[Mossa inviata al server...]%s\n", COLOR_CYAN, COLOR_RESET);
            fflush(stdout);
        } else {
            printf("%s[ERRORE] Inserisci un numero tra 1 e 7: %s", COLOR_RED, COLOR_RESET);
            fflush(stdout);
        }
        break;

    case CLI_STATE_POST_WINNER: {
        int choice = (val == 1) ? 1 : 0;
        Packet pkt; memset(&pkt, 0, sizeof(pkt));
        pkt.type     = MSG_POST_GAME_CHOICE;
        pkt.match_id = g_match_id;
        pkt.status   = choice;
        send_packet(&pkt);
        if (choice == 1) {
            g_state = CLI_STATE_LOBBY;
            printf("%s[Nuova sessione aperta per la partita #%d!]%s\n", COLOR_GREEN, g_match_id, COLOR_RESET);
            print_menu(g_nickname);
        } else {
            g_match_id = -1;
            g_state    = CLI_STATE_LOBBY;
            clear_screen(); print_banner(); print_menu(g_nickname);
        }
        fflush(stdout);
        break;
    }

    case CLI_STATE_POST_LOSER: {
        /* Qualsiasi tasto: abbandona obbligatoriamente */
        Packet pkt; memset(&pkt, 0, sizeof(pkt));
        pkt.type     = MSG_POST_GAME_CHOICE;
        pkt.match_id = g_match_id;
        pkt.status   = 0;
        send_packet(&pkt);
        g_match_id = -1;
        g_state    = CLI_STATE_LOBBY;
        clear_screen(); print_banner(); print_menu(g_nickname);
        fflush(stdout);
        break;
    }

    case CLI_STATE_POST_DRAW: {
        int choice = (val == 1) ? 1 : 0;
        Packet pkt; memset(&pkt, 0, sizeof(pkt));
        pkt.type     = MSG_POST_GAME_CHOICE;
        pkt.match_id = g_match_id;
        pkt.status   = choice;
        send_packet(&pkt);
        if (choice == 0) {
            g_match_id = -1;
            g_state    = CLI_STATE_LOBBY;
            clear_screen(); print_banner(); print_menu(g_nickname);
        } else {
            g_state = CLI_STATE_LOBBY; /* server notifichera' apertura nuova sessione */
            printf("%s[Attendo la decisione dell'avversario...]%s\n", COLOR_CYAN, COLOR_RESET);
        }
        fflush(stdout);
        break;
    }

    default:
        /* In stati di attesa: ignora input */
        break;
    }
}

/* ======================================================
 * Main
 * ====================================================== */
int main(int argc, char *argv[]) {
    char server_ip[64] = "127.0.0.1";
    int  port          = DEFAULT_PORT;

    if (argc > 1) strncpy(server_ip, argv[1], sizeof(server_ip) - 1);
    if (argc > 2) port = atoi(argv[2]);

#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        fprintf(stderr, "[ERRORE] Winsock init fallita.\n");
        return 1;
    }
#else
    signal(SIGPIPE, SIG_IGN);
#endif

    g_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (g_sock == INVALID_SOCKET) {
        perror("[ERRORE] Creazione socket fallita");
        return 1;
    }

    struct hostent *he = gethostbyname(server_ip);
    struct sockaddr_in srv;
    memset(&srv, 0, sizeof(srv));
    srv.sin_family = AF_INET;
    srv.sin_port   = htons(port);
    if (he) {
        memcpy(&srv.sin_addr, he->h_addr_list[0], he->h_length);
    } else {
        srv.sin_addr.s_addr = inet_addr(server_ip);
        if (srv.sin_addr.s_addr == INADDR_NONE) {
            fprintf(stderr, "[ERRORE] Impossibile risolvere: %s\n", server_ip);
            close_socket(g_sock);
            return 1;
        }
    }

    printf("Connessione a %s:%d...\n", server_ip, port);
    if (connect(g_sock, (struct sockaddr *)&srv, sizeof(srv)) == SOCKET_ERROR) {
        perror("[ERRORE] Connessione fallita");
        close_socket(g_sock);
        return 1;
    }

    clear_screen();
    print_banner();

    /* --- Registrazione sincrona --- */
    printf("Inserisci il tuo Nickname: ");
    fflush(stdout);

    char nick_line[64];
    if (!fgets(nick_line, sizeof(nick_line), stdin)) strcpy(nick_line, "Giocatore");
    nick_line[strcspn(nick_line, "\r\n")] = '\0';
    if (nick_line[0] == '\0') strcpy(nick_line, "Giocatore");
    strncpy(g_nickname, nick_line, MAX_NICKNAME - 1);

    Packet reg;
    memset(&reg, 0, sizeof(reg));
    reg.type = MSG_REGISTER;
    strncpy(reg.sender_nick, g_nickname, MAX_NICKNAME - 1);
    if (send_packet(&reg) < 0) {
        fprintf(stderr, "[ERRORE] Invio nickname fallito.\n");
        close_socket(g_sock);
        return 1;
    }

    /* Attendi MSG_REGISTER_ACK, scarta eventuali notifiche spurie */
    Packet ack;
    for (;;) {
        memset(&ack, 0, sizeof(ack));
        if (recv_packet_exact(&ack) <= 0) {
            fprintf(stderr, "[ERRORE] Server disconnesso durante registrazione.\n");
            close_socket(g_sock);
            return 1;
        }
        if (ack.type == MSG_REGISTER_ACK) break;
    }
    printf("%s%s%s\n\n", COLOR_GREEN, ack.text, COLOR_RESET);
    print_menu(g_nickname);
    fflush(stdout);

    /* ============================================================
     * Loop principale: select() su socket + stdin
     *
     * select() blocca finche' c'e' qualcosa da leggere sul socket
     * (pacchetto dal server) O sull'stdin (tasto premuto dall'utente).
     * Non c'e' nessun thread separato: zero race conditions.
     * ============================================================ */
    char   input_buf[256];
    int    input_pos = 0;

    while (1) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(g_sock, &rfds);

#ifdef _WIN32
        /* Windows: select() non gestisce stdin come fd.
           Usiamo un timeout breve e leggiamo stdin in modo non bloccante
           tramite PeekConsoleInput / ReadConsoleInput. */
        struct timeval tv;
        tv.tv_sec  = 0;
        tv.tv_usec = 50000; /* 50ms */
        int ready = select((int)g_sock + 1, &rfds, NULL, NULL, &tv);
#else
        FD_SET(STDIN_FILENO, &rfds);
        int maxfd = (g_sock > STDIN_FILENO) ? (int)g_sock : STDIN_FILENO;
        int ready = select(maxfd + 1, &rfds, NULL, NULL, NULL);
#endif
        if (ready < 0) break;

        /* ---- Pacchetto dal server ---- */
        if (ready > 0 && FD_ISSET(g_sock, &rfds)) {
            Packet rx;
            memset(&rx, 0, sizeof(rx));
            if (recv_packet_exact(&rx) <= 0) {
                printf("\n%s[DISCONNESSO] Il server ha chiuso la connessione.%s\n",
                       COLOR_RED, COLOR_RESET);
                break;
            }
            handle_server_packet(&rx);
        }

#ifdef _WIN32
        /* ---- Input da tastiera su Windows (PeekConsoleInput) ---- */
        {
            HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
            DWORD  events = 0;
            PeekConsoleInput(hIn, NULL, 0, &events);
            /* Se c'e' input disponibile, leggi un carattere */
            if (events > 0) {
                /* Usa ReadConsoleA per leggere un singolo carattere */
                char ch = 0;
                DWORD read_count = 0;
                INPUT_RECORD ir;
                DWORD num;
                ReadConsoleInput(hIn, &ir, 1, &num);
                if (num > 0 && ir.EventType == KEY_EVENT && ir.Event.KeyEvent.bKeyDown) {
                    ch = ir.Event.KeyEvent.uChar.AsciiChar;
                    if (ch == '\r') {
                        printf("\n");
                        input_buf[input_pos] = '\0';
                        handle_user_line(input_buf);
                        input_pos = 0;
                    } else if (ch == '\b' && input_pos > 0) {
                        input_pos--;
                        printf("\b \b");
                        fflush(stdout);
                    } else if (ch >= 32 && ch < 127 && input_pos < (int)sizeof(input_buf) - 1) {
                        input_buf[input_pos++] = ch;
                        printf("%c", ch);
                        fflush(stdout);
                    }
                }
            }
        }
#else
        /* ---- Input da tastiera su Linux/macOS ---- */
        if (FD_ISSET(STDIN_FILENO, &rfds)) {
            char ch;
            int n = read(STDIN_FILENO, &ch, 1);
            if (n <= 0) break;
            if (ch == '\n') {
                input_buf[input_pos] = '\0';
                handle_user_line(input_buf);
                input_pos = 0;
            } else if ((ch == '\b' || ch == 127) && input_pos > 0) {
                input_pos--;
            } else if (ch >= 32 && input_pos < (int)sizeof(input_buf) - 1) {
                input_buf[input_pos++] = ch;
            }
        }
#endif
    }

    close_socket(g_sock);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
