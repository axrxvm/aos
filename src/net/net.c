/*
 * === AOS HEADER BEGIN ===
 * ./src/net/net.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.8.5
 * === AOS HEADER END ===
 */


#include <net/net.h>
#include <net/ipv4.h>
#include <string.h>
#include <stdlib.h>
#include <vmm.h>
#include <serial.h>
#include <panic.h>

// Network interface table
static net_interface_t* net_interfaces[MAX_NET_INTERFACES];
static int net_interface_count_val = 0;

// IP address string buffer (for ip_to_string)
static char ip_string_buffer[16];

void net_init(void) {
    serial_puts("Initializing networking subsystem...\n");
    
    // Initialize interface table
    for (int i = 0; i < MAX_NET_INTERFACES; i++) {
        net_interfaces[i] = NULL;
    }
    
    serial_puts("Network subsystem initialized.\n");
}

net_interface_t* net_interface_register(const char* name) {
    if (net_interface_count_val >= MAX_NET_INTERFACES) {
        serial_puts("Error: Maximum network interfaces reached\n");
        return NULL;
    }
    
    net_interface_t* iface = (net_interface_t*)kmalloc(sizeof(net_interface_t));
    if (!iface) {
        serial_puts("Error: Failed to allocate network interface\n");
        return NULL;
    }
    
    memset(iface, 0, sizeof(net_interface_t));
    strncpy(iface->name, name, sizeof(iface->name) - 1);
    iface->mtu = MTU_SIZE;
    iface->flags = 0;
    
    net_interfaces[net_interface_count_val++] = iface;
    
    serial_puts("Registered network interface: ");
    serial_puts(name);
    serial_puts("\n");
    
    return iface;
}

net_interface_t* net_interface_get(const char* name) {
    for (int i = 0; i < net_interface_count_val; i++) {
        if (strcmp(net_interfaces[i]->name, name) == 0) {
            return net_interfaces[i];
        }
    }
    return NULL;
}

net_interface_t* net_interface_get_by_index(int index) {
    if (index < 0 || index >= net_interface_count_val) {
        return NULL;
    }
    return net_interfaces[index];
}

int net_interface_count(void) {
    return net_interface_count_val;
}

int net_interface_up(net_interface_t* iface) {
    if (!iface) return -1;
    
    iface->flags |= IFF_UP | IFF_RUNNING;
    
    serial_puts("Interface ");
    serial_puts(iface->name);
    serial_puts(" is up\n");
    
    return 0;
}

int net_interface_down(net_interface_t* iface) {
    if (!iface) return -1;
    
    iface->flags &= ~(IFF_UP | IFF_RUNNING);
    
    serial_puts("Interface ");
    serial_puts(iface->name);
    serial_puts(" is down\n");
    
    return 0;
}

int net_interface_set_ip(net_interface_t* iface, uint32_t ip, uint32_t netmask) {
    if (!iface) return -1;
    
    iface->ip_addr = ip;
    iface->netmask = netmask;
    
    serial_puts("Set IP address for ");
    serial_puts(iface->name);
    serial_puts(": ");
    serial_puts(ip_to_string(ip));
    serial_puts("\n");
    
    return 0;
}

net_packet_t* net_packet_alloc(uint32_t size) {
    net_packet_t* packet = (net_packet_t*)kmalloc(sizeof(net_packet_t));
    if (!packet) return NULL;
    
    packet->data = (uint8_t*)kmalloc(size);
    if (!packet->data) {
        kfree(packet);
        return NULL;
    }
    
    packet->len = 0;
    packet->capacity = size;
    
    return packet;
}

void net_packet_free(net_packet_t* packet) {
    if (packet) {
        if (packet->data) {
            kfree(packet->data);
        }
        kfree(packet);
    }
}

int net_transmit_packet(net_interface_t* iface, net_packet_t* packet) {
    if (!iface || !packet) return -1;
    
    if (!(iface->flags & IFF_UP)) {
        serial_puts("Error: Interface is down\n");
        return -1;
    }
    
    if (iface->transmit) {
        int ret = iface->transmit(iface, packet);
        if (ret == 0) {
            iface->stats.tx_packets++;
            iface->stats.tx_bytes += packet->len;
        } else {
            iface->stats.tx_errors++;
        }
        return ret;
    }
    
    return -1;
}

int net_receive_packet(net_interface_t* iface, net_packet_t* packet) {
    if (!iface || !packet) return -1;
    
    if (!(iface->flags & IFF_UP)) {
        return -1;
    }
    
    iface->stats.rx_packets++;
    iface->stats.rx_bytes += packet->len;
    
    if (iface->receive) {
        return iface->receive(iface, packet);
    }
    
    return 0;
}

const char* ip_to_string(uint32_t ip) {
    uint8_t* bytes = (uint8_t*)&ip;
    
    // Format: a.b.c.d
    char temp[4];
    ip_string_buffer[0] = '\0';
    
    for (int i = 0; i < 4; i++) {
        itoa(bytes[i], temp, 10);
        strcat(ip_string_buffer, temp);
        if (i < 3) {
            strcat(ip_string_buffer, ".");
        }
    }
    
    return ip_string_buffer;
}

uint32_t string_to_ip(const char* str) {
    uint32_t ip = 0;
    uint8_t* bytes = (uint8_t*)&ip;
    int octet = 0;
    int value = 0;
    
    while (*str && octet < 4) {
        if (*str >= '0' && *str <= '9') {
            value = value * 10 + (*str - '0');
        } else if (*str == '.' || *str == '\0') {
            bytes[octet++] = (uint8_t)value;
            value = 0;
        }
        str++;
    }
    
    // Last octet
    if (octet < 4) {
        bytes[octet] = (uint8_t)value;
    }
    
    return ip;
}

// Network polling - call this regularly to process incoming packets
void net_poll(void) {
    // Poll hardware for incoming packets (both drivers)
    extern void e1000_handle_interrupt(void);
    extern void pcnet_handle_interrupt(void);
    
    e1000_handle_interrupt();
    pcnet_handle_interrupt();
    
    // Process pending ARP resolutions
    ipv4_process_pending();
}
