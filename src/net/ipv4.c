/*
 * === AOS HEADER BEGIN ===
 * ./src/net/ipv4.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.8.5
 * === AOS HEADER END ===
 */


/**
 * IPv4 Protocol Implementation
 */

#include <net/ipv4.h>
#include <net/ethernet.h>
#include <net/arp.h>
#include <net/icmp.h>
#include <net/tcp.h>
#include <net/udp.h>
#include <net/loopback.h>
#include <string.h>
#include <stdlib.h>
#include <vmm.h>
#include <serial.h>
#include <arch/i386/pit.h>

// IP packet ID counter
static uint16_t ip_id_counter = 1;


// Pending Packet Queue (for ARP resolution)


#define MAX_PENDING_PACKETS 32
#define ARP_RESOLVE_TIMEOUT_MS 3000
#define ARP_RESOLVE_RETRY_INTERVAL_MS 500

typedef struct {
    uint8_t* data;              // Packet data
    uint32_t len;               // Packet length
    uint32_t dest_ip;           // Destination IP
    uint32_t gateway_ip;        // Gateway to resolve (may differ from dest)
    net_interface_t* iface;     // Interface to send on
    uint32_t timestamp;         // When packet was queued
    uint32_t last_arp_time;     // Last ARP request time
    uint8_t valid;              // Slot in use
} pending_packet_t;

static pending_packet_t pending_packets[MAX_PENDING_PACKETS];

void ipv4_init(void) {
    serial_puts("Initializing IPv4...\n");
    
    // Clear pending packet queue
    memset(pending_packets, 0, sizeof(pending_packets));
    
    serial_puts("IPv4 initialized.\n");
}


// Packet Processing


int ipv4_receive(net_interface_t* iface, net_packet_t* packet) {
    if (!packet || packet->len < IPV4_HEADER_LEN) {
        return -1;
    }
    
    ipv4_header_t* ip_hdr = (ipv4_header_t*)packet->data;
    
    // Verify IP version
    if ((ip_hdr->version_ihl >> 4) != IPV4_VERSION) {
        return -1;
    }
    
    uint32_t dest_ip = ip_hdr->dest_addr;
    uint32_t src_ip = ip_hdr->src_addr;
    
    // CRITICAL: Accept packets in these cases:
    // 1. Interface not configured (ip_addr == 0) - DHCP bootstrap phase
    // 2. Destination is our IP
    // 3. Destination is broadcast (255.255.255.255)
    // 4. Destination is 0.0.0.0 (used by some DHCP servers)
    // 5. Destination matches subnet broadcast
    
    if (iface->ip_addr != 0) {
        uint32_t subnet_broadcast = iface->ip_addr | ~iface->netmask;
        
        if (dest_ip != iface->ip_addr && 
            dest_ip != 0xFFFFFFFF && 
            dest_ip != 0 &&
            dest_ip != subnet_broadcast) {
            return 0;  // Not for us, silently drop
        }
    }
    
    // Extract payload
    uint8_t ihl = (ip_hdr->version_ihl & 0x0F) * 4;
    uint16_t total_len = ntohs(ip_hdr->total_len);
    
    if (total_len > packet->len || ihl > total_len) {
        return -1;
    }
    
    net_packet_t payload_packet;
    payload_packet.data = packet->data + ihl;
    payload_packet.len = total_len - ihl;
    payload_packet.capacity = packet->capacity - ihl;
    
    // Dispatch based on protocol
    switch (ip_hdr->protocol) {
        case IP_PROTO_ICMP:
            return icmp_receive(iface, src_ip, &payload_packet);
        case IP_PROTO_TCP:
            return tcp_receive(iface, src_ip, dest_ip, &payload_packet);
        case IP_PROTO_UDP:
            return udp_receive(iface, src_ip, dest_ip, &payload_packet);
        default:
            return -1;
    }
}


// Packet Transmission with ARP Resolution


// Internal: Actually send the packet (MAC already resolved)
static int ipv4_send_internal(net_interface_t* iface, uint32_t dest_ip,
                               const mac_addr_t* dest_mac, 
                               const uint8_t* packet_data, uint32_t total_len) {
    (void)dest_ip;
    int ret;
    
    if (iface->flags & IFF_LOOPBACK) {
        net_packet_t packet;
        packet.data = (uint8_t*)packet_data;
        packet.len = total_len;
        packet.capacity = total_len;
        ret = net_transmit_packet(iface, &packet);
    } else {
        ret = eth_transmit(iface, dest_mac, ETH_TYPE_IPV4, packet_data, total_len);
    }
    
    return ret;
}

// Queue a packet for later transmission after ARP resolves
static int ipv4_queue_packet(net_interface_t* iface, uint32_t dest_ip, 
                              uint32_t gateway_ip, const uint8_t* data, uint32_t len) {
    // Find free slot
    for (int i = 0; i < MAX_PENDING_PACKETS; i++) {
        if (!pending_packets[i].valid) {
            pending_packet_t* pp = &pending_packets[i];
            
            pp->data = (uint8_t*)kmalloc(len);
            if (!pp->data) {
                return -1;
            }
            
            memcpy(pp->data, data, len);
            pp->len = len;
            pp->dest_ip = dest_ip;
            pp->gateway_ip = gateway_ip;
            pp->iface = iface;
            pp->timestamp = get_tick_count();
            pp->last_arp_time = pp->timestamp;
            pp->valid = 1;
            
            return 0;
        }
    }
    
    // Queue full
    return -1;
}

// Process pending packets (call this from network poll)
void ipv4_process_pending(void) {
    uint32_t now = get_tick_count();
    
    for (int i = 0; i < MAX_PENDING_PACKETS; i++) {
        pending_packet_t* pp = &pending_packets[i];
        
        if (!pp->valid) {
            continue;
        }
        
        // Check timeout
        if (now - pp->timestamp > ARP_RESOLVE_TIMEOUT_MS) {
            // Timed out, drop packet
            kfree(pp->data);
            pp->valid = 0;
            continue;
        }
        
        // Try to resolve ARP
        mac_addr_t dest_mac;
        if (arp_cache_lookup(pp->gateway_ip, &dest_mac) == 0) {
            // MAC resolved! Send the packet
            ipv4_send_internal(pp->iface, pp->dest_ip, &dest_mac, pp->data, pp->len);
            kfree(pp->data);
            pp->valid = 0;
        } else {
            // Not resolved yet, send another ARP request if interval passed
            if (now - pp->last_arp_time > ARP_RESOLVE_RETRY_INTERVAL_MS) {
                arp_send_request(pp->iface, pp->gateway_ip);
                pp->last_arp_time = now;
            }
        }
    }
}

// Public send function with automatic ARP resolution
int ipv4_send(net_interface_t* iface, uint32_t dest_ip, uint8_t protocol,
              const uint8_t* payload, uint32_t payload_len) {
    if (!iface || !payload) {
        return -1;
    }
    
    // Check if destination is loopback
    if ((dest_ip & 0xFF) == 0x7F) {
        net_interface_t* lo_iface = loopback_get_interface();
        if (lo_iface) {
            iface = lo_iface;
        }
    }
    
    // Allocate packet buffer
    uint32_t total_len = IPV4_HEADER_LEN + payload_len;
    uint8_t* packet_data = (uint8_t*)kmalloc(total_len);
    if (!packet_data) {
        return -1;
    }
    
    // Build IPv4 header
    ipv4_header_t* ip_hdr = (ipv4_header_t*)packet_data;
    memset(ip_hdr, 0, IPV4_HEADER_LEN);
    
    ip_hdr->version_ihl = (IPV4_VERSION << 4) | 5;
    ip_hdr->tos = 0;
    uint16_t current_id = ip_id_counter++;
    ip_hdr->total_len = htons(total_len);
    ip_hdr->id = htons(current_id);
    ip_hdr->flags_offset = htons(IP_FLAG_DF);
    ip_hdr->ttl = IPV4_DEFAULT_TTL;
    ip_hdr->protocol = protocol;
    ip_hdr->src_addr = iface->ip_addr;
    ip_hdr->dest_addr = dest_ip;
    
    // Calculate checksum
    ip_hdr->checksum = 0;
    ip_hdr->checksum = ipv4_checksum(ip_hdr, IPV4_HEADER_LEN);
    
    // Copy payload
    memcpy(packet_data + IPV4_HEADER_LEN, payload, payload_len);
    
    int ret = -1;
    
    // Route and send packet
    if (iface->flags & IFF_LOOPBACK) {
        // Loopback: send directly
        net_packet_t packet;
        packet.data = packet_data;
        packet.len = total_len;
        packet.capacity = total_len;
        ret = net_transmit_packet(iface, &packet);
    } else {
        mac_addr_t dest_mac;
        
        // Check if destination is broadcast
        if (dest_ip == 0xFFFFFFFF || dest_ip == (iface->ip_addr | ~iface->netmask)) {
            memset(&dest_mac, 0xFF, sizeof(mac_addr_t));
            ret = eth_transmit(iface, &dest_mac, ETH_TYPE_IPV4, packet_data, total_len);
        } else {
            // Determine next-hop (gateway or direct)
            uint32_t gateway_ip = dest_ip;
            if ((dest_ip & iface->netmask) != (iface->ip_addr & iface->netmask)) {
                if (iface->gateway) {
                    gateway_ip = iface->gateway;
                }
            }
            
            if (arp_cache_lookup(gateway_ip, &dest_mac) == 0) {
                // MAC found in cache
                ret = eth_transmit(iface, &dest_mac, ETH_TYPE_IPV4, packet_data, total_len);
            } else {
                // Queue packet and initiate ARP resolution
                if (ipv4_queue_packet(iface, dest_ip, gateway_ip, packet_data, total_len) == 0) {
                    arp_send_request(iface, gateway_ip);
                    ret = 0;  // Queued successfully (will be sent when ARP resolves)
                } else {
                    ret = -1;  // Queue full
                }
            }
        }
    }
    
    kfree(packet_data);
    return ret;
}


// Blocking ARP Resolution


// Resolve ARP with blocking wait (for synchronous operations like DNS)
int ipv4_resolve_arp(net_interface_t* iface, uint32_t ip, mac_addr_t* mac, 
                      uint32_t timeout_ms) {
    if (!iface || !mac) {
        return -1;
    }
    
    // Check cache first
    if (arp_cache_lookup(ip, mac) == 0) {
        return 0;
    }
    
    serial_puts("IPv4: ARP resolve ");
    serial_puts(ip_to_string(ip));
    serial_puts("...\n");
    
    // Send ARP request
    arp_send_request(iface, ip);
    
    // Wait for response with polling
    uint32_t start = get_tick_count();
    uint32_t last_request = start;
    
    while ((get_tick_count() - start) < timeout_ms) {
        // Yield to allow interrupts (critical for packet reception)
        __asm__ volatile("sti");
        __asm__ volatile("hlt");
        
        // Poll network for incoming packets
        extern void e1000_handle_interrupt(void);
        e1000_handle_interrupt();
        
        // Check if resolved
        if (arp_cache_lookup(ip, mac) == 0) {
            serial_puts("IPv4: ARP resolved!\n");
            return 0;
        }
        
        // Retry ARP request every 500ms
        if ((get_tick_count() - last_request) > 500) {
            serial_puts("IPv4: ARP retry\n");
            arp_send_request(iface, ip);
            last_request = get_tick_count();
        }
    }
    
    serial_puts("IPv4: ARP timeout\n");
    return -1;  // Timeout
}


// Utilities


uint16_t ipv4_checksum(const void* data, uint32_t len) {
    const uint16_t* ptr = (const uint16_t*)data;
    uint32_t sum = 0;
    
    while (len > 1) {
        sum += *ptr++;
        len -= 2;
    }
    
    if (len > 0) {
        sum += *(uint8_t*)ptr;
    }
    
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    
    return ~sum;
}

int ipv4_route(uint32_t dest_ip, net_interface_t** out_iface, uint32_t* out_gateway) {
    int iface_count = net_interface_count();
    
    for (int i = 0; i < iface_count; i++) {
        net_interface_t* iface = net_interface_get_by_index(i);
        if (!iface || !(iface->flags & IFF_UP)) {
            continue;
        }
        
        // Check if destination is loopback
        if ((dest_ip & 0xFF) == 0x7F && (iface->flags & IFF_LOOPBACK)) {
            *out_iface = iface;
            *out_gateway = dest_ip;
            return 0;
        }
        
        // Check if on local network
        if ((dest_ip & iface->netmask) == (iface->ip_addr & iface->netmask)) {
            *out_iface = iface;
            *out_gateway = dest_ip;
            return 0;
        }
    }
    
    // Use default route (first non-loopback interface with gateway)
    for (int i = 0; i < iface_count; i++) {
        net_interface_t* iface = net_interface_get_by_index(i);
        if (iface && (iface->flags & IFF_UP) && 
            !(iface->flags & IFF_LOOPBACK) && iface->gateway) {
            *out_iface = iface;
            *out_gateway = iface->gateway;
            return 0;
        }
    }
    
    return -1;
}
