Lo scopo di questo progetto è quello di programmare un sistema che consenta a diverse entità clienti di stabilire relazioni e comunicare tra loro. Il funzionamento si ispira agli attuali social network.
In questo sistema esistono due tipi di entità:

1. I server
2. I clienti

Intuitivamente, i clienti possono registrarsi sul server, stringere amicizie e scambiarsi messaggi. Il server dispone dell’elenco di tutti i suoi clienti e delle loro amicizie, 
ha il compito di inviare notifiche ai clienti e trasmettere i messaggi tra i clienti.
Il funzionamento è il seguente: il server gestisce per ciascuno dei suoi clienti un elenco di flussi.
Un flusso può essere una richiesta di amicizia o un messaggio da un altro cliente o una risposta a una richiesta di
amicizia o un messaggio di flood.
Ad ogni nuovo flusso creato, il server invia una notifica sotto forma di messaggio UDP al cliente interessato, codificato su 3 carattere e nella forma [YXX] dove Y è un codice per
il flusso in questione e XX è la codifica in esadecimale in litte-endian del numero di flussi non consultati.
Il cliente connesso può consultare i propri flussi grazie al comando CONSU.

Esempio di esecuzione. Supponiamo che ci siano due clienti bob12345 e alice123 (che chiameremo Alice
e Bob). Alice è già registrata su un server e ha consultato tutti i suoi flussi. Bob si connette per la prima
volta al server e si registra inviando un messaggio [REGIS␣bob12345␣07878␣1010+++ ] (dove 1010 corrisponde a due byte contenenti ciascuno il numero intero 10), il server accetta inviando [WELCO+++]. 
Bob rimane quindi connesso. Alice può essere connessa o meno. Bob invia una richiesta di amicizia inviando
al server il messaggio [FRIE?␣alice123+++]. Il server invia quindi alla porta UDP di Alice un messaggio
[010] (il primo 0 per indicare che ha ricevuto una richiesta di amicizia, i due caratteri 1 e 0 per indicare che
ha un flusso da consultare). A questo punto può connettersi al server e inviare il messaggio [CONSU+++]
per consultare i suoi flussi. Poiché ha un solo flusso in attesa, ovvero la richiesta di amicizia, il server le invia [EIRF>␣bob12345+++].
Supponiamo che lei accetti questa richiesta di amicizia: risponderà con [OKIRF+++], il server confermerà inviando [ACKRF+++] e invierà a Bob sulla sua porta UDP un messaggio
[110] il primo carattere 1 significa che qualcuno ha accettato una delle sue richieste di amicizia e i caratteri
1 e 0 significano che ha un flusso da consultare (10 essendo la codifica su in esadecimale di 1 in little endian).
A questo punto (se è ancora connesso) può inviare [CONSU+++] al server che invierà il messaggio [FRIEN␣alice123] indicando che Alice ha accettato la sua richiesta di amicizia.


Avvio del server
Per avviare il server:

./server <porta_tcp>

Esempio:

./server 1234
Il server rimarrà in ascolto sulla porta 1234.

Avvio di un client
La sintassi è:

./client <server_ip> <porta_tcp> <id> <porta_udp> <password> <reg|con>

Per esempio, per registrare un nuovo utente:
./client 127.0.0.1 1234 user0001 8001 1234 reg
Per riconnettere un utente già registrato:
./client 127.0.0.1 1234 user0001 8001 1234 con
I parametri sono:
127.0.0.1 → indirizzo IP del server
1234      → porta TCP del server
user0001  → ID del client
8001      → porta UDP del client
1234      → password
reg       → registrazione
con       → riconnessione
L'ID deve essere composto da 8 caratteri alfanumerici.

La password deve essere un numero compreso tra 0 e 65535.

Ogni client eseguito sullo stesso computer deve utilizzare una porta UDP diversa.

Test con più client
Per provare il progetto localmente è possibile aprire più terminali.
Terminale 1 - Server
./server 1234
Terminale 2 - Client A
./client 127.0.0.1 1234 user0001 8001 1111 reg
Terminale 3 - Client B
./client 127.0.0.1 1234 user0002 8002 2222 reg
Terminale 4 - Client C
./client 127.0.0.1 1234 user0003 8003 3333 reg
È importante utilizzare porte UDP differenti (8001, 8002, 8003).
