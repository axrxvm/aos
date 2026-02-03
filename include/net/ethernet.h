/*
 * === AOS HEADER BEGIN ===
 * ./include/net/ethernet.h
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.8.5
 * === AOS HEADER END ===
 */


#ifndef ETHERNET_H
#define ETHERNET_H

#include <stdint.h>
#include <net/net.h>

// Ethernet frame types (EtherType)
#define ETH_TYPE_IPV4   0x0800
#define ETH_TYPE_ARP    0x0806
#define ETH_TYPE_IPV6   0x86DD

// Ethernet header structure
typedef struct {
    mac_addr_t dest;
    mac_addr_t src;
    uint16_t ethertype;
} __attribute__((packed)) eth_header_t;

#define ETH_HEADER_LEN sizeof(eth_header_t)

// Ethernet frame processing
int eth_receive(net_interface_t* iface, net_packet_t* packet);
int eth_transmit(net_interface_t* iface, const mac_addr_t* dest_mac, 
                 uint16_t ethertype, const uint8_t* payload, uint32_t payload_len);

// MAC address utilities
void mac_to_string(const mac_addr_t* mac, char* str);
int mac_compare(const mac_addr_t* mac1, const mac_addr_t* mac2);
void mac_copy(mac_addr_t* dest, const mac_addr_t* src);

#endif // ETHERNET_H
