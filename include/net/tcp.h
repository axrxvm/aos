/*
 * === AOS HEADER BEGIN ===
 * include/net/tcp.h
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */


#ifndef TCP_H
#define TCP_H

#include <stdint.h>
#include <net/net.h>

// TCP header structure
typedef struct {
    uint16_t src_port;
    uint16_t dest_port;
    uint32_t seq_num;
    uint32_t ack_num;
    uint8_t data_offset_flags;  // Data offset (4 bits) + Reserved (3 bits) + NS flag (1 bit)
    uint8_t flags;              // CWR, ECE, URG, ACK, PSH, RST, SYN, FIN
    uint16_t window_size;
    uint16_t checksum;
    uint16_t urgent_ptr;
} __attribute__((packed)) tcp_header_t;

#define TCP_HEADER_LEN sizeof(tcp_header_t)

// TCP flags
#define TCP_FLAG_FIN    0x01
#define TCP_FLAG_SYN    0x02
#define TCP_FLAG_RST    0x04
#define TCP_FLAG_PSH    0x08
#define TCP_FLAG_ACK    0x10
#define TCP_FLAG_URG    0x20

// TCP states
typedef enum {
    TCP_CLOSED = 0,
    TCP_LISTEN,
    TCP_SYN_SENT,
    TCP_SYN_RECEIVED,
    TCP_ESTABLISHED,
    TCP_FIN_WAIT_1,
    TCP_FIN_WAIT_2,
    TCP_CLOSE_WAIT,
    TCP_CLOSING,
    TCP_LAST_ACK,
    TCP_TIME_WAIT
} tcp_state_t;

// TCP socket structure
typedef struct {
    uint16_t local_port;
    uint32_t local_ip;
    uint16_t remote_port;
    uint32_t remote_ip;
    tcp_state_t state;
    uint32_t seq_num;
    uint32_t ack_num;
    uint16_t window_size;
    uint8_t bound;
    uint8_t error;
    // Receive buffer
    uint8_t* rx_buffer;
    uint32_t rx_head;
    uint32_t rx_tail;
    uint32_t rx_size;
} tcp_socket_t;

// TCP initialization
void tcp_init(void);

// TCP packet processing
int tcp_receive(net_interface_t* iface, uint32_t src_ip, uint32_t dest_ip,
                net_packet_t* packet);
int tcp_send(tcp_socket_t* sock, const uint8_t* data, uint32_t len, uint8_t flags);

// TCP socket operations
tcp_socket_t* tcp_socket_create(void);
int tcp_socket_bind(tcp_socket_t* sock, uint32_t ip, uint16_t port);
int tcp_socket_listen(tcp_socket_t* sock, int backlog);
tcp_socket_t* tcp_socket_accept(tcp_socket_t* sock);
int tcp_socket_connect(tcp_socket_t* sock, uint32_t ip, uint16_t port);
int tcp_socket_send(tcp_socket_t* sock, const uint8_t* data, uint32_t len);
int tcp_socket_recv(tcp_socket_t* sock, uint8_t* buffer, uint32_t len);
void tcp_socket_close(tcp_socket_t* sock);

// Blocking operations
int tcp_socket_connect_blocking(tcp_socket_t* sock, uint32_t ip, uint16_t port, uint32_t timeout_ms);
int tcp_socket_recv_blocking(tcp_socket_t* sock, uint8_t* buffer, uint32_t len, uint32_t timeout_ms);

// TCP utilities
const char* tcp_state_to_string(tcp_state_t state);

#endif // TCP_H
