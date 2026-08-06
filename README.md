# Forza 4 

**Progetto di Laboratorio di Sistemi Operativi (LSO)**  
*Corso di Laurea in Informatica*
Andrea Barattolo matricola n86004583
---

## 📌 Descrizione del Progetto

Il progetto implementa un sistema **Client-Server multi-utente in linguaggio C** per il gioco del **Forza 4** in rete tramite **Socket TCP**.

L'architettura del Server è **concorrente multi-thread**, in grado di gestire contemporaneamente molteplici client, consentendo loro di interagire in una **Lobby**, consultare la lista delle partite disponibili, creare nuove partite, richiedere di unificarsi a partite esistenti e giocare in tempo reale con interfaccia grafica da terminale ad alta leggibilità (colori ANSI e simboli Unicode).

Il sistema è sviluppato in conformità con lo standard **POSIX C99**, ed è dotato di supporto nativo sia per l'esecuzione diretta su **Linux, macOS e Windows** sia per l'orchestrazione containerizzata mediante **Docker** e **Docker-Compose**.

---

## 🛠️ Struttura del Progetto

```
.
├── server/
│   ├── protocol.h               # Definizione del protocollo di rete e messaggi a dimensione fissa
│   ├── thread_compat.h          # Astrazione POSIX/Win32 thread cross-platform
│   ├── game.h                   # Definizioni strutture dati di gioco e prototipi
│   ├── game.c                   # Logica di gioco (griglia 6x7, gravità pedine, algoritmo vincita O(1))
│   ├── server.c                 # Server TCP multi-thread, gestione sincronizzata lobby e partite
│   └── Dockerfile               # Dockerfile multi-stage per il container Server
│
├── client/
│   ├── protocol.h               # Definizione condivisa del protocollo di rete
│   ├── thread_compat.h          # Astrazione thread cross-platform
│   ├── ui.h                     # Prototipi interfaccia utente ANSI
│   ├── ui.c                     # Rendering grafico della griglia, colori ed elenchi
│   ├── client.c                 # Client TCP con loop di I/O multiplexato (select)
│   └── Dockerfile               # Dockerfile multi-stage per i container Client
│
├── docker-compose.yml           # Orchestrazione Docker Compose per Server e Client
├── Makefile                     # Makefile principale per la compilazione locale automatica
├── README.md                    # Istruzioni d'uso ed esecuzione del progetto
└── Documentazione_Progetto_LSO.md # Relazione tecnica dettagliata sulle scelte implementative
```

---

## 🚀 Istruzioni di Esecuzione

### 🐳 Esecuzione tramite Docker-Compose 

Docker-Compose permette di compilare ed avviare il Server ed i Client in ambienti totalmente isolati e riproducibili.

#### 1. Avvio del Server
Dalla radice del progetto, eseguire:
```bash
docker compose up --build -d server
```

Per monitorare i log del server in tempo reale:
```bash
docker compose logs -f server
```

#### 2. Avvio dei Client Interattivi
È possibile avviare client containerizzati in terminali distinti che si collegano automaticamente al server sulla rete Docker interna:

- **Terminale 1 (Giocatore 1 / Host):**
  ```bash
  docker compose run --rm client
  ```

- **Terminale 2 (Giocatore 2 / Guest):**
  ```bash
  docker compose run --rm client
  ```

- **Terminale 3 (Eventuale terzo giocatore):**
  ```bash
  docker compose run --rm client
  ```

#### 3. Arresto del Sistema
Per terminare ed eliminare i container attivi:
```bash
docker compose down
```

---
Per pulire i file binari generati:
```bash
make clean
```

---

## 🎮 Guida al Gioco ed Funzionalità

### 1. Registrazione Nickname
All'avvio, il client richiede l'inserimento di un **nickname** che lo identificherà univocamente nella lobby e durante le partite.

### 2. Menu Lobby Principale
Dalla lobby principale l'utente può scegliere tra le seguenti opzioni:
1. **[1] Crea Nuova Partita**: Registra una nuova partita sul server che entra in stato *In Attesa* di sfidanti.
2. **[2] Lista Partite**: Mostra l'elenco di tutte le partite presenti sul server con relativo ID, Host, Guest e Stato (*In Attesa*, *In Corso*, *Terminata*).
3. **[3] Unisciti a una Partita**: Richiede l'inserimento dell'ID partita. La richiesta viene inviata all'Host della partita per accettazione o rifiuto.
4. **[4] Esci**: Invia al server il messaggio di disconnessione ed effettua la chiusura pulita delle risorse socket.

### 3. Fase di Sfida e Accettazione
- Quando un client si unisce a una partita, l'Host riceve un prompt interattivo (`1=ACCETTA`, `0=RIFIUTA`).
- Se l'Host **accetta**, la partita entra in stato *In Corso* e viene assegnato il simbolo **🔴 (X - Rosso)** all'Host e **🟡 (O - Giallo)** al Guest.
- Se l'Host **rifiuta**, il richiedente riceve la notifica e viene riportato in lobby, mentre la partita rimane aperta ad altri sfidanti.

### 4. Svolgimento del Gioco (Forza 4)
- La griglia di gioco è di dimensione **6 righe x 7 colonne**.
- Durante il proprio turno, il giocatore inserisce l'indice della colonna desiderata (**da 1 a 7**). La pedina scende per gravità nella prima cella libera.
- **Opzione Resa immediata**: In qualsiasi momento durante il proprio turno o durante il turno avversario, digitando il comando **`00`**, il giocatore si arrende immediatamente. L'avversario ottiene la vittoria per abbandono e la partita viene conclusa in modo sicuro.

### 5. Fine Partita e Rivincita
Al termine della partita (vittoria, sconfitta o pareggio):
- **In caso di Vittoria/Sconfitta**: Il **Vincitore** ha la facoltà di aprire una nuova sessione per quella partita (diventando l'Host della nuova sessione anche se prima era Guest). Il **Perdente** viene riportato alla lobby.
- **In caso di Pareggio**: Entrambi i giocatori devono esprimere la volontà di effettuare una rivincita. Se entrambi accettano, la partita torna in stato *In Attesa*.

---

## ⚡ Caratteristiche Tecniche Principali

- **Thread-per-Client Concorrente**: Ogni client connesso è gestito da un thread dedicato sul server.
- **Sincronizzazione Gerarchica**: Uso di mutex globali e mutex per-singola-partita per eliminare race condition e deadlock.
- **Multiplexing I/O Lato Client**: Utilizzo di `select()` sul client per gestire l'input dell'utente e i pacchetti di rete in ingresso senza blocchi della console.
- **Rilevamento Disconnessioni ed Anomalia**: Impiego di TCP Keepalive ed invio di messaggi `MSG_DISCONNECT` intercettati tramite gestori di segnale del sistema operativo (`SIGINT`/`CTRL+C`).
