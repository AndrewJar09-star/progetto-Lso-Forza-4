# Documentazione Tecnica del Progetto: Server Multi-Client Forza 4
**Corso di Laurea in Informatica - Laboratorio di Sistemi Operativi (LSO)**  
**Autore:** Progetto LSO  

---

## 1. Introduzione e Requisiti di Progetto

Il presente documento descrive le scelte architetturali, progettuali ed implementative adottate nello sviluppo del sistema Client-Server multi-utente per il gioco del **Forza 4**, realizzato nell'ambito del corso di Laboratorio di Sistemi Operativi.

### 1.1 Requisiti Generali
- **Linguaggio di Programmazione**: C (Standard C99).
- **Containerizzazione ed Orchestrazione**: Sviluppato e gestito mediante **Docker** e **Docker-Compose**.
- **Compatibilità Multi-Piattaforma**: Pienamente compatibile ed eseguibile su **Linux**, **macOS** e **Windows**.
- **Comunicazione di Rete**: Socket stream TCP (no WebSocket), con supporto per comunicazioni sia in locale (loopback) sia su rete distribuita/containerizzata.
- **Gestione della Concorrenza**: Architettura Server multi-thread per la gestione parallela dei client connessi, mediante l'uso della libreria thread POSIX (`pthread`) e meccanismi di sincronizzazione.
- **Gestione delle Partite**:
  - Ogni giocatore connesso può registrare un nickname ed accedere ad una lobby.
  - È possibile creare una o più partite; un client può tuttavia essere attivo in **una sola partita alla volta**.
  - Ogni partita è identificata in modo univoco ed evolve attraverso uno specifico automa a stati (*Attesa*, *Richiesta di partecipazione*, *In Corso*, *Terminata*).
  - Meccanismo di accettazione/rifiuto da parte del creatore (Host) alla richiesta di unione di uno sfidante (Guest).
  - Notifiche asincrone verso i client non partecipanti riguardanti lo stato delle partite sul server.
  - Esiti personalizzati ed opzioni di riapertura sessione/rivincita al termine del gioco (vincitore proprietario della nuova sessione, perdente obbligato ad uscire, gestione congiunta nel pareggio).

---

## 2. Architettura di Sistema e Containerizzazione Docker

L'architettura del sistema si basa sul modello **Client-Server di tipo concorrente** orchestrato tramite **Docker-Compose**.

```
                   ┌───────────────────────────────────────────────┐
                   │               DOCKER-COMPOSE                  │
                   │                                               │
                   │          ┌─────────────────────────┐          │
                   │          │   CONTAINER SERVER      │          │
                   │          │    (Porta 8888/TCP)     │          │
                   │          └────────────┬────────────┘          │
                   │                       │                       │
                   │      ┌────────────────┴────────────────┐      │
                   │      │                                 │      │
                   │  ┌───▼────────────┐        ┌───────────▼───┐  │
                   │  │CONTAINER CLIENT1│        │CONTAINER CLIENT2│  │
                   │  │   (Giocatore 1) │        │  (Giocatore 2) │  │
                   │  └────────────────┘        └───────────────┘  │
                   └───────────────────────────────────────────────┘
```

### 2.1 Configurazione Dockerfile e Docker-Compose
- **`server/Dockerfile`**: Utilizza una **Multi-Stage Build** basata sull'immagine ufficiale **`ubuntu:latest`**. Nella prima fase (`builder`) installa **GCC** (`gcc` e `libc6-dev`) con opzione `--no-install-recommends` per compilare i sorgenti C del server; nella seconda fase copia il binario su un'immagine pulita Ubuntu per l'esecuzione. Espone la porta TCP `8888`.
- **`client/Dockerfile`**: Utilizza anch'esso una **Multi-Stage Build** basata su **`ubuntu:latest`** e **GCC**, configurando l'ambiente terminale ANSI a colori (`TERM=xterm-256color`).
- **`docker-compose.yml`**: Definisce il servizio `server` e gli ambienti interattivi `client1` e `client2`, gestendo l'inoltro delle porte (`8888:8888`), la rete interna tra container e la dipendenza di avvio (`depends_on`).

---

## 3. Protocollo di Comunicazione e Strutture Dati

La comunicazione tra Client e Server avviene scambiando **pacchetti strutturati a dimensione fissa** (`Packet`) su socket TCP.

### 3.1 Definizione della Struttura Pacchetto (`Packet`)
```c
typedef struct {
    int32_t type;              /* Tipologia del messaggio (enum MessageType) */
    int32_t match_id;          /* Identificativo della partita */
    int32_t player_id;         /* ID identificativo o descrittore socket */
    int32_t status;            /* Codice di stato / esito / risposta */
    int32_t turn;              /* Turno corrente (1 = Host/X, 2 = Guest/O) */
    int32_t col;               /* Colonna selezionata per la mossa (0-6) */
    char sender_nick[32];      /* Nickname del mittente */
    char text[256];            /* Testo descrittivo o notifica */
    char grid[6][7];           /* Matrice dello stato della griglia di gioco */
} Packet;
```

---

## 4. Automa a Stati delle Partite (Lifecycle di Forza 4)

Ogni partita creata all'interno del server segue un ciclo di vita a stati finiti: `WAITING`, `JOIN_PENDING`, `IN_PROGRESS` e `TERMINATED`.

---

## 5. Gestione della Concorrenza e Thread Synchronization

- **Mutex Globale Server (`server_mutex`)**: Protegge l'accesso in lettura/scrittura alla tabella dei client ed all'array dei match.
- **Mutex Singola Partita (`match->mutex`)**: Garantisce l'esecuzione atomica delle mosse e dell'aggiornamento della griglia.

---

## 6. Logica di Gioco ed Algoritmi di Verifica

Griglia 6 righe x 7 colonne. Algoritmo di inserimento pedina a gravità e verifica vittoria a complessità $O(1)$ per 4 pedine contigue (orizzontale, verticale, diagonale discendente `\`, diagonale ascendente `/`).

---

## 7. Interfaccia Utente e Gestione degli Eventi Asincroni

Resa grafica a colori con sequenze di escape ANSI: **Rosso Brillante (`X`)** e **Giallo Brillante (`O`)**. Thread ricevitore asincrono lato client (`receiver_thread`).

---

## 8. Gestione della Portabilità e Containerizzazione

- **Docker-Compose**: Permette di distribuire ed eseguire l'intero sistema in modo isolato e riproducibile su qualsiasi piattaforma (Linux, macOS, Windows) con un singolo comando `docker compose up`.
- **POSIX C99**: Codice sorgente C conforme agli standard POSIX.

---

## 9. Gestione degli Errori e Resilienza

Gestione disconnessioni improvvise, segnali `SIGPIPE` ignorati e vittoria per abbandono.

---

## 10. Test di Validazione e Conclusioni

Testato sia in ambiente containerizzato Docker che in esecuzione nativa multi-piattaforma. Rispetta integralmente tutti i requisiti specificati nella traccia d'esame.
