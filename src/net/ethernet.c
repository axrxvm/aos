/*
 * === AOS HEADER BEGIN ===
 * src/net/ethernet.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */


#include <net/ethernet.h>
#include <net/arp.h>
#include <net/ipv4.h>
#include <string.h>
#include <stdlib.h>
#include <serial.h>

int eth_receive(net_interface_t* iface, net_packet_t* packet) {
    if (!packet || packet->len < ETH_HEADER_LEN) {
        return -1;
    }
    
    eth_header_t* eth_hdr = (eth_header_t*)packet->data;
    uint16_t ethertype = ntohs(eth_hdr->ethertype);
    
    // Create new packet for payload (skip Ethernet header)
    net_packet_t payload_packet;
    payload_packet.data = packet->data + ETH_HEADER_LEN;
    payload_packet.len = packet->len - ETH_HEADER_LEN;
    payload_packet.capacity = packet->capacity - ETH_HEADER_LEN;
    
    // Dispatch based on EtherType
    switch (ethertype) {
        case ETH_TYPE_ARP:
            return arp_receive(iface, &payload_packet);
        case ETH_TYPE_IPV4:
            return ipv4_receive(iface, &payload_packet);
        default:
            // Unknown protocol, drop packet
            return -1;
    }
}

int eth_transmit(net_interface_t* iface, const mac_addr_t* dest_mac,
                 uint16_t ethertype, const uint8_t* payload, uint32_t payload_len) {
    if (!iface || !dest_mac || !payload) {
        return -1;
    }
    
    uint32_t total_len = ETH_HEADER_LEN + payload_len;
    net_packet_t* packet = net_packet_alloc(total_len);
    if (!packet) {
        return -1;
    }
    
    // Fill Ethernet header
    eth_header_t* eth_hdr = (eth_header_t*)packet->data;
    mac_copy(&eth_hdr->dest, dest_mac);
    mac_copy(&eth_hdr->src, &iface->mac_addr);
    eth_hdr->ethertype = htons(ethertype);
    
    // Copy payload
    memcpy(packet->data + ETH_HEADER_LEN, payload, payload_len);
    packet->len = total_len;
    
    // Transmit packet
    int ret = net_transmit_packet(iface, packet);
    
    net_packet_free(packet);
    return ret;
}

void mac_to_string(const mac_addr_t* mac, char* str) {
    static const char hex[] = "0123456789ABCDEF";
    
    for (int i = 0; i < MAC_ADDR_LEN; i++) {
        str[i * 3] = hex[(mac->addr[i] >> 4) & 0x0F];
        str[i * 3 + 1] = hex[mac->addr[i] & 0x0F];
        if (i < MAC_ADDR_LEN - 1) {
            str[i * 3 + 2] = ':';
        }
    }
    str[MAC_ADDR_LEN * 3 - 1] = '\0';
}

int mac_compare(const mac_addr_t* mac1, const mac_addr_t* mac2) {
    return memcmp(mac1->addr, mac2->addr, MAC_ADDR_LEN);
}
