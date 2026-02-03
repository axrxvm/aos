/*
 * === AOS HEADER BEGIN ===
 * ./include/net/net.h
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.8.5
 * === AOS HEADER END ===
 */


#ifndef NET_H
#define NET_H

#include <stdint.h>
#include <stddef.h>

// Network byte order conversion
#define htons(x) ((uint16_t)((((x) & 0xFF) << 8) | (((x) >> 8) & 0xFF)))
#define ntohs(x) htons(x)
#define htonl(x) ((uint32_t)((((x) & 0xFF) << 24) | (((x) & 0xFF00) << 8) | \
                             (((x) >> 8) & 0xFF00) | (((x) >> 24) & 0xFF)))
#define ntohl(x) htonl(x)

// Maximum transmission unit
#define MTU_SIZE 1500

// Network interface flags
#define IFF_UP          0x01  // Interface is up
#define IFF_BROADCAST   0x02  // Broadcast address valid
#define IFF_LOOPBACK    0x04  // Is a loopback net
#define IFF_RUNNING     0x08  // Resources allocated

// MAC address length
#define MAC_ADDR_LEN 6

// Maximum network interfaces
#define MAX_NET_INTERFACES 8

// Network statistics
typedef struct {
    uint32_t rx_packets;
    uint32_t rx_bytes;
    uint32_t rx_errors;
    uint32_t tx_packets;
    uint32_t tx_bytes;
    uint32_t tx_errors;
} net_stats_t;

// MAC address structure
typedef struct {
    uint8_t addr[MAC_ADDR_LEN];
} mac_addr_t;

// Network packet structure
typedef struct {
    uint8_t* data;
    uint32_t len;
    uint32_t capacity;
} net_packet_t;

// Network interface structure
typedef struct net_interface {
    char name[16];
    uint32_t flags;
    mac_addr_t mac_addr;
    uint32_t ip_addr;
    uint32_t netmask;
    uint32_t gateway;
    uint32_t mtu;
    net_stats_t stats;
    
    // Function pointers for interface operations
    int (*transmit)(struct net_interface* iface, net_packet_t* packet);
    int (*receive)(struct net_interface* iface, net_packet_t* packet);
} net_interface_t;

// Network initialization
void net_init(void);

// Network interface management
net_interface_t* net_interface_register(const char* name);
net_interface_t* net_interface_get(const char* name);
net_interface_t* net_interface_get_by_index(int index);
int net_interface_count(void);
int net_interface_up(net_interface_t* iface);
int net_interface_down(net_interface_t* iface);
int net_interface_set_ip(net_interface_t* iface, uint32_t ip, uint32_t netmask);

// Packet operations
net_packet_t* net_packet_alloc(uint32_t size);
void net_packet_free(net_packet_t* packet);

// Packet transmission
int net_transmit_packet(net_interface_t* iface, net_packet_t* packet);
int net_receive_packet(net_interface_t* iface, net_packet_t* packet);

// IP address utilities
const char* ip_to_string(uint32_t ip);
uint32_t string_to_ip(const char* str);

// MAC address utilities
static inline void mac_copy(mac_addr_t* dest, const mac_addr_t* src) {
    for (int i = 0; i < MAC_ADDR_LEN; i++) {
        dest->addr[i] = src->addr[i];
    }
}

// Network polling (processes pending packets, ARP, etc.)
void net_poll(void);

#endif // NET_H
