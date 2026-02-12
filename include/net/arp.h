/*
 * === AOS HEADER BEGIN ===
 * include/net/arp.h
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */


#ifndef ARP_H
#define ARP_H

#include <stdint.h>
#include <net/net.h>

// ARP hardware types
#define ARP_HW_ETHERNET 0x0001

// ARP operations
#define ARP_OP_REQUEST  0x0001
#define ARP_OP_REPLY    0x0002

// ARP cache size
#define ARP_CACHE_SIZE 128
#define ARP_CACHE_TIMEOUT 300000  // 5 minutes in milliseconds

// ARP packet structure
typedef struct {
    uint16_t hw_type;       // Hardware type (Ethernet)
    uint16_t proto_type;    // Protocol type (IPv4)
    uint8_t hw_len;         // Hardware address length
    uint8_t proto_len;      // Protocol address length
    uint16_t operation;     // Operation (request/reply)
    mac_addr_t sender_mac;  // Sender hardware address
    uint32_t sender_ip;     // Sender protocol address
    mac_addr_t target_mac;  // Target hardware address
    uint32_t target_ip;     // Target protocol address
} __attribute__((packed)) arp_packet_t;

// ARP cache entry
typedef struct {
    uint32_t ip_addr;
    mac_addr_t mac_addr;
    uint32_t timestamp;
    uint8_t valid;
} arp_cache_entry_t;

// ARP initialization
void arp_init(void);

// ARP operations
int arp_receive(net_interface_t* iface, net_packet_t* packet);
int arp_send_request(net_interface_t* iface, uint32_t target_ip);
int arp_send_reply(net_interface_t* iface, uint32_t target_ip, const mac_addr_t* target_mac);

// ARP cache management
int arp_cache_lookup(uint32_t ip_addr, mac_addr_t* mac_addr);
void arp_cache_add(uint32_t ip_addr, const mac_addr_t* mac_addr);
void arp_cache_update(void);
void arp_cache_clear(void);

// ARP cache inspection
int arp_cache_get_entries(arp_cache_entry_t* entries, int max_entries);

#endif // ARP_H
