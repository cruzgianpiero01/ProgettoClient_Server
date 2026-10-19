#include "protocol.h"

#include <arpa/inet.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

typedef struct {
    int udp_port;
} UdpListenerArgs;

static void *udp_listener(void *arg) {
    UdpListenerArgs *args = (UdpListenerArgs *)arg;
    int udp_port = args->udp_port;
    free(args);

    int udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_sock < 0) {
        perror("socket UDP");
        return NULL;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(udp_port);

    if (bind(udp_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind UDP");
        close(udp_sock);
        return NULL;
    }
    
    printf("Listener UDP attivo sulla porta %04d.\n", udp_port);

    while (1) {
        char notification[4];
        ssize_t n = recvfrom(udp_sock, notification, 3, 0, NULL, NULL);

        if (n == 3) {
            notification[3] = '\0';
            printf("\n[NOTIFICA UDP] %s\n", notification);
            printf("Scelta: ");
            fflush(stdout);
        }
    }

    close(udp_sock);
    return NULL;
}

static int connect_server(const char *server_ip, int tcp_port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket TCP");
        return -1;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(tcp_port);

    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) != 1) {
        fprintf(stderr, "IP server non valido.\n");
        close(sock);
        return -1;
    }

    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect");
        close(sock);
        return -1;
    }

    return sock;
}

static int success(const char *response, const char *expected) {
    return strncmp(response, expected, 5) == 0;
}

static int client_regis(int sock, const char *id, const char *udp_port, uint16_t password) {
    unsigned char buffer[BUFFER_SIZE];
    int len = 0;

    memcpy(buffer + len, "REGIS ", 6);
    len += 6;
    memcpy(buffer + len, id, 8);
    len += 8;
    buffer[len++] = ' ';
    memcpy(buffer + len, udp_port, 4);
    len += 4;
    buffer[len++] = ' ';
    write_le16(buffer + len, password);
    len += 2;
    memcpy(buffer + len, "+++", 3);
    len += 3;

    if (send_all(sock, buffer, (size_t)len) < 0) return -1;

    char response[BUFFER_SIZE];
    int r = recv_until_terminator(sock, response, sizeof(response));
    if (r <= 0) return -1;

    printf("Risposta REGIS: %s\n", response);
    return success(response, "WELCO") ? 0 : -1;
}

static int client_conne(int sock, const char *id, uint16_t password) {
    unsigned char buffer[BUFFER_SIZE];
    int len = 0;

    memcpy(buffer + len, "CONNE ", 6);
    len += 6;
    memcpy(buffer + len, id, 8);
    len += 8;
    buffer[len++] = ' ';
    write_le16(buffer + len, password);
    len += 2;
    memcpy(buffer + len, "+++", 3);
    len += 3;

    if (send_all(sock, buffer, (size_t)len) < 0) return -1;

    char response[BUFFER_SIZE];
    int r = recv_until_terminator(sock, response, sizeof(response));
    if (r <= 0) return -1;

    printf("Risposta CONNE: %s\n", response);
    return success(response, "HELLO") ? 0 : -1;
}

static void client_list(int sock) {
    char buffer[BUFFER_SIZE];

    send_all(sock, "LIST?+++", 8);

    int r = recv_until_terminator(sock, buffer, sizeof(buffer));
    if (r <= 0) return;

    printf("%s\n", buffer);

    int count = 0;
    sscanf(buffer, "RLIST %3d+++", &count);

    for (int i = 0; i < count; i++) {
        r = recv_until_terminator(sock, buffer, sizeof(buffer));
        if (r > 0) {
            printf("  %s\n", buffer);
        }
    }
}

static void client_frie(int sock) {
    char friend_id[9];
    char buffer[BUFFER_SIZE];

    printf("ID cliente a cui inviare richiesta amicizia: ");
    
    if (scanf("%99s", friend_id) != 1) {
        printf("Errore nella lettura dell'id.\n");
        return;
    }



    if (!is_valid_id(friend_id)) {
        printf("ID non valido: deve essere alfanumerico di 8 caratteri.\n");
        return;
    }

    snprintf(buffer, sizeof(buffer), "FRIE? %s+++", friend_id);
    send_all(sock, buffer, strlen(buffer));

    int r = recv_until_terminator(sock, buffer, sizeof(buffer));
    if (r > 0) printf("Risposta FRIE?: %s\n", buffer);
}

static void client_mess(int sock) {
    char target_id[9];
    char message[BUFFER_SIZE];
    char buffer[MAX_MESSAGE+19];

    printf("ID amico destinatario: ");
    
     if (scanf("%99s", target_id) != 1) {
        printf("Errore nella lettura dell'id.\n");
        return;
    }
    
      int c;
     while ((c = getchar()) != '\n' && c != EOF) {
     }

    if (!is_valid_id(target_id)) {
        printf("ID non valido: deve essere alfanumerico di 8 caratteri.\n");
        return;
    }

    printf("Messaggio max 200 caratteri: ");
    if(fgets(message, sizeof(message), stdin)==NULL) {
        printf("Errore nella lettura del messaggio.\n");
         return;
    }
    message[strcspn(message, "\n")] = '\0';

    if (strlen(message) > MAX_MESSAGE) {
    printf("Messaggio troppo lungo: massimo %d caratteri.\n",
           MAX_MESSAGE);
    return;
    }

    if (message_has_forbidden_terminator(message)) {
        printf("Il messaggio non può contenere +++ o terminare con +.\n");
        return;
    }

    snprintf(buffer, sizeof(buffer), "MESS? %.8s %.*s+++", target_id,MAX_MESSAGE, message);
    send_all(sock, buffer, strlen(buffer));

    int r = recv_until_terminator(sock, buffer, sizeof(buffer));
    if (r > 0) printf("Risposta MESS?: %s\n", buffer);
}

static void client_floo(int sock) {
    char message[BUFFER_SIZE];
    char buffer[MAX_MESSAGE + 10];

    int c;
    while ((c = getchar()) != '\n' && c != EOF) {
    }

    printf("Messaggio flood max %d caratteri: ", MAX_MESSAGE);

    if (fgets(message, sizeof(message), stdin) == NULL) {
        printf("Errore nella lettura del messaggio.\n");
        return;
    }

    message[strcspn(message, "\n")] = '\0';

    if (strlen(message) > MAX_MESSAGE) {
        printf("Messaggio troppo lungo: massimo %d caratteri.\n",
               MAX_MESSAGE);
        return;
    }

    if (message_has_forbidden_terminator(message)) {
        printf("Il messaggio non può contenere +++ o terminare con +.\n");
        return;
    }

    snprintf(buffer,sizeof(buffer),"FLOO? %.*s+++",MAX_MESSAGE,message);

    send_all(sock, buffer, strlen(buffer));
        
    int r = recv_until_terminator(sock, buffer, sizeof(buffer));

    if (r > 0) printf("Risposta FLOO?: %s\n", buffer);
    
}

static void client_consu(int sock) {
    char buffer[BUFFER_SIZE];

    send_all(sock, "CONSU+++", 8);

    int r = recv_until_terminator(sock, buffer, sizeof(buffer));
    if (r <= 0) return;

    printf("Risposta CONSU: %s\n", buffer);

    if (strncmp(buffer, "EIRF> ", 6) == 0) {
        char requester_id[9];
        char choice;

        sscanf(buffer, "EIRF> %8s+++", requester_id);

        printf("Richiesta di amicizia da %s. Vuoi accettare? (s/n): ", requester_id);
        scanf(" %c", &choice);

        if (choice == 's' || choice == 'S') {
            send_all(sock, "OKIRF+++", 8);
        } else {
            send_all(sock, "NOKRF+++", 8);
        }

        r = recv_until_terminator(sock, buffer, sizeof(buffer));
        if (r > 0) printf("Risposta server: %s\n", buffer);
    }
}

static void client_iquit(int sock) {
    char buffer[BUFFER_SIZE];

    send_all(sock, "IQUIT+++", 8);

    int r = recv_until_terminator(sock, buffer, sizeof(buffer));
    if (r > 0) {
        printf("Risposta IQUIT: %s\n", buffer);
    }
}

static void print_menu(void) {
    printf("\n===== MENU CLIENT =====\n");
    printf("1. Visualizza lista clienti LIST?\n");
    printf("2. Invia richiesta amicizia FRIE?\n");
    printf("3. Invia messaggio MESS?\n");
    printf("4. Invia flood FLOO?\n");
    printf("5. Consulta un flusso CONSU\n");
    printf("0. Esci IQUIT\n");
    printf("Scelta: ");
    fflush(stdout);
}

int main(int argc, char *argv[]) {
    if (argc != 7) {
        fprintf(stderr, "Uso:\n");
        fprintf(stderr, "  %s <server_ip> <porta_tcp> <id8> <porta_udp4> <password> <reg|con>\n", argv[0]);
        fprintf(stderr, "\nEsempi:\n");
        fprintf(stderr, "  %s 127.0.0.1 1234 user0001 8001 1234 reg\n", argv[0]);
        fprintf(stderr, "  %s 127.0.0.1 1234 user0001 8001 1234 con\n", argv[0]);
        return 1;
    }

    const char *server_ip = argv[1];
    int tcp_port = atoi(argv[2]);
    const char *id = argv[3];
    const char *udp_port = argv[4];

    int password_value;
    char extra;

    if (sscanf(argv[5], "%d%c", &password_value, &extra) != 1 ||
        password_value < 0 ||
        password_value > 65535) {

        fprintf(stderr,
                "Password non valida: deve essere un numero tra 0 e 65535.\n");
        return 1;
    }

    uint16_t password = (uint16_t)password_value;

    const char *mode = argv[6];

    if (!is_valid_id(id)) {
        fprintf(stderr, "ID non valido: deve essere alfanumerico di 8 caratteri.\n");
        return 1;
    }

    if (!is_valid_port_string(udp_port)) {
        fprintf(stderr, "Porta UDP non valida: deve avere 4 cifre ed essere < 9999.\n");
        return 1;
    }


    UdpListenerArgs *udp_args = malloc(sizeof(*udp_args));
    if (udp_args == NULL) return 1;
    udp_args->udp_port = atoi(udp_port);

    pthread_t udp_tid;
    if (pthread_create(&udp_tid, NULL, udp_listener, udp_args) != 0) {
        perror("pthread_create UDP");
        free(udp_args);
        return 1;
    }
    pthread_detach(udp_tid);

    int sock = connect_server(server_ip, tcp_port);
    if (sock < 0) return 1;

    int auth_ok;
    if (strcmp(mode, "reg") == 0) {
        auth_ok = client_regis(sock, id, udp_port, password);
    } else if (strcmp(mode, "con") == 0) {
        auth_ok = client_conne(sock, id, password);
    } else {
        fprintf(stderr, "Ultimo parametro non valido: usa reg oppure con.\n");
        close(sock);
        return 1;
    }

    if (auth_ok < 0) {
        printf("Autenticazione fallita.\n");
        close(sock);
        return 1;
    }


    int choice = -1;

    while (choice != 0) {
        print_menu();

        if (scanf("%d", &choice) != 1) {
            printf("Input non valido.\n");
            break;
        }

        if (choice == 1) {
            client_list(sock);
        } else if (choice == 2) {
            client_frie(sock);
        } else if (choice == 3) {
            client_mess(sock);
        } else if (choice == 4) {
            client_floo(sock);
        } else if (choice == 5) {
            client_consu(sock);
        } else if (choice == 0) {
            client_iquit(sock);
        } else {
            printf("Scelta non valida.\n");
        }
    }

    close(sock);
    return 0;
}
