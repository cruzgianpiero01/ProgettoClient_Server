#include "protocol.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int send_all(int sock, const void *buffer, size_t len) {
    const char *p = (const char *)buffer;
    size_t sent = 0;

    while (sent < len) {
        ssize_t n = send(sock, p + sent, len - sent, 0);
        if (n <= 0) {
            return -1;
        }
        sent += (size_t)n;
    }

    return 0;
}

static int read_exact(int sock, void *buffer, size_t len) {
    char *p = (char *)buffer;
    size_t total = 0;

    while (total < len) {
        ssize_t n = recv(sock, p + total, len - total, 0);
        if (n <= 0) {
            return (int)n;
        }
        total += (size_t)n;
    }

    return (int)total;
}

int recv_until_terminator(int sock, char *buffer, size_t buffer_size) {
    size_t total = 0;
    int plus_count = 0;
    char c;

    if (buffer_size == 0) {
        return -1;
    }

    while (total < buffer_size - 1) {
        ssize_t n = recv(sock, &c, 1, 0);
        if (n <= 0) {
            return (int)n;
        }

        buffer[total++] = c;

        if (c == '+') {
            plus_count++;
        } else {
            plus_count = 0;
        }

        if (plus_count == 3) {
            break;
        }
    }

    buffer[total] = '\0';
    return (int)total;
}

int recv_client_message(int sock, unsigned char *buffer, size_t buffer_size) {
    if (buffer_size < BUFFER_SIZE) {
        return -1;
    }

    int n = read_exact(sock, buffer, 5);
    if (n <= 0) {
        return n;
    }

    buffer[5] = '\0';

    if (memcmp(buffer, "REGIS", 5) == 0) {
        n = read_exact(sock, buffer + 5, 20);
        if (n <= 0) return n;
        buffer[25] = '\0';
        return 25;
    }

    if (memcmp(buffer, "CONNE", 5) == 0) {
        n = read_exact(sock, buffer + 5, 15);
        if (n <= 0) return n;
        buffer[20] = '\0';
        return 20;
    }

    if (memcmp(buffer, "FRIE?", 5) == 0) {
        n = read_exact(sock, buffer + 5, 12);
        if (n <= 0) return n;
        buffer[17] = '\0';
        return 17;
    }

    if (memcmp(buffer, "LIST?", 5) == 0 ||
        memcmp(buffer, "CONSU", 5) == 0 ||
        memcmp(buffer, "OKIRF", 5) == 0 ||
        memcmp(buffer, "NOKRF", 5) == 0 ||
        memcmp(buffer, "IQUIT", 5) == 0) {
        n = read_exact(sock, buffer + 5, 3);
        if (n <= 0) return n;
        buffer[8] = '\0';
        return 8;
    }

    if (memcmp(buffer, "MESS?", 5) == 0 ||
        memcmp(buffer, "FLOO?", 5) == 0) {

        n = recv_until_terminator(sock,(char *)buffer + 5,buffer_size - 5);
        if (n <= 0)  return n;
        
        return 5 + n;
}
       return -1;
}

uint16_t read_le16(const unsigned char *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

void write_le16(unsigned char *p, uint16_t value) {
    p[0] = (unsigned char)(value & 0xFF);
    p[1] = (unsigned char)((value >> 8) & 0xFF);
}

int is_valid_id(const char *id) {
    if (strlen(id) != 8) {
        return 0;
    }

    for (int i = 0; i < 8; i++) {
        if (!isalnum((unsigned char)id[i])) {
            return 0;
        }
    }

    return strstr(id, "+++") == NULL;
}

int is_valid_port_string(const char *port) {
    if (strlen(port) != 4) {
        return 0;
    }

    for (int i = 0; i < 4; i++) {
        if (!isdigit((unsigned char)port[i])) {
            return 0;
        }
    }

    int value = atoi(port);
    return value > 0 && value < 9999;
}

int message_has_forbidden_terminator(const char *message) {
     size_t len = strlen(message);

    if (strstr(message, "+++") != NULL) {
        return 1;
    }

    if (len > 0 && message[len - 1] == '+') {
        return 1;
    }

    return 0;
}
