/*
 * === AOS HEADER BEGIN ===
 * include/net/nat.h
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */


/*
* Network Address Translation (NAT) Module Header
 * 
 * Provides NAT functionality for routing between internal and external networks.
 * Supports SNAT (Source NAT), DNAT (Destination NAT), and port forwarding.
 */

#ifndef NAT_H
#define NAT_H

#include <stdint.h>
#include <net/net.h>

// Maximum NAT table entries
#define NAT_MAX_ENTRIES         256
#define NAT_MAX_PORT_FORWARDS   32

// NAT entry timeout in seconds
#define NAT_ENTRY_TIMEOUT       300     // 5 minutes for TCP
#define NAT_UDP_TIMEOUT         30      // 30 seconds for UDP
#define NAT_ICMP_TIMEOUT        30      // 30 seconds for ICMP

// NAT Types
typedef enum {
    NAT_TYPE_NONE = 0,
    NAT_TYPE_SNAT,          // Source NAT (masquerading)
    NAT_TYPE_DNAT,          // Destination NAT
    NAT_TYPE_FULL,          // Full cone NAT (symmetric)
    NAT_TYPE_RESTRICTED,    // Restricted cone NAT
    NAT_TYPE_PORT_RESTRICTED // Port restricted cone NAT
} nat_type_t;

// Protocol types for NAT tracking
typedef enum {
    NAT_PROTO_TCP = 6,
    NAT_PROTO_UDP = 17,
    NAT_PROTO_ICMP = 1
} nat_protocol_t;

// Connection state (for TCP)
typedef enum {
    NAT_STATE_NONE = 0,
    NAT_STATE_SYN_SENT,
    NAT_STATE_SYN_RECEIVED,
    NAT_STATE_ESTABLISHED,
    NAT_STATE_FIN_WAIT,
    NAT_STATE_CLOSE_WAIT,
    NAT_STATE_TIME_WAIT,
    NAT_STATE_CLOSED
} nat_conn_state_t;

// NAT table entry for connection tracking
typedef struct {
    uint8_t used;               // Entry in use flag
    uint8_t protocol;           // TCP, UDP, or ICMP
    
    // Original (internal) connection info
    uint32_t internal_ip;       // Internal host IP
    uint16_t internal_port;     // Internal host port
    
    // Translated (external) connection info
    uint32_t external_ip;       // External (NAT) IP
    uint16_t external_port;     // External (NAT) port
    
    // Remote endpoint info
    uint32_t remote_ip;         // Remote host IP
    uint16_t remote_port;       // Remote host port
    
    // Connection tracking
    nat_conn_state_t state;     // TCP connection state
    uint32_t timestamp;         // Last activity timestamp
    uint32_t timeout;           // Entry timeout value
    
    // Statistics
    uint32_t packets_in;        // Packets received
    uint32_t packets_out;       // Packets sent
    uint64_t bytes_in;          // Bytes received
    uint64_t bytes_out;         // Bytes sent
} nat_entry_t;

// Port forwarding rule
typedef struct {
    uint8_t enabled;            // Rule enabled flag
    uint8_t protocol;           // TCP, UDP, or both (0 = both)
    
    uint16_t external_port;     // External port to forward
    uint32_t internal_ip;       // Internal IP to forward to
    uint16_t internal_port;     // Internal port to forward to
    
    char description[32];       // Rule description
} nat_port_forward_t;

// NAT configuration
typedef struct {
    uint8_t enabled;            // NAT enabled flag
    nat_type_t type;            // NAT type
    
    uint32_t internal_network;  // Internal network address
    uint32_t internal_netmask;  // Internal network mask
    
    uint32_t external_ip;       // External (public) IP
    net_interface_t* internal_iface;  // Internal interface
    net_interface_t* external_iface;  // External interface
    
    // Port allocation range for dynamic NAT
    uint16_t port_range_start;  // Start of dynamic port range
    uint16_t port_range_end;    // End of dynamic port range
    uint16_t next_port;         // Next port to allocate
    
    // Statistics
    uint32_t total_connections;
    uint32_t active_connections;
    uint64_t total_bytes_in;
    uint64_t total_bytes_out;
} nat_config_t;

// NAT Statistics
typedef struct {
    uint32_t entries_used;
    uint32_t entries_max;
    uint32_t port_forwards_active;
    uint32_t connections_created;
    uint32_t connections_expired;
    uint32_t packets_translated;
    uint32_t packets_dropped;
    uint64_t bytes_translated;
} nat_stats_t;

// NAT Core Functions

// Initialize NAT subsystem
void nat_init(void);

// Enable/disable NAT
int nat_enable(nat_type_t type);
int nat_disable(void);
int nat_is_enabled(void);

// Configure NAT interfaces
int nat_set_internal_interface(net_interface_t* iface, uint32_t network, uint32_t netmask);
int nat_set_external_interface(net_interface_t* iface);

// Set external IP (if different from interface IP)
int nat_set_external_ip(uint32_t ip);

// Packet Processing

// Process outgoing packet (internal -> external)
// Returns: 0 on success, -1 on error, 1 if packet should be dropped
int nat_process_outgoing(net_packet_t* packet, net_interface_t* iface);

// Process incoming packet (external -> internal)
// Returns: 0 on success, -1 on error, 1 if packet should be dropped
int nat_process_incoming(net_packet_t* packet, net_interface_t* iface);

// Connection Tracking

// Find NAT entry by internal connection
nat_entry_t* nat_find_entry_internal(uint8_t protocol, uint32_t internal_ip, 
                                      uint16_t internal_port, uint32_t remote_ip, 
                                      uint16_t remote_port);

// Find NAT entry by external connection
nat_entry_t* nat_find_entry_external(uint8_t protocol, uint32_t external_ip,
                                      uint16_t external_port, uint32_t remote_ip,
                                      uint16_t remote_port);

// Create new NAT entry
nat_entry_t* nat_create_entry(uint8_t protocol, uint32_t internal_ip,
                               uint16_t internal_port, uint32_t remote_ip,
                               uint16_t remote_port);

// Remove NAT entry
void nat_remove_entry(nat_entry_t* entry);

// Update entry timestamp (for timeout tracking)
void nat_touch_entry(nat_entry_t* entry);

// Cleanup expired entries
void nat_cleanup_expired(void);

// Port Forwarding

// Add port forwarding rule
int nat_add_port_forward(uint8_t protocol, uint16_t external_port,
                         uint32_t internal_ip, uint16_t internal_port,
                         const char* description);

// Remove port forwarding rule
int nat_remove_port_forward(uint16_t external_port, uint8_t protocol);

// Find port forwarding rule
nat_port_forward_t* nat_find_port_forward(uint16_t external_port, uint8_t protocol);

// List all port forwarding rules
int nat_list_port_forwards(nat_port_forward_t* rules, int max_rules);

// Statistics and Monitoring

// Get NAT statistics
void nat_get_stats(nat_stats_t* stats);

// Get NAT configuration
nat_config_t* nat_get_config(void);

// Dump NAT table (for debugging)
void nat_dump_table(void);

// Utility Functions

// Check if IP is in internal network
int nat_is_internal_ip(uint32_t ip);

// Allocate port from dynamic range
uint16_t nat_allocate_port(void);

// Calculate new checksum after NAT translation
uint16_t nat_adjust_checksum(uint16_t old_checksum, uint16_t old_data, uint16_t new_data);

// Recompute TCP/UDP checksum
void nat_recompute_checksum(net_packet_t* packet, uint8_t protocol);

// Timer tick (call periodically for timeout management)
void nat_timer_tick(void);

#endif // NAT_H
