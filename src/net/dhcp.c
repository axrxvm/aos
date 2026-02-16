/*
 * === AOS HEADER BEGIN ===
 * src/net/dhcp.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */


#include <net/dhcp.h>
#include <net/udp.h>
#include <net/ipv4.h>
#include <net/arp.h>
#include <net/net.h>
#include <string.h>
#include <serial.h>
#include <stdlib.h>
#include <arch/pit.h>

static dhcp_config_t dhcp_config;
static uint32_t dhcp_xid = 0;
static int dhcp_configured = 0;
static net_interface_t* dhcp_iface = NULL;

// Generate random transaction ID
static uint32_t dhcp_generate_xid(void) {
    return get_tick_count() ^ 0xDEADBEEF;
}

// Build DHCP option
static int dhcp_add_option(uint8_t* options, int offset, uint8_t code, uint8_t len, const void* data) {
    options[offset++] = code;
    options[offset++] = len;
    memcpy(&options[offset], data, len);
    return offset + len;
}

// Parse DHCP options
static void dhcp_parse_options(const uint8_t* options, int len, dhcp_config_t* config) {
    int i = 0;
    
    while (i < len && options[i] != DHCP_OPT_END) {
        uint8_t opt = options[i++];
        
        if (opt == DHCP_OPT_PAD) continue;
        
        uint8_t opt_len = options[i++];
        
        switch (opt) {
            case DHCP_OPT_SUBNET_MASK:
                if (opt_len == 4) {
                    memcpy(&config->netmask, &options[i], 4);
                }
                break;
                
            case DHCP_OPT_ROUTER:
                if (opt_len >= 4) {
                    memcpy(&config->gateway, &options[i], 4);
                }
                break;
                
            case DHCP_OPT_DNS:
                if (opt_len >= 4) {
                    memcpy(&config->dns_server, &options[i], 4);
                }
                break;
                
            case DHCP_OPT_HOSTNAME:
                if (opt_len < 64) {
                    memcpy(config->hostname, &options[i], opt_len);
                    config->hostname[opt_len] = '\0';
                }
                break;
                
            case DHCP_OPT_DOMAIN_NAME:
                if (opt_len < 128) {
                    memcpy(config->domain_name, &options[i], opt_len);
                    config->domain_name[opt_len] = '\0';
                }
                break;
                
            case DHCP_OPT_LEASE_TIME:
                if (opt_len == 4) {
                    memcpy(&config->lease_time, &options[i], 4);
                    config->lease_time = ntohl(config->lease_time);
                }
                break;
                
            case DHCP_OPT_RENEWAL_TIME:
                if (opt_len == 4) {
                    memcpy(&config->renewal_time, &options[i], 4);
                    config->renewal_time = ntohl(config->renewal_time);
                }
                break;
                
            case DHCP_OPT_REBIND_TIME:
                if (opt_len == 4) {
                    memcpy(&config->rebind_time, &options[i], 4);
                    config->rebind_time = ntohl(config->rebind_time);
                }
                break;
                
            case DHCP_OPT_SERVER_ID:
                if (opt_len == 4) {
                    memcpy(&config->server_id, &options[i], 4);
                }
                break;
        }
        
        i += opt_len;
    }
}

// DHCP receive callback
static void dhcp_receive_callback(uint32_t src_ip, uint16_t src_port, const uint8_t* data, uint32_t len) {
    (void)src_ip;
    (void)src_port;
    
    // Minimum DHCP message is 240 bytes (without options)
    // Full message with options can be up to 576 bytes
    if (len < 240) {
        serial_puts("DHCP: Packet too small\n");
        return;
    }
    
    dhcp_message_t* msg = (dhcp_message_t*)data;
    
    // Check if this is a reply for our transaction
    if (msg->op != 2) return;
    if (msg->xid != dhcp_xid) return;
    if (msg->magic != htonl(DHCP_MAGIC_COOKIE)) return;
    
    // Parse message type from options
    uint8_t msg_type = 0;
    uint32_t options_len = len - 240;  // Options start at offset 240
    if (options_len > 312) options_len = 312;
    
    for (uint32_t i = 0; i < options_len; ) {
        uint8_t opt = msg->options[i++];
        if (opt == DHCP_OPT_END) break;
        if (opt == DHCP_OPT_PAD) continue;
        
        if (i >= options_len) break;
        uint8_t opt_len = msg->options[i++];
        
        if (opt == DHCP_OPT_MSG_TYPE && opt_len >= 1) {
            msg_type = msg->options[i];
        }
        
        i += opt_len;
    }
    
    if (msg_type == DHCP_OFFER || msg_type == DHCP_ACK) {
        dhcp_config.ip_addr = msg->yiaddr;
        dhcp_parse_options(msg->options, options_len, &dhcp_config);
        dhcp_configured = 1;
        
        serial_puts("DHCP: ");
        serial_puts(msg_type == DHCP_OFFER ? "Offer" : "ACK");
        serial_puts(" - ");
        serial_puts(ip_to_string(msg->yiaddr));
        serial_puts("\n");
    }
}

// Send DHCP Discover
int dhcp_discover(net_interface_t* iface) {
    if (!iface) return -1;
    
    dhcp_iface = iface;
    dhcp_configured = 0;
    dhcp_xid = dhcp_generate_xid();
    memset(&dhcp_config, 0, sizeof(dhcp_config_t));
    
    // Create UDP socket for DHCP
    udp_socket_t* sock = udp_socket_create();
    if (!sock) {
        serial_puts("DHCP: Socket failed\n");
        return -1;
    }
    
    // Bind to DHCP client port (68)
    if (udp_socket_bind(sock, 0, DHCP_CLIENT_PORT) != 0) {
        serial_puts("DHCP: Bind failed\n");
        udp_socket_close(sock);
        return -1;
    }
    
    // Build DHCP DISCOVER message
    dhcp_message_t msg;
    memset(&msg, 0, sizeof(dhcp_message_t));
    msg.op = 1;  // BOOTREQUEST
    msg.htype = 1;  // Ethernet
    msg.hlen = 6;
    msg.xid = dhcp_xid;
    msg.flags = htons(0x8000);  // Broadcast flag
    memcpy(msg.chaddr, iface->mac_addr.addr, 6);
    msg.magic = htonl(DHCP_MAGIC_COOKIE);
    
    // Add options
    int offset = 0;
    uint8_t msg_type = DHCP_DISCOVER;
    offset = dhcp_add_option(msg.options, offset, DHCP_OPT_MSG_TYPE, 1, &msg_type);
    uint8_t params[] = {DHCP_OPT_SUBNET_MASK, DHCP_OPT_ROUTER, DHCP_OPT_DNS};
    offset = dhcp_add_option(msg.options, offset, DHCP_OPT_PARAM_REQUEST, 3, params);
    msg.options[offset++] = DHCP_OPT_END;
    
    // Send DISCOVER
    serial_puts("DHCP: Sending DISCOVER...\n");
    udp_socket_sendto(sock, (const uint8_t*)&msg, sizeof(dhcp_message_t), 0xFFFFFFFF, DHCP_SERVER_PORT);
    
    // Wait for OFFER (5 second timeout)
    uint8_t recv_buffer[sizeof(dhcp_message_t)];
    uint32_t src_ip;
    uint16_t src_port;
    uint32_t timeout = get_tick_count() + 500;  // 5 seconds
    
    while (get_tick_count() < timeout && !dhcp_configured) {
        net_poll();  // Poll for packets on all network interfaces
        
        int recv_len = udp_socket_recvfrom(sock, recv_buffer, sizeof(recv_buffer), &src_ip, &src_port);
        if (recv_len > 0) {
            dhcp_receive_callback(src_ip, src_port, recv_buffer, recv_len);
        }
    }
    
    if (!dhcp_configured) {
        serial_puts("DHCP: No OFFER received\n");
        udp_socket_close(sock);
        return -1;
    }
    
    // Send REQUEST
    memset(&msg, 0, sizeof(dhcp_message_t));
    msg.op = 1;
    msg.htype = 1;
    msg.hlen = 6;
    msg.xid = dhcp_xid;
    msg.flags = htons(0x8000);
    memcpy(msg.chaddr, iface->mac_addr.addr, 6);
    msg.magic = htonl(DHCP_MAGIC_COOKIE);
    
    offset = 0;
    msg_type = DHCP_REQUEST;
    offset = dhcp_add_option(msg.options, offset, DHCP_OPT_MSG_TYPE, 1, &msg_type);
    offset = dhcp_add_option(msg.options, offset, DHCP_OPT_REQUESTED_IP, 4, &dhcp_config.ip_addr);
    offset = dhcp_add_option(msg.options, offset, DHCP_OPT_SERVER_ID, 4, &dhcp_config.server_id);
    msg.options[offset++] = DHCP_OPT_END;
    
    serial_puts("DHCP: Sending REQUEST...\n");
    dhcp_configured = 0;
    udp_socket_sendto(sock, (const uint8_t*)&msg, sizeof(dhcp_message_t), 0xFFFFFFFF, DHCP_SERVER_PORT);
    
    // Wait for ACK
    timeout = get_tick_count() + 500;
    while (get_tick_count() < timeout && !dhcp_configured) {
        net_poll();
        
        int recv_len = udp_socket_recvfrom(sock, recv_buffer, sizeof(recv_buffer), &src_ip, &src_port);
        if (recv_len > 0) {
            dhcp_receive_callback(src_ip, src_port, recv_buffer, recv_len);
        }
    }
    
    udp_socket_close(sock);
    return dhcp_configured ? 0 : -1;
}

// Configure interface with DHCP settings
int dhcp_configure_interface(net_interface_t* iface, dhcp_config_t* config) {
    iface->ip_addr = config->ip_addr;
    iface->netmask = config->netmask;
    iface->gateway = config->gateway;
    
    serial_puts("DHCP: Interface configured\n");
    serial_puts("  IP: ");
    serial_puts(ip_to_string(config->ip_addr));
    serial_puts("\n  Netmask: ");
    serial_puts(ip_to_string(config->netmask));
    serial_puts("\n  Gateway: ");
    serial_puts(ip_to_string(config->gateway));
    serial_puts("\n");
    
    if (config->dns_server) {
        serial_puts("  DNS: ");
        serial_puts(ip_to_string(config->dns_server));
        serial_puts("\n");
    }
    
    return 0;
}

// Initialize DHCP
void dhcp_init(void) {
    memset(&dhcp_config, 0, sizeof(dhcp_config_t));
    dhcp_configured = 0;
    serial_puts("DHCP client initialized\n");
}

// Get DHCP configuration
dhcp_config_t* dhcp_get_config(void) {
    return &dhcp_config;
}
