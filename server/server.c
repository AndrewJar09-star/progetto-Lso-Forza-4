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
#include "thread_compat.h"

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    typedef int socklen_t;
    #define close_socket(s) closesocket(s)
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <netinet/tcp.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #define close_socket(s) close(s)
    #define SOCKET int
    #define INVALID_SOCKET -1
    #define SOCKET_ERROR -1
#endif

#include "protocol.h"
#include "game.h"

typedef struct {
    SOCKET sock;
    char nickname[MAX_NICKNAME];
    int active_match_id;
    int in_use;
    pthread_t thread;
} Client;

static Client clients[MAX_CLIENTS];
static Match matches[MAX_MATCHES];
static int match_counter = 1;

static pthread_mutex_t server_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Reliable Packet I/O helpers */
int send_packet(SOCKET sock, const Packet *pkt) {
    if (sock == INVALID_SOCKET) return -1;
    int total_sent = 0;
    int bytes_left = sizeof(Packet);
    const char *ptr = (const char *)pkt;

    while (total_sent < (int)sizeof(Packet)) {
        int sent = send(sock, ptr + total_sent, bytes_left, 0);
        if (sent <= 0) return -1;
        total_sent += sent;
        bytes_left -= sent;
    }
    return total_sent;
}

int recv_packet(SOCKET sock, Packet *pkt) {
    if (sock == INVALID_SOCKET) return -1;
    int total_recv = 0;
    int bytes_left = sizeof(Packet);
    char *ptr = (char *)pkt;

    while (total_recv < (int)sizeof(Packet)) {
        int r = recv(sock, ptr + total_recv, bytes_left, 0);
        if (r <= 0) return -1;
        total_recv += r;
        bytes_left -= r;
    }
    return total_recv;
}

/* Broadcast notification to clients except specified sockets */
void broadcast_notification(const char *text, SOCKET ex1, SOCKET ex2) {
    Packet pkt;
    memset(&pkt, 0, sizeof(Packet));
    pkt.type = MSG_NOTIFICATION;
    strncpy(pkt.text, text, sizeof(pkt.text) - 1);

    pthread_mutex_lock(&server_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].in_use && clients[i].sock != INVALID_SOCKET && clients[i].nickname[0] != '\0') {
            if (clients[i].sock != ex1 && clients[i].sock != ex2) {
                send_packet(clients[i].sock, &pkt);
            }
        }
    }
    pthread_mutex_unlock(&server_mutex);
}

/* Helper: find Client* by socket (caller must hold server_mutex) */
static Client *find_client_by_socket(SOCKET s) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].in_use && clients[i].sock == s)
            return &clients[i];
    }
    return NULL;
}

/* Client Handler Thread */
void *client_handler(void *arg) {
    Client *cli = (Client *)arg;
    SOCKET sock = cli->sock;
    Packet rx_pkt, tx_pkt;

    printf("[SERVER] New thread client socket %d connected.\n", (int)sock);

    while (1) {
        memset(&rx_pkt, 0, sizeof(Packet));
        if (recv_packet(sock, &rx_pkt) <= 0) {
            printf("[SERVER] Client %s (sock %d) disconnected.\n", cli->nickname, (int)sock);
            break;
        }

        switch (rx_pkt.type) {
            case MSG_REGISTER: {
                pthread_mutex_lock(&server_mutex);
                strncpy(cli->nickname, rx_pkt.sender_nick, MAX_NICKNAME - 1);
                cli->nickname[MAX_NICKNAME - 1] = '\0';
                pthread_mutex_unlock(&server_mutex);

                memset(&tx_pkt, 0, sizeof(Packet));
                tx_pkt.type = MSG_REGISTER_ACK;
                tx_pkt.status = 1;
                snprintf(tx_pkt.text, sizeof(tx_pkt.text), "Benvenuto %s nel Server Forza 4!", cli->nickname);
                send_packet(sock, &tx_pkt);
                printf("[SERVER] Client registered nickname: %s\n", cli->nickname);
                break;
            }

            case MSG_LIST_MATCHES: {
                pthread_mutex_lock(&server_mutex);
                memset(&tx_pkt, 0, sizeof(Packet));
                tx_pkt.type = MSG_MATCH_LIST_RESP;

                int count = 0;
                for (int i = 0; i < MAX_MATCHES; i++) {
                    if (matches[i].state != MATCH_STATE_EMPTY) {
                        tx_pkt.match_list[count].match_id = matches[i].match_id;
                        tx_pkt.match_list[count].state = matches[i].state;
                        strncpy(tx_pkt.match_list[count].host_nick, matches[i].host_nick, MAX_NICKNAME - 1);
                        strncpy(tx_pkt.match_list[count].guest_nick, matches[i].guest_nick, MAX_NICKNAME - 1);
                        count++;
                    }
                }
                tx_pkt.status = count;
                pthread_mutex_unlock(&server_mutex);

                send_packet(sock, &tx_pkt);
                break;
            }

            case MSG_CREATE_MATCH: {
                pthread_mutex_lock(&server_mutex);
                memset(&tx_pkt, 0, sizeof(Packet));
                tx_pkt.type = MSG_CREATE_MATCH_ACK;

                if (cli->active_match_id != -1) {
                    tx_pkt.status = 0;
                    snprintf(tx_pkt.text, sizeof(tx_pkt.text), "Non puoi creare una partita mentre sei gia' impegnato in una partita in corso (#%d).", cli->active_match_id);
                } else {
                    int slot = -1;
                    for (int i = 0; i < MAX_MATCHES; i++) {
                        if (matches[i].state == MATCH_STATE_EMPTY) {
                            slot = i;
                            break;
                        }
                    }

                    if (slot == -1) {
                        tx_pkt.status = 0;
                        snprintf(tx_pkt.text, sizeof(tx_pkt.text), "Errore: limite massimo partite raggiunto.");
                    } else {
                        Match *m = &matches[slot];
                        memset(m, 0, sizeof(Match));
                        m->match_id = match_counter++;
                        m->state = MATCH_STATE_WAITING;
                        m->host_socket = (int)sock;
                        m->guest_socket = -1;
                        strncpy(m->host_nick, cli->nickname, MAX_NICKNAME - 1);
                        m->host_nick[MAX_NICKNAME - 1] = '\0';
                        init_grid(m->grid);
                        m->current_turn = 1;
                        m->winner = 0;
                        m->host_rematch_choice = -1;
                        m->guest_rematch_choice = -1;
                        pthread_mutex_init(&m->mutex, NULL);
                        pthread_cond_init(&m->cond, NULL);

                        /* Active match is set when game becomes IN_PROGRESS */
                        tx_pkt.status = 1;
                        tx_pkt.match_id = m->match_id;
                        snprintf(tx_pkt.text, sizeof(tx_pkt.text), "Partita #%d creata con successo! In attesa di sfidanti...", m->match_id);

                        char notif[256];
                        snprintf(notif, sizeof(notif), "[NOTIFICA] Nuova partita #%d creata da %s (Stato: In Attesa)", m->match_id, cli->nickname);
                        pthread_mutex_unlock(&server_mutex);

                        broadcast_notification(notif, sock, INVALID_SOCKET);
                        pthread_mutex_lock(&server_mutex);
                    }
                }
                pthread_mutex_unlock(&server_mutex);

                send_packet(sock, &tx_pkt);
                break;
            }

            case MSG_JOIN_REQ: {
                int target_id = rx_pkt.match_id;
                pthread_mutex_lock(&server_mutex);

                Match *target_match = NULL;
                for (int i = 0; i < MAX_MATCHES; i++) {
                    if (matches[i].match_id == target_id && matches[i].state == MATCH_STATE_WAITING) {
                        target_match = &matches[i];
                        break;
                    }
                }

                memset(&tx_pkt, 0, sizeof(Packet));
                tx_pkt.type = MSG_JOIN_RESULT;

                if (!target_match) {
                    tx_pkt.status = 0;
                    snprintf(tx_pkt.text, sizeof(tx_pkt.text), "Partita #%d non trovata o non disponibile.", target_id);
                    send_packet(sock, &tx_pkt);
                } else if (cli->active_match_id != -1) {
                    tx_pkt.status = 0;
                    snprintf(tx_pkt.text, sizeof(tx_pkt.text), "Sei gia' impegnato in una partita in corso (#%d)! Puoi giocare solo a una partita alla volta.", cli->active_match_id);
                    send_packet(sock, &tx_pkt);
                } else if (target_match->host_socket == (int)sock) {
                    tx_pkt.status = 0;
                    snprintf(tx_pkt.text, sizeof(tx_pkt.text), "Non puoi unirti alla tua stessa partita!");
                    send_packet(sock, &tx_pkt);
                } else {
                    int host_busy = 0;
                    for (int ci = 0; ci < MAX_CLIENTS; ci++) {
                        if (clients[ci].in_use && clients[ci].sock == (SOCKET)target_match->host_socket) {
                            if (clients[ci].active_match_id != -1) host_busy = 1;
                            break;
                        }
                    }

                    /* Check if host already has a pending join request on ANY match */
                    int host_has_pending = 0;
                    for (int j = 0; j < MAX_MATCHES; j++) {
                        if (matches[j].state == MATCH_STATE_JOIN_PENDING &&
                            matches[j].host_socket == target_match->host_socket) {
                            host_has_pending = 1;
                            break;
                        }
                    }

                    if (host_busy) {
                        tx_pkt.status = 0;
                        snprintf(tx_pkt.text, sizeof(tx_pkt.text),
                                 "Il creatore della partita (%s) e' al momento impegnato in un'altra partita in corso.",
                                 target_match->host_nick);
                        send_packet(sock, &tx_pkt);
                    } else if (host_has_pending) {
                        tx_pkt.status = 0;
                        snprintf(tx_pkt.text, sizeof(tx_pkt.text),
                                 "Il creatore %s ha gia' una richiesta di sfida in sospeso. Riprova tra poco.",
                                 target_match->host_nick);
                        send_packet(sock, &tx_pkt);
                    } else {
                        target_match->state = MATCH_STATE_JOIN_PENDING;
                        target_match->guest_socket = (int)sock;
                        strncpy(target_match->guest_nick, cli->nickname, MAX_NICKNAME - 1);

                        /* Notify host */
                        Packet prompt_pkt;
                        memset(&prompt_pkt, 0, sizeof(Packet));
                        prompt_pkt.type = MSG_JOIN_PROMPT;
                        prompt_pkt.match_id = target_id;
                        strncpy(prompt_pkt.sender_nick, cli->nickname, MAX_NICKNAME - 1);
                        snprintf(prompt_pkt.text, sizeof(prompt_pkt.text),
                                 "L'utente %s ha chiesto di unirsi alla tua partita #%d! Accetti? (1=Accetta, 0=Rifiuta)",
                                 cli->nickname, target_id);
                        
                        send_packet((SOCKET)target_match->host_socket, &prompt_pkt);
                    }
                }
                pthread_mutex_unlock(&server_mutex);
                break;
            }

            case MSG_JOIN_RESP: {
                int target_id = rx_pkt.match_id;
                int accept = rx_pkt.status;

                pthread_mutex_lock(&server_mutex);
                Match *m = NULL;
                for (int i = 0; i < MAX_MATCHES; i++) {
                    if (matches[i].match_id == target_id && matches[i].host_socket == (int)sock) {
                        m = &matches[i];
                        break;
                    }
                }

                if (m && m->state == MATCH_STATE_JOIN_PENDING) {
                    SOCKET guest_s = (SOCKET)m->guest_socket;

                    /* RACE CONDITION GUARD: verify guest is still connected */
                    Client *guest_cli = find_client_by_socket(guest_s);
                    if (!guest_cli) {
                        /* Guest disconnected while host was deciding */
                        m->state = MATCH_STATE_WAITING;
                        m->guest_socket = -1;
                        m->guest_nick[0] = '\0';

                        Packet notify_pkt;
                        memset(&notify_pkt, 0, sizeof(Packet));
                        notify_pkt.type = MSG_NOTIFICATION;
                        snprintf(notify_pkt.text, sizeof(notify_pkt.text),
                                 "Lo sfidante si e' disconnesso prima della tua risposta. La partita #%d torna IN ATTESA.", target_id);
                        send_packet(sock, &notify_pkt);
                        pthread_mutex_unlock(&server_mutex);
                        break;
                    }

                    int host_busy = 0, guest_busy = 0;
                    for (int ci = 0; ci < MAX_CLIENTS; ci++) {
                        if (clients[ci].in_use) {
                            if (clients[ci].sock == (SOCKET)m->host_socket && clients[ci].active_match_id != -1) host_busy = 1;
                            if (clients[ci].sock == guest_s && clients[ci].active_match_id != -1) guest_busy = 1;
                        }
                    }

                    if (accept && !host_busy && !guest_busy) {
                        m->state = MATCH_STATE_IN_PROGRESS;

                        /* Update active_match_id for both host and guest clients */
                        for (int ci = 0; ci < MAX_CLIENTS; ci++) {
                            if (clients[ci].in_use) {
                                if (clients[ci].sock == (SOCKET)m->host_socket)
                                    clients[ci].active_match_id = m->match_id;
                                else if (clients[ci].sock == guest_s)
                                    clients[ci].active_match_id = m->match_id;
                            }
                        }

                        /* Inform guest: accepted */
                        Packet g_pkt;
                        memset(&g_pkt, 0, sizeof(Packet));
                        g_pkt.type     = MSG_JOIN_RESULT;
                        g_pkt.status   = 1;
                        g_pkt.match_id = target_id;
                        snprintf(g_pkt.text, sizeof(g_pkt.text),
                                 "Il creatore %s ha ACCETTATO la tua richiesta!", m->host_nick);
                        send_packet(guest_s, &g_pkt);

                        /* MSG_GAME_START: host gets X (symbol=1), guest gets O (symbol=2) */
                        Packet start_pkt;
                        memset(&start_pkt, 0, sizeof(Packet));
                        start_pkt.type     = MSG_GAME_START;
                        start_pkt.match_id = target_id;

                        start_pkt.status = 1; /* X = Host */
                        strncpy(start_pkt.sender_nick, m->guest_nick, MAX_NICKNAME - 1);
                        send_packet((SOCKET)m->host_socket, &start_pkt);

                        start_pkt.status = 2; /* O = Guest */
                        strncpy(start_pkt.sender_nick, m->host_nick, MAX_NICKNAME - 1);
                        send_packet((SOCKET)m->guest_socket, &start_pkt);

                        /* Initial grid update: turn=1 (host goes first) */
                        Packet grid_pkt;
                        memset(&grid_pkt, 0, sizeof(Packet));
                        grid_pkt.type     = MSG_GRID_UPDATE;
                        grid_pkt.match_id = target_id;
                        grid_pkt.turn     = 1; /* host first */
                        memcpy(grid_pkt.grid, m->grid, sizeof(m->grid));
                        snprintf(grid_pkt.text, sizeof(grid_pkt.text),
                                 "Partita Iniziata! Turno di %s (X - Rosso)", m->host_nick);

                        send_packet((SOCKET)m->host_socket, &grid_pkt);
                        send_packet((SOCKET)m->guest_socket, &grid_pkt);

                        /* Broadcast to spectators */
                        char notif[256];
                        snprintf(notif, sizeof(notif),
                                 "[NOTIFICA] La partita #%d (%s vs %s) e' ora IN CORSO!",
                                 m->match_id, m->host_nick, m->guest_nick);
                        /* AUTO-REJECT: reject all other JOIN_PENDING matches for this host */
                        for (int mi = 0; mi < MAX_MATCHES; mi++) {
                            if (&matches[mi] == m) continue; /* skip current match */
                            if (matches[mi].state == MATCH_STATE_JOIN_PENDING &&
                                matches[mi].host_socket == m->host_socket) {
                                /* Notify pending guest that host is now busy */
                                Packet reject_pkt;
                                memset(&reject_pkt, 0, sizeof(Packet));
                                reject_pkt.type   = MSG_JOIN_RESULT;
                                reject_pkt.status = 0;
                                reject_pkt.match_id = matches[mi].match_id;
                                snprintf(reject_pkt.text, sizeof(reject_pkt.text),
                                         "Partita rifiutata: il giocatore %s e' ora impegnato in un'altra partita.",
                                         m->host_nick);
                                send_packet((SOCKET)matches[mi].guest_socket, &reject_pkt);
                                /* Reset match back to WAITING */
                                matches[mi].state = MATCH_STATE_WAITING;
                                matches[mi].guest_socket = -1;
                                matches[mi].guest_nick[0] = '\0';
                            }
                        }

                        pthread_mutex_unlock(&server_mutex);
                        broadcast_notification(notif, (SOCKET)m->host_socket, guest_s);
                        pthread_mutex_lock(&server_mutex);

                    } else {
                        /* Refused or busy */
                        m->state = MATCH_STATE_WAITING;
                        Packet g_pkt;
                        memset(&g_pkt, 0, sizeof(Packet));
                        g_pkt.type   = MSG_JOIN_RESULT;
                        g_pkt.status = 0;
                        if (!accept) {
                            snprintf(g_pkt.text, sizeof(g_pkt.text),
                                     "Il creatore %s ha RIFIUTATO la tua richiesta.", m->host_nick);
                        } else {
                            snprintf(g_pkt.text, sizeof(g_pkt.text),
                                     "Impossibile avviare la partita: uno dei giocatori e' gia' impegnato in un'altra partita in corso.");
                        }
                        send_packet(guest_s, &g_pkt);

                        m->guest_socket    = -1;
                        m->guest_nick[0]   = '\0';
                    }
                }
                pthread_mutex_unlock(&server_mutex);
                break;
            }

            case MSG_MOVE_REQ: {
                int target_id = rx_pkt.match_id;
                int col = rx_pkt.col;

                pthread_mutex_lock(&server_mutex);
                Match *m = NULL;
                for (int i = 0; i < MAX_MATCHES; i++) {
                    if (matches[i].match_id == target_id && matches[i].state == MATCH_STATE_IN_PROGRESS) {
                        m = &matches[i];
                        break;
                    }
                }

                if (!m) {
                    pthread_mutex_unlock(&server_mutex);
                    break;
                }

                pthread_mutex_lock(&m->mutex);
                pthread_mutex_unlock(&server_mutex);

                /* Verify turn */
                int player_num = ((int)sock == m->host_socket) ? 1 : 2;
                if (player_num != m->current_turn) {
                    Packet err_pkt;
                    memset(&err_pkt, 0, sizeof(Packet));
                    err_pkt.type = MSG_MOVE_RESP;
                    err_pkt.status = 0;
                    snprintf(err_pkt.text, sizeof(err_pkt.text), "Non e' il tuo turno!");
                    send_packet(sock, &err_pkt);
                    pthread_mutex_unlock(&m->mutex);
                    break;
                }

                char symbol = (player_num == 1) ? CELL_X : CELL_O;
                int row = drop_piece(m->grid, col, symbol);

                if (row == -1) {
                    Packet err_pkt;
                    memset(&err_pkt, 0, sizeof(Packet));
                    err_pkt.type = MSG_MOVE_RESP;
                    err_pkt.status = 0;
                    snprintf(err_pkt.text, sizeof(err_pkt.text), "Mossa non valida! Colonna piena o fuori dai limiti.");
                    send_packet(sock, &err_pkt);
                    pthread_mutex_unlock(&m->mutex);
                    break;
                }

                /* Check Win or Draw */
                if (check_win(m->grid, symbol)) {
                    m->winner = player_num;
                    m->state = MATCH_STATE_TERMINATED;

                    /* Send final Grid */
                    Packet grid_pkt;
                    memset(&grid_pkt, 0, sizeof(Packet));
                    grid_pkt.type = MSG_GRID_UPDATE;
                    grid_pkt.match_id = target_id;
                    grid_pkt.turn = 0;
                    memcpy(grid_pkt.grid, m->grid, sizeof(m->grid));
                    snprintf(grid_pkt.text, sizeof(grid_pkt.text), "Partita Terminata!");

                    send_packet((SOCKET)m->host_socket, &grid_pkt);
                    send_packet((SOCKET)m->guest_socket, &grid_pkt);

                    /* Send GAME_OVER */
                    Packet host_go, guest_go;
                    memset(&host_go, 0, sizeof(Packet));
                    memset(&guest_go, 0, sizeof(Packet));
                    host_go.type = MSG_GAME_OVER;
                    guest_go.type = MSG_GAME_OVER;

                    if (player_num == 1) {
                        host_go.status = 1; /* WIN */
                        snprintf(host_go.text, sizeof(host_go.text), "COMPLIMENTI %s! HAI VINTO LA PARTITA! 🏆", m->host_nick);
                        guest_go.status = 2; /* LOSS */
                        snprintf(guest_go.text, sizeof(guest_go.text), "HAI PERSO LA PARTITA contro %s.", m->host_nick);
                    } else {
                        guest_go.status = 1; /* WIN */
                        snprintf(guest_go.text, sizeof(guest_go.text), "COMPLIMENTI %s! HAI VINTO LA PARTITA! 🏆", m->guest_nick);
                        host_go.status = 2; /* LOSS */
                        snprintf(host_go.text, sizeof(host_go.text), "HAI PERSO LA PARTITA contro %s.", m->guest_nick);
                    }

                    send_packet((SOCKET)m->host_socket, &host_go);
                    send_packet((SOCKET)m->guest_socket, &guest_go);

                    char win_nick[MAX_NICKNAME];
                    strncpy(win_nick, (player_num == 1) ? m->host_nick : m->guest_nick, MAX_NICKNAME - 1);
                    int notif_mid = m->match_id;
                    SOCKET notif_ex1 = (SOCKET)m->host_socket;
                    SOCKET notif_ex2 = (SOCKET)m->guest_socket;
                    pthread_mutex_unlock(&m->mutex);

                    pthread_mutex_lock(&server_mutex);
                    for (int ci = 0; ci < MAX_CLIENTS; ci++) {
                        if (clients[ci].in_use && (clients[ci].sock == notif_ex1 || clients[ci].sock == notif_ex2)) {
                            clients[ci].active_match_id = -1;
                        }
                    }
                    pthread_mutex_unlock(&server_mutex);

                    char notif[256];
                    snprintf(notif, sizeof(notif), "[NOTIFICA] La partita #%d si e' conclusa! Vincitore: %s", notif_mid, win_nick);
                    broadcast_notification(notif, notif_ex1, notif_ex2);

                } else if (check_draw(m->grid)) {
                    m->winner = 3;
                    m->state = MATCH_STATE_TERMINATED;

                    Packet grid_pkt;
                    memset(&grid_pkt, 0, sizeof(Packet));
                    grid_pkt.type = MSG_GRID_UPDATE;
                    grid_pkt.match_id = target_id;
                    grid_pkt.turn = 0;
                    memcpy(grid_pkt.grid, m->grid, sizeof(m->grid));
                    snprintf(grid_pkt.text, sizeof(grid_pkt.text), "Partita Terminata in Pareggio!");

                    send_packet((SOCKET)m->host_socket, &grid_pkt);
                    send_packet((SOCKET)m->guest_socket, &grid_pkt);

                    Packet go_pkt;
                    memset(&go_pkt, 0, sizeof(Packet));
                    go_pkt.type = MSG_GAME_OVER;
                    go_pkt.status = 3; /* DRAW */
                    snprintf(go_pkt.text, sizeof(go_pkt.text), "PARTITA FINITA IN PAREGGIO! 🤝");

                    send_packet((SOCKET)m->host_socket, &go_pkt);
                    send_packet((SOCKET)m->guest_socket, &go_pkt);

                    int notif_mid = m->match_id;
                    SOCKET notif_ex1 = (SOCKET)m->host_socket;
                    SOCKET notif_ex2 = (SOCKET)m->guest_socket;
                    pthread_mutex_unlock(&m->mutex);

                    pthread_mutex_lock(&server_mutex);
                    for (int ci = 0; ci < MAX_CLIENTS; ci++) {
                        if (clients[ci].in_use && (clients[ci].sock == notif_ex1 || clients[ci].sock == notif_ex2)) {
                            clients[ci].active_match_id = -1;
                        }
                    }
                    pthread_mutex_unlock(&server_mutex);

                    char notif[256];
                    snprintf(notif, sizeof(notif), "[NOTIFICA] La partita #%d si e' conclusa in PAREGGIO.", notif_mid);
                    broadcast_notification(notif, notif_ex1, notif_ex2);

                } else {
                    /* Switch turn */
                    m->current_turn = (m->current_turn == 1) ? 2 : 1;
                    char turn_nick[MAX_NICKNAME];
                    strncpy(turn_nick, (m->current_turn == 1) ? m->host_nick : m->guest_nick, MAX_NICKNAME - 1);

                    Packet grid_pkt;
                    memset(&grid_pkt, 0, sizeof(Packet));
                    grid_pkt.type = MSG_GRID_UPDATE;
                    grid_pkt.match_id = target_id;
                    grid_pkt.turn = m->current_turn;
                    memcpy(grid_pkt.grid, m->grid, sizeof(m->grid));
                    snprintf(grid_pkt.text, sizeof(grid_pkt.text), "Mossa registrata. Turno di: %s (%c)",
                             turn_nick, (m->current_turn == 1) ? CELL_X : CELL_O);

                    send_packet((SOCKET)m->host_socket, &grid_pkt);
                    send_packet((SOCKET)m->guest_socket, &grid_pkt);
                    pthread_mutex_unlock(&m->mutex);
                }
                break;
            }

            case MSG_POST_GAME_CHOICE: {
                int target_id = rx_pkt.match_id;
                int choice = rx_pkt.status; /* 1 = Rematch/New Session, 0 = Leave */

                pthread_mutex_lock(&server_mutex);
                Match *m = NULL;
                for (int i = 0; i < MAX_MATCHES; i++) {
                    if (matches[i].match_id == target_id && matches[i].state == MATCH_STATE_TERMINATED) {
                        m = &matches[i];
                        break;
                    }
                }

                if (m) {
                    if (m->winner == 1 || m->winner == 2) {
                        int win_sock = (m->winner == 1) ? m->host_socket : m->guest_socket;
                        int lose_sock = (m->winner == 1) ? m->guest_socket : m->host_socket;

                        if ((int)sock == win_sock) {
                            if (choice == 1) {
                                /* Winner chooses to create a new session! */
                                if (m->winner == 2) {
                                    /* Guest won, transfer ownership to Guest! */
                                    m->host_socket = m->guest_socket;
                                    strncpy(m->host_nick, m->guest_nick, MAX_NICKNAME - 1);
                                }
                                m->guest_socket = -1;
                                m->guest_nick[0] = '\0';
                                reset_match(m);
                                m->state = MATCH_STATE_WAITING;

                                Packet ack;
                                memset(&ack, 0, sizeof(Packet));
                                ack.type = MSG_NOTIFICATION;
                                snprintf(ack.text, sizeof(ack.text),
                                         "Hai creato una nuova sessione per la partita #%d! Ora e' in stato IN ATTESA.", m->match_id);
                                send_packet((SOCKET)win_sock, &ack);

                                char notif[256];
                                snprintf(notif, sizeof(notif), "[NOTIFICA] Il vincitore %s ha riaperto la partita #%d (Stato: In Attesa di sfidanti)", m->host_nick, m->match_id);
                                pthread_mutex_unlock(&server_mutex);
                                broadcast_notification(notif, (SOCKET)win_sock, INVALID_SOCKET);
                                pthread_mutex_lock(&server_mutex);
                            } else {
                                /* Winner left match */
                                m->state = MATCH_STATE_EMPTY;
                            }
                        } else if ((int)sock == lose_sock) {
                            /* Loser forced to leave match */
                            cli->active_match_id = -1;
                        }
                    } else if (m->winner == 3) {
                        /* Draw case: both must agree */
                        if ((int)sock == m->host_socket) m->host_rematch_choice = choice;
                        if ((int)sock == m->guest_socket) m->guest_rematch_choice = choice;

                        if (m->host_rematch_choice != -1 && m->guest_rematch_choice != -1) {
                            if (m->host_rematch_choice == 1 && m->guest_rematch_choice == 1) {
                                /* Both agreed to new session! */
                                m->guest_socket = -1;
                                m->guest_nick[0] = '\0';
                                reset_match(m);
                                m->state = MATCH_STATE_WAITING;

                                Packet ack;
                                memset(&ack, 0, sizeof(Packet));
                                ack.type = MSG_NOTIFICATION;
                                snprintf(ack.text, sizeof(ack.text),
                                         "Entrambi avete accettato la rivincita! La partita #%d e' torna in ATTESA.", m->match_id);
                                send_packet((SOCKET)m->host_socket, &ack);

                                char notif[256];
                                snprintf(notif, sizeof(notif), "[NOTIFICA] La partita #%d e' stata riaperta dopo un pareggio (Stato: In Attesa)", m->match_id);
                                pthread_mutex_unlock(&server_mutex);
                                broadcast_notification(notif, (SOCKET)m->host_socket, INVALID_SOCKET);
                                pthread_mutex_lock(&server_mutex);
                            } else {
                                m->state = MATCH_STATE_EMPTY;
                            }
                        }
                    }
                }
                pthread_mutex_unlock(&server_mutex);
                break;
            }

            case MSG_LEAVE_MATCH: {
                int target_id = rx_pkt.match_id;
                pthread_mutex_lock(&server_mutex);

                cli->active_match_id = -1;

                Match *m = NULL;
                for (int i = 0; i < MAX_MATCHES; i++) {
                    if (matches[i].state != MATCH_STATE_EMPTY) {
                        if (target_id > 0 && matches[i].match_id == target_id) {
                            m = &matches[i];
                            break;
                        }
                        if (matches[i].host_socket == (int)sock || matches[i].guest_socket == (int)sock) {
                            m = &matches[i];
                            break;
                        }
                    }
                }

                if (m) {
                    int is_host = (m->host_socket == (int)sock);
                    int other_sock = is_host ? m->guest_socket : m->host_socket;

                    if (m->state == MATCH_STATE_IN_PROGRESS && other_sock != -1) {
                        /* Send final grid update */
                        Packet grid_pkt;
                        memset(&grid_pkt, 0, sizeof(Packet));
                        grid_pkt.type = MSG_GRID_UPDATE;
                        grid_pkt.match_id = m->match_id;
                        grid_pkt.turn = 0;
                        memcpy(grid_pkt.grid, m->grid, sizeof(m->grid));
                        snprintf(grid_pkt.text, sizeof(grid_pkt.text), "Partita Terminata per resa dell'avversario!");
                        send_packet((SOCKET)other_sock, &grid_pkt);

                        /* Send win by forfeit to opponent */
                        Packet forfeit;
                        memset(&forfeit, 0, sizeof(Packet));
                        forfeit.type = MSG_GAME_OVER;
                        forfeit.status = 1; /* Win by forfeit */
                        snprintf(forfeit.text, sizeof(forfeit.text),
                                 "L'avversario %s si e' ARRESO! VITTORIA PER ABBANDONO! 🏆", cli->nickname);
                        send_packet((SOCKET)other_sock, &forfeit);

                        /* Reset opponent's active_match_id */
                        Client *survivor = find_client_by_socket((SOCKET)other_sock);
                        if (survivor) {
                            survivor->active_match_id = -1;
                        }

                        char notif[256];
                        snprintf(notif, sizeof(notif),
                                 "[NOTIFICA] La partita #%d si e' conclusa per resa di %s. Vittoria per %s.",
                                 m->match_id, cli->nickname, is_host ? m->guest_nick : m->host_nick);

                        pthread_mutex_destroy(&m->mutex);
                        pthread_cond_destroy(&m->cond);
                        memset(m, 0, sizeof(Match));
                        m->state = MATCH_STATE_EMPTY;

                        pthread_mutex_unlock(&server_mutex);
                        broadcast_notification(notif, (SOCKET)other_sock, sock);
                        pthread_mutex_lock(&server_mutex);
                    } else if (m->state == MATCH_STATE_WAITING && is_host) {
                        char notif[256];
                        snprintf(notif, sizeof(notif),
                                 "[NOTIFICA] La partita #%d e' stata cancellata da %s.",
                                 m->match_id, cli->nickname);
                        pthread_mutex_destroy(&m->mutex);
                        pthread_cond_destroy(&m->cond);
                        memset(m, 0, sizeof(Match));
                        m->state = MATCH_STATE_EMPTY;
                        pthread_mutex_unlock(&server_mutex);
                        broadcast_notification(notif, sock, INVALID_SOCKET);
                        pthread_mutex_lock(&server_mutex);
                    } else if (m->state == MATCH_STATE_JOIN_PENDING) {
                        if (is_host) {
                            if (other_sock != -1) {
                                Packet cancel_pkt;
                                memset(&cancel_pkt, 0, sizeof(Packet));
                                cancel_pkt.type = MSG_JOIN_RESULT;
                                cancel_pkt.status = 0;
                                snprintf(cancel_pkt.text, sizeof(cancel_pkt.text),
                                         "La partita #%d e' stata annullata dall'host.", m->match_id);
                                send_packet((SOCKET)other_sock, &cancel_pkt);
                            }
                            pthread_mutex_destroy(&m->mutex);
                            pthread_cond_destroy(&m->cond);
                            memset(m, 0, sizeof(Match));
                            m->state = MATCH_STATE_EMPTY;
                        } else {
                            if (other_sock != -1) {
                                Packet notify_pkt;
                                memset(&notify_pkt, 0, sizeof(Packet));
                                notify_pkt.type = MSG_NOTIFICATION;
                                snprintf(notify_pkt.text, sizeof(notify_pkt.text),
                                         "Lo sfidante %s si e' ritirato. La partita #%d torna IN ATTESA.", cli->nickname, m->match_id);
                                send_packet((SOCKET)other_sock, &notify_pkt);
                            }
                            m->guest_socket = -1;
                            m->guest_nick[0] = '\0';
                            m->state = MATCH_STATE_WAITING;
                        }
                    }
                }
                pthread_mutex_unlock(&server_mutex);
                break;
            }

            case MSG_DISCONNECT:
                goto cleanup;

            default:
                break;
        }
    }

cleanup:
    pthread_mutex_lock(&server_mutex);
    printf("[SERVER] Cleanup per client '%s' (socket %d)...\n", cli->nickname, (int)sock);
    cli->in_use = 0;
    cli->sock = INVALID_SOCKET;
    cli->active_match_id = -1;

    /* ================================================================
     * Gestione robusta disconnessione: ogni stato della partita
     * ================================================================ */
    for (int i = 0; i < MAX_MATCHES; i++) {
        Match *m = &matches[i];
        if (m->state == MATCH_STATE_EMPTY) continue;

        int is_host  = (m->host_socket  == (int)sock);
        int is_guest = (m->guest_socket == (int)sock);
        if (!is_host && !is_guest) continue;

        int other_sock = is_host ? m->guest_socket : m->host_socket;
        char match_notif[256];

        switch (m->state) {

        case MATCH_STATE_WAITING: {
            /* Host disconnesso mentre attendeva uno sfidante:
             * distrugge la partita, nessun guest da notificare */
            int dead_mid = m->match_id;
            snprintf(match_notif, sizeof(match_notif),
                     "[NOTIFICA] La partita #%d (creata da %s) e' stata annullata per disconnessione dell'host.",
                     m->match_id, m->host_nick);
            pthread_mutex_destroy(&m->mutex);
            pthread_cond_destroy(&m->cond);
            memset(m, 0, sizeof(Match));
            m->state = MATCH_STATE_EMPTY;
            printf("[SERVER] Partita #%d distrutta (host disconnesso in WAITING).\n", dead_mid);
            pthread_mutex_unlock(&server_mutex);
            broadcast_notification(match_notif, sock, INVALID_SOCKET);
            pthread_mutex_lock(&server_mutex);
            break;
        }

        case MATCH_STATE_JOIN_PENDING:
            if (is_host) {
                /* Host disconnesso: notifica al guest che la partita e' annullata */
                if (other_sock != -1) {
                    Packet cancel_pkt;
                    memset(&cancel_pkt, 0, sizeof(Packet));
                    cancel_pkt.type   = MSG_JOIN_RESULT;
                    cancel_pkt.status = 0;
                    cancel_pkt.match_id = m->match_id;
                    snprintf(cancel_pkt.text, sizeof(cancel_pkt.text),
                             "La partita #%d e' stata annullata: l'host %s si e' disconnesso.",
                             m->match_id, m->host_nick);
                    send_packet((SOCKET)other_sock, &cancel_pkt);
                }
                snprintf(match_notif, sizeof(match_notif),
                         "[NOTIFICA] La partita #%d e' stata annullata (host %s disconnesso).",
                         m->match_id, m->host_nick);
            } else {
                /* Guest disconnesso: notifica all'host, rimetti in WAITING */
                if (other_sock != -1) {
                    Packet notify_pkt;
                    memset(&notify_pkt, 0, sizeof(Packet));
                    notify_pkt.type = MSG_NOTIFICATION;
                    snprintf(notify_pkt.text, sizeof(notify_pkt.text),
                             "Lo sfidante %s si e' disconnesso. La tua partita #%d torna IN ATTESA.",
                             m->guest_nick, m->match_id);
                    send_packet((SOCKET)other_sock, &notify_pkt);
                }
                /* Reset guest info, match goes back to WAITING */
                m->guest_socket  = -1;
                m->guest_nick[0] = '\0';
                m->state = MATCH_STATE_WAITING;
                snprintf(match_notif, sizeof(match_notif),
                         "[NOTIFICA] Lo sfidante della partita #%d si e' disconnesso. Partita torna in attesa.",
                         m->match_id);
                printf("[SERVER] Partita #%d: guest disconnesso in JOIN_PENDING, torna in WAITING.\n", m->match_id);
                pthread_mutex_unlock(&server_mutex);
                broadcast_notification(match_notif, (SOCKET)other_sock, sock);
                pthread_mutex_lock(&server_mutex);
                continue; /* Don't destroy the match */
            }
            /* Host disconnected: destroy match */
            pthread_mutex_destroy(&m->mutex);
            pthread_cond_destroy(&m->cond);
            memset(m, 0, sizeof(Match));
            m->state = MATCH_STATE_EMPTY;
            pthread_mutex_unlock(&server_mutex);
            broadcast_notification(match_notif, sock, INVALID_SOCKET);
            pthread_mutex_lock(&server_mutex);
            break;

        case MATCH_STATE_IN_PROGRESS:
            /* Partita in corso: l'avversario vince per abbandono */
            if (other_sock != -1) {
                /* Send final grid to surviving player */
                Packet grid_pkt;
                memset(&grid_pkt, 0, sizeof(Packet));
                grid_pkt.type = MSG_GRID_UPDATE;
                grid_pkt.match_id = m->match_id;
                grid_pkt.turn = 0;
                memcpy(grid_pkt.grid, m->grid, sizeof(m->grid));
                snprintf(grid_pkt.text, sizeof(grid_pkt.text), "Partita Terminata per disconnessione avversario!");
                send_packet((SOCKET)other_sock, &grid_pkt);

                Packet forfeit;
                memset(&forfeit, 0, sizeof(Packet));
                forfeit.type = MSG_GAME_OVER;
                forfeit.status = 1; /* Win by forfeit */
                snprintf(forfeit.text, sizeof(forfeit.text),
                         "L'avversario si e' disconnesso. VITTORIA PER ABBANDONO! 🏆");
                send_packet((SOCKET)other_sock, &forfeit);

                /* CRITICAL: Reset active_match_id for the surviving player */
                Client *survivor = find_client_by_socket((SOCKET)other_sock);
                if (survivor) {
                    survivor->active_match_id = -1;
                }
            }
            snprintf(match_notif, sizeof(match_notif),
                     "[NOTIFICA] La partita #%d si e' conclusa: %s si e' disconnesso. Vittoria a tavolino per l'avversario.",
                     m->match_id, is_host ? m->host_nick : m->guest_nick);
            printf("[SERVER] Partita #%d: forfeit per disconnessione di %s.\n",
                   m->match_id, is_host ? m->host_nick : m->guest_nick);
            pthread_mutex_destroy(&m->mutex);
            pthread_cond_destroy(&m->cond);
            memset(m, 0, sizeof(Match));
            m->state = MATCH_STATE_EMPTY;
            pthread_mutex_unlock(&server_mutex);
            broadcast_notification(match_notif, (SOCKET)other_sock, sock);
            pthread_mutex_lock(&server_mutex);
            break;

        case MATCH_STATE_TERMINATED:
            /* Partita gia' conclusa: cleanup risorse */
            if (other_sock != -1) {
                Client *other_cli = find_client_by_socket((SOCKET)other_sock);
                if (other_cli && other_cli->active_match_id == m->match_id) {
                    other_cli->active_match_id = -1;
                }
            }
            pthread_mutex_destroy(&m->mutex);
            pthread_cond_destroy(&m->cond);
            memset(m, 0, sizeof(Match));
            m->state = MATCH_STATE_EMPTY;
            break;

        default:
            break;
        }
    }
    pthread_mutex_unlock(&server_mutex);

    close_socket(sock);
    printf("[SERVER] Socket %d cleaned up completamente.\n", (int)sock);
    return NULL;
}

int main(int argc, char *argv[]) {
    setvbuf(stdout, NULL, _IONBF, 0);
    int port = DEFAULT_PORT;
    if (argc > 1) {
        port = atoi(argv[1]);
    }

#ifndef _WIN32
    /* Ignore SIGPIPE signal on Linux/macOS when a client disconnects abruptly */
    signal(SIGPIPE, SIG_IGN);
#else
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        printf("[ERRORE] Winsock initialization failed.\n");
        return 1;
    }
#endif

    SOCKET server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock == INVALID_SOCKET) {
        perror("[ERRORE] Socket creation failed");
        return 1;
    }

    int opt = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        perror("[ERRORE] Bind failed");
        close_socket(server_sock);
        return 1;
    }

    if (listen(server_sock, 10) == SOCKET_ERROR) {
        perror("[ERRORE] Listen failed");
        close_socket(server_sock);
        return 1;
    }

    printf("====================================================\n");
    printf("     SERVER FORZA 4 MULTI-CLIENT AVVIATO\n");
    printf("     Porta: %d\n", port);
    printf("====================================================\n");

    for (int i = 0; i < MAX_CLIENTS; i++) {
        clients[i].in_use = 0;
        clients[i].sock = INVALID_SOCKET;
        clients[i].active_match_id = -1;
    }

    for (int i = 0; i < MAX_MATCHES; i++) {
        matches[i].state = MATCH_STATE_EMPTY;
    }

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        SOCKET client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &addr_len);

        if (client_sock == INVALID_SOCKET) {
            continue;
        }

        /* Enable TCP keepalive for faster dead connection detection */
        {
            int keepalive = 1;
            setsockopt(client_sock, SOL_SOCKET, SO_KEEPALIVE, (const char *)&keepalive, sizeof(keepalive));
#ifndef _WIN32
            int idle = 10;   /* Start probing after 10s of inactivity */
            int interval = 5; /* Probe every 5s */
            int count = 3;    /* Give up after 3 failed probes */
            setsockopt(client_sock, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(idle));
            setsockopt(client_sock, IPPROTO_TCP, TCP_KEEPINTVL, &interval, sizeof(interval));
            setsockopt(client_sock, IPPROTO_TCP, TCP_KEEPCNT, &count, sizeof(count));
#endif
        }

        pthread_mutex_lock(&server_mutex);
        int slot = -1;
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (!clients[i].in_use) {
                slot = i;
                break;
            }
        }

        if (slot != -1) {
            clients[slot].in_use = 1;
            clients[slot].sock = client_sock;
            clients[slot].active_match_id = -1;
            pthread_create(&clients[slot].thread, NULL, client_handler, &clients[slot]);
            pthread_detach(clients[slot].thread);
        } else {
            printf("[SERVER] Max clients reached. Closing incoming connection.\n");
            close_socket(client_sock);
        }
        pthread_mutex_unlock(&server_mutex);
    }

    close_socket(server_sock);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
