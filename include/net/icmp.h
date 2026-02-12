/*
 * === AOS HEADER BEGIN ===
 * include/net/icmp.h
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */


#ifndef ICMP_H
#define ICMP_H

#include <stdint.h>
#include <net/net.h>

// ICMP message types
#define ICMP_TYPE_ECHO_REPLY    0
#define ICMP_TYPE_DEST_UNREACH  3
#define ICMP_TYPE_ECHO_REQUEST  8
#define ICMP_TYPE_TIME_EXCEEDED 11

// ICMP destination unreachable codes
#define ICMP_CODE_NET_UNREACH   0
#define ICMP_CODE_HOST_UNREACH  1
#define ICMP_CODE_PROTO_UNREACH 2
#define ICMP_CODE_PORT_UNREACH  3

// ICMP header structure
typedef struct {
    uint8_t type;
    uint8_t code;
    uint16_t checksum;
    union {
        struct {
            uint16_t id;
            uint16_t sequence;
        } echo;
        uint32_t gateway;
        uint32_t unused;
    } data;
} __attribute__((packed)) icmp_header_t;

#define ICMP_HEADER_LEN sizeof(icmp_header_t)

// ICMP initialization
void icmp_init(void);

// ICMP packet processing
int icmp_receive(net_interface_t* iface, uint32_t src_ip, net_packet_t* packet);
int icmp_send_echo_request(uint32_t dest_ip, uint16_t id, uint16_t sequence, 
                           const uint8_t* data, uint32_t data_len);
int icmp_send_echo_reply(net_interface_t* iface, uint32_t dest_ip, 
                         uint16_t id, uint16_t sequence,
                         const uint8_t* data, uint32_t data_len);

// Ping callback for receiving replies
typedef void (*ping_callback_t)(uint32_t src_ip, uint16_t sequence, uint32_t rtt_ms);
void icmp_set_ping_callback(ping_callback_t callback);

#endif // ICMP_H
