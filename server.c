#include "protocol.h"

#include <arpa/inet.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

typedef struct {
    char from_id[9];
} FriendRequest;

typedef struct {
    char from_id[9];
    char message[MAX_MESSAGE + 1];
} MessageFlow;

typedef struct {
    char from_id[9];
    char message[MAX_MESSAGE + 1];
} FloodFlow;

typedef struct {
    char from_id[9];
} FriendResponse;

typedef struct {
    char id[9];
    char udp_port[5];
    uint16_t password;
    struct sockaddr_in addr;

    int flow_count;
    int flow_types[MAX_FLOWS];

    FriendRequest requests[MAX_FLOWS];
    MessageFlow messages[MAX_FLOWS];
    FloodFlow floods[MAX_FLOWS];
    FriendResponse responses[MAX_FLOWS];

    char friends[MAX_FRIENDS][9];
    int friend_count;
} Client;

typedef struct {
    int sock;
    struct sockaddr_in addr;
} ClientThreadArgs;

static Client clients[MAX_CLIENTS];
static int client_count = 0;
static pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

static void copy_fixed(char *dst, const unsigned char *src, size_t n) {
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static int find_client_index(const char *id) {
    for (int i = 0; i < client_count; i++) {
        if (strncmp(clients[i].id, id, 8) == 0) {
            return i;
        }
    }
    return -1;
}

static int are_friends(int a, int b) {
    if (a < 0 || b < 0) return 0;

    for (int i = 0; i < clients[a].friend_count; i++) {
        if (strncmp(clients[a].friends[i], clients[b].id, 8) == 0) {
            return 1;
        }
    }

    return 0;
}

static void add_friend_one_way(int a, int b) {
    if (a < 0 || b < 0) return;
    if (clients[a].friend_count >= MAX_FRIENDS) return;
    if (are_friends(a, b)) return;

    strcpy(clients[a].friends[clients[a].friend_count], clients[b].id);
    clients[a].friend_count++;
}

static void add_friendship(int a, int b) {
    add_friend_one_way(a, b);
    add_friend_one_way(b, a);
}

static int add_friend_request_flow(int target_index, const char *from_id) {
    Client *target = &clients[target_index];

    if (target->flow_count >= MAX_FLOWS) return 0;

    int pos = target->flow_count;
    target->flow_types[pos] = FLOW_FRIEND_REQUEST;
    strncpy(target->requests[pos].from_id, from_id, 8);
    target->requests[pos].from_id[8] = '\0';
    target->flow_count++;

    return 1;
}

static int add_message_flow(int target_index, const char *from_id, const char *message) {
    Client *target = &clients[target_index];

    if (target->flow_count >= MAX_FLOWS) return 0;

    int pos = target->flow_count;
    target->flow_types[pos] = FLOW_MESSAGE;
    strncpy(target->messages[pos].from_id, from_id, 8);
    target->messages[pos].from_id[8] = '\0';
    strncpy(target->messages[pos].message, message, MAX_MESSAGE);
    target->messages[pos].message[MAX_MESSAGE] = '\0';
    target->flow_count++;

    return 1;
}

static int add_flood_flow(int target_index, const char *from_id, const char *message) {
    Client *target = &clients[target_index];

    if (target->flow_count >= MAX_FLOWS) return 0;

    int pos = target->flow_count;
    target->flow_types[pos] = FLOW_FLOOD;
    strncpy(target->floods[pos].from_id, from_id, 8);
    target->floods[pos].from_id[8] = '\0';
    strncpy(target->floods[pos].message, message, MAX_MESSAGE);
    target->floods[pos].message[MAX_MESSAGE] = '\0';
    target->flow_count++;

    return 1;
}

static int add_friend_response_flow(int target_index, int accepted, const char *from_id) {
    Client *target = &clients[target_index];

    if (target->flow_count >= MAX_FLOWS) return 0;

    int pos = target->flow_count;
    target->flow_types[pos] = accepted ? FLOW_FRIEND_ACCEPT : FLOW_FRIEND_REJECT;
    strncpy(target->responses[pos].from_id, from_id, 8);
    target->responses[pos].from_id[8] = '\0';
    target->flow_count++;

    return 1;
}

static void remove_first_flow(int client_index) {
    Client *client = &clients[client_index];

    if (client->flow_count <= 0) return;

    for (int i = 1; i < client->flow_count; i++) {
        client->flow_types[i - 1] = client->flow_types[i];
        client->requests[i - 1] = client->requests[i];
        client->messages[i - 1] = client->messages[i];
        client->floods[i - 1] = client->floods[i];
        client->responses[i - 1] = client->responses[i];
    }

    client->flow_count--;
}

static void send_udp_notification(struct sockaddr_in addr, int flow_count, char code) {
    int udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_sock < 0) return;


    char notification[4];

     const char hex[] = "0123456789ABCDEF";

      notification[0] = code;
      notification[1] = hex[flow_count & 0x0F];
      notification[2] = hex[(flow_count >> 4) & 0x0F];
      notification[3] = '\0';

    sendto(udp_sock,
           notification,
           3,
           0,
           (struct sockaddr *)&addr,
           sizeof(addr));

    close(udp_sock);
}

static void notify_client(int client_index, char code) {
    struct sockaddr_in addr = clients[client_index].addr;
    
    int flow_count = clients[client_index].flow_count;

    send_udp_notification(addr, flow_count, code);
}

static void flood_recursive(int sender_index, int current_index, int visited[], const char *message) {
    visited[current_index] = 1;

    for (int i = 0; i < clients[current_index].friend_count; i++) {
        int friend_index = find_client_index(clients[current_index].friends[i]);

        if (friend_index == -1) continue;
        if (visited[friend_index]) continue;

        visited[friend_index] = 1;

        if (add_flood_flow(friend_index, clients[sender_index].id, message)) {
            notify_client(friend_index, '4');
        }

        flood_recursive(sender_index, friend_index, visited, message);
    }
}

static void handle_regis(int sock, const unsigned char *buffer, struct sockaddr_in addr, int *current_client_index) {
    char id[9];
    char udp_port[5];
    uint16_t password;

    *current_client_index = -1;

    
    if (buffer[5] != ' ' ||
        buffer[14] != ' ' ||
        buffer[19] != ' ' ||
        memcmp(buffer + 22, "+++", 3) != 0) {

        send_all(sock, "GOBYE+++", 8);
        return;
    }

    copy_fixed(id, buffer + 6, 8);
    copy_fixed(udp_port, buffer + 15, 4);
    password = read_le16(buffer + 20);

    pthread_mutex_lock(&clients_mutex);

    if (!is_valid_id(id) ||
        !is_valid_port_string(udp_port) ||
        client_count >= MAX_CLIENTS ||
        find_client_index(id) != -1) {

        pthread_mutex_unlock(&clients_mutex);
        send_all(sock, "GOBYE+++", 8);
        *current_client_index = -1;
        return;
    }

      uint16_t requested_udp_port = htons(atoi(udp_port));

     for (int i = 0; i < client_count; i++) {
        if (clients[i].addr.sin_addr.s_addr == addr.sin_addr.s_addr &&
        clients[i].addr.sin_port == requested_udp_port) {

        pthread_mutex_unlock(&clients_mutex);

        send_all(sock, "GOBYE+++", 8);
        return;
      }
    }
    
    Client *client = &clients[client_count];
    memset(client, 0, sizeof(*client));

    strcpy(client->id, id);
    strcpy(client->udp_port, udp_port);
    client->password = password;
    client->addr = addr;
    client->addr.sin_port = htons(atoi(udp_port));

    *current_client_index = client_count;
    client_count++;

    pthread_mutex_unlock(&clients_mutex);

    send_all(sock, "WELCO+++", 8);
}

static void handle_conne(int sock, const unsigned char *buffer, struct sockaddr_in addr, int *current_client_index) {
    char id[9];
    uint16_t password;

    copy_fixed(id, buffer + 6, 8);
    password = read_le16(buffer + 15);

    pthread_mutex_lock(&clients_mutex);

    int index = find_client_index(id);

    if (index == -1 || clients[index].password != password) {
        pthread_mutex_unlock(&clients_mutex);
        send_all(sock, "GOBYE+++", 8);
        *current_client_index = -1;
        return;
    }

    clients[index].addr.sin_addr = addr.sin_addr;
    *current_client_index = index;

    pthread_mutex_unlock(&clients_mutex);

    send_all(sock, "HELLO+++", 8);
}

static void handle_frie(int sock, const unsigned char *buffer, int current_client_index) {
    char target_id[9];
    int should_notify = 0;
    struct sockaddr_in notify_addr;
    int notify_count = 0;

    copy_fixed(target_id, buffer + 6, 8);

    pthread_mutex_lock(&clients_mutex);

    int target_index = find_client_index(target_id);

    if (current_client_index == -1 || target_index == -1 || target_index == current_client_index || are_friends(current_client_index, target_index) || !is_valid_id(target_id)) {
        pthread_mutex_unlock(&clients_mutex);
        send_all(sock, "FRIE<+++", 8);
        return;
    }

      if (clients[current_client_index].friend_count >= MAX_FRIENDS ||
      clients[target_index].friend_count >= MAX_FRIENDS) {

      pthread_mutex_unlock(&clients_mutex);
      send_all(sock, "FRIE<+++", 8);
      return;
    }

    if (add_friend_request_flow(target_index, clients[current_client_index].id)) {
        should_notify = 1;
        notify_addr = clients[target_index].addr;
        notify_count = clients[target_index].flow_count;
    }

    pthread_mutex_unlock(&clients_mutex);

    if (!should_notify) {
        send_all(sock, "FRIE<+++", 8);
        return;
    }

    send_udp_notification(notify_addr, notify_count, '0');
    send_all(sock, "FRIE>+++", 8);
}

static void handle_mess(int sock, const unsigned char *buffer, int current_client_index) {
    char target_id[9];
    char message[MAX_MESSAGE + 1];
    int should_notify = 0;
    struct sockaddr_in notify_addr;
    int notify_count = 0;

    copy_fixed(target_id, buffer + 6, 8);

    const char *msg_start = (const char *)buffer + 15;
    const char *end = strstr(msg_start, "+++");

    if (end == NULL) {
        send_all(sock, "MESS<+++", 8);
        return;
    }

    int msg_len = (int)(end - msg_start);
    if (msg_len > MAX_MESSAGE) {
        send_all(sock, "MESS<+++", 8);
         return;
    }

    strncpy(message, msg_start, msg_len);
    message[msg_len] = '\0';

    pthread_mutex_lock(&clients_mutex);

    int target_index = find_client_index(target_id);

    if (current_client_index == -1 ||
        target_index == -1 ||
        !are_friends(current_client_index, target_index)) {
        pthread_mutex_unlock(&clients_mutex);
        send_all(sock, "MESS<+++", 8);
        return;
    }

    if (add_message_flow(target_index, clients[current_client_index].id, message)) {
        should_notify = 1;
        notify_addr = clients[target_index].addr;
        notify_count = clients[target_index].flow_count;
    }

    pthread_mutex_unlock(&clients_mutex);

    if (!should_notify) {
        send_all(sock, "MESS<+++", 8);
        return;
    }

    send_udp_notification(notify_addr, notify_count, '3');
    send_all(sock, "MESS>+++", 8);
}

static void handle_floo(int sock, const unsigned char *buffer, int current_client_index) {
    char message[MAX_MESSAGE + 1];

    const char *msg_start = (const char *)buffer + 6;
    const char *end = strstr(msg_start, "+++");

    if (current_client_index == -1 || end == NULL) {
        send_all(sock, "GOBYE+++", 8);
        return;
    }

    int msg_len = (int)(end - msg_start);
    if (msg_len > MAX_MESSAGE) msg_len = MAX_MESSAGE;

    strncpy(message, msg_start, msg_len);
    message[msg_len] = '\0';

    if (message_has_forbidden_terminator(message)) {
        send_all(sock, "GOBYE+++", 8);
        return;
    }

    pthread_mutex_lock(&clients_mutex);
    int visited[MAX_CLIENTS] = {0};
    flood_recursive(current_client_index, current_client_index, visited, message);
    pthread_mutex_unlock(&clients_mutex);

    send_all(sock, "FLOO>+++", 8);
}

static void handle_list(int sock) {
    char response[BUFFER_SIZE];
    char ids[MAX_CLIENTS][9];
    int count;

    pthread_mutex_lock(&clients_mutex);
    count = client_count;
    for (int i = 0; i < count; i++) {
        strcpy(ids[i], clients[i].id);
    }
    pthread_mutex_unlock(&clients_mutex);

    snprintf(response, sizeof(response), "RLIST %03d+++", count);
    send_all(sock, response, strlen(response));

    for (int i = 0; i < count; i++) {
        snprintf(response, sizeof(response), "LINUM %s+++", ids[i]);
        send_all(sock, response, strlen(response));
    }
}

static void handle_consu(int sock, int current_client_index) {
    char response[BUFFER_SIZE];

    pthread_mutex_lock(&clients_mutex);

    if (current_client_index == -1 || current_client_index >= client_count) {
        pthread_mutex_unlock(&clients_mutex);
        send_all(sock, "GOBYE+++", 8);
        return;
    }

    Client *client = &clients[current_client_index];

    if (client->flow_count == 0) {
        pthread_mutex_unlock(&clients_mutex);
        send_all(sock, "NOCON+++", 8);
        return;
    }

    int type = client->flow_types[0];

    if (type == FLOW_MESSAGE) {
        snprintf(response, sizeof(response), "SSEM> %s %s+++", client->messages[0].from_id, client->messages[0].message);
        remove_first_flow(current_client_index);
        pthread_mutex_unlock(&clients_mutex);
        send_all(sock, response, strlen(response));
        return;
    }

    if (type == FLOW_FLOOD) {
        snprintf(response, sizeof(response), "OOLF> %s %s+++", client->floods[0].from_id, client->floods[0].message);
        remove_first_flow(current_client_index);
        pthread_mutex_unlock(&clients_mutex);
        send_all(sock, response, strlen(response));
        return;
    }

    if (type == FLOW_FRIEND_ACCEPT) {
        snprintf(response, sizeof(response), "FRIEN %s+++", client->responses[0].from_id);
        remove_first_flow(current_client_index);
        pthread_mutex_unlock(&clients_mutex);
        send_all(sock, response, strlen(response));
        return;
    }

    if (type == FLOW_FRIEND_REJECT) {
        snprintf(response, sizeof(response), "NOFRI %s+++", client->responses[0].from_id);
        remove_first_flow(current_client_index);
        pthread_mutex_unlock(&clients_mutex);
        send_all(sock, response, strlen(response));
        return;
    }

    if (type == FLOW_FRIEND_REQUEST) {
        char requester_id[9];

        strcpy(requester_id, client->requests[0].from_id);

        snprintf(response,sizeof(response),"EIRF> %s+++",requester_id);

        pthread_mutex_unlock(&clients_mutex);

        send_all(sock, response, strlen(response));

        unsigned char answer[BUFFER_SIZE];

        int r = recv_client_message(sock, answer, sizeof(answer));

        if (r <= 0)
           return;

        pthread_mutex_lock(&clients_mutex);

        int requester_index = find_client_index(requester_id);

        int accepted =(memcmp(answer, "OKIRF+++", 8) == 0);

        int rejected =(memcmp(answer, "NOKRF+++", 8) == 0);

        if (!accepted && !rejected) {
        pthread_mutex_unlock(&clients_mutex);
        send_all(sock, "GOBYE+++", 8);
        return;
        }

        if (requester_index != -1) {

           if (accepted) {

            if (clients[current_client_index].friend_count < MAX_FRIENDS &&
                clients[requester_index].friend_count < MAX_FRIENDS && 
                clients[requester_index].flow_count < MAX_FLOWS) {

                add_friendship(current_client_index,requester_index);

                if (add_friend_response_flow(requester_index,1,clients[current_client_index].id)) {
                     notify_client(requester_index, '1');
                }

            } else {

                if (add_friend_response_flow(requester_index,0,clients[current_client_index].id)) {
                    notify_client(requester_index, '2');
                }
            }

        } else {

            if (add_friend_response_flow(requester_index,0,clients[current_client_index].id)) {
                notify_client(requester_index, '2');
            }
        }
    }

    if (clients[current_client_index].flow_count > 0 &&
        clients[current_client_index].flow_types[0] ==FLOW_FRIEND_REQUEST &&
        strncmp(clients[current_client_index].requests[0].from_id,requester_id,8) == 0) {

        remove_first_flow(current_client_index);
    }

    pthread_mutex_unlock(&clients_mutex);

    send_all(sock, "ACKRF+++", 8);

    return;
}

    pthread_mutex_unlock(&clients_mutex);
    send_all(sock, "NOCON+++", 8);
}

static void *client_thread(void *arg) {
    ClientThreadArgs *args = (ClientThreadArgs *)arg;
    int sock = args->sock;
    struct sockaddr_in peer_addr = args->addr;
    free(args);

    int current_client_index = -1;
    unsigned char buffer[BUFFER_SIZE];

    while (1) {
        int len = recv_client_message(sock, buffer, sizeof(buffer));
        if (len <= 0) break;

        if (memcmp(buffer, "REGIS", 5) == 0) {
            handle_regis(sock, buffer, peer_addr, &current_client_index);
            if (current_client_index == -1) break;
        } else if (memcmp(buffer, "CONNE", 5) == 0) {
            handle_conne(sock, buffer, peer_addr, &current_client_index);
            if (current_client_index == -1) break;
        } else if (memcmp(buffer, "FRIE?", 5) == 0) {
            handle_frie(sock, buffer, current_client_index);
        } else if (memcmp(buffer, "MESS?", 5) == 0) {
            handle_mess(sock, buffer, current_client_index);
        } else if (memcmp(buffer, "FLOO?", 5) == 0) {
            handle_floo(sock, buffer, current_client_index);
        } else if (memcmp(buffer, "LIST?", 5) == 0) {
            handle_list(sock);
        } else if (memcmp(buffer, "CONSU", 5) == 0) {
            handle_consu(sock, current_client_index);
        } else if (memcmp(buffer, "IQUIT", 5) == 0) {
            send_all(sock, "GOBYE+++", 8);
            break;
        } else {
            send_all(sock, "GOBYE+++", 8);
            break;
        }
    }

    close(sock);
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Uso: %s <porta_tcp>\n", argv[0]);
        fprintf(stderr, "Esempio: %s 1234\n", argv[0]);
        return 1;
    }

    int tcp_port = atoi(argv[1]);
    if (tcp_port >= 9999) {
        fprintf(stderr, "La porta TCP deve essere inferiore a 9999.\n");
        return 1;
    }

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return 1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(tcp_port);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, 16) < 0) {
        perror("listen");
        close(server_fd);
        return 1;
    }

    printf("Server in ascolto sulla porta TCP %d...\n", tcp_port);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t len = sizeof(client_addr);
        int client_sock = accept(server_fd, (struct sockaddr *)&client_addr, &len);

        if (client_sock < 0) {
            perror("accept");
            continue;
        }

        ClientThreadArgs *args = malloc(sizeof(*args));
        if (args == NULL) {
            close(client_sock);
            continue;
        }

        args->sock = client_sock;
        args->addr = client_addr;

        pthread_t tid;
        if (pthread_create(&tid, NULL, client_thread, args) != 0) {
            perror("pthread_create");
            close(client_sock);
            free(args);
            continue;
        }

        pthread_detach(tid);
    }

    close(server_fd);
    return 0;
}
