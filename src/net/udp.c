/*
 * === AOS HEADER BEGIN ===
 * ./src/net/udp.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.8.5
 * === AOS HEADER END ===
 */


#include <net/udp.h>
#include <net/ipv4.h>
#include <string.h>
#include <stdlib.h>
#include <vmm.h>
#include <serial.h>

#define MAX_UDP_SOCKETS 32

// Ephemeral port counter for UDP
static uint16_t udp_next_ephemeral_port = 49152;

// UDP socket table (dynamically allocated to avoid huge BSS)
static udp_socket_t* udp_sockets = NULL;

// UDP pseudo-header for checksum calculation
typedef struct {
    uint32_t src_ip;
    uint32_t dest_ip;
    uint8_t zero;
    uint8_t protocol;
    uint16_t udp_length;
} __attribute__((packed)) udp_pseudo_header_t;

void udp_init(void) {
    serial_puts("Initializing UDP...\n");
    
    // Allocate UDP sockets dynamically to avoid huge BSS section
    udp_sockets = (udp_socket_t*)kmalloc(sizeof(udp_socket_t) * MAX_UDP_SOCKETS);
    if (!udp_sockets) {
        serial_puts("FATAL: Failed to allocate UDP socket table\n");
        asm volatile("cli; hlt");
        while(1);
    }
    
    memset(udp_sockets, 0, sizeof(udp_socket_t) * MAX_UDP_SOCKETS);
    
    serial_puts("UDP initialized.\n");
}

int udp_receive(net_interface_t* iface, uint32_t src_ip, uint32_t dest_ip,
                net_packet_t* packet) {
    (void)iface;
    (void)dest_ip;
    
    // Validate all inputs
    if (!packet || !packet->data || packet->len < UDP_HEADER_LEN) {
        serial_puts("UDP: Invalid packet\n");
        return -1;
    }
    
    // Additional length sanity check
    if (packet->len > 65535) {
        serial_puts("UDP: Packet too large\n");
        return -1;
    }
    
    udp_header_t* udp_hdr = (udp_header_t*)packet->data;
    uint16_t dest_port = ntohs(udp_hdr->dest_port);
    uint16_t src_port = ntohs(udp_hdr->src_port);
    uint16_t udp_len = ntohs(udp_hdr->length);
    
    // Validate length
    if (udp_len < UDP_HEADER_LEN || udp_len > packet->len) {
        return -1;
    }
    
    //serial_puts("UDP: Rx port ");
    //char pbuf[8];
    //itoa(dest_port, pbuf, 10);
    //serial_puts(pbuf);
    //serial_puts(" from ");
   // serial_puts(ip_to_string(src_ip));
  //  serial_puts(":");
    //itoa(src_port, pbuf, 10);
    //serial_puts(pbuf);
    //serial_puts("\n");
    
    // Find socket bound to this port
    for (int i = 0; i < MAX_UDP_SOCKETS; i++) {
        if (udp_sockets[i].bound && udp_sockets[i].local_port == dest_port) {
            udp_socket_t* sock = &udp_sockets[i];
            uint32_t next_tail = (sock->rx_tail + 1) % UDP_RX_QUEUE_SIZE;
            
            if (next_tail == sock->rx_head) {
                serial_puts("UDP: Queue full!\n");
                return -1;
            }
            
            udp_rx_entry_t* entry = &sock->rx_queue[sock->rx_tail];
            uint32_t payload_len = udp_len - UDP_HEADER_LEN;
            
            // Validate payload_len is reasonable
            if (payload_len > UDP_RX_BUFFER_SIZE) {
                serial_puts("UDP: Payload too large, truncating\n");
                payload_len = UDP_RX_BUFFER_SIZE;
            }
            
            // Double-check we have enough data
            if (UDP_HEADER_LEN + payload_len > packet->len) {
                serial_puts("UDP: Payload length exceeds packet size\n");
                return -1;
            }
            
            memcpy(entry->data, packet->data + UDP_HEADER_LEN, payload_len);
            entry->len = payload_len;
            entry->src_ip = src_ip;
            entry->src_port = src_port;
            entry->valid = 1;
            
            sock->rx_tail = next_tail;
            
            serial_puts("UDP: Queued for socket\n");
            return 0;
        }
    }
    
    //serial_puts("UDP: No socket on port ");
    //itoa(dest_port, pbuf, 10);
    //serial_puts(pbuf);
    //serial_puts("\n");
    
    return -1;  // No socket listening
}

int udp_send(udp_socket_t* sock, const uint8_t* data, uint32_t len) {
    if (!sock || !data || !sock->connected) {
        return -1;
    }
    
    return udp_socket_sendto(sock, data, len, sock->remote_ip, sock->remote_port);
}

udp_socket_t* udp_socket_create(void) {
    for (int i = 0; i < MAX_UDP_SOCKETS; i++) {
        if (!udp_sockets[i].bound && !udp_sockets[i].connected) {
            memset(&udp_sockets[i], 0, sizeof(udp_socket_t));
            return &udp_sockets[i];
        }
    }
    return NULL;  // No free sockets
}

int udp_socket_bind(udp_socket_t* sock, uint32_t ip, uint16_t port) {
    if (!sock || sock->bound) {
        return -1;
    }
    
    // Allocate ephemeral port if not specified
    if (port == 0) {
        port = udp_next_ephemeral_port++;
        if (udp_next_ephemeral_port == 0) {
            udp_next_ephemeral_port = 49152;
        }
    }
    
    // Check if port is already in use
    for (int i = 0; i < MAX_UDP_SOCKETS; i++) {
        if (udp_sockets[i].bound && udp_sockets[i].local_port == port) {
            return -1;  // Port already in use
        }
    }
    
    sock->local_ip = ip;
    sock->local_port = port;
    sock->bound = 1;
    
    serial_puts("UDP: Bound to port ");
    char pbuf[8];
    itoa(port, pbuf, 10);
    serial_puts(pbuf);
    serial_puts("\n");
    
    return 0;
}

int udp_socket_connect(udp_socket_t* sock, uint32_t ip, uint16_t port) {
    if (!sock) {
        return -1;
    }
    
    sock->remote_ip = ip;
    sock->remote_port = port;
    sock->connected = 1;
    
    return 0;
}

int udp_socket_sendto(udp_socket_t* sock, const uint8_t* data, uint32_t len,
                      uint32_t dest_ip, uint16_t dest_port) {
    if (!sock || !data || len == 0) {
        serial_puts("UDP: Invalid send parameters\n");
        return -1;
    }
    
    // Validate length doesn't exceed max UDP payload
    if (len > 65507) { // 65535 - 20 (IP) - 8 (UDP)
        serial_puts("UDP: Payload too large\n");
        return -1;
    }
    
    // Find appropriate interface
    net_interface_t* iface;
    uint32_t gateway;
    if (ipv4_route(dest_ip, &iface, &gateway) != 0) {
        return -1;
    }
    
    // Use bound port
    uint16_t src_port = sock->local_port;
    
    // Build UDP packet
    uint32_t total_len = UDP_HEADER_LEN + len;
    uint8_t* packet_data = (uint8_t*)kmalloc(total_len);
    if (!packet_data) {
        return -1;
    }
    
    udp_header_t* udp_hdr = (udp_header_t*)packet_data;
    udp_hdr->src_port = htons(src_port);
    udp_hdr->dest_port = htons(dest_port);
    udp_hdr->length = htons(total_len);
    udp_hdr->checksum = 0;  // Optional for IPv4
    
    // Copy data
    memcpy(packet_data + UDP_HEADER_LEN, data, len);
    
    // Send via IPv4
    int ret = ipv4_send(iface, dest_ip, IP_PROTO_UDP, packet_data, total_len);
    
    kfree(packet_data);
    return ret;
}

int udp_socket_recvfrom(udp_socket_t* sock, uint8_t* buffer, uint32_t len,
                        uint32_t* src_ip, uint16_t* src_port) {
    if (!sock || !buffer) {
        return -1;
    }
    
    // Check if queue is empty
    if (sock->rx_head == sock->rx_tail) {
        return 0;  // No data available
    }
    
    // Dequeue packet
    udp_rx_entry_t* entry = &sock->rx_queue[sock->rx_head];
    uint32_t copy_len = entry->len < len ? entry->len : len;
    
    memcpy(buffer, entry->data, copy_len);
    if (src_ip) *src_ip = entry->src_ip;
    if (src_port) *src_port = entry->src_port;
    
    entry->valid = 0;
    sock->rx_head = (sock->rx_head + 1) % UDP_RX_QUEUE_SIZE;
    
    return copy_len;
}

void udp_socket_close(udp_socket_t* sock) {
    if (sock) {
        memset(sock, 0, sizeof(udp_socket_t));
    }
}
