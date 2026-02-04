/*
 * === AOS HEADER BEGIN ===
 * ./src/net/socket.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.8.5
 * === AOS HEADER END ===
 */


#include <net/socket.h>
#include <string.h>
#include <stdlib.h>
#include <serial.h>

// Socket table
static socket_t socket_table[MAX_SOCKETS];

int socket_create(int domain, int type, int protocol) {
    if (domain != AF_INET) {
        serial_puts("Socket: Unsupported address family\n");
        return -1;
    }
    
    // Find free socket
    int sockfd = -1;
    for (int i = 0; i < MAX_SOCKETS; i++) {
        if (!socket_table[i].allocated) {
            sockfd = i;
            break;
        }
    }
    
    if (sockfd < 0) {
        serial_puts("Socket: No free sockets\n");
        return -1;
    }
    
    socket_t* sock = &socket_table[sockfd];
    memset(sock, 0, sizeof(socket_t));
    sock->type = type;
    sock->protocol = protocol;
    sock->allocated = 1;
    
    // Create protocol-specific socket
    if (type == SOCK_STREAM) {
        sock->proto_socket.tcp = tcp_socket_create();
        if (!sock->proto_socket.tcp) {
            sock->allocated = 0;
            return -1;
        }
    } else if (type == SOCK_DGRAM) {
        sock->proto_socket.udp = udp_socket_create();
        if (!sock->proto_socket.udp) {
            sock->allocated = 0;
            return -1;
        }
    } else {
        sock->allocated = 0;
        serial_puts("Socket: Unsupported socket type\n");
        return -1;
    }
    
    return sockfd;
}

int socket_bind(int sockfd, const sockaddr_in_t* addr) {
    socket_t* sock = socket_get(sockfd);
    if (!sock || !addr) {
        return -1;
    }
    
    uint32_t ip = addr->sin_addr;
    uint16_t port = ntohs(addr->sin_port);
    
    if (sock->type == SOCK_STREAM) {
        return tcp_socket_bind(sock->proto_socket.tcp, ip, port);
    } else if (sock->type == SOCK_DGRAM) {
        return udp_socket_bind(sock->proto_socket.udp, ip, port);
    }
    
    return -1;
}

int socket_listen(int sockfd, int backlog) {
    socket_t* sock = socket_get(sockfd);
    if (!sock || sock->type != SOCK_STREAM) {
        return -1;
    }
    
    return tcp_socket_listen(sock->proto_socket.tcp, backlog);
}

int socket_accept(int sockfd, sockaddr_in_t* addr) {
    socket_t* sock = socket_get(sockfd);
    if (!sock || sock->type != SOCK_STREAM) {
        return -1;
    }
    
    tcp_socket_t* new_tcp_sock = tcp_socket_accept(sock->proto_socket.tcp);
    if (!new_tcp_sock) {
        return -1;
    }
    
    // Create new socket descriptor for accepted connection
    int new_sockfd = socket_create(AF_INET, SOCK_STREAM, 0);
    if (new_sockfd < 0) {
        return -1;
    }
    
    socket_table[new_sockfd].proto_socket.tcp = new_tcp_sock;
    
    // Fill in address if provided
    if (addr) {
        addr->sa_family = AF_INET;
        addr->sin_addr = new_tcp_sock->remote_ip;
        addr->sin_port = htons(new_tcp_sock->remote_port);
    }
    
    return new_sockfd;
}

int socket_connect(int sockfd, const sockaddr_in_t* addr) {
    socket_t* sock = socket_get(sockfd);
    if (!sock || !addr) {
        return -1;
    }
    
    uint32_t ip = addr->sin_addr;
    uint16_t port = ntohs(addr->sin_port);
    
    if (sock->type == SOCK_STREAM) {
        return tcp_socket_connect(sock->proto_socket.tcp, ip, port);
    } else if (sock->type == SOCK_DGRAM) {
        return udp_socket_connect(sock->proto_socket.udp, ip, port);
    }
    
    return -1;
}

int socket_send(int sockfd, const uint8_t* data, uint32_t len, int flags) {
    (void)flags;  // Not implemented yet
    
    socket_t* sock = socket_get(sockfd);
    if (!sock || !data) {
        return -1;
    }
    
    if (sock->type == SOCK_STREAM) {
        return tcp_socket_send(sock->proto_socket.tcp, data, len);
    } else if (sock->type == SOCK_DGRAM) {
        return udp_send(sock->proto_socket.udp, data, len);
    }
    
    return -1;
}

int socket_recv(int sockfd, uint8_t* buffer, uint32_t len, int flags) {
    (void)flags;  // Not implemented yet
    
    socket_t* sock = socket_get(sockfd);
    if (!sock || !buffer) {
        return -1;
    }
    
    if (sock->type == SOCK_STREAM) {
        return tcp_socket_recv(sock->proto_socket.tcp, buffer, len);
    } else if (sock->type == SOCK_DGRAM) {
        return udp_socket_recvfrom(sock->proto_socket.udp, buffer, len, NULL, NULL);
    }
    
    return -1;
}

int socket_sendto(int sockfd, const uint8_t* data, uint32_t len, int flags,
                  const sockaddr_in_t* dest_addr) {
    (void)flags;  // Not implemented yet
    
    socket_t* sock = socket_get(sockfd);
    if (!sock || !data || !dest_addr || sock->type != SOCK_DGRAM) {
        return -1;
    }
    
    uint32_t dest_ip = dest_addr->sin_addr;
    uint16_t dest_port = ntohs(dest_addr->sin_port);
    
    return udp_socket_sendto(sock->proto_socket.udp, data, len, dest_ip, dest_port);
}

int socket_recvfrom(int sockfd, uint8_t* buffer, uint32_t len, int flags,
                    sockaddr_in_t* src_addr) {
    (void)flags;  // Not implemented yet
    
    socket_t* sock = socket_get(sockfd);
    if (!sock || !buffer || sock->type != SOCK_DGRAM) {
        return -1;
    }
    
    uint32_t src_ip;
    uint16_t src_port;
    int ret = udp_socket_recvfrom(sock->proto_socket.udp, buffer, len, &src_ip, &src_port);
    
    if (ret >= 0 && src_addr) {
        src_addr->sa_family = AF_INET;
        src_addr->sin_addr = src_ip;
        src_addr->sin_port = htons(src_port);
    }
    
    return ret;
}

int socket_close(int sockfd) {
    socket_t* sock = socket_get(sockfd);
    if (!sock) {
        return -1;
    }
    
    if (sock->type == SOCK_STREAM) {
        tcp_socket_close(sock->proto_socket.tcp);
    } else if (sock->type == SOCK_DGRAM) {
        udp_socket_close(sock->proto_socket.udp);
    }
    
    memset(sock, 0, sizeof(socket_t));
    return 0;
}

socket_t* socket_get(int sockfd) {
    if (sockfd < 0 || sockfd >= MAX_SOCKETS) {
        return NULL;
    }
    
    if (!socket_table[sockfd].allocated) {
        return NULL;
    }
    
    return &socket_table[sockfd];
}
