/*
 * === AOS HEADER BEGIN ===
 * src/net/http.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */


/**
 * HTTP Client Implementation
 * Now with HTTPS/TLS support
 */

#include <net/http.h>
#include <net/tcp.h>
#include <net/tls.h>
#include <net/dns.h>
#include <net/net.h>
#include <string.h>
#include <stdlib.h>
#include <vmm.h>
#include <serial.h>
#include <arch/i386/pit.h>
#include <fs/vfs.h>

#define HTTP_VERSION "HTTP/1.1"
#define HTTP_USER_AGENT "aOS/0.8 HttpClient/1.0"
#define HTTP_CONNECT_TIMEOUT 10000
#define HTTP_RECV_TIMEOUT 30000


// Initialization


void http_init(void) {
    serial_puts("Initializing HTTP client...\n");
    serial_puts("HTTP client initialized.\n");
}


// URL Parsing


int url_parse(const char* url_string, url_t* url) {
    if (!url_string || !url) {
        return -1;
    }
    
    memset(url, 0, sizeof(url_t));
    url->port = HTTP_PORT;
    strcpy(url->path, "/");
    
    const char* ptr = url_string;
    
    // Parse protocol
    const char* protocol_end = strstr(ptr, "://");
    if (protocol_end) {
        int proto_len = protocol_end - ptr;
        if (proto_len >= (int)sizeof(url->protocol)) {
            return -1;
        }
        strncpy(url->protocol, ptr, proto_len);
        url->protocol[proto_len] = '\0';
        ptr = protocol_end + 3;
        
        if (strcmp(url->protocol, "https") == 0) {
            url->port = HTTPS_PORT;
        }
    } else {
        strcpy(url->protocol, "http");
    }
    
    // Parse host
    const char* host_end = ptr;
    while (*host_end && *host_end != '/' && *host_end != ':' && *host_end != '?') {
        host_end++;
    }
    
    int host_len = host_end - ptr;
    if (host_len == 0 || host_len >= (int)sizeof(url->host)) {
        return -1;
    }
    strncpy(url->host, ptr, host_len);
    url->host[host_len] = '\0';
    ptr = host_end;
    
    // Parse port
    if (*ptr == ':') {
        ptr++;
        url->port = atoi(ptr);
        while (*ptr && *ptr >= '0' && *ptr <= '9') {
            ptr++;
        }
    }
    
    // Parse path
    if (*ptr == '/') {
        const char* path_end = ptr;
        while (*path_end && *path_end != '?') {
            path_end++;
        }
        int path_len = path_end - ptr;
        if (path_len >= (int)sizeof(url->path)) {
            path_len = sizeof(url->path) - 1;
        }
        strncpy(url->path, ptr, path_len);
        url->path[path_len] = '\0';
        ptr = path_end;
    }
    
    // Parse query
    if (*ptr == '?') {
        ptr++;
        strncpy(url->query, ptr, sizeof(url->query) - 1);
    }
    
    return 0;
}


// Request Management


http_request_t* http_request_create(const char* method, const char* url_string) {
    if (!method || !url_string) {
        return NULL;
    }
    
    http_request_t* request = (http_request_t*)kmalloc(sizeof(http_request_t));
    if (!request) {
        return NULL;
    }
    
    memset(request, 0, sizeof(http_request_t));
    strncpy(request->method, method, sizeof(request->method) - 1);
    
    url_t url;
    if (url_parse(url_string, &url) != 0) {
        kfree(request);
        return NULL;
    }
    
    strcpy(request->host, url.host);
    strcpy(request->path, url.path);
    request->port = url.port;
    
    // Default headers
    http_request_add_header(request, "Host", url.host);
    http_request_add_header(request, "User-Agent", HTTP_USER_AGENT);
    http_request_add_header(request, "Accept", "*/*");
    http_request_add_header(request, "Connection", "close");
    
    return request;
}

void http_request_free(http_request_t* request) {
    if (request) {
        if (request->body) {
            kfree(request->body);
        }
        kfree(request);
    }
}

int http_request_add_header(http_request_t* request, const char* name, const char* value) {
    if (!request || !name || !value) {
        return -1;
    }
    
    if (request->header_count >= HTTP_MAX_HEADERS) {
        return -1;
    }
    
    http_header_t* header = &request->headers[request->header_count];
    strncpy(header->name, name, sizeof(header->name) - 1);
    strncpy(header->value, value, sizeof(header->value) - 1);
    request->header_count++;
    
    return 0;
}

int http_request_set_body(http_request_t* request, const uint8_t* body, uint32_t len) {
    if (!request) {
        return -1;
    }
    
    if (request->body) {
        kfree(request->body);
        request->body = NULL;
    }
    
    if (body && len > 0) {
        request->body = (uint8_t*)kmalloc(len);
        if (!request->body) {
            return -1;
        }
        memcpy(request->body, body, len);
        request->body_len = len;
        
        char len_str[16];
        itoa(len, len_str, 10);
        http_request_add_header(request, "Content-Length", len_str);
    }
    
    return 0;
}


// Response Management


http_response_t* http_response_create(void) {
    http_response_t* response = (http_response_t*)kmalloc(sizeof(http_response_t));
    if (response) {
        memset(response, 0, sizeof(http_response_t));
    }
    return response;
}

void http_response_free(http_response_t* response) {
    if (response) {
        if (response->body) {
            kfree(response->body);
        }
        kfree(response);
    }
}

// Case-insensitive compare
static int http_strcasecmp(const char* s1, const char* s2) {
    while (*s1 && *s2) {
        char c1 = *s1;
        char c2 = *s2;
        if (c1 >= 'A' && c1 <= 'Z') c1 += 32;
        if (c2 >= 'A' && c2 <= 'Z') c2 += 32;
        if (c1 != c2) return c1 - c2;
        s1++;
        s2++;
    }
    return *s1 - *s2;
}

const char* http_response_get_header(http_response_t* response, const char* name) {
    if (!response || !name) {
        return NULL;
    }
    
    for (int i = 0; i < response->header_count; i++) {
        if (http_strcasecmp(response->headers[i].name, name) == 0) {
            return response->headers[i].value;
        }
    }
    
    return NULL;
}


// Request/Response Building and Parsing


static int http_build_request(http_request_t* request, char* buffer, int max_len) {
    if (!request || !buffer) {
        return -1;
    }
    
    int offset = 0;
    
    // Request line
    offset += snprintf(buffer + offset, max_len - offset, 
                       "%s %s %s\r\n", request->method, request->path, HTTP_VERSION);
    
    // Headers
    for (int i = 0; i < request->header_count; i++) {
        offset += snprintf(buffer + offset, max_len - offset,
                           "%s: %s\r\n", 
                           request->headers[i].name, 
                           request->headers[i].value);
    }
    
    // End of headers
    offset += snprintf(buffer + offset, max_len - offset, "\r\n");
    
    return offset;
}

static int http_parse_status_line(const char* line, http_response_t* response) {
    if (strncmp(line, "HTTP/", 5) != 0) {
        return -1;
    }
    
    const char* ptr = line;
    while (*ptr && *ptr != ' ') ptr++;
    while (*ptr == ' ') ptr++;
    
    response->status_code = atoi(ptr);
    
    while (*ptr && *ptr != ' ') ptr++;
    while (*ptr == ' ') ptr++;
    
    int i = 0;
    while (*ptr && *ptr != '\r' && *ptr != '\n' && i < 63) {
        response->status_text[i++] = *ptr++;
    }
    response->status_text[i] = '\0';
    
    return 0;
}

static int http_parse_header(const char* line, http_response_t* response) {
    if (response->header_count >= HTTP_MAX_HEADERS) {
        return -1;
    }
    
    const char* colon = strchr(line, ':');
    if (!colon) {
        return -1;
    }
    
    http_header_t* header = &response->headers[response->header_count];
    
    int name_len = colon - line;
    if (name_len >= (int)sizeof(header->name)) {
        name_len = sizeof(header->name) - 1;
    }
    strncpy(header->name, line, name_len);
    header->name[name_len] = '\0';
    
    const char* value = colon + 1;
    while (*value == ' ' || *value == '\t') value++;
    
    int value_len = strlen(value);
    while (value_len > 0 && (value[value_len-1] == '\r' || value[value_len-1] == '\n' || 
                              value[value_len-1] == ' ' || value[value_len-1] == '\t')) {
        value_len--;
    }
    if (value_len >= (int)sizeof(header->value)) {
        value_len = sizeof(header->value) - 1;
    }
    strncpy(header->value, value, value_len);
    header->value[value_len] = '\0';
    
    response->header_count++;
    
    // Handle special headers
    if (http_strcasecmp(header->name, "Content-Length") == 0) {
        response->content_length = atoi(header->value);
    } else if (http_strcasecmp(header->name, "Content-Type") == 0) {
        strncpy(response->content_type, header->value, sizeof(response->content_type) - 1);
    }
    
    return 0;
}


// HTTP Send/Receive


int http_send(http_request_t* request, http_response_t* response) {
    if (!request || !response) {
        return -1;
    }
    
    // Determine if this is HTTPS
    int use_https = (request->port == HTTPS_PORT);
    
    // Resolve hostname
    uint32_t ip_addr;
    if (dns_resolve(request->host, &ip_addr) != 0) {
        serial_puts("HTTP: Failed to resolve ");
        serial_puts(request->host);
        serial_puts("\n");
        return -1;
    }
    
    serial_puts(use_https ? "HTTPS: " : "HTTP: ");
    serial_puts("Connecting to ");
    serial_puts(ip_to_string(ip_addr));
    serial_puts(":");
    char port_str[8];
    itoa(request->port, port_str, 10);
    serial_puts(port_str);
    serial_puts("\n");
    
    // Create and connect socket
    tcp_socket_t* sock = tcp_socket_create();
    if (!sock) {
        serial_puts("HTTP: Socket creation failed\n");
        return -1;
    }
    
    // Use blocking connect
    if (tcp_socket_connect_blocking(sock, ip_addr, request->port, HTTP_CONNECT_TIMEOUT) != 0) {
        serial_puts("HTTP: Connection failed\n");
        tcp_socket_close(sock);
        return -1;
    }
    
    // If HTTPS, establish TLS session
    tls_session_t* tls = NULL;
    if (use_https) {
        serial_puts("HTTPS: Establishing TLS connection...\n");
        tls = tls_session_create(sock, request->host);
        if (!tls) {
            serial_puts("HTTPS: TLS session creation failed\n");
            tcp_socket_close(sock);
            return -1;
        }
        
        if (tls_handshake(tls) != 0) {
            serial_puts("HTTPS: TLS handshake failed\n");
            tls_session_free(tls);
            tcp_socket_close(sock);
            return -1;
        }
        serial_puts("HTTPS: TLS connection established\n");
    } else {
        serial_puts("HTTP: Connected, sending request...\n");
    }
    
    // Build request
    char* req_buffer = (char*)kmalloc(HTTP_MAX_HEADER_LEN);
    if (!req_buffer) {
        if (tls) tls_session_free(tls);
        tcp_socket_close(sock);
        return -1;
    }
    
    int req_len = http_build_request(request, req_buffer, HTTP_MAX_HEADER_LEN);
    if (req_len < 0) {
        kfree(req_buffer);
        if (tls) tls_session_free(tls);
        tcp_socket_close(sock);
        return -1;
    }
    
    // Send request headers (via TLS if HTTPS)
    int send_result;
    if (use_https) {
        send_result = tls_send(tls, (uint8_t*)req_buffer, req_len);
    } else {
        send_result = tcp_socket_send(sock, (uint8_t*)req_buffer, req_len);
    }
    
    if (send_result < 0) {
        serial_puts("HTTP: Send failed\n");
        kfree(req_buffer);
        if (tls) tls_session_free(tls);
        tcp_socket_close(sock);
        return -1;
    }
    
    // Send body if present
    if (request->body && request->body_len > 0) {
        if (use_https) {
            send_result = tls_send(tls, request->body, request->body_len);
        } else {
            send_result = tcp_socket_send(sock, request->body, request->body_len);
        }
        
        if (send_result < 0) {
            serial_puts("HTTP: Body send failed\n");
            kfree(req_buffer);
            if (tls) tls_session_free(tls);
            tcp_socket_close(sock);
            return -1;
        }
    }
    
    kfree(req_buffer);
    
    serial_puts(use_https ? "HTTPS: " : "HTTP: ");
    serial_puts("Waiting for response...\n");
    
    // Receive response
    uint8_t* recv_buffer = (uint8_t*)kmalloc(HTTP_MAX_BODY_LEN);
    if (!recv_buffer) {
        if (tls) tls_session_free(tls);
        tcp_socket_close(sock);
        return -1;
    }
    
    int total_received = 0;
    int headers_done = 0;
    int header_end_pos = 0;
    
    // Receive loop with blocking recv
    uint32_t start_time = get_tick_count();
    while ((get_tick_count() - start_time) < HTTP_RECV_TIMEOUT) {
        int received;
        
        if (use_https) {
            // For HTTPS, receive one TLS record at a time
            // This handles the decryption and gives us plaintext data
            received = tls_recv(tls, recv_buffer + total_received, 
                               HTTP_MAX_BODY_LEN - total_received - 1);
            
            // For HTTPS, received == 0 means no more data (not an error)
            if (received == 0) {
                break;
            }
        } else {
            received = tcp_socket_recv_blocking(sock, 
                                                 recv_buffer + total_received, 
                                                 HTTP_MAX_BODY_LEN - total_received - 1,
                                                 1000);  // 1 second chunk timeout
        }
        
        if (received > 0) {
            total_received += received;
            recv_buffer[total_received] = '\0';
            
            // Look for end of headers
            if (!headers_done) {
                for (int i = 0; i < total_received - 3; i++) {
                    if (recv_buffer[i] == '\r' && recv_buffer[i+1] == '\n' &&
                        recv_buffer[i+2] == '\r' && recv_buffer[i+3] == '\n') {
                        headers_done = 1;
                        header_end_pos = i + 4;
                        
                        // Parse headers now to get Content-Length
                        char* line = (char*)recv_buffer;
                        int first_line = 1;
                        
                        while (line < (char*)(recv_buffer + header_end_pos)) {
                            char* line_end = line;
                            while (*line_end && *line_end != '\r' && *line_end != '\n') {
                                line_end++;
                            }
                            
                            if (line_end == line) break;
                            
                            char saved = *line_end;
                            *line_end = '\0';
                            
                            if (first_line) {
                                http_parse_status_line(line, response);
                                first_line = 0;
                            } else {
                                http_parse_header(line, response);
                            }
                            
                            *line_end = saved;
                            
                            line = line_end;
                            if (*line == '\r') line++;
                            if (*line == '\n') line++;
                        }
                        break;
                    }
                }
            }
            
            // Check if we have all data
            if (headers_done && response->content_length > 0) {
                if ((uint32_t)(total_received - header_end_pos) >= response->content_length) {
                    break;
                }
            }
            
            start_time = get_tick_count();  // Reset timeout
        } else if (received == 0) {
            // For HTTPS, 0 means end of data (already handled above)
            if (use_https) {
                break;
            }
            
            // Check connection state - CLOSE_WAIT means server sent FIN, no more data coming
            if (sock->state == TCP_CLOSE_WAIT || sock->state == TCP_CLOSED ||
                sock->state == TCP_FIN_WAIT_1 || sock->state == TCP_FIN_WAIT_2 ||
                sock->state == TCP_TIME_WAIT) {
                break;  // Connection closing, no more data
            }
            // Also break if not in a data-receiving state
            if (sock->state != TCP_ESTABLISHED) {
                break;
            }
        } else {
            // Error - but for HTTPS we already broke above
            if (!use_https) {
                break;
            }
        }
    }
    
    if (total_received == 0) {
        serial_puts("HTTP: No response received\n");
        kfree(recv_buffer);
        if (tls) tls_session_free(tls);
        tcp_socket_close(sock);
        return -1;
    }
    
    // Copy body
    if (headers_done) {
        int body_len = total_received - header_end_pos;
        if (body_len > 0) {
            response->body = (uint8_t*)kmalloc(body_len + 1);
            if (response->body) {
                memcpy(response->body, recv_buffer + header_end_pos, body_len);
                response->body[body_len] = '\0';
                response->body_len = body_len;
            }
        }
    }
    
    kfree(recv_buffer);
    
    // Clean up TLS session if used
    if (tls) {
        tls_session_close(tls);
        tls_session_free(tls);
    }
    
    tcp_socket_close(sock);
    
    serial_puts("HTTP: Response received, status ");
    char status_str[8];
    itoa(response->status_code, status_str, 10);
    serial_puts(status_str);
    serial_puts("\n");
    
    return 0;
}


// Convenience Functions


int http_get(const char* url, http_response_t* response) {
    http_request_t* request = http_request_create(HTTP_METHOD_GET, url);
    if (!request) {
        return -1;
    }
    
    int result = http_send(request, response);
    http_request_free(request);
    
    return result;
}

int http_post(const char* url, const uint8_t* body, uint32_t body_len, http_response_t* response) {
    http_request_t* request = http_request_create(HTTP_METHOD_POST, url);
    if (!request) {
        return -1;
    }
    
    http_request_add_header(request, "Content-Type", "application/x-www-form-urlencoded");
    http_request_set_body(request, body, body_len);
    
    int result = http_send(request, response);
    http_request_free(request);
    
    return result;
}

int http_download(const char* url, const char* path) {
    http_response_t* response = http_response_create();
    if (!response) {
        return -1;
    }
    
    int result = http_get(url, response);
    if (result != 0) {
        http_response_free(response);
        return -1;
    }
    
    if (response->status_code != HTTP_STATUS_OK) {
        http_response_free(response);
        return -1;
    }
    
    if (response->body && response->body_len > 0) {
        int fd = vfs_open(path, O_WRONLY | O_CREAT | O_TRUNC);
        if (fd >= 0) {
            vfs_write(fd, response->body, response->body_len);
            vfs_close(fd);
            result = 0;
        } else {
            result = -1;
        }
    }
    
    http_response_free(response);
    return result;
}

const char* http_status_text(int status_code) {
    switch (status_code) {
        case 200: return "OK";
        case 201: return "Created";
        case 204: return "No Content";
        case 301: return "Moved Permanently";
        case 302: return "Found";
        case 304: return "Not Modified";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 500: return "Internal Server Error";
        case 501: return "Not Implemented";
        case 502: return "Bad Gateway";
        case 503: return "Service Unavailable";
        default: return "Unknown";
    }
}
