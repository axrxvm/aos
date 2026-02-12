/*
 * === AOS HEADER BEGIN ===
 * src/net/ftp.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */


#include <net/ftp.h>
#include <net/tcp.h>
#include <net/dns.h>
#include <net/net.h>
#include <string.h>
#include <stdlib.h>
#include <vmm.h>
#include <serial.h>
#include <arch/i386/pit.h>
#include <fs/vfs.h>

void ftp_init(void) {
    serial_puts("Initializing FTP client...\n");
    serial_puts("FTP client initialized.\n");
}

// Create FTP session
ftp_session_t* ftp_session_create(void) {
    ftp_session_t* session = (ftp_session_t*)kmalloc(sizeof(ftp_session_t));
    if (session) {
        memset(session, 0, sizeof(ftp_session_t));
        session->port = FTP_CONTROL_PORT;
        session->transfer_mode = FTP_MODE_BINARY;
        session->passive_mode = 1;  // Default to passive mode
        strncpy(session->current_dir, "/", sizeof(session->current_dir) - 1);
        session->current_dir[sizeof(session->current_dir) - 1] = '\0';
    }
    return session;
}

// Free FTP session
void ftp_session_free(ftp_session_t* session) {
    if (session) {
        ftp_disconnect(session);
        kfree(session);
    }
}

// Wait for TCP connection to establish
static int ftp_wait_connected(tcp_socket_t* sock, uint32_t timeout_ms) {
    uint32_t start = get_tick_count();
    while ((get_tick_count() - start) < timeout_ms) {
        if (sock->state == TCP_ESTABLISHED) {
            return 0;
        }
        for (volatile int i = 0; i < 1000; i++);
    }
    return -1;
}

// Read reply from FTP server
int ftp_read_reply(ftp_session_t* session) {
    if (!session || !session->control_socket) {
        return -1;
    }
    
    tcp_socket_t* sock = (tcp_socket_t*)session->control_socket;
    uint8_t buffer[512];
    int total = 0;
    int done = 0;
    
    uint32_t start = get_tick_count();
    while (!done && (get_tick_count() - start) < 10000) {
        int received = tcp_socket_recv(sock, buffer + total, sizeof(buffer) - total - 1);
        if (received > 0) {
            total += received;
            buffer[total] = '\0';
            
            // Check for complete response (ends with \r\n and code at start)
            if (total >= 4 && buffer[total-1] == '\n') {
                // Multi-line response check
                if (buffer[3] == '-') {
                    // Multi-line, keep reading until we get final line
                    char code_str[4];
                    strncpy(code_str, (char*)buffer, 3);
                    code_str[3] = '\0';
                    
                    // Look for final line: "XXX " where XXX is the code
                    char* line = (char*)buffer;
                    while ((line = strstr(line + 1, "\n")) != NULL) {
                        if (strncmp(line + 1, code_str, 3) == 0 && line[4] == ' ') {
                            done = 1;
                            break;
                        }
                    }
                } else {
                    done = 1;
                }
            }
            start = get_tick_count();  // Reset timeout
        }
        for (volatile int i = 0; i < 1000; i++);
    }
    
    if (total == 0) {
        return -1;
    }
    
    // Copy reply
    strncpy(session->last_reply, (char*)buffer, sizeof(session->last_reply) - 1);
    session->last_reply[sizeof(session->last_reply) - 1] = '\0';
    
    // Parse response code
    if (total >= 3) {
        char code_str[4];
        strncpy(code_str, (char*)buffer, 3);
        code_str[3] = '\0';
        session->last_code = atoi(code_str);
    } else {
        session->last_code = 0;
    }
    
    serial_puts("FTP < ");
    serial_puts(session->last_reply);
    
    return session->last_code;
}

// Send FTP command
int ftp_send_command(ftp_session_t* session, const char* cmd, const char* arg) {
    if (!session || !session->control_socket || !cmd) {
        return -1;
    }
    
    tcp_socket_t* sock = (tcp_socket_t*)session->control_socket;
    
    // Build command string
    char buffer[512];
    if (arg && *arg) {
        int len = strlen(cmd);
        strcpy(buffer, cmd);
        buffer[len++] = ' ';
        strcpy(buffer + len, arg);
        len += strlen(arg);
        buffer[len++] = '\r';
        buffer[len++] = '\n';
        buffer[len] = '\0';
    } else {
        int len = strlen(cmd);
        strcpy(buffer, cmd);
        buffer[len++] = '\r';
        buffer[len++] = '\n';
        buffer[len] = '\0';
    }
    
    serial_puts("FTP > ");
    serial_puts(buffer);
    
    // Send command
    if (tcp_socket_send(sock, (uint8_t*)buffer, strlen(buffer)) < 0) {
        return -1;
    }
    
    return 0;
}

// Connect to FTP server
int ftp_connect(ftp_session_t* session, const char* host, uint16_t port) {
    if (!session || !host) {
        return -1;
    }
    
    strncpy(session->host, host, sizeof(session->host) - 1);
    session->port = port ? port : FTP_CONTROL_PORT;
    
    // Resolve hostname
    if (dns_resolve(host, &session->ip_addr) != 0) {
        serial_puts("FTP: Failed to resolve ");
        serial_puts(host);
        serial_puts("\n");
        return -1;
    }
    
    serial_puts("FTP: Connecting to ");
    serial_puts(ip_to_string(session->ip_addr));
    serial_puts(":");
    char port_str[8];
    itoa(session->port, port_str, 10);
    serial_puts(port_str);
    serial_puts("\n");
    
    // Create control socket
    tcp_socket_t* sock = tcp_socket_create();
    if (!sock) {
        serial_puts("FTP: Socket creation failed\n");
        return -1;
    }
    
    // Bind to ephemeral port
    if (tcp_socket_bind(sock, 0, 0) != 0) {
        serial_puts("FTP: Bind failed\n");
        tcp_socket_close(sock);
        return -1;
    }
    
    // Connect
    if (tcp_socket_connect(sock, session->ip_addr, session->port) != 0) {
        serial_puts("FTP: Connect failed\n");
        tcp_socket_close(sock);
        return -1;
    }
    
    // Wait for connection
    if (ftp_wait_connected(sock, 10000) != 0) {
        serial_puts("FTP: Connection timeout\n");
        tcp_socket_close(sock);
        return -1;
    }
    
    session->control_socket = sock;
    session->connected = 1;
    
    // Read welcome message
    int code = ftp_read_reply(session);
    if (code != FTP_REPLY_READY) {
        serial_puts("FTP: Server not ready\n");
        ftp_disconnect(session);
        return -1;
    }
    
    serial_puts("FTP: Connected\n");
    return 0;
}

// Login to FTP server
int ftp_login(ftp_session_t* session, const char* username, const char* password) {
    if (!session || !session->connected) {
        return -1;
    }
    
    const char* user = username ? username : "anonymous";
    const char* pass = password ? password : "user@aOS";
    
    strncpy(session->username, user, sizeof(session->username) - 1);
    
    // Send USER command
    if (ftp_send_command(session, "USER", user) != 0) {
        return -1;
    }
    
    int code = ftp_read_reply(session);
    if (code == FTP_REPLY_LOGIN_OK) {
        session->logged_in = 1;
        return 0;  // Logged in without password
    }
    
    if (code != FTP_REPLY_NEED_PASS) {
        return -1;
    }
    
    // Send PASS command
    if (ftp_send_command(session, "PASS", pass) != 0) {
        return -1;
    }
    
    code = ftp_read_reply(session);
    if (code == FTP_REPLY_LOGIN_OK) {
        session->logged_in = 1;
        serial_puts("FTP: Logged in as ");
        serial_puts(user);
        serial_puts("\n");
        return 0;
    }
    
    serial_puts("FTP: Login failed\n");
    return -1;
}

// Disconnect from FTP server
int ftp_disconnect(ftp_session_t* session) {
    if (!session) {
        return -1;
    }
    
    if (session->data_socket) {
        tcp_socket_close((tcp_socket_t*)session->data_socket);
        session->data_socket = NULL;
    }
    
    if (session->control_socket) {
        if (session->connected) {
            ftp_send_command(session, "QUIT", NULL);
            ftp_read_reply(session);
        }
        tcp_socket_close((tcp_socket_t*)session->control_socket);
        session->control_socket = NULL;
    }
    
    session->connected = 0;
    session->logged_in = 0;
    
    serial_puts("FTP: Disconnected\n");
    return 0;
}

// Get current directory
int ftp_pwd(ftp_session_t* session, char* path, int max_len) {
    if (!session || !session->logged_in || !path) {
        return -1;
    }
    
    if (ftp_send_command(session, "PWD", NULL) != 0) {
        return -1;
    }
    
    int code = ftp_read_reply(session);
    if (code != FTP_REPLY_PATHNAME) {
        return -1;
    }
    
    // Parse path from reply: 257 "/path" ...
    char* start = strchr(session->last_reply, '"');
    if (start) {
        start++;
        char* end = strchr(start, '"');
        if (end) {
            int len = end - start;
            if (len >= max_len) len = max_len - 1;
            strncpy(path, start, len);
            path[len] = '\0';
            strncpy(session->current_dir, path, sizeof(session->current_dir) - 1);
            return 0;
        }
    }
    
    return -1;
}

// Change directory
int ftp_cwd(ftp_session_t* session, const char* path) {
    if (!session || !session->logged_in || !path) {
        return -1;
    }
    
    if (ftp_send_command(session, "CWD", path) != 0) {
        return -1;
    }
    
    int code = ftp_read_reply(session);
    if (code == FTP_REPLY_FILE_OK) {
        // Update current directory
        ftp_pwd(session, session->current_dir, sizeof(session->current_dir));
        return 0;
    }
    
    return -1;
}

// Change to parent directory
int ftp_cdup(ftp_session_t* session) {
    if (!session || !session->logged_in) {
        return -1;
    }
    
    if (ftp_send_command(session, "CDUP", NULL) != 0) {
        return -1;
    }
    
    int code = ftp_read_reply(session);
    if (code == FTP_REPLY_FILE_OK) {
        ftp_pwd(session, session->current_dir, sizeof(session->current_dir));
        return 0;
    }
    
    return -1;
}

// Create directory
int ftp_mkdir(ftp_session_t* session, const char* path) {
    if (!session || !session->logged_in || !path) {
        return -1;
    }
    
    if (ftp_send_command(session, "MKD", path) != 0) {
        return -1;
    }
    
    int code = ftp_read_reply(session);
    return (code == FTP_REPLY_PATHNAME) ? 0 : -1;
}

// Remove directory
int ftp_rmdir(ftp_session_t* session, const char* path) {
    if (!session || !session->logged_in || !path) {
        return -1;
    }
    
    if (ftp_send_command(session, "RMD", path) != 0) {
        return -1;
    }
    
    int code = ftp_read_reply(session);
    return (code == FTP_REPLY_FILE_OK) ? 0 : -1;
}

// Delete file
int ftp_delete(ftp_session_t* session, const char* path) {
    if (!session || !session->logged_in || !path) {
        return -1;
    }
    
    if (ftp_send_command(session, "DELE", path) != 0) {
        return -1;
    }
    
    int code = ftp_read_reply(session);
    return (code == FTP_REPLY_FILE_OK) ? 0 : -1;
}

// Rename file
int ftp_rename(ftp_session_t* session, const char* from, const char* to) {
    if (!session || !session->logged_in || !from || !to) {
        return -1;
    }
    
    if (ftp_send_command(session, "RNFR", from) != 0) {
        return -1;
    }
    
    int code = ftp_read_reply(session);
    if (code != FTP_REPLY_PENDING) {
        return -1;
    }
    
    if (ftp_send_command(session, "RNTO", to) != 0) {
        return -1;
    }
    
    code = ftp_read_reply(session);
    return (code == FTP_REPLY_FILE_OK) ? 0 : -1;
}

// Get file size
int ftp_size(ftp_session_t* session, const char* path, uint32_t* size) {
    if (!session || !session->logged_in || !path || !size) {
        return -1;
    }
    
    if (ftp_send_command(session, "SIZE", path) != 0) {
        return -1;
    }
    
    int code = ftp_read_reply(session);
    if (code == 213) {  // 213 SIZE reply
        // Parse size from reply: "213 1234"
        char* ptr = session->last_reply + 4;
        *size = atoi(ptr);
        return 0;
    }
    
    return -1;
}

// Set transfer mode (ASCII or Binary)
int ftp_set_mode(ftp_session_t* session, char mode) {
    if (!session || !session->logged_in) {
        return -1;
    }
    
    char mode_str[2] = {mode, '\0'};
    if (ftp_send_command(session, "TYPE", mode_str) != 0) {
        return -1;
    }
    
    int code = ftp_read_reply(session);
    if (code == 200) {
        session->transfer_mode = mode;
        return 0;
    }
    
    return -1;
}

// Enter passive mode and get data connection address
static int ftp_enter_passive(ftp_session_t* session) {
    if (!session || !session->logged_in) {
        return -1;
    }
    
    if (ftp_send_command(session, "PASV", NULL) != 0) {
        return -1;
    }
    
    int code = ftp_read_reply(session);
    if (code != FTP_REPLY_PASSIVE_OK) {
        return -1;
    }
    
    // Parse passive mode reply: 227 Entering Passive Mode (h1,h2,h3,h4,p1,p2)
    char* start = strchr(session->last_reply, '(');
    if (!start) {
        return -1;
    }
    
    int h1, h2, h3, h4, p1, p2;
    start++;
    
    // Simple parsing
    h1 = atoi(start);
    while (*start && *start != ',') start++;
    if (*start == ',') start++;
    h2 = atoi(start);
    while (*start && *start != ',') start++;
    if (*start == ',') start++;
    h3 = atoi(start);
    while (*start && *start != ',') start++;
    if (*start == ',') start++;
    h4 = atoi(start);
    while (*start && *start != ',') start++;
    if (*start == ',') start++;
    p1 = atoi(start);
    while (*start && *start != ',') start++;
    if (*start == ',') start++;
    p2 = atoi(start);
    
    session->data_ip = (h1) | (h2 << 8) | (h3 << 16) | (h4 << 24);
    session->data_port = (p1 << 8) | p2;
    
    serial_puts("FTP: Passive mode ");
    serial_puts(ip_to_string(session->data_ip));
    serial_puts(":");
    char port_str[8];
    itoa(session->data_port, port_str, 10);
    serial_puts(port_str);
    serial_puts("\n");
    
    return 0;
}

// Open data connection
static tcp_socket_t* ftp_open_data_connection(ftp_session_t* session) {
    if (!session || !session->logged_in) {
        return NULL;
    }
    
    // Enter passive mode
    if (ftp_enter_passive(session) != 0) {
        return NULL;
    }
    
    // Create data socket
    tcp_socket_t* sock = tcp_socket_create();
    if (!sock) {
        return NULL;
    }
    
    if (tcp_socket_bind(sock, 0, 0) != 0) {
        tcp_socket_close(sock);
        return NULL;
    }
    
    if (tcp_socket_connect(sock, session->data_ip, session->data_port) != 0) {
        tcp_socket_close(sock);
        return NULL;
    }
    
    if (ftp_wait_connected(sock, 10000) != 0) {
        tcp_socket_close(sock);
        return NULL;
    }
    
    return sock;
}

// List directory contents
int ftp_list(ftp_session_t* session, const char* path, char* buffer, int max_len) {
    if (!session || !session->logged_in || !buffer) {
        return -1;
    }
    
    // Open data connection first
    tcp_socket_t* data_sock = ftp_open_data_connection(session);
    if (!data_sock) {
        return -1;
    }
    
    // Send LIST command
    if (ftp_send_command(session, "LIST", path) != 0) {
        tcp_socket_close(data_sock);
        return -1;
    }
    
    int code = ftp_read_reply(session);
    if (code != 150 && code != 125) {  // 150 or 125 = data connection starting
        tcp_socket_close(data_sock);
        return -1;
    }
    
    // Receive directory listing
    int total = 0;
    uint32_t start = get_tick_count();
    
    while ((get_tick_count() - start) < 30000 && total < max_len - 1) {
        int received = tcp_socket_recv(data_sock, (uint8_t*)buffer + total, max_len - total - 1);
        if (received > 0) {
            total += received;
            start = get_tick_count();
        } else if (data_sock->state != TCP_ESTABLISHED) {
            break;
        }
        for (volatile int i = 0; i < 1000; i++);
    }
    
    buffer[total] = '\0';
    tcp_socket_close(data_sock);
    
    // Read transfer complete reply
    code = ftp_read_reply(session);
    
    return (code == FTP_REPLY_TRANSFER_OK) ? total : -1;
}

// Download file from FTP server
int ftp_download(ftp_session_t* session, const char* remote_path, const char* local_path) {
    if (!session || !session->logged_in || !remote_path || !local_path) {
        return -1;
    }
    
    serial_puts("FTP: Downloading ");
    serial_puts(remote_path);
    serial_puts(" -> ");
    serial_puts(local_path);
    serial_puts("\n");
    
    // Set binary mode
    ftp_set_mode(session, FTP_MODE_BINARY);
    
    // Open data connection
    tcp_socket_t* data_sock = ftp_open_data_connection(session);
    if (!data_sock) {
        return -1;
    }
    
    // Send RETR command
    if (ftp_send_command(session, "RETR", remote_path) != 0) {
        tcp_socket_close(data_sock);
        return -1;
    }
    
    int code = ftp_read_reply(session);
    if (code != 150 && code != 125) {
        tcp_socket_close(data_sock);
        return -1;
    }
    
    // Open local file
    int fd = vfs_open(local_path, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) {
        serial_puts("FTP: Cannot create local file\n");
        tcp_socket_close(data_sock);
        return -1;
    }
    
    // Receive file data
    uint8_t buffer[FTP_BUFFER_SIZE];
    uint32_t total = 0;
    uint32_t start = get_tick_count();
    
    while ((get_tick_count() - start) < 60000) {  // 60 second timeout
        int received = tcp_socket_recv(data_sock, buffer, sizeof(buffer));
        if (received > 0) {
            vfs_write(fd, buffer, received);
            total += received;
            start = get_tick_count();
        } else if (data_sock->state != TCP_ESTABLISHED) {
            break;
        }
        for (volatile int i = 0; i < 1000; i++);
    }
    
    vfs_close(fd);
    tcp_socket_close(data_sock);
    
    // Read transfer complete reply
    code = ftp_read_reply(session);
    
    serial_puts("FTP: Downloaded ");
    char size_str[16];
    itoa(total, size_str, 10);
    serial_puts(size_str);
    serial_puts(" bytes\n");
    
    return (code == FTP_REPLY_TRANSFER_OK) ? 0 : -1;
}

// Upload file to FTP server
int ftp_upload(ftp_session_t* session, const char* local_path, const char* remote_path) {
    if (!session || !session->logged_in || !local_path || !remote_path) {
        return -1;
    }
    
    serial_puts("FTP: Uploading ");
    serial_puts(local_path);
    serial_puts(" -> ");
    serial_puts(remote_path);
    serial_puts("\n");
    
    // Set binary mode
    ftp_set_mode(session, FTP_MODE_BINARY);
    
    // Open local file
    int fd = vfs_open(local_path, O_RDONLY);
    if (fd < 0) {
        serial_puts("FTP: Cannot open local file\n");
        return -1;
    }
    
    // Open data connection
    tcp_socket_t* data_sock = ftp_open_data_connection(session);
    if (!data_sock) {
        vfs_close(fd);
        return -1;
    }
    
    // Send STOR command
    if (ftp_send_command(session, "STOR", remote_path) != 0) {
        tcp_socket_close(data_sock);
        vfs_close(fd);
        return -1;
    }
    
    int code = ftp_read_reply(session);
    if (code != 150 && code != 125) {
        tcp_socket_close(data_sock);
        vfs_close(fd);
        return -1;
    }
    
    // Send file data
    uint8_t buffer[FTP_BUFFER_SIZE];
    uint32_t total = 0;
    int bytes_read;
    
    while ((bytes_read = vfs_read(fd, buffer, sizeof(buffer))) > 0) {
        if (tcp_socket_send(data_sock, buffer, bytes_read) < 0) {
            break;
        }
        total += bytes_read;
    }
    
    vfs_close(fd);
    tcp_socket_close(data_sock);
    
    // Read transfer complete reply
    code = ftp_read_reply(session);
    
    serial_puts("FTP: Uploaded ");
    char size_str[16];
    itoa(total, size_str, 10);
    serial_puts(size_str);
    serial_puts(" bytes\n");
    
    return (code == FTP_REPLY_TRANSFER_OK) ? 0 : -1;
}

// Get file data into memory
int ftp_get(ftp_session_t* session, const char* path, uint8_t** data, uint32_t* len) {
    if (!session || !session->logged_in || !path || !data || !len) {
        return -1;
    }
    
    // Set binary mode
    ftp_set_mode(session, FTP_MODE_BINARY);
    
    // Get file size
    uint32_t file_size = 0;
    ftp_size(session, path, &file_size);
    
    // Open data connection
    tcp_socket_t* data_sock = ftp_open_data_connection(session);
    if (!data_sock) {
        return -1;
    }
    
    // Send RETR command
    if (ftp_send_command(session, "RETR", path) != 0) {
        tcp_socket_close(data_sock);
        return -1;
    }
    
    int code = ftp_read_reply(session);
    if (code != 150 && code != 125) {
        tcp_socket_close(data_sock);
        return -1;
    }
    
    // Allocate buffer
    uint32_t buffer_size = file_size > 0 ? file_size : 65536;
    uint8_t* buffer = (uint8_t*)kmalloc(buffer_size);
    if (!buffer) {
        tcp_socket_close(data_sock);
        return -1;
    }
    
    // Receive file data
    uint32_t total = 0;
    uint32_t start = get_tick_count();
    
    while ((get_tick_count() - start) < 60000) {
        int received = tcp_socket_recv(data_sock, buffer + total, buffer_size - total);
        if (received > 0) {
            total += received;
            start = get_tick_count();
            
            // Expand buffer if needed
            if (total >= buffer_size - 1024) {
                uint32_t new_size = buffer_size * 2;
                uint8_t* new_buffer = (uint8_t*)kmalloc(new_size);
                if (new_buffer) {
                    memcpy(new_buffer, buffer, total);
                    kfree(buffer);
                    buffer = new_buffer;
                    buffer_size = new_size;
                }
            }
        } else if (data_sock->state != TCP_ESTABLISHED) {
            break;
        }
        for (volatile int i = 0; i < 1000; i++);
    }
    
    tcp_socket_close(data_sock);
    
    // Read transfer complete reply
    code = ftp_read_reply(session);
    
    if (code == FTP_REPLY_TRANSFER_OK) {
        *data = buffer;
        *len = total;
        return 0;
    }
    
    kfree(buffer);
    return -1;
}

// Put file data from memory
int ftp_put(ftp_session_t* session, const char* path, const uint8_t* data, uint32_t len) {
    if (!session || !session->logged_in || !path || !data || len == 0) {
        return -1;
    }
    
    // Set binary mode
    ftp_set_mode(session, FTP_MODE_BINARY);
    
    // Open data connection
    tcp_socket_t* data_sock = ftp_open_data_connection(session);
    if (!data_sock) {
        return -1;
    }
    
    // Send STOR command
    if (ftp_send_command(session, "STOR", path) != 0) {
        tcp_socket_close(data_sock);
        return -1;
    }
    
    int code = ftp_read_reply(session);
    if (code != 150 && code != 125) {
        tcp_socket_close(data_sock);
        return -1;
    }
    
    // Send data in chunks
    uint32_t sent = 0;
    while (sent < len) {
        uint32_t chunk_size = len - sent;
        if (chunk_size > FTP_BUFFER_SIZE) {
            chunk_size = FTP_BUFFER_SIZE;
        }
        
        if (tcp_socket_send(data_sock, data + sent, chunk_size) < 0) {
            break;
        }
        sent += chunk_size;
    }
    
    tcp_socket_close(data_sock);
    
    // Read transfer complete reply
    code = ftp_read_reply(session);
    
    return (code == FTP_REPLY_TRANSFER_OK && sent == len) ? 0 : -1;
}

// Set passive mode
int ftp_set_passive(ftp_session_t* session, int passive) {
    if (!session) {
        return -1;
    }
    session->passive_mode = passive;
    return 0;
}
