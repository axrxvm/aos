/*
 * === AOS HEADER BEGIN ===
 * include/net/dhcp.h
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */


#ifndef DHCP_H
#define DHCP_H

#include <stdint.h>
#include <net/socket.h>

// DHCP Message Types
#define DHCP_DISCOVER   1
#define DHCP_OFFER      2
#define DHCP_REQUEST    3
#define DHCP_DECLINE    4
#define DHCP_ACK        5
#define DHCP_NAK        6
#define DHCP_RELEASE    7
#define DHCP_INFORM     8

// DHCP Options
#define DHCP_OPT_PAD            0
#define DHCP_OPT_SUBNET_MASK    1
#define DHCP_OPT_ROUTER         3
#define DHCP_OPT_DNS            6
#define DHCP_OPT_HOSTNAME       12
#define DHCP_OPT_DOMAIN_NAME    15
#define DHCP_OPT_REQUESTED_IP   50
#define DHCP_OPT_LEASE_TIME     51
#define DHCP_OPT_MSG_TYPE       53
#define DHCP_OPT_SERVER_ID      54
#define DHCP_OPT_PARAM_REQUEST  55
#define DHCP_OPT_RENEWAL_TIME   58
#define DHCP_OPT_REBIND_TIME    59
#define DHCP_OPT_END            255

// DHCP Ports
#define DHCP_SERVER_PORT    67
#define DHCP_CLIENT_PORT    68

// DHCP Magic Cookie
#define DHCP_MAGIC_COOKIE   0x63825363

// DHCP Message Structure
typedef struct {
    uint8_t op;           // Message op code / message type (1 = BOOTREQUEST, 2 = BOOTREPLY)
    uint8_t htype;        // Hardware address type (1 = Ethernet)
    uint8_t hlen;         // Hardware address length (6 for Ethernet)
    uint8_t hops;         // Client sets to zero
    uint32_t xid;         // Transaction ID
    uint16_t secs;        // Seconds elapsed since client began address acquisition
    uint16_t flags;       // Flags (bit 0 = broadcast flag)
    uint32_t ciaddr;      // Client IP address (only filled in if client is in BOUND, RENEW or REBINDING)
    uint32_t yiaddr;      // 'Your' (client) IP address
    uint32_t siaddr;      // IP address of next server to use in bootstrap
    uint32_t giaddr;      // Relay agent IP address
    uint8_t chaddr[16];   // Client hardware address
    uint8_t sname[64];    // Optional server host name
    uint8_t file[128];    // Boot file name
    uint32_t magic;       // Magic cookie (0x63825363)
    uint8_t options[312]; // Optional parameters field
} __attribute__((packed)) dhcp_message_t;

// DHCP Configuration Result
typedef struct {
    uint32_t ip_addr;
    uint32_t netmask;
    uint32_t gateway;
    uint32_t dns_server;
    uint32_t lease_time;
    uint32_t renewal_time;
    uint32_t rebind_time;
    uint32_t server_id;
    char hostname[64];
    char domain_name[128];
} dhcp_config_t;

// DHCP Functions
void dhcp_init(void);
int dhcp_discover(net_interface_t* iface);
int dhcp_discover_timed(net_interface_t* iface, uint32_t offer_timeout_ticks, uint32_t ack_timeout_ticks);
int dhcp_configure_interface(net_interface_t* iface, dhcp_config_t* config);
dhcp_config_t* dhcp_get_config(void);

#endif // DHCP_H
