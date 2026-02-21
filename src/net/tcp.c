/*
 * === AOS HEADER BEGIN ===
 * src/net/tcp.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */


/**
 * TCP Protocol Implementation
 * With full state machine, receive buffers, and retransmission
 */

#include <net/tcp.h>
#include <net/ipv4.h>
#include <net/net.h>
#include <string.h>
#include <stdlib.h>
#include <vmm.h>
#include <serial.h>
#include <arch/pit.h>

#define MAX_TCP_SOCKETS 32
#define TCP_RX_BUFFER_SIZE 16384
#define TCP_TX_BUFFER_SIZE 4096
#define TCP_CONNECT_TIMEOUT_MS 10000
#define TCP_RETRANSMIT_TIMEOUT_MS 1000
#define TCP_MAX_RETRANSMITS 5

// TCP socket table
static tcp_socket_t tcp_sockets[MAX_TCP_SOCKETS];

// Ephemeral port counter
static uint16_t next_ephemeral_port = 49152;


// TCP Pseudo-header for Checksum


typedef struct {
    uint32_t src_ip;
    uint32_t dest_ip;
    uint8_t zero;
    uint8_t protocol;
    uint16_t tcp_length;
} __attribute__((packed)) tcp_pseudo_header_t;


// TCP Checksum Calculation


static uint16_t tcp_checksum(uint32_t src_ip, uint32_t dest_ip, 
                              const uint8_t* tcp_data, uint32_t tcp_len) {
    uint32_t sum = 0;
    
    // Add pseudo-header fields directly (they're already in network byte order)
    // Sum as 16-bit words in network byte order
    sum += (src_ip >> 16) & 0xFFFF;
    sum += src_ip & 0xFFFF;
    sum += (dest_ip >> 16) & 0xFFFF;
    sum += dest_ip & 0xFFFF;
    sum += htons(IP_PROTO_TCP);
    sum += htons(tcp_len);
    
    // Add TCP segment (already in network byte order)
    const uint16_t* ptr = (const uint16_t*)tcp_data;
    uint32_t remaining = tcp_len;
    while (remaining > 1) {
        sum += *ptr++;
        remaining -= 2;
    }
    if (remaining == 1) {
        sum += *(uint8_t*)ptr;
    }
    
    // Fold 32-bit sum to 16 bits
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    
    return ~sum;
}


// Initialization


void tcp_init(void) {
    serial_puts("Initializing TCP...\n");
    
    memset(tcp_sockets, 0, sizeof(tcp_sockets));
    
    for (int i = 0; i < MAX_TCP_SOCKETS; i++) {
        tcp_sockets[i].state = TCP_CLOSED;
    }
    
    serial_puts("TCP initialized.\n");
}


// Socket Lookup


static tcp_socket_t* tcp_find_socket(uint16_t local_port, uint32_t remote_ip, 
                                      uint16_t remote_port) {
    // First, look for exact match (connected socket)
    for (int i = 0; i < MAX_TCP_SOCKETS; i++) {
        tcp_socket_t* sock = &tcp_sockets[i];
        if (sock->bound && sock->local_port == local_port &&
            sock->remote_ip == remote_ip && sock->remote_port == remote_port) {
            return sock;
        }
    }
    
    // Then, look for listening socket
    for (int i = 0; i < MAX_TCP_SOCKETS; i++) {
        tcp_socket_t* sock = &tcp_sockets[i];
        if (sock->bound && sock->local_port == local_port && 
            sock->state == TCP_LISTEN) {
            return sock;
        }
    }
    
    // Finally, look for any socket on this port
    for (int i = 0; i < MAX_TCP_SOCKETS; i++) {
        tcp_socket_t* sock = &tcp_sockets[i];
        if (sock->bound && sock->local_port == local_port) {
            return sock;
        }
    }
    
    return NULL;
}


// Receive Buffer Management


static int tcp_rx_buffer_write(tcp_socket_t* sock, const uint8_t* data, uint32_t len) {
    if (!sock || !data || len == 0) {
        return -1;
    }
    
    // Validate length is reasonable
    if (len > TCP_RX_BUFFER_SIZE) {
        serial_puts("TCP: Excessive data length, truncating\n");
        len = TCP_RX_BUFFER_SIZE;
    }
    
    if (!sock->rx_buffer) {
        sock->rx_buffer = (uint8_t*)kmalloc(TCP_RX_BUFFER_SIZE);
        if (!sock->rx_buffer) {
            return -1;
        }
        sock->rx_head = 0;
        sock->rx_tail = 0;
        sock->rx_size = TCP_RX_BUFFER_SIZE;
    }
    
    uint32_t available = (sock->rx_head - sock->rx_tail - 1 + sock->rx_size) % sock->rx_size;
    if (available == 0) available = sock->rx_size - 1;
    
    if (len > available) {
        len = available;
    }
    
    for (uint32_t i = 0; i < len; i++) {
        sock->rx_buffer[sock->rx_tail] = data[i];
        sock->rx_tail = (sock->rx_tail + 1) % sock->rx_size;
    }
    
    return len;
}

static int tcp_rx_buffer_read(tcp_socket_t* sock, uint8_t* buffer, uint32_t len) {
    if (!sock || !buffer || len == 0) {
        return 0;
    }
    
    if (!sock->rx_buffer) {
        return 0;
    }
    
    uint32_t available = (sock->rx_tail - sock->rx_head + sock->rx_size) % sock->rx_size;
    
    if (available == 0) {
        return 0;
    }
    
    if (len > available) {
        len = available;
    }
    
    for (uint32_t i = 0; i < len; i++) {
        buffer[i] = sock->rx_buffer[sock->rx_head];
        sock->rx_head = (sock->rx_head + 1) % sock->rx_size;
    }
    
    return len;
}

static uint32_t tcp_rx_buffer_available(tcp_socket_t* sock) {
    if (!sock->rx_buffer) {
        return 0;
    }
    return (sock->rx_tail - sock->rx_head + sock->rx_size) % sock->rx_size;
}


// Packet Transmission


int tcp_send(tcp_socket_t* sock, const uint8_t* data, uint32_t len, uint8_t flags) {
    if (!sock) {
        return -1;
    }
    
    // Debug output
    serial_puts("TCP: Tx to ");
    serial_puts(ip_to_string(sock->remote_ip));
    serial_puts(":");
    char port_str[8];
    itoa(sock->remote_port, port_str, 10);
    serial_puts(port_str);
    serial_puts(" flags=0x");
    char flags_str[8];
    itoa(flags, flags_str, 16);
    serial_puts(flags_str);
    serial_puts(" len=");
    char len_str[16];
    itoa(len, len_str, 10);
    serial_puts(len_str);
    serial_puts("\n");
    
    // Find appropriate interface
    net_interface_t* iface;
    uint32_t gateway;
    if (ipv4_route(sock->remote_ip, &iface, &gateway) != 0) {
        serial_puts("TCP: No route to host\n");
        return -1;
    }
    
    // Build TCP packet
    uint32_t total_len = TCP_HEADER_LEN + len;
    uint8_t* packet_data = (uint8_t*)kmalloc(total_len);
    if (!packet_data) {
        return -1;
    }
    
    tcp_header_t* tcp_hdr = (tcp_header_t*)packet_data;
    memset(tcp_hdr, 0, TCP_HEADER_LEN);
    
    tcp_hdr->src_port = htons(sock->local_port);
    tcp_hdr->dest_port = htons(sock->remote_port);
    tcp_hdr->seq_num = htonl(sock->seq_num);
    tcp_hdr->ack_num = htonl(sock->ack_num);
    tcp_hdr->data_offset_flags = (5 << 4);  // 5 * 4 = 20 bytes (no options)
    tcp_hdr->flags = flags;
    tcp_hdr->window_size = htons(sock->window_size);
    tcp_hdr->urgent_ptr = 0;
    
    // Copy data if present
    if (data && len > 0) {
        memcpy(packet_data + TCP_HEADER_LEN, data, len);
    }
    
    // Calculate checksum
    tcp_hdr->checksum = 0;
    tcp_hdr->checksum = tcp_checksum(iface->ip_addr, sock->remote_ip, packet_data, total_len);
    
    // Update sequence number
    if (flags & (TCP_FLAG_SYN | TCP_FLAG_FIN)) {
        sock->seq_num++;
    }
    if (data && len > 0) {
        sock->seq_num += len;
    }
    
    // Send via IPv4
    int ret = ipv4_send(iface, sock->remote_ip, IP_PROTO_TCP, packet_data, total_len);
    
    kfree(packet_data);
    return ret;
}


// Packet Reception


int tcp_receive(net_interface_t* iface, uint32_t src_ip, uint32_t dest_ip,
                net_packet_t* packet) {
    (void)iface;
    (void)dest_ip;
    
    if (!packet || packet->len < TCP_HEADER_LEN) {
        return -1;
    }
    
    tcp_header_t* tcp_hdr = (tcp_header_t*)packet->data;
    uint16_t dest_port = ntohs(tcp_hdr->dest_port);
    uint16_t src_port = ntohs(tcp_hdr->src_port);
    uint8_t flags = tcp_hdr->flags;
    uint32_t seq_num = ntohl(tcp_hdr->seq_num);
    uint32_t ack_num = ntohl(tcp_hdr->ack_num);
    
    // Calculate data offset
    uint8_t data_offset = ((tcp_hdr->data_offset_flags >> 4) & 0x0F) * 4;
    uint32_t payload_len = packet->len - data_offset;
    uint8_t* payload = packet->data + data_offset;
    
    // Debug: log received TCP packet
    serial_puts("TCP: Rx from ");
    serial_puts(ip_to_string(src_ip));
    serial_puts(":");
    char port_str[8];
    itoa(src_port, port_str, 10);
    serial_puts(port_str);
    serial_puts(" -> port ");
    itoa(dest_port, port_str, 10);
    serial_puts(port_str);
    serial_puts(" flags=0x");
    char flags_str[8];
    itoa(flags, flags_str, 16);
    serial_puts(flags_str);
    serial_puts(" len=");
    char len_str[16];
    itoa(payload_len, len_str, 10);
    serial_puts(len_str);
    serial_puts("\n");
    
    // Find matching socket
    tcp_socket_t* sock = tcp_find_socket(dest_port, src_ip, src_port);
    if (!sock) {
        serial_puts("TCP: No matching socket found!\n");
        // No socket, send RST if not RST
        if (!(flags & TCP_FLAG_RST)) {
            // TODO: Send RST packet
        }
        return -1;
    }
    
    // Handle RST
    if (flags & TCP_FLAG_RST) {
        sock->state = TCP_CLOSED;
        sock->error = 1;
        return 0;
    }
    
    // State machine processing
    serial_puts("TCP: Socket state=");
    serial_puts(tcp_state_to_string(sock->state));
    serial_puts("\n");
    
    switch (sock->state) {
        case TCP_LISTEN:
            if (flags & TCP_FLAG_SYN) {
                sock->remote_ip = src_ip;
                sock->remote_port = src_port;
                sock->ack_num = seq_num + 1;
                sock->state = TCP_SYN_RECEIVED;
                
                // Send SYN-ACK
                tcp_send(sock, NULL, 0, TCP_FLAG_SYN | TCP_FLAG_ACK);
            }
            break;
            
        case TCP_SYN_SENT:
            if ((flags & (TCP_FLAG_SYN | TCP_FLAG_ACK)) == (TCP_FLAG_SYN | TCP_FLAG_ACK)) {
                // SYN-ACK received
                serial_puts("TCP: SYN-ACK received, connection established!\n");
                sock->ack_num = seq_num + 1;
                sock->state = TCP_ESTABLISHED;
                
                // Send ACK
                tcp_send(sock, NULL, 0, TCP_FLAG_ACK);
            } else if (flags & TCP_FLAG_SYN) {
                // Simultaneous open
                sock->ack_num = seq_num + 1;
                sock->state = TCP_SYN_RECEIVED;
                
                // Send SYN-ACK
                tcp_send(sock, NULL, 0, TCP_FLAG_SYN | TCP_FLAG_ACK);
            }
            break;
            
        case TCP_SYN_RECEIVED:
            if (flags & TCP_FLAG_ACK) {
                if (ack_num == sock->seq_num) {
                    sock->state = TCP_ESTABLISHED;
                }
            }
            break;
            
        case TCP_ESTABLISHED:
            // Handle incoming data - only accept in-order packets
            if (payload_len > 0) {
                // Only accept if this is the expected sequence number
                if (seq_num == sock->ack_num) {
                    // Store data in receive buffer
                    tcp_rx_buffer_write(sock, payload, payload_len);
                    sock->ack_num = seq_num + payload_len;
                }
                // Always send ACK (even for out-of-order, to trigger retransmit)
                tcp_send(sock, NULL, 0, TCP_FLAG_ACK);
            }
            
            // Handle FIN (note: FIN consumes one sequence number)
            if (flags & TCP_FLAG_FIN) {
                // If there was data, ack_num already includes it
                // FIN sequence number is seq_num + payload_len for packets with data
                uint32_t fin_seq = seq_num + payload_len;
                if (fin_seq == sock->ack_num) {
                    sock->ack_num = fin_seq + 1;  // FIN consumes 1 seq number
                    sock->state = TCP_CLOSE_WAIT;
                    // Send ACK for the FIN
                    tcp_send(sock, NULL, 0, TCP_FLAG_ACK);
                }
            } else if (payload_len == 0 && (flags & TCP_FLAG_ACK)) {
                // Pure ACK with no data - no need to respond
            }
            break;
            
        case TCP_FIN_WAIT_1:
            // Still receive data while in FIN_WAIT_1
            if (payload_len > 0) {
                if (seq_num == sock->ack_num) {
                    tcp_rx_buffer_write(sock, payload, payload_len);
                    sock->ack_num = seq_num + payload_len;
                }
                tcp_send(sock, NULL, 0, TCP_FLAG_ACK);
            }
            if (flags & TCP_FLAG_ACK) {
                sock->state = TCP_FIN_WAIT_2;
            }
            if (flags & TCP_FLAG_FIN) {
                uint32_t fin_seq = seq_num + payload_len;
                if (fin_seq == sock->ack_num) {
                    sock->ack_num = fin_seq + 1;
                }
                tcp_send(sock, NULL, 0, TCP_FLAG_ACK);
                sock->state = TCP_TIME_WAIT;
            }
            break;
            
        case TCP_FIN_WAIT_2:
            // Still receive data while in FIN_WAIT_2
            if (payload_len > 0) {
                if (seq_num == sock->ack_num) {
                    tcp_rx_buffer_write(sock, payload, payload_len);
                    sock->ack_num = seq_num + payload_len;
                }
                tcp_send(sock, NULL, 0, TCP_FLAG_ACK);
            }
            if (flags & TCP_FLAG_FIN) {
                uint32_t fin_seq = seq_num + payload_len;
                if (fin_seq == sock->ack_num) {
                    sock->ack_num = fin_seq + 1;
                }
                tcp_send(sock, NULL, 0, TCP_FLAG_ACK);
                sock->state = TCP_TIME_WAIT;
            }
            break;
            
        case TCP_CLOSE_WAIT:
            // Application should call close
            break;
            
        case TCP_LAST_ACK:
            if (flags & TCP_FLAG_ACK) {
                sock->state = TCP_CLOSED;
                sock->bound = 0;
            }
            break;
            
        case TCP_TIME_WAIT:
            // Stay in TIME_WAIT for 2*MSL
            // For simplicity, just close
            sock->state = TCP_CLOSED;
            sock->bound = 0;
            break;
            
        default:
            break;
    }
    
    return 0;
}


// Socket API


tcp_socket_t* tcp_socket_create(void) {
    for (int i = 0; i < MAX_TCP_SOCKETS; i++) {
        if (tcp_sockets[i].state == TCP_CLOSED && !tcp_sockets[i].bound) {
            tcp_socket_t* sock = &tcp_sockets[i];
            memset(sock, 0, sizeof(tcp_socket_t));
            sock->state = TCP_CLOSED;
            sock->window_size = TCP_RX_BUFFER_SIZE;
            sock->seq_num = get_tick_count();  // Random-ish initial sequence
            sock->rx_buffer = NULL;
            sock->rx_size = 0;
            return sock;
        }
    }
    return NULL;
}

int tcp_socket_bind(tcp_socket_t* sock, uint32_t ip, uint16_t port) {
    if (!sock || sock->bound) {
        return -1;
    }
    
    // Allocate ephemeral port if not specified
    if (port == 0) {
        port = next_ephemeral_port++;
        if (next_ephemeral_port == 0) {  // Wrapped around
            next_ephemeral_port = 49152;
        }
    }
    
    // Check if port already in use
    for (int i = 0; i < MAX_TCP_SOCKETS; i++) {
        if (tcp_sockets[i].bound && tcp_sockets[i].local_port == port) {
            return -1;
        }
    }
    
    sock->local_ip = ip;
    sock->local_port = port;
    sock->bound = 1;
    
    return 0;
}

int tcp_socket_listen(tcp_socket_t* sock, int backlog) {
    (void)backlog;
    
    if (!sock || !sock->bound) {
        return -1;
    }
    
    sock->state = TCP_LISTEN;
    return 0;
}

tcp_socket_t* tcp_socket_accept(tcp_socket_t* sock) {
    if (!sock || sock->state != TCP_LISTEN) {
        return NULL;
    }
    
    // TODO: Implement proper accept queue
    return NULL;
}

int tcp_socket_connect(tcp_socket_t* sock, uint32_t ip, uint16_t port) {
    if (!sock) {
        return -1;
    }
    
    // Auto-bind if not bound
    if (!sock->bound) {
        if (tcp_socket_bind(sock, 0, 0) != 0) {
            return -1;
        }
    }
    
    sock->remote_ip = ip;
    sock->remote_port = port;
    sock->state = TCP_SYN_SENT;
    sock->error = 0;
    
    // Send SYN
    if (tcp_send(sock, NULL, 0, TCP_FLAG_SYN) != 0) {
        sock->state = TCP_CLOSED;
        return -1;
    }
    
    return 0;
}

// Connect with blocking wait for connection establishment
// Military-grade implementation with proper ARP pre-resolution
int tcp_socket_connect_blocking(tcp_socket_t* sock, uint32_t ip, uint16_t port, 
                                 uint32_t timeout_ms) {
    if (!sock) {
        return -1;
    }
    
    // Auto-bind if not bound
    if (!sock->bound) {
        if (tcp_socket_bind(sock, 0, 0) != 0) {
            serial_puts("TCP: Failed to bind socket\n");
            return -1;
        }
    }
    
    // Find route to destination
    net_interface_t* iface;
    uint32_t gateway;
    if (ipv4_route(ip, &iface, &gateway) != 0) {
        serial_puts("TCP: No route to host\n");
        return -1;
    }
    
    serial_puts("TCP: Route via gateway ");
    serial_puts(ip_to_string(gateway));
    serial_puts("\n");
    
    // Pre-resolve ARP for the gateway BEFORE sending SYN
    // This is critical - otherwise SYN gets queued and lost
    mac_addr_t gateway_mac;
    if (ipv4_resolve_arp(iface, gateway, &gateway_mac, 3000) != 0) {
        serial_puts("TCP: Gateway ARP resolution failed\n");
        return -1;
    }
    
    serial_puts("TCP: Gateway MAC resolved: ");
    // Print MAC address byte by byte
    for (int i = 0; i < 6; i++) {
        char hex[4];
        uint8_t b = gateway_mac.addr[i];
        hex[0] = "0123456789abcdef"[b >> 4];
        hex[1] = "0123456789abcdef"[b & 0xf];
        hex[2] = (i < 5) ? ':' : '\0';
        hex[3] = '\0';
        serial_puts(hex);
    }
    serial_puts("\n");
    
    // Now initiate TCP connection
    sock->remote_ip = ip;
    sock->remote_port = port;
    sock->state = TCP_SYN_SENT;
    sock->error = 0;
    
    // Send initial SYN
    serial_puts("TCP: Sending SYN to ");
    serial_puts(ip_to_string(ip));
    serial_puts(":");
    char port_str[8];
    itoa(port, port_str, 10);
    serial_puts(port_str);
    serial_puts("\n");
    
    if (tcp_send(sock, NULL, 0, TCP_FLAG_SYN) != 0) {
        serial_puts("TCP: Failed to send SYN\n");
        sock->state = TCP_CLOSED;
        return -1;
    }
    
    // Wait for connection with polling
    extern void net_poll(void);
    
    uint32_t start = get_tick_count();
    uint32_t last_syn = start;
    int retries = 0;
    
    while ((get_tick_count() - start) < timeout_ms) {
        // Yield to allow interrupts
        __asm__ volatile("sti");
        __asm__ volatile("hlt");
        
        // Poll for incoming packets (handles both e1000 and pcnet)
        net_poll();
        
        // Check if connected
        if (sock->state == TCP_ESTABLISHED) {
            serial_puts("TCP: Connection established!\n");
            return 0;
        }
        
        // Check for error (RST received, etc)
        if (sock->error) {
            serial_puts("TCP: Connection error (RST received)\n");
            sock->state = TCP_CLOSED;
            return -1;
        }
        
        if (sock->state == TCP_CLOSED) {
            serial_puts("TCP: Connection closed unexpectedly\n");
            return -1;
        }
        
        // Retransmit SYN if needed
        if ((get_tick_count() - last_syn) > TCP_RETRANSMIT_TIMEOUT_MS) {
            if (retries++ >= TCP_MAX_RETRANSMITS) {
                serial_puts("TCP: Max retransmits exceeded, giving up\n");
                sock->state = TCP_CLOSED;
                return -1;
            }
            serial_puts("TCP: Retransmitting SYN (retry ");
            char retry_str[8];
            itoa(retries, retry_str, 10);
            serial_puts(retry_str);
            serial_puts(")\n");
            sock->seq_num--;  // Reset seq_num since SYN wasn't ACKed
            tcp_send(sock, NULL, 0, TCP_FLAG_SYN);
            last_syn = get_tick_count();
        }
    }
    
    // Timeout
    serial_puts("TCP: Connection timeout after ");
    char ms_str[16];
    itoa(timeout_ms, ms_str, 10);
    serial_puts(ms_str);
    serial_puts("ms\n");
    sock->state = TCP_CLOSED;
    return -1;
}

int tcp_socket_send(tcp_socket_t* sock, const uint8_t* data, uint32_t len) {
    if (!sock || sock->state != TCP_ESTABLISHED) {
        return -1;
    }
    
    return tcp_send(sock, data, len, TCP_FLAG_PSH | TCP_FLAG_ACK);
}

int tcp_socket_recv(tcp_socket_t* sock, uint8_t* buffer, uint32_t len) {
    if (!sock || !buffer) {
        return -1;
    }
    
    // Check if connection is still valid
    if (sock->state != TCP_ESTABLISHED && 
        sock->state != TCP_CLOSE_WAIT &&
        sock->state != TCP_FIN_WAIT_1 &&
        sock->state != TCP_FIN_WAIT_2) {
        if (tcp_rx_buffer_available(sock) == 0) {
            return -1;
        }
    }
    
    // Read from buffer
    return tcp_rx_buffer_read(sock, buffer, len);
}

// Blocking receive with timeout - Military-grade implementation
int tcp_socket_recv_blocking(tcp_socket_t* sock, uint8_t* buffer, uint32_t len, 
                              uint32_t timeout_ms) {
    if (!sock || !buffer) {
        return -1;
    }
    
    extern void net_poll(void);
    
    uint32_t start = get_tick_count();
    
    while ((get_tick_count() - start) < timeout_ms) {
        // Yield to allow interrupts
        __asm__ volatile("sti");
        __asm__ volatile("hlt");
        
        // Poll for incoming packets (handles both e1000 and pcnet)
        net_poll();
        
        // Check if data available
        int received = tcp_rx_buffer_read(sock, buffer, len);
        if (received > 0) {
            return received;
        }
        
        // Check if connection closed
        if (sock->state == TCP_CLOSED || sock->state == TCP_CLOSE_WAIT) {
            if (tcp_rx_buffer_available(sock) == 0) {
                return 0;  // EOF
            }
        }
        
        if (sock->error) {
            return -1;
        }
    }
    
    return 0;  // Timeout, no data
}

void tcp_socket_close(tcp_socket_t* sock) {
    if (!sock) {
        return;
    }
    
    if (sock->state == TCP_ESTABLISHED || sock->state == TCP_CLOSE_WAIT) {
        sock->state = (sock->state == TCP_CLOSE_WAIT) ? TCP_LAST_ACK : TCP_FIN_WAIT_1;
        tcp_send(sock, NULL, 0, TCP_FLAG_FIN | TCP_FLAG_ACK);
    } else {
        sock->state = TCP_CLOSED;
    }
    
    // Free receive buffer
    if (sock->rx_buffer) {
        kfree(sock->rx_buffer);
        sock->rx_buffer = NULL;
    }
    
    sock->bound = 0;
}

const char* tcp_state_to_string(tcp_state_t state) {
    switch (state) {
        case TCP_CLOSED: return "CLOSED";
        case TCP_LISTEN: return "LISTEN";
        case TCP_SYN_SENT: return "SYN_SENT";
        case TCP_SYN_RECEIVED: return "SYN_RECEIVED";
        case TCP_ESTABLISHED: return "ESTABLISHED";
        case TCP_FIN_WAIT_1: return "FIN_WAIT_1";
        case TCP_FIN_WAIT_2: return "FIN_WAIT_2";
        case TCP_CLOSE_WAIT: return "CLOSE_WAIT";
        case TCP_CLOSING: return "CLOSING";
        case TCP_LAST_ACK: return "LAST_ACK";
        case TCP_TIME_WAIT: return "TIME_WAIT";
        default: return "UNKNOWN";
    }
}
