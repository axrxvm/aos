/*
 * === AOS HEADER BEGIN ===
 * ./src/net/netconfig.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.8.5
 * === AOS HEADER END ===
 */


#include <net/netconfig.h>
#include <net/dhcp.h>
#include <net/dns.h>
#include <net/net.h>
#include <string.h>
#include <stdlib.h>
#include <vmm.h>
#include <serial.h>
#include <fs/vfs.h>

// Default configuration for each interface
#define MAX_NETCONFIGS 8
static netconfig_t netconfigs[MAX_NETCONFIGS];
static char system_hostname[64] = "aOS";

void netconfig_init(void) {
    serial_puts("Initializing network configuration...\n");
    
    memset(netconfigs, 0, sizeof(netconfigs));
    
    // Set default configurations
    for (int i = 0; i < MAX_NETCONFIGS; i++) {
        netconfigs[i].mode = NETCONFIG_MODE_DHCP;  // Default to DHCP
        // Default DNS servers (Google)
        netconfigs[i].primary_dns = string_to_ip("8.8.8.8");
        netconfigs[i].secondary_dns = string_to_ip("8.8.4.4");
    }
    
    serial_puts("Network configuration initialized.\n");
}

// Get configuration for an interface (by index)
static int netconfig_get_index(net_interface_t* iface) {
    if (!iface) return -1;
    
    for (int i = 0; i < net_interface_count(); i++) {
        if (net_interface_get_by_index(i) == iface) {
            return i;
        }
    }
    return -1;
}

netconfig_t* netconfig_get(net_interface_t* iface) {
    int idx = netconfig_get_index(iface);
    if (idx < 0 || idx >= MAX_NETCONFIGS) {
        return NULL;
    }
    return &netconfigs[idx];
}

int netconfig_set(net_interface_t* iface, netconfig_t* config) {
    int idx = netconfig_get_index(iface);
    if (idx < 0 || idx >= MAX_NETCONFIGS || !config) {
        return -1;
    }
    
    memcpy(&netconfigs[idx], config, sizeof(netconfig_t));
    return 0;
}

// Configure interface with static IP
int netconfig_set_static(net_interface_t* iface, uint32_t ip, uint32_t netmask, 
                         uint32_t gateway, uint32_t dns) {
    if (!iface) {
        return -1;
    }
    
    int idx = netconfig_get_index(iface);
    if (idx < 0 || idx >= MAX_NETCONFIGS) {
        return -1;
    }
    
    netconfig_t* config = &netconfigs[idx];
    config->mode = NETCONFIG_MODE_STATIC;
    config->ip_addr = ip;
    config->netmask = netmask;
    config->gateway = gateway;
    if (dns) {
        config->primary_dns = dns;
    }
    
    serial_puts("Static IP configuration set for ");
    serial_puts(iface->name);
    serial_puts(":\n  IP: ");
    serial_puts(ip_to_string(ip));
    serial_puts("\n  Netmask: ");
    serial_puts(ip_to_string(netmask));
    serial_puts("\n  Gateway: ");
    serial_puts(ip_to_string(gateway));
    serial_puts("\n");
    
    return netconfig_apply(iface);
}

// Configure interface to use DHCP
int netconfig_set_dhcp(net_interface_t* iface) {
    if (!iface) {
        return -1;
    }
    
    int idx = netconfig_get_index(iface);
    if (idx < 0 || idx >= MAX_NETCONFIGS) {
        return -1;
    }
    
    netconfig_t* config = &netconfigs[idx];
    config->mode = NETCONFIG_MODE_DHCP;
    
    serial_puts("DHCP configuration set for ");
    serial_puts(iface->name);
    serial_puts("\n");
    
    return netconfig_apply(iface);
}

// Apply configuration to interface
int netconfig_apply(net_interface_t* iface) {
    if (!iface) {
        return -1;
    }
    
    int idx = netconfig_get_index(iface);
    if (idx < 0 || idx >= MAX_NETCONFIGS) {
        return -1;
    }
    
    netconfig_t* config = &netconfigs[idx];
    
    if (config->mode == NETCONFIG_MODE_STATIC) {
        // Apply static configuration
        iface->ip_addr = config->ip_addr;
        iface->netmask = config->netmask;
        iface->gateway = config->gateway;
        
        // Update DNS configuration
        dns_set_server(config->primary_dns, config->secondary_dns);
        
        // Bring interface up
        net_interface_up(iface);
        
        serial_puts("Applied static configuration to ");
        serial_puts(iface->name);
        serial_puts("\n");
        
        return 0;
    } 
    else if (config->mode == NETCONFIG_MODE_DHCP) {
        // Run DHCP discovery
        serial_puts("Running DHCP on ");
        serial_puts(iface->name);
        serial_puts("...\n");
        
        int result = dhcp_discover(iface);
        if (result == 0) {
            // DHCP successful, get configuration
            dhcp_config_t* dhcp_cfg = dhcp_get_config();
            if (dhcp_cfg) {
                config->ip_addr = dhcp_cfg->ip_addr;
                config->netmask = dhcp_cfg->netmask;
                config->gateway = dhcp_cfg->gateway;
                
                if (dhcp_cfg->dns_server) {
                    config->primary_dns = dhcp_cfg->dns_server;
                    dns_set_server(dhcp_cfg->dns_server, config->secondary_dns);
                }
                
                if (dhcp_cfg->hostname[0]) {
                    strncpy(config->hostname, dhcp_cfg->hostname, sizeof(config->hostname) - 1);
                }
                if (dhcp_cfg->domain_name[0]) {
                    strncpy(config->domain, dhcp_cfg->domain_name, sizeof(config->domain) - 1);
                }
            }
        }
        
        return result;
    }
    
    return -1;
}

// Save configuration to file
int netconfig_save(const char* path) {
    if (!path) {
        path = "/etc/network.conf";
    }
    
    int fd = vfs_open(path, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) {
        return -1;
    }
    
    char buffer[512];
    int offset = 0;
    const size_t buffer_size = sizeof(buffer);
    
    // Write hostname
    offset = 0;
    strncpy(buffer + offset, "hostname=", buffer_size - offset - 1);
    offset += 9;
    if (offset < (int)(buffer_size - 1)) {
        strncpy(buffer + offset, system_hostname, buffer_size - offset - 1);
        offset += strlen(system_hostname);
    }
    buffer[offset++] = '\n';
    vfs_write(fd, buffer, offset);
    
    // Write interface configurations
    for (int i = 0; i < net_interface_count(); i++) {
        net_interface_t* iface = net_interface_get_by_index(i);
        if (!iface || (iface->flags & IFF_LOOPBACK)) continue;
        
        netconfig_t* config = &netconfigs[i];
        offset = 0;
        
        // Interface name
        if (offset + 2 < (int)buffer_size) {
            strncpy(buffer + offset, "\n[", buffer_size - offset - 1);
            offset += 2;
        }
        if (offset + strlen(iface->name) < (int)(buffer_size - 1)) {
            strncpy(buffer + offset, iface->name, buffer_size - offset - 1);
            offset += strlen(iface->name);
        }
        if (offset + 2 < (int)buffer_size) {
            strncpy(buffer + offset, "]\n", buffer_size - offset - 1);
            offset += 2;
        }
        
        // Mode
        if (offset + 5 < (int)buffer_size) {
            strncpy(buffer + offset, "mode=", buffer_size - offset - 1);
            offset += 5;
        }
        if (config->mode == NETCONFIG_MODE_STATIC) {
            if (offset + 7 < (int)buffer_size) {
                strncpy(buffer + offset, "static\n", buffer_size - offset - 1);
                offset += 7;
            }
            
            // Static configuration
            if (offset + 3 < (int)buffer_size) {
                strncpy(buffer + offset, "ip=", buffer_size - offset - 1);
                offset += 3;
            }
            const char* ip_str1 = ip_to_string(config->ip_addr);
            if (offset + strlen(ip_str1) + 1 < (int)buffer_size) {
                strncpy(buffer + offset, ip_str1, buffer_size - offset - 1);
                offset += strlen(ip_str1);
                buffer[offset++] = '\n';
            }
            
            if (offset + 8 < (int)buffer_size) {
                strncpy(buffer + offset, "netmask=", buffer_size - offset - 1);
                offset += 8;
            }
            const char* ip_str2 = ip_to_string(config->netmask);
            if (offset + strlen(ip_str2) + 1 < (int)buffer_size) {
                strncpy(buffer + offset, ip_str2, buffer_size - offset - 1);
                offset += strlen(ip_str2);
                buffer[offset++] = '\n';
            }
            
            if (offset + 8 < (int)buffer_size) {
                strncpy(buffer + offset, "gateway=", buffer_size - offset - 1);
                offset += 8;
            }
            const char* ip_str3 = ip_to_string(config->gateway);
            if (offset + strlen(ip_str3) + 1 < (int)buffer_size) {
                strncpy(buffer + offset, ip_str3, buffer_size - offset - 1);
                offset += strlen(ip_str3);
                buffer[offset++] = '\n';
            }
            
            if (offset + 4 < (int)buffer_size) {
                strncpy(buffer + offset, "dns=", buffer_size - offset - 1);
                offset += 4;
            }
            const char* ip_str4 = ip_to_string(config->primary_dns);
            if (offset + strlen(ip_str4) + 1 < (int)buffer_size) {
                strncpy(buffer + offset, ip_str4, buffer_size - offset - 1);
                offset += strlen(ip_str4);
                buffer[offset++] = '\n';
            }
        } else {
            if (offset + 5 < (int)buffer_size) {
                strncpy(buffer + offset, "dhcp\n", buffer_size - offset - 1);
                offset += 5;
            }
        }
        
        vfs_write(fd, buffer, offset);
    }
    
    vfs_close(fd);
    serial_puts("Network configuration saved to ");
    serial_puts(path);
    serial_puts("\n");
    
    return 0;
}

// Simple line parser helper
static int parse_line(const char* line, char* key, char* value, int max_len) {
    const char* eq = strchr(line, '=');
    if (!eq) return -1;
    
    int key_len = eq - line;
    if (key_len >= max_len) key_len = max_len - 1;
    strncpy(key, line, key_len);
    key[key_len] = '\0';
    
    eq++;
    int val_len = strlen(eq);
    // Trim newlines
    while (val_len > 0 && (eq[val_len-1] == '\n' || eq[val_len-1] == '\r')) {
        val_len--;
    }
    if (val_len >= max_len) val_len = max_len - 1;
    strncpy(value, eq, val_len);
    value[val_len] = '\0';
    
    return 0;
}

// Load configuration from file
int netconfig_load(const char* path) {
    if (!path) {
        path = "/etc/network.conf";
    }
    
    int fd = vfs_open(path, O_RDONLY);
    if (fd < 0) {
        serial_puts("Cannot open network config file\n");
        return -1;
    }
    
    char buffer[1024];
    int bytes_read = vfs_read(fd, buffer, sizeof(buffer) - 1);
    vfs_close(fd);
    
    if (bytes_read <= 0) {
        return -1;
    }
    buffer[bytes_read] = '\0';
    
    // Parse configuration
    char* line = buffer;
    net_interface_t* current_iface = NULL;
    netconfig_t* current_config = NULL;
    char key[64], value[128];
    
    while (*line) {
        // Find end of line
        char* line_end = line;
        while (*line_end && *line_end != '\n') line_end++;
        
        // Temporarily null-terminate
        char saved = *line_end;
        *line_end = '\0';
        
        // Skip empty lines and comments
        if (*line && *line != '#' && *line != ';') {
            // Check for section header [interface]
            if (*line == '[') {
                char* end = strchr(line, ']');
                if (end) {
                    *end = '\0';
                    current_iface = net_interface_get(line + 1);
                    if (current_iface) {
                        current_config = netconfig_get(current_iface);
                    } else {
                        current_config = NULL;
                    }
                }
            }
            // Key=value pairs
            else if (parse_line(line, key, value, sizeof(key)) == 0) {
                if (strcmp(key, "hostname") == 0) {
                    strncpy(system_hostname, value, sizeof(system_hostname) - 1);
                }
                else if (current_config) {
                    if (strcmp(key, "mode") == 0) {
                        if (strcmp(value, "static") == 0) {
                            current_config->mode = NETCONFIG_MODE_STATIC;
                        } else {
                            current_config->mode = NETCONFIG_MODE_DHCP;
                        }
                    }
                    else if (strcmp(key, "ip") == 0) {
                        current_config->ip_addr = string_to_ip(value);
                    }
                    else if (strcmp(key, "netmask") == 0) {
                        current_config->netmask = string_to_ip(value);
                    }
                    else if (strcmp(key, "gateway") == 0) {
                        current_config->gateway = string_to_ip(value);
                    }
                    else if (strcmp(key, "dns") == 0) {
                        current_config->primary_dns = string_to_ip(value);
                    }
                }
            }
        }
        
        *line_end = saved;
        line = line_end;
        if (*line == '\n') line++;
    }
    
    serial_puts("Network configuration loaded from ");
    serial_puts(path);
    serial_puts("\n");
    
    // Apply configurations
    for (int i = 0; i < net_interface_count(); i++) {
        net_interface_t* iface = net_interface_get_by_index(i);
        if (iface && !(iface->flags & IFF_LOOPBACK)) {
            netconfig_apply(iface);
        }
    }
    
    return 0;
}

// Hostname management
const char* netconfig_get_hostname(void) {
    return system_hostname;
}

int netconfig_set_hostname(const char* hostname) {
    if (!hostname || strlen(hostname) == 0) {
        return -1;
    }
    
    strncpy(system_hostname, hostname, sizeof(system_hostname) - 1);
    system_hostname[sizeof(system_hostname) - 1] = '\0';
    
    return 0;
}

// DNS configuration shortcuts
int netconfig_set_dns(uint32_t primary, uint32_t secondary) {
    dns_set_server(primary, secondary);
    
    // Update all configs
    for (int i = 0; i < MAX_NETCONFIGS; i++) {
        if (primary) netconfigs[i].primary_dns = primary;
        if (secondary) netconfigs[i].secondary_dns = secondary;
    }
    
    return 0;
}

void netconfig_get_dns(uint32_t* primary, uint32_t* secondary) {
    dns_get_servers(primary, secondary);
}
