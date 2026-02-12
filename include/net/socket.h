/*
 * === AOS HEADER BEGIN ===
 * include/net/socket.h
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */


#ifndef SOCKET_H
#define SOCKET_H

#include <stdint.h>
#include <net/tcp.h>
#include <net/udp.h>

// Socket types
#define SOCK_STREAM 1  // TCP
#define SOCK_DGRAM  2  // UDP
#define SOCK_RAW    3  // Raw socket

// Address families
#define AF_INET  2     // IPv4

// Socket options
#define SO_REUSEADDR    1
#define SO_KEEPALIVE    2
#define SO_BROADCAST    3

// Maximum sockets
#define MAX_SOCKETS 64

// Socket address structure
typedef struct {
    uint16_t sa_family;
    uint16_t sin_port;
    uint32_t sin_addr;
    uint8_t padding[8];
} sockaddr_in_t;

// Generic socket structure
typedef struct {
    int type;               // SOCK_STREAM or SOCK_DGRAM
    int protocol;           // IP protocol number
    union {
        tcp_socket_t* tcp;
        udp_socket_t* udp;
        void* raw;
    } proto_socket;
    uint8_t allocated;
} socket_t;

// Socket API
int socket_create(int domain, int type, int protocol);
int socket_bind(int sockfd, const sockaddr_in_t* addr);
int socket_listen(int sockfd, int backlog);
int socket_accept(int sockfd, sockaddr_in_t* addr);
int socket_connect(int sockfd, const sockaddr_in_t* addr);
int socket_send(int sockfd, const uint8_t* data, uint32_t len, int flags);
int socket_recv(int sockfd, uint8_t* buffer, uint32_t len, int flags);
int socket_sendto(int sockfd, const uint8_t* data, uint32_t len, int flags,
                  const sockaddr_in_t* dest_addr);
int socket_recvfrom(int sockfd, uint8_t* buffer, uint32_t len, int flags,
                    sockaddr_in_t* src_addr);
int socket_close(int sockfd);

// Socket utilities
socket_t* socket_get(int sockfd);

#endif // SOCKET_H
