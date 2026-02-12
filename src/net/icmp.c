/*
 * === AOS HEADER BEGIN ===
 * src/net/icmp.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */


#include <net/icmp.h>
#include <net/ipv4.h>
#include <string.h>
#include <stdlib.h>
#include <vmm.h>
#include <serial.h>

// Ping callback
static ping_callback_t ping_callback = NULL;

// Ping tracking
static uint16_t ping_id = 0;
static uint32_t ping_start_time = 0;

extern uint32_t get_tick_count(void);

void icmp_init(void) {
    serial_puts("Initializing ICMP...\n");
    serial_puts("ICMP initialized.\n");
}

int icmp_receive(net_interface_t* iface, uint32_t src_ip, net_packet_t* packet) {
    (void)iface;  // Unused
    
    if (!packet || packet->len < ICMP_HEADER_LEN) {
        return -1;
    }
    
    icmp_header_t* icmp_hdr = (icmp_header_t*)packet->data;
    
    // Verify checksum
    uint16_t received_checksum = icmp_hdr->checksum;
    icmp_hdr->checksum = 0;
    uint16_t calculated_checksum = ipv4_checksum(packet->data, packet->len);
    icmp_hdr->checksum = received_checksum;
    
    if (received_checksum != calculated_checksum) {
        serial_puts("ICMP: Checksum mismatch\n");
        return -1;
    }
    
    switch (icmp_hdr->type) {
        case ICMP_TYPE_ECHO_REQUEST: {
            // Send echo reply
            uint8_t* data = packet->data + ICMP_HEADER_LEN;
            uint32_t data_len = packet->len - ICMP_HEADER_LEN;
            
            net_interface_t* reply_iface;
            uint32_t gateway;
            if (ipv4_route(src_ip, &reply_iface, &gateway) == 0) {
                icmp_send_echo_reply(reply_iface, src_ip,
                                    ntohs(icmp_hdr->data.echo.id),
                                    ntohs(icmp_hdr->data.echo.sequence),
                                    data, data_len);
            }
            break;
        }
        
        case ICMP_TYPE_ECHO_REPLY: {
            // Calculate round-trip time
            uint32_t current_time = get_tick_count();
            uint32_t rtt = current_time - ping_start_time;
            
            uint16_t id = ntohs(icmp_hdr->data.echo.id);
            uint16_t sequence = ntohs(icmp_hdr->data.echo.sequence);
            
            // Call callback if registered
            if (ping_callback && id == ping_id) {
                ping_callback(src_ip, sequence, rtt);
            }
            break;
        }
        
        default:
            // Other ICMP types not handled
            break;
    }
    
    return 0;
}

int icmp_send_echo_request(uint32_t dest_ip, uint16_t id, uint16_t sequence,
                           const uint8_t* data, uint32_t data_len) {
    // Find appropriate interface
    net_interface_t* iface;
    uint32_t gateway;
    
    if (ipv4_route(dest_ip, &iface, &gateway) != 0) {
        serial_puts("ICMP: No route to host\n");
        return -1;
    }
    
    // Build ICMP packet
    uint32_t total_len = ICMP_HEADER_LEN + data_len;
    uint8_t* packet_data = (uint8_t*)kmalloc(total_len);
    if (!packet_data) {
        return -1;
    }
    
    icmp_header_t* icmp_hdr = (icmp_header_t*)packet_data;
    icmp_hdr->type = ICMP_TYPE_ECHO_REQUEST;
    icmp_hdr->code = 0;
    icmp_hdr->data.echo.id = htons(id);
    icmp_hdr->data.echo.sequence = htons(sequence);
    
    // Copy data
    if (data && data_len > 0) {
        memcpy(packet_data + ICMP_HEADER_LEN, data, data_len);
    }
    
    // Calculate checksum
    icmp_hdr->checksum = 0;
    icmp_hdr->checksum = ipv4_checksum(packet_data, total_len);
    
    // Store ping tracking info
    ping_id = id;
    ping_start_time = get_tick_count();
    
    // Send via IPv4
    int ret = ipv4_send(iface, dest_ip, IP_PROTO_ICMP, packet_data, total_len);
    
    kfree(packet_data);
    return ret;
}

int icmp_send_echo_reply(net_interface_t* iface, uint32_t dest_ip,
                         uint16_t id, uint16_t sequence,
                         const uint8_t* data, uint32_t data_len) {
    // Build ICMP packet
    uint32_t total_len = ICMP_HEADER_LEN + data_len;
    uint8_t* packet_data = (uint8_t*)kmalloc(total_len);
    if (!packet_data) {
        return -1;
    }
    
    icmp_header_t* icmp_hdr = (icmp_header_t*)packet_data;
    icmp_hdr->type = ICMP_TYPE_ECHO_REPLY;
    icmp_hdr->code = 0;
    icmp_hdr->data.echo.id = htons(id);
    icmp_hdr->data.echo.sequence = htons(sequence);
    
    // Copy data
    if (data && data_len > 0) {
        memcpy(packet_data + ICMP_HEADER_LEN, data, data_len);
    }
    
    // Calculate checksum
    icmp_hdr->checksum = 0;
    icmp_hdr->checksum = ipv4_checksum(packet_data, total_len);
    
    // Send via IPv4
    int ret = ipv4_send(iface, dest_ip, IP_PROTO_ICMP, packet_data, total_len);
    
    kfree(packet_data);
    return ret;
}

void icmp_set_ping_callback(ping_callback_t callback) {
    ping_callback = callback;
}
