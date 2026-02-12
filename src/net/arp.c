/*
 * === AOS HEADER BEGIN ===
 * src/net/arp.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */


#include <net/arp.h>
#include <net/ethernet.h>
#include <string.h>
#include <stdlib.h>
#include <serial.h>

// ARP cache
static arp_cache_entry_t arp_cache[ARP_CACHE_SIZE];

// System tick counter (assume we have this for timing)
extern uint32_t get_tick_count(void);

void arp_init(void) {
    serial_puts("Initializing ARP...\n");
    
    // Clear ARP cache
    memset(arp_cache, 0, sizeof(arp_cache));
    
    serial_puts("ARP initialized.\n");
}

int arp_receive(net_interface_t* iface, net_packet_t* packet) {
    if (!packet || packet->len < sizeof(arp_packet_t)) {
        return -1;
    }
    
    arp_packet_t* arp = (arp_packet_t*)packet->data;
    
    // Verify it's Ethernet/IPv4 ARP
    if (ntohs(arp->hw_type) != ARP_HW_ETHERNET ||
        ntohs(arp->proto_type) != ETH_TYPE_IPV4) {
        return -1;
    }
    
    uint16_t operation = ntohs(arp->operation);
    
    // Update ARP cache with sender's mapping
    arp_cache_add(arp->sender_ip, &arp->sender_mac);
    
    // Check if this ARP is for us
    if (arp->target_ip != iface->ip_addr) {
        return 0;  // Not for us, but not an error
    }
    
    if (operation == ARP_OP_REQUEST) {
        // Send ARP reply
        arp_send_reply(iface, arp->sender_ip, &arp->sender_mac);
    }
    
    return 0;
}

int arp_send_request(net_interface_t* iface, uint32_t target_ip) {
    if (!iface) return -1;
    
    arp_packet_t arp;
    memset(&arp, 0, sizeof(arp));
    
    arp.hw_type = htons(ARP_HW_ETHERNET);
    arp.proto_type = htons(ETH_TYPE_IPV4);
    arp.hw_len = MAC_ADDR_LEN;
    arp.proto_len = 4;
    arp.operation = htons(ARP_OP_REQUEST);
    
    mac_copy(&arp.sender_mac, &iface->mac_addr);
    arp.sender_ip = iface->ip_addr;
    
    memset(&arp.target_mac, 0, MAC_ADDR_LEN);  // Unknown
    arp.target_ip = target_ip;
    
    // Broadcast MAC address
    mac_addr_t broadcast_mac;
    memset(&broadcast_mac, 0xFF, MAC_ADDR_LEN);
    
    return eth_transmit(iface, &broadcast_mac, ETH_TYPE_ARP,
                       (uint8_t*)&arp, sizeof(arp));
}

int arp_send_reply(net_interface_t* iface, uint32_t target_ip, const mac_addr_t* target_mac) {
    if (!iface || !target_mac) return -1;
    
    arp_packet_t arp;
    memset(&arp, 0, sizeof(arp));
    
    arp.hw_type = htons(ARP_HW_ETHERNET);
    arp.proto_type = htons(ETH_TYPE_IPV4);
    arp.hw_len = MAC_ADDR_LEN;
    arp.proto_len = 4;
    arp.operation = htons(ARP_OP_REPLY);
    
    mac_copy(&arp.sender_mac, &iface->mac_addr);
    arp.sender_ip = iface->ip_addr;
    
    mac_copy(&arp.target_mac, target_mac);
    arp.target_ip = target_ip;
    
    return eth_transmit(iface, target_mac, ETH_TYPE_ARP,
                       (uint8_t*)&arp, sizeof(arp));
}

int arp_cache_lookup(uint32_t ip_addr, mac_addr_t* mac_addr) {
    uint32_t current_time = get_tick_count();
    
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].valid && arp_cache[i].ip_addr == ip_addr) {
            // Check if entry is still valid (not expired)
            if (current_time - arp_cache[i].timestamp < ARP_CACHE_TIMEOUT) {
                if (mac_addr) {
                    mac_copy(mac_addr, &arp_cache[i].mac_addr);
                }
                return 0;
            } else {
                // Expired entry
                arp_cache[i].valid = 0;
            }
        }
    }
    
    return -1;  // Not found
}

void arp_cache_add(uint32_t ip_addr, const mac_addr_t* mac_addr) {
    if (!mac_addr) return;
    
    uint32_t current_time = get_tick_count();
    
    // Check if entry already exists
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].valid && arp_cache[i].ip_addr == ip_addr) {
            // Update existing entry
            mac_copy(&arp_cache[i].mac_addr, mac_addr);
            arp_cache[i].timestamp = current_time;
            return;
        }
    }
    
    // Find free slot
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (!arp_cache[i].valid) {
            arp_cache[i].ip_addr = ip_addr;
            mac_copy(&arp_cache[i].mac_addr, mac_addr);
            arp_cache[i].timestamp = current_time;
            arp_cache[i].valid = 1;
            return;
        }
    }
    
    // Cache full, replace oldest entry
    int oldest = 0;
    uint32_t oldest_time = arp_cache[0].timestamp;
    
    for (int i = 1; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].timestamp < oldest_time) {
            oldest = i;
            oldest_time = arp_cache[i].timestamp;
        }
    }
    
    arp_cache[oldest].ip_addr = ip_addr;
    mac_copy(&arp_cache[oldest].mac_addr, mac_addr);
    arp_cache[oldest].timestamp = current_time;
    arp_cache[oldest].valid = 1;
}

void arp_cache_update(void) {
    uint32_t current_time = get_tick_count();
    
    // Remove expired entries
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].valid) {
            if (current_time - arp_cache[i].timestamp >= ARP_CACHE_TIMEOUT) {
                arp_cache[i].valid = 0;
            }
        }
    }
}

void arp_cache_clear(void) {
    memset(arp_cache, 0, sizeof(arp_cache));
}

int arp_cache_get_entries(arp_cache_entry_t* entries, int max_entries) {
    int count = 0;
    
    for (int i = 0; i < ARP_CACHE_SIZE && count < max_entries; i++) {
        if (arp_cache[i].valid) {
            entries[count++] = arp_cache[i];
        }
    }
    
    return count;
}
