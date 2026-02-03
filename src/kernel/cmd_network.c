/*
 * === AOS HEADER BEGIN ===
 * ./src/kernel/cmd_network.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.8.5
 * === AOS HEADER END ===
 */


#include <command.h>
#include <command_registry.h>
#include <net/net.h>
#include <net/icmp.h>
#include <net/tcp.h>
#include <net/udp.h>
#include <net/arp.h>
#include <net/ethernet.h>
#include <net/dns.h>
#include <net/http.h>
#include <net/ftp.h>
#include <net/dhcp.h>
#include <net/netconfig.h>
#include <vga.h>
#include <string.h>
#include <stdlib.h>
#include <vmm.h>
#include <fs/vfs.h>
#include <shell.h>

extern uint32_t get_tick_count(void);

// Static variables for ping tracking
static int ping_count = 0;
static int ping_received = 0;
static uint16_t ping_sequence = 0;
static uint32_t ping_dest_ip = 0;
static volatile int ping_reply_received = 0;  // Flag for current ping reply
#define REPO_BASE_URL "http://repo.aosproject.workers.dev/main/"

// Ping callback
static void ping_reply_handler(uint32_t src_ip, uint16_t sequence, uint32_t rtt_ms) {
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    vga_puts("Reply from ");
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    vga_puts(ip_to_string(src_ip));
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    vga_puts(": seq=");
    
    char buf[16];
    itoa(sequence, buf, 10);
    vga_puts(buf);
    vga_puts(" time=");
    vga_set_color(VGA_ATTR(VGA_COLOR_YELLOW, VGA_COLOR_BLACK));
    itoa(rtt_ms, buf, 10);
    vga_puts(buf);
    vga_puts("ms");
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    vga_puts("\n");
    
    ping_received++;
    ping_reply_received = 1;  // Signal that reply was received
}

void cmd_ping(const char* args) {
    if (!args || *args == '\0') {
        vga_set_color(VGA_ATTR(VGA_COLOR_YELLOW, VGA_COLOR_BLACK));
        vga_puts("Usage: ");
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
        vga_puts("ping <ip_address> [count]\n");
        vga_set_color(VGA_ATTR(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK));
        vga_puts("Example: ping 127.0.0.1 (loopback test)\n");
        vga_puts("Note: Loopback works, external IPs require functional QEMU networking\n");
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        return;
    }
    
    // Parse arguments
    char ip_str[32];
    int count = 4;  // Default 4 pings
    
    // Extract IP address
    int i = 0;
    while (*args && *args != ' ' && i < 31) {
        ip_str[i++] = *args++;
    }
    ip_str[i] = '\0';
    
    // Skip spaces
    while (*args == ' ') args++;
    
    // Parse count if provided
    if (*args) {
        count = atoi(args);
        if (count <= 0 || count > 100) {
            count = 4;
        }
    }
    
    // Convert IP string to address
    ping_dest_ip = string_to_ip(ip_str);
    
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    vga_puts("PING ");
    vga_puts(ip_str);
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    vga_puts("\n");
    
    // Set ping callback
    icmp_set_ping_callback(ping_reply_handler);
    
    // Send ping requests
    ping_count = count;
    ping_received = 0;
    ping_sequence = 0;
    
    for (int i = 0; i < count; i++) {
        // Check for cancellation
        if (shell_is_cancelled()) {
            vga_set_color(VGA_ATTR(VGA_COLOR_YELLOW, VGA_COLOR_BLACK));
            vga_puts("\nPing cancelled by user.\n");
            vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
            break;
        }
        
        // Prepare ping data
        uint8_t ping_data[56];
        for (int j = 0; j < 56; j++) {
            ping_data[j] = 0x20 + (j % 32);
        }
        
        // Reset reply flag
        ping_reply_received = 0;
        
        // Send ICMP echo request
        int ret = icmp_send_echo_request(ping_dest_ip, 1, ping_sequence++, ping_data, 56);
        
        if (ret < 0) {
            vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
            vga_puts("Failed to send ping request\n");
            vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
            continue;
        }
        
        // Wait for reply with timeout (1000ms)
        uint32_t start = get_tick_count();
        uint32_t timeout_ms = 1000;
        
        while ((get_tick_count() - start) < timeout_ms && !ping_reply_received && !shell_is_cancelled()) {
            // Yield to allow interrupts
            __asm__ volatile("sti");
            __asm__ volatile("hlt");
            
            // Poll for incoming packets
            net_poll();
        }
        
        // Check if cancelled during wait
        if (shell_is_cancelled()) {
            vga_set_color(VGA_ATTR(VGA_COLOR_YELLOW, VGA_COLOR_BLACK));
            vga_puts("\nPing cancelled by user.\n");
            vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
            break;
        }
        
        // If no reply received, print timeout message
        if (!ping_reply_received) {
            vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
            vga_puts("Request timeout for icmp_seq ");
            char buf[16];
            itoa(ping_sequence - 1, buf, 10);
            vga_puts(buf);
            vga_puts("\n");
            vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        }
    }
    
    // Print statistics
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    vga_puts("\n--- ping statistics ---\n");
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    char buf[64];
    itoa(ping_count, buf, 10);
    vga_puts(buf);
    vga_puts(" packets transmitted, ");
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    itoa(ping_received, buf, 10);
    vga_puts(buf);
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    vga_puts(" received, ");
    
    int loss = ((ping_count - ping_received) * 100) / ping_count;
    if (loss > 50) {
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
    } else if (loss > 0) {
        vga_set_color(VGA_ATTR(VGA_COLOR_YELLOW, VGA_COLOR_BLACK));
    } else {
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    }
    itoa(loss, buf, 10);
    vga_puts(buf);
    vga_puts("% packet loss");
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    vga_puts("\n");
}

void cmd_ifconfig(const char* args) {
    if (!args || *args == '\0') {
        // List all interfaces
        int count = net_interface_count();
        
        if (count == 0) {
            vga_set_color(VGA_ATTR(VGA_COLOR_YELLOW, VGA_COLOR_BLACK));
            vga_puts("No network interfaces found\n");
            vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
            return;
        }
        
        for (int i = 0; i < count; i++) {
            if (shell_is_cancelled()) {
                vga_set_color(VGA_ATTR(VGA_COLOR_YELLOW, VGA_COLOR_BLACK));
                vga_puts("\nCommand cancelled.\n");
                vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
                return;
            }
            
            net_interface_t* iface = net_interface_get_by_index(i);
            if (!iface) continue;
            
            vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
            vga_puts(iface->name);
            vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
            vga_puts(": ");
            
            if (iface->flags & IFF_UP) {
                vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
                vga_puts("UP ");
                vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
            }
            if (iface->flags & IFF_LOOPBACK) {
                vga_set_color(VGA_ATTR(VGA_COLOR_YELLOW, VGA_COLOR_BLACK));
                vga_puts("LOOPBACK ");
                vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
            }
            if (iface->flags & IFF_RUNNING) {
                vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
                vga_puts("RUNNING ");
                vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
            }
            vga_puts("\n");
            
            // IP address
            vga_set_color(VGA_ATTR(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK));
            vga_puts("  inet ");
            vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
            vga_puts(ip_to_string(iface->ip_addr));
            vga_set_color(VGA_ATTR(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK));
            vga_puts("  netmask ");
            vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
            vga_puts(ip_to_string(iface->netmask));
            vga_puts("\n");
            
            // MAC address (if not loopback)
            if (!(iface->flags & IFF_LOOPBACK)) {
                char mac_str[20];
                mac_to_string(&iface->mac_addr, mac_str);
                vga_set_color(VGA_ATTR(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK));
                vga_puts("  ether ");
                vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
                vga_puts(mac_str);
                vga_puts("\n");
            }
            
            // Statistics
            char buf[16];
            vga_puts("  RX packets: ");
            itoa(iface->stats.rx_packets, buf, 10);
            vga_puts(buf);
            vga_puts("  bytes: ");
            itoa(iface->stats.rx_bytes, buf, 10);
            vga_puts(buf);
            vga_puts("\n  TX packets: ");
            itoa(iface->stats.tx_packets, buf, 10);
            vga_puts(buf);
            vga_puts("  bytes: ");
            itoa(iface->stats.tx_bytes, buf, 10);
            vga_puts(buf);
            vga_puts("\n\n");
        }
    } else {
        vga_puts("Interface configuration not yet supported\n");
        vga_puts("Usage: ifconfig [interface] [up|down] [ip] [netmask]\n");
    }
}

void cmd_netstat(const char* args) {
    (void)args;
    
    vga_puts("Active Internet connections\n");
    vga_puts("Proto  Local Address          State\n");
    
    // This is a simplified implementation
    // In a real implementation, we would iterate through all TCP/UDP sockets
    
    vga_puts("\nActive ARP cache entries:\n");
    vga_puts("IP Address       Hardware Address\n");
    
    arp_cache_entry_t entries[32];
    int count = arp_cache_get_entries(entries, 32);
    
    if (count == 0) {
        vga_puts("(No entries)\n");
    } else {
        for (int i = 0; i < count; i++) {
            vga_puts(ip_to_string(entries[i].ip_addr));
            vga_puts("  ");
            
            char mac_str[20];
            mac_to_string(&entries[i].mac_addr, mac_str);
            vga_puts(mac_str);
            vga_puts("\n");
        }
    }
}

void cmd_arp(const char* args) {
    if (!args || *args == '\0') {
        // Display ARP cache
        vga_puts("ARP cache:\n");
        vga_puts("IP Address       Hardware Address\n");
        
        arp_cache_entry_t entries[32];
        int count = arp_cache_get_entries(entries, 32);
        
        if (count == 0) {
            vga_puts("(No entries)\n");
        } else {
            for (int i = 0; i < count; i++) {
                vga_puts(ip_to_string(entries[i].ip_addr));
                vga_puts("  ");
                
                char mac_str[20];
                mac_to_string(&entries[i].mac_addr, mac_str);
                vga_puts(mac_str);
                vga_puts("\n");
            }
        }
    } else if (strncmp(args, "-d", 2) == 0) {
        // Clear ARP cache
        arp_cache_clear();
        vga_puts("ARP cache cleared\n");
    } else {
        vga_puts("Usage: arp [-d]\n");
        vga_puts("  -d  Clear ARP cache\n");
    }
}

// DNS resolver command
void cmd_nslookup(const char* args) {
    if (!args || *args == '\0') {
        vga_puts("Usage: nslookup <hostname>\n");
        vga_puts("Example: nslookup www.example.com\n");
        return;
    }
    
    vga_puts("Resolving ");
    vga_puts(args);
    vga_puts("...\n");
    
    uint32_t ip_addr;
    int result = dns_resolve(args, &ip_addr);
    
    if (result == 0) {
        vga_puts("Address: ");
        vga_puts(ip_to_string(ip_addr));
        vga_puts("\n");
    } else {
        vga_puts("Failed to resolve hostname\n");
    }
}

// DNS cache command
void cmd_dns(const char* args) {
    if (!args || *args == '\0') {
        // Show DNS servers and cache
        uint32_t primary, secondary;
        dns_get_servers(&primary, &secondary);
        
        vga_puts("DNS Servers:\n");
        vga_puts("  Primary:   ");
        vga_puts(ip_to_string(primary));
        vga_puts("\n  Secondary: ");
        vga_puts(ip_to_string(secondary));
        vga_puts("\n\nDNS Cache:\n");
        
        dns_cache_entry_t entries[32];
        int count = dns_cache_get_entries(entries, 32);
        
        if (count == 0) {
            vga_puts("(empty)\n");
        } else {
            for (int i = 0; i < count; i++) {
                vga_puts("  ");
                vga_puts(entries[i].hostname);
                vga_puts(" -> ");
                vga_puts(ip_to_string(entries[i].ip_addr));
                vga_puts("\n");
            }
        }
        return;
    }
    
    if (strncmp(args, "-c", 2) == 0 || strncmp(args, "clear", 5) == 0) {
        dns_cache_clear();
        vga_puts("DNS cache cleared\n");
        return;
    }
    
    if (strncmp(args, "-s", 2) == 0 || strncmp(args, "set", 3) == 0) {
        // Parse: dns -s <primary> [secondary]
        const char* ptr = args;
        while (*ptr && *ptr != ' ') ptr++;
        while (*ptr == ' ') ptr++;
        
        if (!*ptr) {
            vga_puts("Usage: dns -s <primary_ip> [secondary_ip]\n");
            return;
        }
        
        char primary_str[32], secondary_str[32] = "";
        int i = 0;
        while (*ptr && *ptr != ' ' && i < 31) {
            primary_str[i++] = *ptr++;
        }
        primary_str[i] = '\0';
        
        while (*ptr == ' ') ptr++;
        if (*ptr) {
            i = 0;
            while (*ptr && *ptr != ' ' && i < 31) {
                secondary_str[i++] = *ptr++;
            }
            secondary_str[i] = '\0';
        }
        
        uint32_t primary = string_to_ip(primary_str);
        uint32_t secondary = secondary_str[0] ? string_to_ip(secondary_str) : 0;
        
        dns_set_server(primary, secondary);
        vga_puts("DNS servers updated\n");
        return;
    }
    
    vga_puts("Usage: dns [-c|-s <primary> [secondary]]\n");
    vga_puts("  dns        Show DNS servers and cache\n");
    vga_puts("  dns -c     Clear DNS cache\n");
    vga_puts("  dns -s     Set DNS servers\n");
}

// HTTP GET command
void cmd_wget(const char* args) {
    if (!args || *args == '\0') {
        vga_puts("Usage: wget <url|@repo/path> [output_file]\n");
        return;
    }
    // Parse arguments
    char url[256];
    char output[128] = "";
    int i = 0;
    // Parse URL
    while (*args && *args != ' ' && i < (int)sizeof(url) - 1) {
        url[i++] = *args++;
    }
    url[i] = '\0';
    // Skip spaces
    while (*args == ' ') args++;
    // Parse optional output file
    if (*args) {
        i = 0;
        while (*args && *args != ' ' && i < (int)sizeof(output) - 1) {
            output[i++] = *args++;
        }
        output[i] = '\0';
    }
    // Expand @repo shorthand
    if (strncmp(url, "@repo", 5) == 0) {
        const char* path = url + 5;
        // Require a path
        if (*path == '\0') {
            vga_puts("Error: @repo requires a file path\n");
            return;
        }
        if (*path == '/') path++;
        char full_url[256];
        full_url[0] = '\0';
        strncat(full_url, REPO_BASE_URL, sizeof(full_url) - 1);
        strncat(full_url, path, sizeof(full_url) - strlen(full_url) - 1);
        strncpy(url, full_url, sizeof(url) - 1);
        url[sizeof(url) - 1] = '\0';
    }
    vga_puts("Downloading ");
    vga_puts(url);
    vga_puts("...\n");
    if (output[0]) {
        // Download to file
        int result = http_download(url, output);
        if (result == 0) {
            vga_puts("Downloaded to ");
            vga_puts(output);
            vga_puts("\n");
        } else {
            vga_puts("Download failed\n");
        }
    } else {
        // Display content
        http_response_t* response = http_response_create();
        if (!response) {
            vga_puts("Memory allocation failed\n");
            return;
        }
        int result = http_get(url, response);
        if (result == 0) {
            vga_puts("\nHTTP ");
            char code_str[8];
            itoa(response->status_code, code_str, 10);
            vga_puts(code_str);
            vga_puts(" ");
            vga_puts(response->status_text);
            vga_puts("\n\n");
            if (response->body && response->body_len > 0) {
                int display_len = response->body_len;
                if (display_len > 2048) {
                    display_len = 2048;
                }
                vga_puts((const char*)response->body);

                if (response->body_len > 2048) {
                    vga_puts("\n... (truncated)\n");
                } else {
                    vga_puts("\n");
                }
            }
        } else {
            vga_puts("Request failed\n");
        }
        http_response_free(response);
    }
}

// HTTP aurl command (aOS URL fetcher) - Military-grade HTTP client
void cmd_aurl(const char* args) {
    if (!args || *args == '\0') {
        vga_puts("Usage: aurl [-v] <url>\n");
        vga_puts("aOS URL Fetcher - Military-grade HTTP client\n");
        vga_puts("Options:\n");
        vga_puts("  -v  Verbose (show headers)\n");
        return;
    }
    
    int verbose = 0;
    const char* url = args;
    
    if (strncmp(args, "-v", 2) == 0) {
        verbose = 1;
        url = args + 2;
        while (*url == ' ') url++;
    }
    
    http_response_t* response = http_response_create();
    if (!response) {
        vga_puts("Memory allocation failed\n");
        return;
    }
    
    int result = http_get(url, response);
    if (result == 0) {
        if (verbose) {
            vga_puts("< HTTP/1.1 ");
            char code_str[8];
            itoa(response->status_code, code_str, 10);
            vga_puts(code_str);
            vga_puts(" ");
            vga_puts(response->status_text);
            vga_puts("\n");
            
            for (int i = 0; i < response->header_count; i++) {
                vga_puts("< ");
                vga_puts(response->headers[i].name);
                vga_puts(": ");
                vga_puts(response->headers[i].value);
                vga_puts("\n");
            }
            vga_puts("<\n");
        }
        
        if (response->body && response->body_len > 0) {
            vga_puts((const char*)response->body);
        }
    } else {
        vga_puts("Request failed\n");
    }
    
    http_response_free(response);
}

// FTP session state
static ftp_session_t* ftp_current_session = NULL;

// FTP client command
void cmd_ftp(const char* args) {
    if (!args || *args == '\0') {
        vga_puts("Usage: ftp <command> [args]\n");
        vga_puts("Commands:\n");
        vga_puts("  open <host> [port]     Connect to FTP server\n");
        vga_puts("  user <username> <pass> Login\n");
        vga_puts("  close                  Disconnect\n");
        vga_puts("  pwd                    Print working directory\n");
        vga_puts("  cd <dir>               Change directory\n");
        vga_puts("  ls [path]              List directory\n");
        vga_puts("  get <remote> [local]   Download file\n");
        vga_puts("  put <local> [remote]   Upload file\n");
        vga_puts("  mkdir <dir>            Create directory\n");
        vga_puts("  rm <file>              Delete file\n");
        vga_puts("  status                 Show connection status\n");
        return;
    }
    
    // Parse command
    char cmd[32], arg1[128], arg2[128];
    cmd[0] = arg1[0] = arg2[0] = '\0';
    
    int i = 0;
    while (*args && *args != ' ' && i < 31) {
        cmd[i++] = *args++;
    }
    cmd[i] = '\0';
    
    while (*args == ' ') args++;
    i = 0;
    while (*args && *args != ' ' && i < 127) {
        arg1[i++] = *args++;
    }
    arg1[i] = '\0';
    
    while (*args == ' ') args++;
    i = 0;
    while (*args && *args != ' ' && i < 127) {
        arg2[i++] = *args++;
    }
    arg2[i] = '\0';
    
    // Handle commands
    if (strcmp(cmd, "open") == 0) {
        if (!arg1[0]) {
            vga_puts("Usage: ftp open <host> [port]\n");
            return;
        }
        
        if (ftp_current_session) {
            ftp_session_free(ftp_current_session);
        }
        
        ftp_current_session = ftp_session_create();
        if (!ftp_current_session) {
            vga_puts("Failed to create FTP session\n");
            return;
        }
        
        uint16_t port = arg2[0] ? atoi(arg2) : FTP_CONTROL_PORT;
        if (ftp_connect(ftp_current_session, arg1, port) == 0) {
            vga_puts("Connected to ");
            vga_puts(arg1);
            vga_puts("\n");
        } else {
            vga_puts("Connection failed\n");
            ftp_session_free(ftp_current_session);
            ftp_current_session = NULL;
        }
    }
    else if (strcmp(cmd, "user") == 0) {
        if (!ftp_current_session || !ftp_current_session->connected) {
            vga_puts("Not connected. Use 'ftp open' first.\n");
            return;
        }
        
        const char* user = arg1[0] ? arg1 : "anonymous";
        const char* pass = arg2[0] ? arg2 : "user@aOS";
        
        if (ftp_login(ftp_current_session, user, pass) == 0) {
            vga_puts("Logged in as ");
            vga_puts(user);
            vga_puts("\n");
        } else {
            vga_puts("Login failed\n");
        }
    }
    else if (strcmp(cmd, "close") == 0) {
        if (ftp_current_session) {
            ftp_disconnect(ftp_current_session);
            ftp_session_free(ftp_current_session);
            ftp_current_session = NULL;
            vga_puts("Disconnected\n");
        } else {
            vga_puts("Not connected\n");
        }
    }
    else if (strcmp(cmd, "pwd") == 0) {
        if (!ftp_current_session || !ftp_current_session->logged_in) {
            vga_puts("Not logged in\n");
            return;
        }
        
        char path[256];
        if (ftp_pwd(ftp_current_session, path, sizeof(path)) == 0) {
            vga_puts(path);
            vga_puts("\n");
        }
    }
    else if (strcmp(cmd, "cd") == 0) {
        if (!ftp_current_session || !ftp_current_session->logged_in) {
            vga_puts("Not logged in\n");
            return;
        }
        
        if (!arg1[0]) {
            vga_puts("Usage: ftp cd <directory>\n");
            return;
        }
        
        if (ftp_cwd(ftp_current_session, arg1) == 0) {
            vga_puts("Directory changed\n");
        } else {
            vga_puts("Failed to change directory\n");
        }
    }
    else if (strcmp(cmd, "ls") == 0) {
        if (!ftp_current_session || !ftp_current_session->logged_in) {
            vga_puts("Not logged in\n");
            return;
        }
        
        char* buffer = (char*)kmalloc(4096);
        if (!buffer) {
            vga_puts("Memory allocation failed\n");
            return;
        }
        
        int len = ftp_list(ftp_current_session, arg1[0] ? arg1 : NULL, buffer, 4096);
        if (len > 0) {
            vga_puts(buffer);
        } else {
            vga_puts("Failed to list directory\n");
        }
        
        kfree(buffer);
    }
    else if (strcmp(cmd, "get") == 0) {
        if (!ftp_current_session || !ftp_current_session->logged_in) {
            vga_puts("Not logged in\n");
            return;
        }
        
        if (!arg1[0]) {
            vga_puts("Usage: ftp get <remote_file> [local_file]\n");
            return;
        }
        
        const char* local = arg2[0] ? arg2 : arg1;
        if (ftp_download(ftp_current_session, arg1, local) == 0) {
            vga_puts("Download complete\n");
        } else {
            vga_puts("Download failed\n");
        }
    }
    else if (strcmp(cmd, "put") == 0) {
        if (!ftp_current_session || !ftp_current_session->logged_in) {
            vga_puts("Not logged in\n");
            return;
        }
        
        if (!arg1[0]) {
            vga_puts("Usage: ftp put <local_file> [remote_file]\n");
            return;
        }
        
        const char* remote = arg2[0] ? arg2 : arg1;
        if (ftp_upload(ftp_current_session, arg1, remote) == 0) {
            vga_puts("Upload complete\n");
        } else {
            vga_puts("Upload failed\n");
        }
    }
    else if (strcmp(cmd, "mkdir") == 0) {
        if (!ftp_current_session || !ftp_current_session->logged_in) {
            vga_puts("Not logged in\n");
            return;
        }
        
        if (!arg1[0]) {
            vga_puts("Usage: ftp mkdir <directory>\n");
            return;
        }
        
        if (ftp_mkdir(ftp_current_session, arg1) == 0) {
            vga_puts("Directory created\n");
        } else {
            vga_puts("Failed to create directory\n");
        }
    }
    else if (strcmp(cmd, "rm") == 0) {
        if (!ftp_current_session || !ftp_current_session->logged_in) {
            vga_puts("Not logged in\n");
            return;
        }
        
        if (!arg1[0]) {
            vga_puts("Usage: ftp rm <file>\n");
            return;
        }
        
        if (ftp_delete(ftp_current_session, arg1) == 0) {
            vga_puts("File deleted\n");
        } else {
            vga_puts("Failed to delete file\n");
        }
    }
    else if (strcmp(cmd, "status") == 0) {
        if (!ftp_current_session) {
            vga_puts("Not connected\n");
        } else {
            vga_puts("Connected to: ");
            vga_puts(ftp_current_session->host);
            vga_puts("\n");
            
            if (ftp_current_session->logged_in) {
                vga_puts("Logged in as: ");
                vga_puts(ftp_current_session->username);
                vga_puts("\nCurrent dir: ");
                vga_puts(ftp_current_session->current_dir);
                vga_puts("\nMode: ");
                vga_puts(ftp_current_session->transfer_mode == FTP_MODE_BINARY ? "Binary" : "ASCII");
                vga_puts("\n");
            } else {
                vga_puts("Not logged in\n");
            }
        }
    }
    else {
        vga_puts("Unknown FTP command: ");
        vga_puts(cmd);
        vga_puts("\n");
    }
}

// DHCP command
void cmd_dhcp(const char* args) {
    net_interface_t* iface = NULL;
    
    if (args && *args) {
        iface = net_interface_get(args);
        if (!iface) {
            vga_puts("Interface not found: ");
            vga_puts(args);
            vga_puts("\n");
            return;
        }
    } else {
        // Use first non-loopback interface
        for (int i = 0; i < net_interface_count(); i++) {
            net_interface_t* if_check = net_interface_get_by_index(i);
            if (if_check && !(if_check->flags & IFF_LOOPBACK)) {
                iface = if_check;
                break;
            }
        }
    }
    
    if (!iface) {
        vga_puts("No suitable network interface found\n");
        return;
    }
    
    vga_puts("Running DHCP on ");
    vga_puts(iface->name);
    vga_puts("...\n");
    
    if (dhcp_discover(iface) == 0) {
        dhcp_config_t* config = dhcp_get_config();
        if (config) {
            vga_puts("DHCP configuration received:\n");
            vga_puts("  IP Address: ");
            vga_puts(ip_to_string(config->ip_addr));
            vga_puts("\n  Netmask:    ");
            vga_puts(ip_to_string(config->netmask));
            vga_puts("\n  Gateway:    ");
            vga_puts(ip_to_string(config->gateway));
            vga_puts("\n  DNS:        ");
            vga_puts(ip_to_string(config->dns_server));
            vga_puts("\n");
        }
    } else {
        vga_puts("DHCP failed\n");
    }
}

// Network configuration command
void cmd_netconfig(const char* args) {
    if (!args || *args == '\0') {
        vga_puts("Usage: netconfig <interface> <command> [args]\n");
        vga_puts("Commands:\n");
        vga_puts("  static <ip> <netmask> <gateway> [dns]  Set static IP\n");
        vga_puts("  dhcp                                   Use DHCP\n");
        vga_puts("  show                                   Show configuration\n");
        vga_puts("  save                                   Save to file\n");
        vga_puts("  load                                   Load from file\n");
        return;
    }
    
    // Parse interface name
    char iface_name[16], cmd[16];
    const char* ptr = args;
    int i = 0;
    
    while (*ptr && *ptr != ' ' && i < 15) {
        iface_name[i++] = *ptr++;
    }
    iface_name[i] = '\0';
    
    // Handle global commands
    if (strcmp(iface_name, "save") == 0) {
        if (netconfig_save(NULL) == 0) {
            vga_puts("Configuration saved\n");
        } else {
            vga_puts("Failed to save configuration\n");
        }
        return;
    }
    
    if (strcmp(iface_name, "load") == 0) {
        if (netconfig_load(NULL) == 0) {
            vga_puts("Configuration loaded\n");
        } else {
            vga_puts("Failed to load configuration\n");
        }
        return;
    }
    
    // Get interface
    net_interface_t* iface = net_interface_get(iface_name);
    if (!iface) {
        vga_puts("Interface not found: ");
        vga_puts(iface_name);
        vga_puts("\n");
        return;
    }
    
    // Parse command
    while (*ptr == ' ') ptr++;
    i = 0;
    while (*ptr && *ptr != ' ' && i < 15) {
        cmd[i++] = *ptr++;
    }
    cmd[i] = '\0';
    
    if (strcmp(cmd, "static") == 0) {
        // Parse: static <ip> <netmask> <gateway> [dns]
        char ip_str[32], mask_str[32], gw_str[32], dns_str[32] = "";
        
        while (*ptr == ' ') ptr++;
        i = 0;
        while (*ptr && *ptr != ' ' && i < 31) ip_str[i++] = *ptr++;
        ip_str[i] = '\0';
        
        while (*ptr == ' ') ptr++;
        i = 0;
        while (*ptr && *ptr != ' ' && i < 31) mask_str[i++] = *ptr++;
        mask_str[i] = '\0';
        
        while (*ptr == ' ') ptr++;
        i = 0;
        while (*ptr && *ptr != ' ' && i < 31) gw_str[i++] = *ptr++;
        gw_str[i] = '\0';
        
        while (*ptr == ' ') ptr++;
        if (*ptr) {
            i = 0;
            while (*ptr && *ptr != ' ' && i < 31) dns_str[i++] = *ptr++;
            dns_str[i] = '\0';
        }
        
        if (!ip_str[0] || !mask_str[0] || !gw_str[0]) {
            vga_puts("Usage: netconfig <iface> static <ip> <netmask> <gateway> [dns]\n");
            return;
        }
        
        uint32_t ip = string_to_ip(ip_str);
        uint32_t mask = string_to_ip(mask_str);
        uint32_t gw = string_to_ip(gw_str);
        uint32_t dns = dns_str[0] ? string_to_ip(dns_str) : 0;
        
        if (netconfig_set_static(iface, ip, mask, gw, dns) == 0) {
            vga_puts("Static configuration applied\n");
        } else {
            vga_puts("Failed to apply configuration\n");
        }
    }
    else if (strcmp(cmd, "dhcp") == 0) {
        if (netconfig_set_dhcp(iface) == 0) {
            vga_puts("DHCP configuration applied\n");
        } else {
            vga_puts("Failed to apply DHCP configuration\n");
        }
    }
    else if (strcmp(cmd, "show") == 0 || cmd[0] == '\0') {
        netconfig_t* config = netconfig_get(iface);
        if (config) {
            vga_puts("Configuration for ");
            vga_puts(iface->name);
            vga_puts(":\n");
            vga_puts("  Mode:    ");
            vga_puts(config->mode == NETCONFIG_MODE_STATIC ? "Static" : "DHCP");
            vga_puts("\n  IP:      ");
            vga_puts(ip_to_string(config->ip_addr));
            vga_puts("\n  Netmask: ");
            vga_puts(ip_to_string(config->netmask));
            vga_puts("\n  Gateway: ");
            vga_puts(ip_to_string(config->gateway));
            vga_puts("\n  DNS:     ");
            vga_puts(ip_to_string(config->primary_dns));
            vga_puts("\n");
        }
    }
    else {
        vga_puts("Unknown command: ");
        vga_puts(cmd);
        vga_puts("\n");
    }
}

// Hostname command
void cmd_hostname(const char* args) {
    if (!args || *args == '\0') {
        vga_puts(netconfig_get_hostname());
        vga_puts("\n");
    } else {
        if (netconfig_set_hostname(args) == 0) {
            vga_puts("Hostname set to: ");
            vga_puts(args);
            vga_puts("\n");
        } else {
            vga_puts("Failed to set hostname\n");
        }
    }
}

// Network command registration
void cmd_module_network_register(void) {
    command_register_with_category(
        "ping",
        "ping <ip_address> [count]",
        "Test network connectivity",
        "Network",
        cmd_ping
    );
    
    command_register_with_category(
        "ifconfig",
        "ifconfig [interface] [up|down] [ip] [netmask]",
        "Configure network interfaces",
        "Network",
        cmd_ifconfig
    );
    
    command_register_with_category(
        "netstat",
        "netstat",
        "Display network connections",
        "Network",
        cmd_netstat
    );
    
    command_register_with_category(
        "arp",
        "arp [-d]",
        "Display or clear ARP cache",
        "Network",
        cmd_arp
    );
    
    command_register_with_category(
        "nslookup",
        "nslookup <hostname>",
        "Resolve hostname to IP",
        "Network",
        cmd_nslookup
    );
    
    command_register_with_category(
        "dns",
        "dns [-c|-s <primary> [secondary]]",
        "Configure DNS settings",
        "Network",
        cmd_dns
    );
    
    command_register_with_category(
        "wget",
        "wget <url> [output_file]",
        "Download file via HTTP",
        "Network",
        cmd_wget
    );
    
    command_register_with_category(
        "aurl",
        "aurl [-v] <url>",
        "Advanced HTTP client",
        "Network",
        cmd_aurl
    );
    
    command_register_with_category(
        "ftp",
        "ftp <command> [args]",
        "FTP client",
        "Network",
        cmd_ftp
    );
    
    command_register_with_category(
        "dhcp",
        "dhcp [interface]",
        "Request IP via DHCP",
        "Network",
        cmd_dhcp
    );
    
    command_register_with_category(
        "netconfig",
        "netconfig <interface> <static|dhcp|show> [args]",
        "Configure network interface",
        "Network",
        cmd_netconfig
    );
    
    command_register_with_category(
        "hostname",
        "hostname [new_hostname]",
        "Display or set hostname",
        "Network",
        cmd_hostname
    );
}
