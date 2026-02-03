/*
 * === AOS HEADER BEGIN ===
 * ./include/net/netconfig.h
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.8.5
 * === AOS HEADER END ===
 */


#ifndef NETCONFIG_H
#define NETCONFIG_H

#include <stdint.h>
#include <net/net.h>

// Network configuration modes
#define NETCONFIG_MODE_NONE     0
#define NETCONFIG_MODE_STATIC   1
#define NETCONFIG_MODE_DHCP     2

// Network configuration structure
typedef struct {
    int mode;                   // Configuration mode
    uint32_t ip_addr;           // Static IP address
    uint32_t netmask;           // Subnet mask
    uint32_t gateway;           // Default gateway
    uint32_t primary_dns;       // Primary DNS server
    uint32_t secondary_dns;     // Secondary DNS server
    char hostname[64];          // System hostname
    char domain[128];           // Domain name
} netconfig_t;

// Network configuration initialization
void netconfig_init(void);

// Get/set network configuration
netconfig_t* netconfig_get(net_interface_t* iface);
int netconfig_set(net_interface_t* iface, netconfig_t* config);

// Configuration modes
int netconfig_set_static(net_interface_t* iface, uint32_t ip, uint32_t netmask, 
                         uint32_t gateway, uint32_t dns);
int netconfig_set_dhcp(net_interface_t* iface);

// Apply configuration to interface
int netconfig_apply(net_interface_t* iface);

// Configuration persistence
int netconfig_save(const char* path);
int netconfig_load(const char* path);

// Hostname management
const char* netconfig_get_hostname(void);
int netconfig_set_hostname(const char* hostname);

// DNS configuration shortcuts
int netconfig_set_dns(uint32_t primary, uint32_t secondary);
void netconfig_get_dns(uint32_t* primary, uint32_t* secondary);

#endif // NETCONFIG_H
