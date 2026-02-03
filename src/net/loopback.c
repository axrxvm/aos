/*
 * === AOS HEADER BEGIN ===
 * ./src/net/loopback.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.8.5
 * === AOS HEADER END ===
 */


#include <net/net.h>
#include <net/ethernet.h>
#include <net/ipv4.h>
#include <string.h>
#include <serial.h>

// Forward declaration
static int loopback_receive(net_interface_t* iface, net_packet_t* packet);

// Loopback interface
static net_interface_t* loopback_iface = NULL;

// Loopback transmit function
static int loopback_transmit(net_interface_t* iface, net_packet_t* packet) {
    // Loopback: immediately receive what we transmit
    return loopback_receive(iface, packet);
}

// Loopback receive function
static int loopback_receive(net_interface_t* iface, net_packet_t* packet) {
    if (!packet || packet->len < IPV4_HEADER_LEN) {
        return -1;
    }
    
    // Pass directly to IPv4 layer (loopback doesn't use Ethernet framing)
    return ipv4_receive(iface, packet);
}

void loopback_init(void) {
    serial_puts("Initializing loopback interface...\n");
    
    loopback_iface = net_interface_register("lo");
    if (!loopback_iface) {
        serial_puts("Error: Failed to register loopback interface\n");
        return;
    }
    
    // Set loopback properties
    loopback_iface->flags = IFF_LOOPBACK | IFF_UP | IFF_RUNNING;
    loopback_iface->mtu = 65536;  // Loopback can have larger MTU
    
    // Set loopback IP: 127.0.0.1
    loopback_iface->ip_addr = 0x0100007F;  // 127.0.0.1 in little-endian
    loopback_iface->netmask = 0x000000FF;  // 255.0.0.0
    
    // Set MAC address to zeros for loopback
    memset(&loopback_iface->mac_addr, 0, MAC_ADDR_LEN);
    
    // Set interface functions
    loopback_iface->transmit = loopback_transmit;
    loopback_iface->receive = loopback_receive;
    
    serial_puts("Loopback interface initialized (127.0.0.1)\n");
}

net_interface_t* loopback_get_interface(void) {
    return loopback_iface;
}
