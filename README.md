# Forza 4 - Client/Server Multi-Player in C con Docker-Compose
**Progetto di Laboratorio di Sistemi Operativi (LSO)**  
*Corso di Laurea in Informatica*

---

## 📌 Descrizione del Progetto

Il progetto consiste nello sviluppo in linguaggio **C** di un sistema **Client-Server multi-thread** per giocare a **Forza 4** in rete tramite **Socket TCP**.

Il progetto è sviluppato seguendo lo standard POSIX C99 ed è gestito mediante **Docker** e **Docker-Compose** per la containerizzazione e l'orchestrazione automatica del server e dei client.

---

## 🛠️ Struttura del Progetto

```
.
├── server/                      # 📁 LATO SERVER
│   ├── protocol.h               # Definizione del protocollo di rete e messaggi
│   ├── thread_compat.h          # Astrazione thread cross-platform
│   ├── game.h                   # Struttura Match e prototipi della logica
│   ├── game.c                   # Logica di Forza 4 (griglia 6x7, vincita/pareggio)
│   ├── server.c                 # Server TCP multi-thread con gestione partite e lobby
│   └── Dockerfile               # Dockerfile per il container del Server
│
├── client/                      # 📁 LATO CLIENT
│   ├── protocol.h               # Definizione del protocollo di rete e messaggi
│   ├── thread_compat.h          # Astrazione thread cross-platform
│   ├── ui.h                     # Prototipi per la grafica ANSI da terminale
│   ├── ui.c                     # Rendering della griglia con colori ed elenchi
│   ├── client.c                 # Client TCP interattivo con thread ricevitore asincrono
│   └── Dockerfile               # Dockerfile per i container dei Client
│
├── docker-compose.yml           # 🐳 Orchestrazione Docker Compose per Server e Client
├── Makefile                     # Makefile principale per la compilazione locale
├── README.md                    # Istruzioni di esecuzione ed utilizzo
└── Documentazione_Progetto_LSO.md # Relazione tecnica sulle scelte implementative
```

---

## 🐳 Esecuzione tramite Docker-Compose (Consigliato)

### 1. Avvio del Server Containerizzato
Per compilare l'immagine ed avviare il container del **Server Forza 4** sulla porta **8888**:
```bash
docker compose up --build -d server
```

Per verificare i log del server in esecuzione:
```bash
docker compose logs -f server
```

Per arrestare il server:
```bash
docker compose down
```

---

### 2. Avvio dei Client tramite Docker
È possibile avviare client interattivi containerizzati in terminali distinti che si collegano automaticamente al server:

**Terminale 1 (Giocatore 1):**
```bash
docker compose run --rm client1
```

**Terminale 2 (Giocatore 2):**
```bash
docker compose run --rm client2
```

---

## 💻 Esecuzione Locale (Senza Docker)

È anche possibile compilare ed eseguire il progetto direttamente in locale senza Docker:

```bash
# Compilazione (Linux, macOS, Windows MinGW)
make

# Avvio del Server in locale
./server/server 8888          # (su Windows: server\server.exe 8888)

# Avvio dei Client in locale
./client/client 127.0.0.1 8888  # (su Windows: client\client.exe 127.0.0.1 8888)
```
