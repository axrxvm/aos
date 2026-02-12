/*
 * === AOS HEADER BEGIN ===
 * include/net/dns.h
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */


#ifndef DNS_H
#define DNS_H

#include <stdint.h>
#include <net/net.h>

// DNS port
#define DNS_PORT 53

// DNS query types
#define DNS_TYPE_A      1   // IPv4 address
#define DNS_TYPE_NS     2   // Name server
#define DNS_TYPE_CNAME  5   // Canonical name
#define DNS_TYPE_MX     15  // Mail exchange
#define DNS_TYPE_TXT    16  // Text record
#define DNS_TYPE_AAAA   28  // IPv6 address

// DNS query class
#define DNS_CLASS_IN    1   // Internet

// DNS flags
#define DNS_FLAG_QR     0x8000  // Query/Response (1 = response)
#define DNS_FLAG_OPCODE 0x7800  // Operation code
#define DNS_FLAG_AA     0x0400  // Authoritative answer
#define DNS_FLAG_TC     0x0200  // Truncated
#define DNS_FLAG_RD     0x0100  // Recursion desired
#define DNS_FLAG_RA     0x0080  // Recursion available
#define DNS_FLAG_RCODE  0x000F  // Response code

// DNS response codes
#define DNS_RCODE_OK        0   // No error
#define DNS_RCODE_FORMERR   1   // Format error
#define DNS_RCODE_SERVFAIL  2   // Server failure
#define DNS_RCODE_NXDOMAIN  3   // Non-existent domain
#define DNS_RCODE_NOTIMP    4   // Not implemented
#define DNS_RCODE_REFUSED   5   // Query refused

// DNS cache settings
#define DNS_CACHE_SIZE      32
#define DNS_DEFAULT_TTL     300  // 5 minutes

// DNS header structure (12 bytes)
typedef struct {
    uint16_t id;            // Transaction ID
    uint16_t flags;         // Flags
    uint16_t qdcount;       // Question count
    uint16_t ancount;       // Answer count
    uint16_t nscount;       // Authority count
    uint16_t arcount;       // Additional count
} __attribute__((packed)) dns_header_t;

// DNS question structure
typedef struct {
    // Name is variable length (not in struct)
    uint16_t qtype;         // Query type
    uint16_t qclass;        // Query class
} __attribute__((packed)) dns_question_t;

// DNS resource record structure
typedef struct {
    // Name is variable length (not in struct)
    uint16_t type;          // Record type
    uint16_t class;         // Record class
    uint32_t ttl;           // Time to live
    uint16_t rdlength;      // Resource data length
    // Resource data follows (variable length)
} __attribute__((packed)) dns_rr_t;

// DNS cache entry
typedef struct {
    char hostname[128];     // Hostname
    uint32_t ip_addr;       // Resolved IP address
    uint32_t ttl;           // Time to live (remaining)
    uint32_t timestamp;     // When entry was added
    uint8_t valid;          // Entry is valid
} dns_cache_entry_t;

// DNS resolver configuration
typedef struct {
    uint32_t primary_dns;       // Primary DNS server
    uint32_t secondary_dns;     // Secondary DNS server
    uint32_t timeout_ms;        // Query timeout in ms
    uint8_t retry_count;        // Number of retries
} dns_config_t;

// DNS initialization
void dns_init(void);

// DNS configuration
void dns_set_server(uint32_t primary, uint32_t secondary);
void dns_get_servers(uint32_t* primary, uint32_t* secondary);
void dns_set_timeout(uint32_t timeout_ms);
void dns_set_retry_count(uint8_t count);

// DNS resolution
int dns_resolve(const char* hostname, uint32_t* ip_addr);
int dns_resolve_async(const char* hostname, void (*callback)(const char*, uint32_t, int));

// DNS cache management
void dns_cache_clear(void);
int dns_cache_add(const char* hostname, uint32_t ip_addr, uint32_t ttl);
int dns_cache_lookup(const char* hostname, uint32_t* ip_addr);
void dns_cache_remove(const char* hostname);
int dns_cache_get_entries(dns_cache_entry_t* entries, int max_entries);

// DNS utilities
int dns_encode_name(const char* hostname, uint8_t* buffer, int max_len);
int dns_decode_name(const uint8_t* packet, int packet_len, int offset, 
                    char* name, int max_name_len);

#endif // DNS_H
