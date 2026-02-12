/*
 * === AOS HEADER BEGIN ===
 * include/net/udp.h
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */


#ifndef UDP_H
#define UDP_H

#include <stdint.h>
#include <net/net.h>

// UDP header structure
typedef struct {
    uint16_t src_port;
    uint16_t dest_port;
    uint16_t length;
    uint16_t checksum;
} __attribute__((packed)) udp_header_t;

#define UDP_HEADER_LEN sizeof(udp_header_t)
#define UDP_RX_QUEUE_SIZE 8
#define UDP_RX_BUFFER_SIZE 1500

// UDP receive buffer entry
typedef struct {
    uint8_t data[UDP_RX_BUFFER_SIZE];
    uint32_t len;
    uint32_t src_ip;
    uint16_t src_port;
    uint8_t valid;
} udp_rx_entry_t;

// UDP socket structure
typedef struct {
    uint16_t local_port;
    uint32_t local_ip;
    uint16_t remote_port;
    uint32_t remote_ip;
    uint8_t bound;
    uint8_t connected;
    udp_rx_entry_t rx_queue[UDP_RX_QUEUE_SIZE];
    uint32_t rx_head;
    uint32_t rx_tail;
} udp_socket_t;

// UDP initialization
void udp_init(void);

// UDP packet processing
int udp_receive(net_interface_t* iface, uint32_t src_ip, uint32_t dest_ip, 
                net_packet_t* packet);
int udp_send(udp_socket_t* sock, const uint8_t* data, uint32_t len);

// UDP socket operations
udp_socket_t* udp_socket_create(void);
int udp_socket_bind(udp_socket_t* sock, uint32_t ip, uint16_t port);
int udp_socket_connect(udp_socket_t* sock, uint32_t ip, uint16_t port);
int udp_socket_sendto(udp_socket_t* sock, const uint8_t* data, uint32_t len,
                      uint32_t dest_ip, uint16_t dest_port);
int udp_socket_recvfrom(udp_socket_t* sock, uint8_t* buffer, uint32_t len,
                        uint32_t* src_ip, uint16_t* src_port);
void udp_socket_close(udp_socket_t* sock);

#endif // UDP_H
