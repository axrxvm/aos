/*
 * === AOS HEADER BEGIN ===
 * ./src/net/dns.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.8.5
 * === AOS HEADER END ===
 */


/**
 * DNS Resolver Implementation
 */

#include <net/dns.h>
#include <net/udp.h>
#include <net/net.h>
#include <net/ipv4.h>
#include <net/arp.h>
#include <string.h>
#include <stdlib.h>
#include <serial.h>
#include <arch/i386/pit.h>


// Configuration


static dns_config_t dns_config = {
    .primary_dns = 0,
    .secondary_dns = 0,
    .timeout_ms = 5000,
    .retry_count = 3
};

// DNS cache
static dns_cache_entry_t dns_cache[DNS_CACHE_SIZE];
static uint16_t dns_transaction_id = 1;


// Forward declarations


extern void e1000_handle_interrupt(void);


// Utilities


static uint16_t dns_generate_id(void) {
    return dns_transaction_id++;
}

// Network polling function
static void dns_poll_network(void) {
    e1000_handle_interrupt();
    ipv4_process_pending();
}

// Yield to interrupts - allows packet reception
static void dns_yield(void) {
    // HLT waits for next interrupt (timer tick, NIC interrupt, etc.)
    // This is critical - without it, we spin too fast for the NIC to process
    __asm__ volatile("sti");  // Ensure interrupts enabled
    __asm__ volatile("hlt");  // Wait for interrupt
}

// Small delay with interrupt yielding
static void dns_delay(uint32_t iterations) {
    for (uint32_t i = 0; i < iterations; i++) {
        dns_yield();
        dns_poll_network();
    }
}


// Initialization


void dns_init(void) {
    serial_puts("Initializing DNS resolver...\n");
    
    memset(dns_cache, 0, sizeof(dns_cache));
    
    // Default to Google's public DNS
    dns_config.primary_dns = string_to_ip("8.8.8.8");
    dns_config.secondary_dns = string_to_ip("8.8.4.4");
    
    serial_puts("DNS resolver initialized.\n");
}


// Configuration


void dns_set_server(uint32_t primary, uint32_t secondary) {
    dns_config.primary_dns = primary;
    dns_config.secondary_dns = secondary;
}

void dns_get_servers(uint32_t* primary, uint32_t* secondary) {
    if (primary) *primary = dns_config.primary_dns;
    if (secondary) *secondary = dns_config.secondary_dns;
}

void dns_set_timeout(uint32_t timeout_ms) {
    dns_config.timeout_ms = timeout_ms;
}

void dns_set_retry_count(uint8_t count) {
    dns_config.retry_count = count;
}


// Name Encoding/Decoding


int dns_encode_name(const char* hostname, uint8_t* buffer, int max_len) {
    if (!hostname || !buffer || max_len < 2) {
        return -1;
    }
    
    int pos = 0;
    const char* label_start = hostname;
    
    while (*hostname) {
        if (*hostname == '.') {
            int label_len = hostname - label_start;
            if (label_len > 63 || pos + label_len + 1 >= max_len) {
                return -1;
            }
            buffer[pos++] = label_len;
            memcpy(&buffer[pos], label_start, label_len);
            pos += label_len;
            label_start = hostname + 1;
        }
        hostname++;
    }
    
    // Handle last label
    int label_len = hostname - label_start;
    if (label_len > 0) {
        if (pos + label_len + 2 > max_len) {
            return -1;
        }
        buffer[pos++] = label_len;
        memcpy(&buffer[pos], label_start, label_len);
        pos += label_len;
    }
    
    buffer[pos++] = 0;
    return pos;
}

int dns_decode_name(const uint8_t* packet, int packet_len, int offset, 
                    char* name, int max_name_len) {
    if (!packet || !name || offset < 0 || offset >= packet_len) {
        return -1;
    }
    
    int name_pos = 0;
    int current_offset = offset;
    int jumped = 0;
    int final_offset = -1;
    int jump_count = 0;
    
    while (current_offset < packet_len) {
        uint8_t len = packet[current_offset];
        
        if (len == 0) {
            if (!jumped) {
                final_offset = current_offset + 1;
            }
            break;
        }
        
        // Compression pointer
        if ((len & 0xC0) == 0xC0) {
            if (current_offset + 1 >= packet_len) {
                return -1;
            }
            
            if (!jumped) {
                final_offset = current_offset + 2;
            }
            
            uint16_t ptr = ((len & 0x3F) << 8) | packet[current_offset + 1];
            if (ptr >= (uint16_t)packet_len) {
                return -1;
            }
            
            current_offset = ptr;
            jumped = 1;
            
            if (++jump_count > 10) {
                return -1;  // Prevent infinite loops
            }
            continue;
        }
        
        current_offset++;
        
        if (current_offset + len > packet_len) {
            return -1;
        }
        
        if (name_pos > 0) {
            if (name_pos >= max_name_len - 1) {
                return -1;
            }
            name[name_pos++] = '.';
        }
        
        if (name_pos + len >= max_name_len) {
            return -1;
        }
        memcpy(&name[name_pos], &packet[current_offset], len);
        name_pos += len;
        current_offset += len;
    }
    
    name[name_pos] = '\0';
    return final_offset >= 0 ? final_offset : current_offset + 1;
}


// Cache Management


void dns_cache_clear(void) {
    memset(dns_cache, 0, sizeof(dns_cache));
}

int dns_cache_add(const char* hostname, uint32_t ip_addr, uint32_t ttl) {
    if (!hostname || strlen(hostname) >= 128) {
        return -1;
    }
    
    // Look for existing entry or free slot
    int free_slot = -1;
    for (int i = 0; i < DNS_CACHE_SIZE; i++) {
        if (dns_cache[i].valid) {
            if (strcmp(dns_cache[i].hostname, hostname) == 0) {
                dns_cache[i].ip_addr = ip_addr;
                dns_cache[i].ttl = ttl;
                dns_cache[i].timestamp = get_tick_count();
                return 0;
            }
        } else if (free_slot < 0) {
            free_slot = i;
        }
    }
    
    if (free_slot < 0) {
        // Find oldest entry
        uint32_t oldest_time = 0xFFFFFFFF;
        for (int i = 0; i < DNS_CACHE_SIZE; i++) {
            if (dns_cache[i].timestamp < oldest_time) {
                oldest_time = dns_cache[i].timestamp;
                free_slot = i;
            }
        }
    }
    
    if (free_slot >= 0) {
        strncpy(dns_cache[free_slot].hostname, hostname, sizeof(dns_cache[free_slot].hostname) - 1);
        dns_cache[free_slot].hostname[sizeof(dns_cache[free_slot].hostname) - 1] = '\0';
        dns_cache[free_slot].ip_addr = ip_addr;
        dns_cache[free_slot].ttl = ttl;
        dns_cache[free_slot].timestamp = get_tick_count();
        dns_cache[free_slot].valid = 1;
        return 0;
    }
    
    return -1;
}

int dns_cache_lookup(const char* hostname, uint32_t* ip_addr) {
    if (!hostname || !ip_addr) {
        return -1;
    }
    
    uint32_t current_time = get_tick_count();
    
    for (int i = 0; i < DNS_CACHE_SIZE; i++) {
        if (dns_cache[i].valid && strcmp(dns_cache[i].hostname, hostname) == 0) {
            uint32_t age_ticks = current_time - dns_cache[i].timestamp;
            uint32_t age_secs = age_ticks / 1000;
            
            if (age_secs < dns_cache[i].ttl) {
                *ip_addr = dns_cache[i].ip_addr;
                return 0;
            } else {
                dns_cache[i].valid = 0;
                return -1;
            }
        }
    }
    
    return -1;
}

void dns_cache_remove(const char* hostname) {
    if (!hostname) return;
    
    for (int i = 0; i < DNS_CACHE_SIZE; i++) {
        if (dns_cache[i].valid && strcmp(dns_cache[i].hostname, hostname) == 0) {
            dns_cache[i].valid = 0;
            break;
        }
    }
}

int dns_cache_get_entries(dns_cache_entry_t* entries, int max_entries) {
    if (!entries || max_entries <= 0) {
        return 0;
    }
    
    int count = 0;
    for (int i = 0; i < DNS_CACHE_SIZE && count < max_entries; i++) {
        if (dns_cache[i].valid) {
            memcpy(&entries[count], &dns_cache[i], sizeof(dns_cache_entry_t));
            count++;
        }
    }
    
    return count;
}


// Query Building


static int dns_build_query(const char* hostname, uint16_t id, uint8_t* buffer, int max_len) {
    if (!hostname || !buffer || max_len < (int)sizeof(dns_header_t) + 4) {
        return -1;
    }
    
    dns_header_t* hdr = (dns_header_t*)buffer;
    hdr->id = htons(id);
    hdr->flags = htons(DNS_FLAG_RD);
    hdr->qdcount = htons(1);
    hdr->ancount = 0;
    hdr->nscount = 0;
    hdr->arcount = 0;
    
    int offset = sizeof(dns_header_t);
    
    int name_len = dns_encode_name(hostname, &buffer[offset], max_len - offset - 4);
    if (name_len < 0) {
        return -1;
    }
    offset += name_len;
    
    if (offset + 4 > max_len) {
        return -1;
    }
    
    buffer[offset++] = 0;
    buffer[offset++] = DNS_TYPE_A;
    buffer[offset++] = 0;
    buffer[offset++] = DNS_CLASS_IN;
    
    return offset;
}


// Response Parsing


static int dns_parse_response(const uint8_t* packet, int packet_len, 
                               uint32_t* ip_addr, uint32_t* ttl) {
    if (!packet || packet_len < (int)sizeof(dns_header_t) || !ip_addr) {
        return -1;
    }
    
    dns_header_t* hdr = (dns_header_t*)packet;
    uint16_t flags = ntohs(hdr->flags);
    
    if (!(flags & DNS_FLAG_QR)) {
        return -1;
    }
    
    int rcode = flags & DNS_FLAG_RCODE;
    if (rcode != DNS_RCODE_OK) {
        serial_puts("DNS: Error response code\n");
        return -1;
    }
    
    uint16_t ancount = ntohs(hdr->ancount);
    if (ancount == 0) {
        serial_puts("DNS: No answers\n");
        return -1;
    }
    
    int offset = sizeof(dns_header_t);
    
    // Skip question section
    uint16_t qdcount = ntohs(hdr->qdcount);
    for (int i = 0; i < qdcount; i++) {
        while (offset < packet_len) {
            uint8_t len = packet[offset];
            if (len == 0) {
                offset++;
                break;
            }
            if ((len & 0xC0) == 0xC0) {
                offset += 2;
                break;
            }
            offset += len + 1;
        }
        offset += 4;  // Type and class
    }
    
    // Parse answer section
    for (int i = 0; i < ancount && offset < packet_len; i++) {
        char name[128];
        int new_offset = dns_decode_name(packet, packet_len, offset, name, sizeof(name));
        if (new_offset < 0) {
            return -1;
        }
        offset = new_offset;
        
        if (offset + 10 > packet_len) {
            return -1;
        }
        
        uint16_t type = (packet[offset] << 8) | packet[offset + 1];
        uint16_t class = (packet[offset + 2] << 8) | packet[offset + 3];
        uint32_t record_ttl = (packet[offset + 4] << 24) | (packet[offset + 5] << 16) |
                              (packet[offset + 6] << 8) | packet[offset + 7];
        uint16_t rdlength = (packet[offset + 8] << 8) | packet[offset + 9];
        offset += 10;
        
        if (offset + rdlength > packet_len) {
            return -1;
        }
        
        if (type == DNS_TYPE_A && class == DNS_CLASS_IN && rdlength == 4) {
            memcpy(ip_addr, &packet[offset], 4);
            if (ttl) *ttl = record_ttl;
            return 0;
        }
        
        offset += rdlength;
    }
    
    serial_puts("DNS: No A record found\n");
    return -1;
}


// Pre-Resolution ARP


static int dns_ensure_gateway_arp(uint32_t dns_server, uint32_t timeout_ms) {
    // Find interface for DNS server
    net_interface_t* iface;
    uint32_t gateway;
    
    if (ipv4_route(dns_server, &iface, &gateway) != 0) {
        serial_puts("DNS: No route to DNS server\n");
        return -1;
    }
    
    // Resolve gateway MAC
    mac_addr_t mac;
    if (ipv4_resolve_arp(iface, gateway, &mac, timeout_ms) != 0) {
        serial_puts("DNS: Failed to resolve gateway MAC\n");
        return -1;
    }
    
    return 0;
}


// Main Resolution Function


int dns_resolve(const char* hostname, uint32_t* ip_addr) {
    if (!hostname || !ip_addr) {
        return -1;
    }
    
    // Check if it's already an IP address
    uint32_t parsed_ip = string_to_ip(hostname);
    if (parsed_ip != 0) {
        *ip_addr = parsed_ip;
        return 0;
    }
    
    // Check cache
    if (dns_cache_lookup(hostname, ip_addr) == 0) {
        serial_puts("DNS: Cache hit for ");
        serial_puts(hostname);
        serial_puts("\n");
        return 0;
    }
    
    if (dns_config.primary_dns == 0) {
        serial_puts("DNS: No DNS server configured\n");
        return -1;
    }
    
    serial_puts("DNS: Resolving ");
    serial_puts(hostname);
    serial_puts("...\n");
    
    // First, ensure we can reach the DNS server (resolve gateway ARP)
    if (dns_ensure_gateway_arp(dns_config.primary_dns, 3000) != 0) {
        serial_puts("DNS: Cannot reach DNS server (ARP failed)\n");
        return -1;
    }
    
    // Create UDP socket
    udp_socket_t* sock = udp_socket_create();
    if (!sock) {
        serial_puts("DNS: Socket creation failed\n");
        return -1;
    }
    
    // Bind to any port
    if (udp_socket_bind(sock, 0, 0) != 0) {
        serial_puts("DNS: Bind failed\n");
        udp_socket_close(sock);
        return -1;
    }
    
    // Build query
    uint8_t query[512];
    uint16_t query_id = dns_generate_id();
    int query_len = dns_build_query(hostname, query_id, query, sizeof(query));
    if (query_len < 0) {
        serial_puts("DNS: Query build failed\n");
        udp_socket_close(sock);
        return -1;
    }
    
    int result = -1;
    uint32_t dns_server = dns_config.primary_dns;
    
    for (int retry = 0; retry < dns_config.retry_count && result != 0; retry++) {
        // Send query
        serial_puts("DNS: Sending query to ");
        serial_puts(ip_to_string(dns_server));
        serial_puts(" (retry ");
        char buf[4];
        itoa(retry + 1, buf, 10);
        serial_puts(buf);
        serial_puts(")\n");
        
        if (udp_socket_sendto(sock, query, query_len, dns_server, DNS_PORT) < 0) {
            serial_puts("DNS: Send failed, retrying...\n");
            
            // Poll network to process pending packets
            dns_delay(10);
            continue;
        }
        
        serial_puts("DNS: Query sent, waiting for response...\n");
        
        // Wait for response with active polling
        uint8_t response[512];
        uint32_t src_ip;
        uint16_t src_port;
        
        uint32_t start_time = get_tick_count();
        uint32_t poll_count = 0;
        
        while ((get_tick_count() - start_time) < dns_config.timeout_ms) {
            // Yield and poll network for incoming packets
            dns_yield();
            dns_poll_network();
            
            poll_count++;
            
            // Debug output every 100 polls
            if (poll_count % 500 == 0) {
                serial_puts("DNS: Still waiting (");
                char tbuf[16];
                itoa(get_tick_count() - start_time, tbuf, 10);
                serial_puts(tbuf);
                serial_puts("ms elapsed)\n");
            }
            
            int len = udp_socket_recvfrom(sock, response, sizeof(response), &src_ip, &src_port);
            if (len > 0) {
                serial_puts("DNS: Received packet from ");
                serial_puts(ip_to_string(src_ip));
                serial_puts(":");
                char pbuf[8];
                itoa(src_port, pbuf, 10);
                serial_puts(pbuf);
                serial_puts(", len=");
                itoa(len, pbuf, 10);
                serial_puts(pbuf);
                serial_puts("\n");
                
                if (src_port == DNS_PORT) {
                    dns_header_t* resp_hdr = (dns_header_t*)response;
                    if (ntohs(resp_hdr->id) == query_id) {
                        uint32_t ttl = DNS_DEFAULT_TTL;
                        if (dns_parse_response(response, len, ip_addr, &ttl) == 0) {
                            dns_cache_add(hostname, *ip_addr, ttl);
                            
                            serial_puts("DNS: Resolved ");
                            serial_puts(hostname);
                            serial_puts(" -> ");
                            serial_puts(ip_to_string(*ip_addr));
                            serial_puts("\n");
                            
                            result = 0;
                            goto done;
                        } else {
                            serial_puts("DNS: Parse failed\n");
                        }
                    } else {
                        serial_puts("DNS: ID mismatch\n");
                    }
                }
            }
        }
        
        serial_puts("DNS: Timeout after ");
        itoa(dns_config.timeout_ms, buf, 10);
        serial_puts(buf);
        serial_puts("ms\n");
        
        // Try secondary DNS on later retries
        if (retry >= 1 && dns_config.secondary_dns != 0) {
            dns_server = dns_config.secondary_dns;
        }
    }
    
done:
    udp_socket_close(sock);
    return result;
}


// Async Resolution (simplified)


static void (*dns_async_callback)(const char*, uint32_t, int) = NULL;
static char dns_async_hostname[128];

int dns_resolve_async(const char* hostname, void (*callback)(const char*, uint32_t, int)) {
    if (!hostname || !callback) {
        return -1;
    }
    
    strncpy(dns_async_hostname, hostname, sizeof(dns_async_hostname) - 1);
    dns_async_callback = callback;
    
    uint32_t ip_addr;
    int result = dns_resolve(hostname, &ip_addr);
    
    if (dns_async_callback) {
        dns_async_callback(dns_async_hostname, ip_addr, result);
    }
    
    return result;
}
