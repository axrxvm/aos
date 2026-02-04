/*
 * === AOS HEADER BEGIN ===
 * ./src/net/nat.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.8.5
 * === AOS HEADER END ===
 */


/* 
 * Network Address Translation (NAT) Module Implementation
 * 
 * Provides NAT functionality for routing between internal and external networks.
 * Features connection tracking, port forwarding, and checksum recalculation.
 */

#include <net/nat.h>
#include <net/net.h>
#include <net/ipv4.h>
#include <net/tcp.h>
#include <net/udp.h>
#include <net/icmp.h>
#include <net/ethernet.h>
#include <string.h>
#include <stdlib.h>
#include <serial.h>
#include <vmm.h>

// NAT table
static nat_entry_t nat_table[NAT_MAX_ENTRIES];
static nat_port_forward_t port_forwards[NAT_MAX_PORT_FORWARDS];
static nat_config_t nat_config;

// System tick counter (incremented externally)
static uint32_t nat_tick_count = 0;

// Statistics
static nat_stats_t nat_statistics;

// Initialization

void nat_init(void) {
    serial_puts("NAT: Initializing...\n");
    
    // Clear NAT table
    memset(nat_table, 0, sizeof(nat_table));
    memset(port_forwards, 0, sizeof(port_forwards));
    memset(&nat_config, 0, sizeof(nat_config));
    memset(&nat_statistics, 0, sizeof(nat_statistics));
    
    // Set default port range (49152-65535 are dynamic/private ports)
    nat_config.port_range_start = 49152;
    nat_config.port_range_end = 65535;
    nat_config.next_port = nat_config.port_range_start;
    
    nat_statistics.entries_max = NAT_MAX_ENTRIES;
    
    serial_puts("NAT: Initialized\n");
}

// NAT Enable/Disable

int nat_enable(nat_type_t type) {
    if (!nat_config.internal_iface || !nat_config.external_iface) {
        serial_puts("NAT: Cannot enable - interfaces not configured\n");
        return -1;
    }
    
    nat_config.enabled = 1;
    nat_config.type = type;
    
    // Use external interface IP as NAT IP if not set
    if (nat_config.external_ip == 0) {
        nat_config.external_ip = nat_config.external_iface->ip_addr;
    }
    
    serial_puts("NAT: Enabled (type=");
    char buf[8];
    itoa(type, buf, 10);
    serial_puts(buf);
    serial_puts(", external_ip=");
    serial_puts(ip_to_string(nat_config.external_ip));
    serial_puts(")\n");
    
    return 0;
}

int nat_disable(void) {
    nat_config.enabled = 0;
    
    // Clear all NAT entries
    for (int i = 0; i < NAT_MAX_ENTRIES; i++) {
        nat_table[i].used = 0;
    }
    
    nat_config.active_connections = 0;
    
    serial_puts("NAT: Disabled\n");
    return 0;
}

int nat_is_enabled(void) {
    return nat_config.enabled;
}

// Interface Configuration

int nat_set_internal_interface(net_interface_t* iface, uint32_t network, uint32_t netmask) {
    if (!iface) return -1;
    
    nat_config.internal_iface = iface;
    nat_config.internal_network = network;
    nat_config.internal_netmask = netmask;
    
    serial_puts("NAT: Internal interface set to ");
    serial_puts(iface->name);
    serial_puts(" (network=");
    serial_puts(ip_to_string(network));
    serial_puts("/");
    serial_puts(ip_to_string(netmask));
    serial_puts(")\n");
    
    return 0;
}

int nat_set_external_interface(net_interface_t* iface) {
    if (!iface) return -1;
    
    nat_config.external_iface = iface;
    nat_config.external_ip = iface->ip_addr;
    
    serial_puts("NAT: External interface set to ");
    serial_puts(iface->name);
    serial_puts(" (");
    serial_puts(ip_to_string(nat_config.external_ip));
    serial_puts(")\n");
    
    return 0;
}

int nat_set_external_ip(uint32_t ip) {
    nat_config.external_ip = ip;
    return 0;
}

// Utility Functions

int nat_is_internal_ip(uint32_t ip) {
    return (ip & nat_config.internal_netmask) == 
           (nat_config.internal_network & nat_config.internal_netmask);
}

uint16_t nat_allocate_port(void) {
    uint16_t start = nat_config.next_port;
    
    do {
        uint16_t port = nat_config.next_port;
        nat_config.next_port++;
        
        // Wrap around
        if (nat_config.next_port > nat_config.port_range_end) {
            nat_config.next_port = nat_config.port_range_start;
        }
        
        // Check if port is already in use
        int in_use = 0;
        for (int i = 0; i < NAT_MAX_ENTRIES; i++) {
            if (nat_table[i].used && nat_table[i].external_port == port) {
                in_use = 1;
                break;
            }
        }
        
        if (!in_use) {
            return port;
        }
    } while (nat_config.next_port != start);
    
    // All ports exhausted
    return 0;
}

// Incremental checksum adjustment (RFC 1624)
uint16_t nat_adjust_checksum(uint16_t old_checksum, uint16_t old_data, uint16_t new_data) {
    uint32_t sum;
    
    // ~old_checksum + ~old_data + new_data
    sum = (~old_checksum & 0xFFFF);
    sum += (~old_data & 0xFFFF);
    sum += new_data;
    
    // Fold 32-bit sum to 16 bits
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    
    return ~sum & 0xFFFF;
}

// Connection Tracking

nat_entry_t* nat_find_entry_internal(uint8_t protocol, uint32_t internal_ip,
                                      uint16_t internal_port, uint32_t remote_ip,
                                      uint16_t remote_port) {
    for (int i = 0; i < NAT_MAX_ENTRIES; i++) {
        nat_entry_t* entry = &nat_table[i];
        if (entry->used &&
            entry->protocol == protocol &&
            entry->internal_ip == internal_ip &&
            entry->internal_port == internal_port &&
            entry->remote_ip == remote_ip &&
            entry->remote_port == remote_port) {
            return entry;
        }
    }
    return NULL;
}

nat_entry_t* nat_find_entry_external(uint8_t protocol, uint32_t external_ip,
                                      uint16_t external_port, uint32_t remote_ip,
                                      uint16_t remote_port) {
    for (int i = 0; i < NAT_MAX_ENTRIES; i++) {
        nat_entry_t* entry = &nat_table[i];
        if (entry->used &&
            entry->protocol == protocol &&
            entry->external_ip == external_ip &&
            entry->external_port == external_port &&
            entry->remote_ip == remote_ip &&
            entry->remote_port == remote_port) {
            return entry;
        }
    }
    return NULL;
}

nat_entry_t* nat_create_entry(uint8_t protocol, uint32_t internal_ip,
                               uint16_t internal_port, uint32_t remote_ip,
                               uint16_t remote_port) {
    // Find free entry
    nat_entry_t* entry = NULL;
    for (int i = 0; i < NAT_MAX_ENTRIES; i++) {
        if (!nat_table[i].used) {
            entry = &nat_table[i];
            break;
        }
    }
    
    if (!entry) {
        // Try to find an expired entry
        nat_cleanup_expired();
        
        for (int i = 0; i < NAT_MAX_ENTRIES; i++) {
            if (!nat_table[i].used) {
                entry = &nat_table[i];
                break;
            }
        }
        
        if (!entry) {
            serial_puts("NAT: Table full\n");
            nat_statistics.packets_dropped++;
            return NULL;
        }
    }
    
    // Allocate external port
    uint16_t external_port = nat_allocate_port();
    if (external_port == 0) {
        serial_puts("NAT: Port allocation failed\n");
        return NULL;
    }
    
    // Initialize entry
    memset(entry, 0, sizeof(nat_entry_t));
    entry->used = 1;
    entry->protocol = protocol;
    entry->internal_ip = internal_ip;
    entry->internal_port = internal_port;
    entry->external_ip = nat_config.external_ip;
    entry->external_port = external_port;
    entry->remote_ip = remote_ip;
    entry->remote_port = remote_port;
    entry->state = NAT_STATE_NONE;
    entry->timestamp = nat_tick_count;
    
    // Set timeout based on protocol
    switch (protocol) {
        case NAT_PROTO_TCP:
            entry->timeout = NAT_ENTRY_TIMEOUT;
            break;
        case NAT_PROTO_UDP:
            entry->timeout = NAT_UDP_TIMEOUT;
            break;
        case NAT_PROTO_ICMP:
            entry->timeout = NAT_ICMP_TIMEOUT;
            break;
        default:
            entry->timeout = NAT_UDP_TIMEOUT;
    }
    
    nat_config.active_connections++;
    nat_config.total_connections++;
    nat_statistics.connections_created++;
    nat_statistics.entries_used++;
    
    return entry;
}

void nat_remove_entry(nat_entry_t* entry) {
    if (entry && entry->used) {
        entry->used = 0;
        nat_config.active_connections--;
        nat_statistics.entries_used--;
        nat_statistics.connections_expired++;
    }
}

void nat_touch_entry(nat_entry_t* entry) {
    if (entry) {
        entry->timestamp = nat_tick_count;
    }
}

void nat_cleanup_expired(void) {
    for (int i = 0; i < NAT_MAX_ENTRIES; i++) {
        nat_entry_t* entry = &nat_table[i];
        if (entry->used) {
            uint32_t age = nat_tick_count - entry->timestamp;
            if (age > entry->timeout) {
                nat_remove_entry(entry);
            }
        }
    }
}

// Port Forwarding

int nat_add_port_forward(uint8_t protocol, uint16_t external_port,
                         uint32_t internal_ip, uint16_t internal_port,
                         const char* description) {
    // Find free slot
    nat_port_forward_t* rule = NULL;
    for (int i = 0; i < NAT_MAX_PORT_FORWARDS; i++) {
        if (!port_forwards[i].enabled) {
            rule = &port_forwards[i];
            break;
        }
    }
    
    if (!rule) {
        serial_puts("NAT: Port forward table full\n");
        return -1;
    }
    
    rule->enabled = 1;
    rule->protocol = protocol;
    rule->external_port = external_port;
    rule->internal_ip = internal_ip;
    rule->internal_port = internal_port;
    
    if (description) {
        strncpy(rule->description, description, sizeof(rule->description) - 1);
    }
    
    nat_statistics.port_forwards_active++;
    
    serial_puts("NAT: Added port forward ");
    char buf[8];
    itoa(external_port, buf, 10);
    serial_puts(buf);
    serial_puts(" -> ");
    serial_puts(ip_to_string(internal_ip));
    serial_puts(":");
    itoa(internal_port, buf, 10);
    serial_puts(buf);
    serial_puts("\n");
    
    return 0;
}

int nat_remove_port_forward(uint16_t external_port, uint8_t protocol) {
    for (int i = 0; i < NAT_MAX_PORT_FORWARDS; i++) {
        nat_port_forward_t* rule = &port_forwards[i];
        if (rule->enabled && 
            rule->external_port == external_port &&
            (rule->protocol == protocol || rule->protocol == 0 || protocol == 0)) {
            rule->enabled = 0;
            nat_statistics.port_forwards_active--;
            return 0;
        }
    }
    return -1;
}

nat_port_forward_t* nat_find_port_forward(uint16_t external_port, uint8_t protocol) {
    for (int i = 0; i < NAT_MAX_PORT_FORWARDS; i++) {
        nat_port_forward_t* rule = &port_forwards[i];
        if (rule->enabled && 
            rule->external_port == external_port &&
            (rule->protocol == protocol || rule->protocol == 0)) {
            return rule;
        }
    }
    return NULL;
}

int nat_list_port_forwards(nat_port_forward_t* rules, int max_rules) {
    int count = 0;
    for (int i = 0; i < NAT_MAX_PORT_FORWARDS && count < max_rules; i++) {
        if (port_forwards[i].enabled) {
            rules[count++] = port_forwards[i];
        }
    }
    return count;
}

// Packet Processing

// Helper to get IP header from packet
static ipv4_header_t* get_ip_header(net_packet_t* packet) {
    if (!packet || !packet->data || packet->len < sizeof(eth_header_t) + sizeof(ipv4_header_t)) {
        return NULL;
    }
    return (ipv4_header_t*)(packet->data + sizeof(eth_header_t));
}

// Helper to get TCP header from packet
static tcp_header_t* get_tcp_header(net_packet_t* packet, ipv4_header_t* ip) {
    (void)packet;  // Reserved for future packet validation
    if (!ip || ip->protocol != NAT_PROTO_TCP) return NULL;
    
    uint8_t ihl = (ip->version_ihl & 0x0F) * 4;
    return (tcp_header_t*)((uint8_t*)ip + ihl);
}

// Helper to get UDP header from packet
static udp_header_t* get_udp_header(net_packet_t* packet, ipv4_header_t* ip) {
    (void)packet;  // Reserved for future packet validation
    if (!ip || ip->protocol != NAT_PROTO_UDP) return NULL;
    
    uint8_t ihl = (ip->version_ihl & 0x0F) * 4;
    return (udp_header_t*)((uint8_t*)ip + ihl);
}

// Recalculate IP header checksum
static void recalc_ip_checksum(ipv4_header_t* ip) {
    ip->checksum = 0;
    
    uint32_t sum = 0;
    uint16_t* ptr = (uint16_t*)ip;
    int len = (ip->version_ihl & 0x0F) * 2;  // Length in 16-bit words
    
    for (int i = 0; i < len; i++) {
        sum += ntohs(ptr[i]);
    }
    
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    
    ip->checksum = htons(~sum);
}

// Process outgoing packet (SNAT: internal -> external)
int nat_process_outgoing(net_packet_t* packet, net_interface_t* iface) {
    if (!nat_config.enabled || !packet) return 0;
    
    // Verify this is from internal interface
    if (iface != nat_config.internal_iface) return 0;
    
    ipv4_header_t* ip = get_ip_header(packet);
    if (!ip) return 0;
    
    // Only process packets from internal network
    if (!nat_is_internal_ip(ntohl(ip->src_addr))) return 0;
    
    uint32_t src_ip = ntohl(ip->src_addr);
    uint32_t dst_ip = ntohl(ip->dest_addr);
    uint16_t src_port = 0;
    uint16_t dst_port = 0;
    
    // Get ports based on protocol
    if (ip->protocol == NAT_PROTO_TCP) {
        tcp_header_t* tcp = get_tcp_header(packet, ip);
        if (!tcp) return -1;
        src_port = ntohs(tcp->src_port);
        dst_port = ntohs(tcp->dest_port);
    } else if (ip->protocol == NAT_PROTO_UDP) {
        udp_header_t* udp = get_udp_header(packet, ip);
        if (!udp) return -1;
        src_port = ntohs(udp->src_port);
        dst_port = ntohs(udp->dest_port);
    } else if (ip->protocol == NAT_PROTO_ICMP) {
        // For ICMP, use identifier as "port"
        icmp_header_t* icmp = (icmp_header_t*)((uint8_t*)ip + ((ip->version_ihl & 0x0F) * 4));
        src_port = ntohs(icmp->data.echo.id);
        dst_port = 0;
    } else {
        // Unsupported protocol - pass through
        return 0;
    }
    
    // Find or create NAT entry
    nat_entry_t* entry = nat_find_entry_internal(ip->protocol, src_ip, src_port, dst_ip, dst_port);
    
    if (!entry) {
        entry = nat_create_entry(ip->protocol, src_ip, src_port, dst_ip, dst_port);
        if (!entry) {
            return 1;  // Drop packet
        }
    }
    
    // Update timestamp
    nat_touch_entry(entry);
    
    // Perform translation
    uint32_t old_src_ip = ip->src_addr;
    uint16_t old_src_port = htons(src_port);
    
    // Replace source IP with external NAT IP
    ip->src_addr = htonl(nat_config.external_ip);
    
    // Replace source port with NAT port
    if (ip->protocol == NAT_PROTO_TCP) {
        tcp_header_t* tcp = get_tcp_header(packet, ip);
        tcp->src_port = htons(entry->external_port);
        
        // Update TCP checksum using incremental update
        tcp->checksum = nat_adjust_checksum(tcp->checksum, 
            (old_src_ip >> 16) & 0xFFFF, (ip->src_addr >> 16) & 0xFFFF);
        tcp->checksum = nat_adjust_checksum(tcp->checksum,
            old_src_ip & 0xFFFF, ip->src_addr & 0xFFFF);
        tcp->checksum = nat_adjust_checksum(tcp->checksum,
            old_src_port, tcp->src_port);
            
        // Track TCP connection state
        if (tcp->flags & TCP_FLAG_SYN) {
            entry->state = NAT_STATE_SYN_SENT;
        }
    } else if (ip->protocol == NAT_PROTO_UDP) {
        udp_header_t* udp = get_udp_header(packet, ip);
        udp->src_port = htons(entry->external_port);
        
        // UDP checksum is optional (0 means no checksum)
        if (udp->checksum != 0) {
            udp->checksum = nat_adjust_checksum(udp->checksum,
                (old_src_ip >> 16) & 0xFFFF, (ip->src_addr >> 16) & 0xFFFF);
            udp->checksum = nat_adjust_checksum(udp->checksum,
                old_src_ip & 0xFFFF, ip->src_addr & 0xFFFF);
            udp->checksum = nat_adjust_checksum(udp->checksum,
                old_src_port, udp->src_port);
        }
    } else if (ip->protocol == NAT_PROTO_ICMP) {
        icmp_header_t* icmp = (icmp_header_t*)((uint8_t*)ip + ((ip->version_ihl & 0x0F) * 4));
        uint16_t old_id = icmp->data.echo.id;
        icmp->data.echo.id = htons(entry->external_port);
        
        // Recalculate ICMP checksum
        icmp->checksum = nat_adjust_checksum(icmp->checksum, old_id, icmp->data.echo.id);
    }
    
    // Recalculate IP header checksum
    recalc_ip_checksum(ip);
    
    // Update statistics
    entry->packets_out++;
    entry->bytes_out += packet->len;
    nat_statistics.packets_translated++;
    nat_statistics.bytes_translated += packet->len;
    nat_config.total_bytes_out += packet->len;
    
    return 0;
}

// Process incoming packet (DNAT: external -> internal)
int nat_process_incoming(net_packet_t* packet, net_interface_t* iface) {
    if (!nat_config.enabled || !packet) return 0;
    
    // Verify this is from external interface
    if (iface != nat_config.external_iface) return 0;
    
    ipv4_header_t* ip = get_ip_header(packet);
    if (!ip) return 0;
    
    // Check if destination is our external IP
    if (ntohl(ip->dest_addr) != nat_config.external_ip) return 0;
    
    uint32_t src_ip = ntohl(ip->src_addr);
    uint16_t src_port = 0;
    uint16_t dst_port = 0;
    
    // Get ports based on protocol
    if (ip->protocol == NAT_PROTO_TCP) {
        tcp_header_t* tcp = get_tcp_header(packet, ip);
        if (!tcp) return -1;
        src_port = ntohs(tcp->src_port);
        dst_port = ntohs(tcp->dest_port);
    } else if (ip->protocol == NAT_PROTO_UDP) {
        udp_header_t* udp = get_udp_header(packet, ip);
        if (!udp) return -1;
        src_port = ntohs(udp->src_port);
        dst_port = ntohs(udp->dest_port);
    } else if (ip->protocol == NAT_PROTO_ICMP) {
        icmp_header_t* icmp = (icmp_header_t*)((uint8_t*)ip + ((ip->version_ihl & 0x0F) * 4));
        dst_port = ntohs(icmp->data.echo.id);
        src_port = 0;
    } else {
        return 0;
    }
    
    // Check for port forwarding rule first
    nat_port_forward_t* forward = nat_find_port_forward(dst_port, ip->protocol);
    if (forward) {
        // DNAT to internal host
        uint32_t old_dst_ip = ip->dest_addr;
        uint16_t old_dst_port = htons(dst_port);
        
        ip->dest_addr = htonl(forward->internal_ip);
        
        if (ip->protocol == NAT_PROTO_TCP) {
            tcp_header_t* tcp = get_tcp_header(packet, ip);
            tcp->dest_port = htons(forward->internal_port);
            
            tcp->checksum = nat_adjust_checksum(tcp->checksum,
                (old_dst_ip >> 16) & 0xFFFF, (ip->dest_addr >> 16) & 0xFFFF);
            tcp->checksum = nat_adjust_checksum(tcp->checksum,
                old_dst_ip & 0xFFFF, ip->dest_addr & 0xFFFF);
            tcp->checksum = nat_adjust_checksum(tcp->checksum,
                old_dst_port, tcp->dest_port);
        } else if (ip->protocol == NAT_PROTO_UDP) {
            udp_header_t* udp = get_udp_header(packet, ip);
            udp->dest_port = htons(forward->internal_port);
            
            if (udp->checksum != 0) {
                udp->checksum = nat_adjust_checksum(udp->checksum,
                    (old_dst_ip >> 16) & 0xFFFF, (ip->dest_addr >> 16) & 0xFFFF);
                udp->checksum = nat_adjust_checksum(udp->checksum,
                    old_dst_ip & 0xFFFF, ip->dest_addr & 0xFFFF);
                udp->checksum = nat_adjust_checksum(udp->checksum,
                    old_dst_port, udp->dest_port);
            }
        }
        
        recalc_ip_checksum(ip);
        nat_statistics.packets_translated++;
        nat_statistics.bytes_translated += packet->len;
        
        return 0;
    }
    
    // Find existing NAT entry
    nat_entry_t* entry = nat_find_entry_external(ip->protocol, nat_config.external_ip,
                                                  dst_port, src_ip, src_port);
    
    if (!entry) {
        // No NAT entry and no port forward - drop packet
        nat_statistics.packets_dropped++;
        return 1;
    }
    
    // Update timestamp
    nat_touch_entry(entry);
    
    // Perform reverse translation
    uint32_t old_dst_ip = ip->dest_addr;
    uint16_t old_dst_port = htons(dst_port);
    
    // Replace destination IP with internal IP
    ip->dest_addr = htonl(entry->internal_ip);
    
    // Replace destination port with internal port
    if (ip->protocol == NAT_PROTO_TCP) {
        tcp_header_t* tcp = get_tcp_header(packet, ip);
        tcp->dest_port = htons(entry->internal_port);
        
        tcp->checksum = nat_adjust_checksum(tcp->checksum,
            (old_dst_ip >> 16) & 0xFFFF, (ip->dest_addr >> 16) & 0xFFFF);
        tcp->checksum = nat_adjust_checksum(tcp->checksum,
            old_dst_ip & 0xFFFF, ip->dest_addr & 0xFFFF);
        tcp->checksum = nat_adjust_checksum(tcp->checksum,
            old_dst_port, tcp->dest_port);
            
        // Track TCP connection state
        if (tcp->flags & TCP_FLAG_SYN && tcp->flags & TCP_FLAG_ACK) {
            entry->state = NAT_STATE_SYN_RECEIVED;
        } else if (tcp->flags & TCP_FLAG_ACK && entry->state == NAT_STATE_SYN_RECEIVED) {
            entry->state = NAT_STATE_ESTABLISHED;
            entry->timeout = NAT_ENTRY_TIMEOUT;  // Extend timeout for established connections
        } else if (tcp->flags & TCP_FLAG_FIN) {
            entry->state = NAT_STATE_FIN_WAIT;
            entry->timeout = 30;  // Shorter timeout during close
        } else if (tcp->flags & TCP_FLAG_RST) {
            entry->state = NAT_STATE_CLOSED;
            entry->timeout = 5;
        }
    } else if (ip->protocol == NAT_PROTO_UDP) {
        udp_header_t* udp = get_udp_header(packet, ip);
        udp->dest_port = htons(entry->internal_port);
        
        if (udp->checksum != 0) {
            udp->checksum = nat_adjust_checksum(udp->checksum,
                (old_dst_ip >> 16) & 0xFFFF, (ip->dest_addr >> 16) & 0xFFFF);
            udp->checksum = nat_adjust_checksum(udp->checksum,
                old_dst_ip & 0xFFFF, ip->dest_addr & 0xFFFF);
            udp->checksum = nat_adjust_checksum(udp->checksum,
                old_dst_port, udp->dest_port);
        }
    } else if (ip->protocol == NAT_PROTO_ICMP) {
        icmp_header_t* icmp = (icmp_header_t*)((uint8_t*)ip + ((ip->version_ihl & 0x0F) * 4));
        uint16_t old_id = icmp->data.echo.id;
        icmp->data.echo.id = htons(entry->internal_port);
        
        icmp->checksum = nat_adjust_checksum(icmp->checksum, old_id, icmp->data.echo.id);
    }
    
    // Recalculate IP header checksum
    recalc_ip_checksum(ip);
    
    // Update statistics
    entry->packets_in++;
    entry->bytes_in += packet->len;
    nat_statistics.packets_translated++;
    nat_statistics.bytes_translated += packet->len;
    nat_config.total_bytes_in += packet->len;
    
    return 0;
}

// Statistics and Monitoring

void nat_get_stats(nat_stats_t* stats) {
    if (stats) {
        *stats = nat_statistics;
    }
}

nat_config_t* nat_get_config(void) {
    return &nat_config;
}

void nat_dump_table(void) {
    serial_puts("NAT Table:\n");
    serial_puts("-----------------------------------------------------------------\n");
    
    for (int i = 0; i < NAT_MAX_ENTRIES; i++) {
        nat_entry_t* entry = &nat_table[i];
        if (entry->used) {
            char buf[16];
            
            // Protocol
            if (entry->protocol == NAT_PROTO_TCP) {
                serial_puts("TCP ");
            } else if (entry->protocol == NAT_PROTO_UDP) {
                serial_puts("UDP ");
            } else if (entry->protocol == NAT_PROTO_ICMP) {
                serial_puts("ICMP ");
            }
            
            // Internal
            serial_puts(ip_to_string(entry->internal_ip));
            serial_puts(":");
            itoa(entry->internal_port, buf, 10);
            serial_puts(buf);
            
            serial_puts(" <-> ");
            
            // External
            serial_puts(ip_to_string(entry->external_ip));
            serial_puts(":");
            itoa(entry->external_port, buf, 10);
            serial_puts(buf);
            
            serial_puts(" -> ");
            
            // Remote
            serial_puts(ip_to_string(entry->remote_ip));
            serial_puts(":");
            itoa(entry->remote_port, buf, 10);
            serial_puts(buf);
            
            serial_puts("\n");
        }
    }
    
    serial_puts("-----------------------------------------------------------------\n");
    serial_puts("Active connections: ");
    char buf[16];
    itoa(nat_config.active_connections, buf, 10);
    serial_puts(buf);
    serial_puts("\n");
}

void nat_timer_tick(void) {
    nat_tick_count++;
    
    // Cleanup every 10 ticks
    if (nat_tick_count % 10 == 0) {
        nat_cleanup_expired();
    }
}

void nat_recompute_checksum(net_packet_t* packet, uint8_t protocol) {
    (void)packet;
    (void)protocol;
    // Full checksum recomputation - used when incremental update isn't possible
    // This is a placeholder for more complex scenarios
}
