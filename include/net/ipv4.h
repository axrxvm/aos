/*
 * === AOS HEADER BEGIN ===
 * ./include/net/ipv4.h
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.8.5
 * === AOS HEADER END ===
 */


#ifndef IPV4_H
#define IPV4_H

#include <stdint.h>
#include <net/net.h>

// IP protocols
#define IP_PROTO_ICMP   1
#define IP_PROTO_TCP    6
#define IP_PROTO_UDP    17

// IP header flags
#define IP_FLAG_DF      0x4000  // Don't fragment
#define IP_FLAG_MF      0x2000  // More fragments

// IPv4 header structure
typedef struct {
    uint8_t version_ihl;    // Version (4 bits) + IHL (4 bits)
    uint8_t tos;            // Type of service
    uint16_t total_len;     // Total length
    uint16_t id;            // Identification
    uint16_t flags_offset;  // Flags (3 bits) + Fragment offset (13 bits)
    uint8_t ttl;            // Time to live
    uint8_t protocol;       // Protocol
    uint16_t checksum;      // Header checksum
    uint32_t src_addr;      // Source address
    uint32_t dest_addr;     // Destination address
} __attribute__((packed)) ipv4_header_t;

#define IPV4_HEADER_LEN sizeof(ipv4_header_t)
#define IPV4_VERSION 4
#define IPV4_DEFAULT_TTL 64

// IPv4 initialization
void ipv4_init(void);

// IPv4 packet processing
int ipv4_receive(net_interface_t* iface, net_packet_t* packet);
int ipv4_send(net_interface_t* iface, uint32_t dest_ip, uint8_t protocol,
              const uint8_t* payload, uint32_t payload_len);

// IPv4 utilities
uint16_t ipv4_checksum(const void* data, uint32_t len);
int ipv4_route(uint32_t dest_ip, net_interface_t** out_iface, uint32_t* out_gateway);

// ARP resolution and packet queue processing
void ipv4_process_pending(void);
int ipv4_resolve_arp(net_interface_t* iface, uint32_t ip, mac_addr_t* mac, uint32_t timeout_ms);

#endif // IPV4_H
