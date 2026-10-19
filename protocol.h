#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#define MAX_CLIENTS 100
#define MAX_FLOWS 100
#define MAX_FRIENDS 100
#define MAX_MESSAGE 200
#define BUFFER_SIZE 512

#define FLOW_NONE 0
#define FLOW_FRIEND_REQUEST 1
#define FLOW_MESSAGE 2
#define FLOW_FLOOD 3
#define FLOW_FRIEND_ACCEPT 4
#define FLOW_FRIEND_REJECT 5

int send_all(int sock, const void *buffer, size_t len);
int recv_until_terminator(int sock, char *buffer, size_t buffer_size);
int recv_client_message(int sock, unsigned char *buffer, size_t buffer_size);

uint16_t read_le16(const unsigned char *p);
void write_le16(unsigned char *p, uint16_t value);

int is_valid_id(const char *id);
int is_valid_port_string(const char *port);
int message_has_forbidden_terminator(const char *message);

#endif
