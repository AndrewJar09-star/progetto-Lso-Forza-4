# Documentazione Tecnica e Relazione di Progetto: Server Multi-Client Forza 4

**Corso di Laurea in Informatica - Laboratorio di Sistemi Operativi (LSO)**  
**Autore:** Andrea Barattolo matricola n86004583  

---

## 1. Introduzione ed Inquadramento del Progetto

Il presente documento descrive in modo approfondito le **scelte architetturali, progettuali ed implementative** adottate nello sviluppo del sistema Client-Server multi-utente per il gioco del **Forza 4**, realizzato nell'ambito del corso di Laboratorio di Sistemi Operativi.

### 1.1 Finalità ed Obiettivi Progettuali
L'obiettivo del progetto è la realizzazione di una piattaforma di gioco in rete che consenta a molteplici utenti di connettersi simultaneamente ad un server centrale, interagire all'interno di una lobby virtuale, consultare ed amministrare partite, sfidare altri giocatori e disputare incontri di Forza 4 in tempo reale da terminale.

I requisiti chiave che hanno guidato l'intero processo di progettazione sono:
- **Concorrenza e Parallelismo**: Capacità del server di gestire molteplici client ed usufruire di risorse multi-core tramite threading parallelizzato.
- **Correttezza e Sincronizzazione**: Totale assenza di race condition, starvation e deadlock nella manipolazione dello stato globale e delle singole partite.
- **Portabilità Cross-Platform**: Trasparenza esecutiva ed assoluta parità di comportamento tra ambienti Linux, macOS e Windows.
- **Resilienza e Tolleranza ai Guasti**: Gestione robusta di disconnessioni improvvise, cadute di rete, segnali di terminazione anomala e ripristino delle risorse di memoria.
- **Containerizzazione e Riproducibilità**: Integrazione completa con Docker e Docker-Compose per la compilazione, la distribuzione e l'esecuzione isolata del sistema.

---

## 2. Architettura di Rete e Protocollo di Comunicazione

### 2.1 Scelta del Modello di Comunicazione: Socket TCP Stream
Per l'interazione tra Client e Server si è adottato lo standard delle **Socket Stream TCP** (`SOCK_STREAM` sulla famiglia di protocolli `AF_INET`). La scelta di preferire TCP rispetto ad altre soluzioni (come UDP o WebSocket) è giustificata dalle seguenti motivazioni di progettazione:
- **Affidabilità e Garanzia di Consegna**: Il gioco del Forza 4 richiede la certezza assoluta dell'arrivo delle mosse e dei cambi di stato della partita. La perdita anche di un solo pacchetto comprometterebbe la sincronizzazione della griglia di gioco.
- **Ordinamento dei Messaggi (In-Order Delivery)**: La sequenzialità dei messaggi (es. invio mossa -> conferma -> aggiornamento griglia) è garantita nativamente dal controllo di flusso di TCP, eliminando la necessità di implementare complessi meccanismi di acknowledment e re-invio a livello applicativo.
- **Assenza di Middleware Dipendenti**: L'uso delle socket native POSIX evita l'introduzione di dipendenze esterne pesanti (es. librerie per WebSocket o HTTP server embedded), mantenendo il binario estremamente snello e portabile.

### 2.2 Progettazione del Pacchetto a Dimensione Fissa (`Packet`)
Nel determinare la struttura dei dati scambiati in rete, si è scelto di adottare una **struttura binaria fissa a 2048 byte** (`Packet`) anziché formati di serializzazione testuali o dinamici (come JSON, XML o Protocol Buffers).

#### Motivazioni e Trade-off:
1. **Azzeramento del Costo di Parsing e Memory Allocation**: L'adozione di un pacchetto fisso consente di effettuare la lettura e la scrittura diretta della memoria tramite le chiamate di sistema `recv()` e `send()`. Non è richiesta alcuna allocazione dinamica della memoria (`malloc`/`free`) né alcun parsing di stringhe per ogni messaggio scambiato.
2. **Semplificazione dell'I/O su Socket**: Essendo la dimensione del pacchetto nota a priori e costante (`sizeof(Packet)`), le routine di trasmissione e ricezione possono verificare con esattezza il completamento del trasferimento.
3. **Analisi del Trade-off di Banda**: Sebbene un pacchetto di 2KB introduca un leggero overhead di banda per messaggi brevi (es. un semplice ACK), la frequenza degli scambi in un gioco a turni da terminale rende questo impatto del tutto trascurabile rispetto all'enorme guadagno in termini di semplicità, stabilità della memoria ed efficienza CPU.

### 2.3 Strategia di I/O Affidabile: Loop di Trasmissione e Ricezione Esatta
Poiché il protocollo TCP è orientato al flusso di byte (*byte-stream*) e non garantisce che una singola chiamata `send()` invii l'intero blocco di dati o che una `recv()` legga l'intero pacchetto in un'unica operazione, sono state progettate le funzioni helper `send_packet` e `recv_packet`.

Tali funzioni implementano un ciclo di lettura/scrittura che itera fino al completo trasferimento dell'esatto numero di byte richiesti (`sizeof(Packet)`). Questo approccio garantisce l'immunità totale contro il fenomeno della frammentazione dei pacchetti TCP.

### 2.4 Rilevamento Tempestivo dei Client Disconnessi: TCP Keepalive
Per far fronte al problema dei client che perdono improvvisamente la connessione di rete senza inviare il pacchetto di chiusura (es. spegnimento del PC, caduta del Wi-Fi), a livello di socket server è stata abilitata l'opzione `SO_KEEPALIVE` combinata con parametri di probing personalizzati (`TCP_KEEPIDLE`, `TCP_KEEPINTVL`, `TCP_KEEPCNT`). Ciò consente al kernel del server di rilevare le connessioni "fantasma" entro pochi secondi ed avviare automaticamente la bonifica delle risorse senza rimanere bloccato a tempo indeterminato in chiamate di ricezione.

---

## 3. Architettura Concorrente del Server e Modello di Threading

### 3.1 Scelta del Modello: Thread-per-Client Concorrente
L'architettura del Server si basa sul modello **Thread-per-Client**, implementato mediante la libreria **POSIX Threads (`pthread`)**. All'atto dell'accettazione di una nuova connessione da parte del socket principale in ascolto (`accept`), il server alloca uno slot nella tabella dei client ed avvia un thread dedicato (`client_handler`) per gestire l'intero ciclo di vita della sessione dell'utente.

#### Ragioni della Scelta rispetto al Modello Event-Driven (Monothread select/epoll):
- **Isolamento del Ciclo di Vita del Client**: Ogni thread esegue un loop sequenziale di lettura/elaborazione/risposta per il singolo client. Ciò rende la logica di gestione dei singoli pacchetti estremamente pulita e lineare.
- **Sfruttamento dei Processori Multi-Core**: In presenza di decine di client connessi, i thread possono essere schedulati in parallelo su core CPU differenti da parte del sistema operativo, massimizzando il throughput del server.
- **Semplicità di Sincronizzazione**: La concorrenza tra i thread viene disciplinata tramite costrutti di sincronizzazione ben definiti (Mutex), evitando la complessità di una macchina a stati globale monothread.

### 3.2 Gerarchia di Sincronizzazione ed Evitamento dei Deadlock
La presenza di molteplici thread che accedono a risorse condivise (l'elenco dei client connessi, la lista delle partite e lo stato delle singole griglie) ha richiesto una rigorosa strategia di **locking gerarchico**.

1. **Mutex Globale di Server (`server_mutex`)**:
   - Protegge l'accesso in lettura e scrittura all'array globale dei client, all'array globale delle partite, al contatore progressivo degli ID partita ed alle operazioni di notifiche in broadcast.
   - Viene acquisito prima di effettuare qualsiasi modifica allo stato delle partite o prima di scorrere la lista dei client per inviare notifiche.

2. **Mutex di Singola Partita (`match->mutex`)**:
   - Ogni struttura `Match` possiede un mutex dedicato.
   - Protegge lo stato interno della specifica partita durante la validazione e l'esecuzione di una mossa, l'aggiornamento della griglia ed il calcolo dell'esito di vittoria o pareggio.

#### Regola di Prevenzione Deadlock:
Per prevenire condizioni di stallo d'attesa circolare (Deadlock), è stata stabilita una rigida gerarchia di acquisizione:
- Un thread non deve mai acquisire il mutex di una partita ed in seguito richiedere il `server_mutex`.
- Se un thread ha necessità di accedere sia allo stato globale sia alla partita, deve acquisire prima il `server_mutex`, estrarre il puntatore alla partita o apportare le modifiche globali, e rilasciare il `server_mutex` prima di bloccare il mutex locale della partita (oppure acquisirli in ordine rigorosamente discendente).

---

## 4. Automa a Stati delle Partite e Gestione della Concorrenza Avanzata

### 4.1 Definizione del Ciclo di Vita delle Partite
Ogni partita gestita dal server si evolve secondo una macchina a stati finiti con 5 stati distinti:

1. **`MATCH_STATE_EMPTY`**: Slot libero nell'array delle partite, disponibile per nuove creazioni.
2. **`MATCH_STATE_WAITING`**: Partita creata dall'Host, in attesa che un altro giocatore (Guest) invii una richiesta di sfida.
3. **`MATCH_STATE_JOIN_PENDING`**: Un Guest ha inviato la richiesta di unione; la partita è congelata in attesa della decisione dell'Host (accettazione o rifiuto).
4. **`MATCH_STATE_IN_PROGRESS`**: La sfida è stata accettata. La griglia è attiva ed i due giocatori si alternano nell'effettuare le mosse.
5. **`MATCH_STATE_TERMINATED`**: La partita si è conclusa (per vittoria, pareggio o resa). I giocatori stanno valutando l'opzione di rivincita o uscita.

### 4.2 Enforcement del Vincolo: "Singola Partita Attiva per Utente"
La specifica di progetto richiede che ogni giocatore possa creare più partite ma possa **giocare attivamente solo a una partita alla volta**.

Per garantire questo vincolo, ogni struttura `Client` nel server traccia il campo `active_match_id` (impostato a `-1` quando l'utente è libero in lobby).
- Se un utente tenta di creare una nuova partita o di uniti a una partita esistente mentre il suo `active_match_id` è diverso da `-1`, il server blocca la richiesta sul nascere inviando un messaggio di errore esplicito.
- Quando una partita passa in stato `IN_PROGRESS`, l'ID della partita viene assegnato contemporaneamente al campo `active_match_id` sia dell'Host che del Guest.
- Al termine della partita o in caso di resa/disconnessione, il campo viene resettato a `-1`, ripristinando la piena libertà dell'utente nella lobby.

### 4.3 Mitigazione delle Race Condition nei Flussi di Sfida

Durante la fase di negoziazione delle partite (`JOIN_REQ` e `JOIN_RESP`), si possono verificare scenari di race condition che sono stati brillantemente risolti a livello implementativo:

#### Scenari gestiti:
- **Disconnessione del Guest durante la decisione dell'Host**: Se il Guest si disconnette mentre l'Host sta valutando il prompt di accettazione, quando l'Host risponde `1 (Accetta)`, il server verifica l'esistenza ed il tracciamento del socket del Guest. In caso di assenza, la partita non inizia, viene notificato l'Host e lo stato della partita viene ripristinato in modo sicuro a `WAITING`.
- **Rifiuto Automatico delle Sfide Concorrenti (Auto-Reject)**: Se un Host possiede più partite create in attesa e ne accetta una, diventa "impegnato". Il server intercetta automaticamente tutte le sue altre partite in stato `JOIN_PENDING`, invia un notifica di rifiuto automatico ai relativi sfidanti ed annulla o ripristina lo stato delle partite pendenti, evitando che il giocatore rimanga bloccato in più richieste simultanee.

### 4.4 Ciclo di Vita Post-Partita e Trasferimento di Proprietà (Ownership Transfer)
Al termine di un incontro, l'applicazione gestisce le opzioni di rivincita secondo le regole richieste:
- **Vittoria/Sconfitta**: Il Vincitore ha il diritto esclusivo di decidere se riaprire la sessione. In caso di scelta positiva, se a vincere è stato il **Guest**, il server esegue un **trasferimento di proprietà atomico**: il Guest diventa il nuovo Host della partita (`host_socket` ed `host_nick` aggiornati), il Guest precedente viene rimosso e la partita ritorna in stato `WAITING` per accogliere un nuovo sfidante. Il perdente viene obbligatoriamente rimesso in lobby.
- **Pareggio**: La riapertura della sessione richiede il doppio consenso esplicito di entrambi i giocatori (`host_rematch_choice` e `guest_rematch_choice`). Solo quando entrambi hanno risposto positivamente la partita viene ripristinata in stato `WAITING`.

---

## 5. Architettura del Client e Multiplexing dell'I/O

### 5.1 Scelta Architetturale: Loop Monothread Multiplexato tramite `select()`
Per lo sviluppo del Client, si è valutato se adottare un'architettura multi-thread (un thread per la lettura da tastiera ed uno per la ricezione da socket) oppure un'architettura **singolo thread multiplexata tramite la chiamata di sistema `select()`**.

Si è scelto con decisione il modello **Single-Thread Multiplexed (`select`)** per le seguenti motivazioni:

#### Motivi della Scelta:
1. **Immunità dal Corrompimento dell'Interfaccia Grafica**: In un client di terminale ad albero di rettifica, se un thread secondario riceve un pacchetto dal server ed effettua la stampa a schermo proprio mentre l'utente sta digitando un input su `stdin`, la console si corrompe visivamente ed i caratteri digitati dall'utente vengono spezzati o sovrapposti.
2. **Sincronizzazione Naturale degli Eventi**: Tramite `select()`, il client monitora contemporaneamente il descrittore del socket ed il descrittore del formato di input standard (`STDIN_FILENO`). Il thread si sveglia esclusivamente quando vi sono dati pronti da leggere su uno dei due flussi, elaborando l'evento in modo strettamente sequenziale ed eliminando alla radice ogni problema di concorrenza sull'output della console.

### 5.2 Intercettazione dei Segnali e Uscita Pulita (Graceful Shutdown)
Per evitare che la chiusura improvvisa della finestra del terminale o la pressione della combinazione di tasti `CTRL+C` lasci connessioni pendenti sul server, il client registra appositi gestori di segnale:
- Su sistemi **POSIX (Linux/macOS)**: tramite l'intercettazione dei segnali `SIGINT` e `SIGTERM`.
- Su sistemi **Windows**: tramite la registrazione della funzione di callback `SetConsoleCtrlHandler`.

All'atto dell'intercettazione di un segnale di chiusura, il client invia immediatamente al server un pacchetto di tipo `MSG_DISCONNECT` prima di chiudere la socket e terminare il processo.

---

## 6. Meccanismo di Resa Immediata (Tasto 00)

### 6.1 Motivazione e Progettazione del Tasto `00`
Per migliorare la fruibilità del gioco ed evitare che un utente rimanga bloccato in una partita che desidera abbandonare, è stata introdotta la funzionalità di **Resa Immediata tramite il comando `00`**.

### 6.2 Integrazione nel Protocollo e Flusso di Esecuzione
- Durante la fase di gioco (`CLI_STATE_IN_GAME`), l'interfaccia utente indica chiaramente la possibilità di digitare `00` sia quando è il proprio turno di mossa, sia quando si è in attesa della mossa dell'avversario.
- Quando l'utente digita `00`, il client invia al server un pacchetto `MSG_LEAVE_MATCH`.
- Il server intercetta il messaggio, identifica la partita attiva, invia all'avversario ancora connesso l'aggiornamento finale della griglia ed il messaggio di **Vittoria per Abbandono (`MSG_GAME_OVER` con stato di vittoria)**.
- Il server bonifica lo stato della partita, resetta il campo `active_match_id` di entrambi gli utenti, notifica la lobby tramite broadcast ed autorizza entrambi i client a tornare alle rispettive attività senza provocare errori di rete.

---

## 7. Progettazione dell'Interfaccia Utente (UI) da Terminale

### 7.1 Estetica Rich-Design con Sequenze ANSI e Simboli Unicode
Nonostante l'applicazione sia nativa da terminale a riga di comando, si è prestata estrema cura alla resa estetica e all'esperienza utente (User Experience):
- **Palette Colori ANSI**: Utilizzo di codici di escape ANSI a 256 colori per distinguere visivamente i titoli (Grassetto/Ciano), i menu (Verde), gli avvisi ed i messaggi di notifica (Magenta/Giallo) e gli errori (Rosso).
- **Simboli Unicode per le Pedine**: I classici caratteri testuali `X` ed `O` sono stati sostituiti nel rendering della griglia con emoticon sferiche Unicode a colori: **🔴 (Rosso)** per l'Host e **🟡 (Giallo)** per il Guest.
- **Disegno della Griglia**: La griglia di gioco 6x7 viene formattata con cornici grafiche a doppio tratto (`╔═╦═╗`, `╠═╬═╣`, `╚═╩═╝`), fornendo un impatto visivo moderno e professionale.

### 7.2 Notifiche di Lobby Non-Intrusive
Le notifiche broadcast inviate dal server per informare i client degli eventi globali (creazione di nuove partite, avvio o conclusione di incontri) vengono intercettate dal client e stampate in colore Magenta. Se il client si trova nella lobby, il menu viene automaticamente ristampato per mantenere la console pulita e pronta a ricevere nuovi comandi.

---

## 8. Gestione della Portabilità Multi-Piattaforma

### 8.1 Strato di Compatibilità Sockets (BSD vs Winsock2)
Le API di rete di Windows (Winsock2) differiscono sostanzialmente dalle Socket POSIX tradizionali di Unix/Linux per quanto riguarda l'inizializzazione (`WSAStartup`/`WSACleanup`), la tipologia dei descrittori (`SOCKET` vs `int`), i codici di errore e le funzioni di chiusura (`closesocket()` vs `close()`).

Per garantire una compilazione ed un'esecuzione identica senza duplicare il codice, sono state definite macro di astrazione condizionali (`#ifdef _WIN32`):
- Incapsulamento delle chiamate di inizializzazione e pulizia Winsock.
- Mappatura unificata del tipo `SOCKET` e della macro `close_socket(s)`.

### 8.2 Strato di Compatibilità Threading (`thread_compat.h`)
Per supportare la gestione multi-thread sia su sistemi POSIX che su ambienti Windows nativi (es. compilatori MSVC o MinGW privi di pthreads nativi), è stato realizzato l'header di astrazione `thread_compat.h`.
Esso mappa i tipi ed i metodi POSIX (`pthread_t`, `pthread_mutex_t`, `pthread_create`, `pthread_mutex_lock`) sulle rispettive API native Win32 (`HANDLE`, `CRITICAL_SECTION`, `CreateThread`, `EnterCriticalSection`), rendendo il codice sorgente del server e del client totalmente agnostico rispetto alla piattaforma di destinazione.

### 8.3 Gestione dell'Input della Console su Windows
Mentre su Linux/macOS la funzione `select()` può monitorare direttamente lo standard input (`STDIN_FILENO`), su Windows l'implementazione di Winsock limita l'uso di `select()` ai soli descrittori di socket di rete.

Per superare questo limite architetturale del sistema operativo Windows, sul client è stato implementato un loop di polling che combina la `select()` con timeout ridotto sui socket con le API di console Win32 `PeekConsoleInput` e `ReadConsoleInput`, ottenendo un comportamento di multiplexing perfettamente equivalente su tutte le piattaforme.

---

## 9. Containerizzazione ed Orchestrazione Docker

### 9.1 Multi-Stage Build nei Dockerfile
Sia per il Server che per il Client sono stati realizzati **Dockerfile basati su Multi-Stage Build**, suddivisi in due fasi:

1. **Stage 1 (Builder)**:
   - Basato su un'immagine di compilazione `ubuntu:latest`.
   - Installa la toolchain di compilazione `gcc` e `libc6-dev`.
   - Compila i file sorgenti C con il flag di ottimizzazione `-O2` e genera i binari eseguibili.
2. **Stage 2 (Runtime)**:
   - Utilizza un'immagine Ubuntu pulita e ridotta all'essenziale.
   - Copia esclusivamente il binario compilato dallo stage di builder.
   - Elimina la toolchain di compilazione ed i file sorgente, riducendo drasticamente la dimensione finale dell'immagine ed innalzando il livello di sicurezza del container.

### 9.2 Orchestrazione con Docker-Compose
Il file `docker-compose.yml` definisce ed orchestra i servizi del sistema:
- **Servizio Server**: Espone la porta TCP `8888` verso l'host ed attiva le opzioni di riavvio automatico.
- **Servizi Client (`client1`, `client2`)**: Configurate con le direttive `stdin_open: true` e `tty: true` (equivalenti ai flag `-it` di Docker CLI), indispensabili per consentire l'interazione da terminale via tastiera all'interno dei container.
- **Rete Dedicata**: I container comunicano tramite una rete bridge interna in cui il server è raggiungibile tramite il suo hostname di servizio (`server`).

---

## 10. Resilienza, Gestione degli Errori e Conclusione

### 10.1 Gestione del Segnale `SIGPIPE`
Nei sistemi Unix/Linux, quando un processo tenta di scrivere su un socket TCP la cui controparte ha chiuso la connessione, il sistema operativo invia al processo il segnale `SIGPIPE`, che per default provoca la terminazione immediata dell'applicazione.
Per prevenire il crash del Server in presenza di client che si collegano e disconnettono rapidamente, all'avvio del server viene eseguita l'istruzione `signal(SIGPIPE, SIG_IGN)`, delegando la gestione dell'errore di scrittura al valore di ritorno di `send()`.

### 10.2 Bonifica delle Risorse ed Assenza di Memory Leak
In caso di disconnessione di un client, la funzione `cleanup` del server scorrere l'intero array delle partite:
- Se il client era Host in attesa (`WAITING`), la partita viene distrutta ed il relativo mutex/condvar deallocato.
- Se il client era in partita (`IN_PROGRESS`), l'avversario riceve la vittoria a tavolino ed il proprio stato viene resettato a libero.
- La struttura socket viene chiusa ed lo slot client liberato per nuove connessioni.

### 10.3 Conclusioni
Il sistema Client-Server sviluppato risponde appieno a tutti i requisiti didattici e tecnici stabiliti per il progetto di Laboratorio di Sistemi Operativi. L'adozione di standard POSIX C99, l'attenzione maniacale alla sincronizzazione concorrente, la gestione elegante dei casi limite di rete e l'integrazione nativa con Docker garantiscono un software solido, efficiente, portabile ed estremamente piacevole da utilizzare.
