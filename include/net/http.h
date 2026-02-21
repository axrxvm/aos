/*
 * === AOS HEADER BEGIN ===
 * include/net/http.h
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */


#ifndef HTTP_H
#define HTTP_H

#include <stdint.h>
#include <net/net.h>

// HTTP default port
#define HTTP_PORT 80
#define HTTPS_PORT 443

// HTTP methods
#define HTTP_METHOD_GET     "GET"
#define HTTP_METHOD_POST    "POST"
#define HTTP_METHOD_PUT     "PUT"
#define HTTP_METHOD_DELETE  "DELETE"
#define HTTP_METHOD_HEAD    "HEAD"

// HTTP status codes
#define HTTP_STATUS_OK              200
#define HTTP_STATUS_CREATED         201
#define HTTP_STATUS_NO_CONTENT      204
#define HTTP_STATUS_MOVED_PERM      301
#define HTTP_STATUS_MOVED_TEMP      302
#define HTTP_STATUS_NOT_MODIFIED    304
#define HTTP_STATUS_BAD_REQUEST     400
#define HTTP_STATUS_UNAUTHORIZED    401
#define HTTP_STATUS_FORBIDDEN       403
#define HTTP_STATUS_NOT_FOUND       404
#define HTTP_STATUS_INTERNAL_ERROR  500
#define HTTP_STATUS_NOT_IMPL        501
#define HTTP_STATUS_BAD_GATEWAY     502
#define HTTP_STATUS_UNAVAILABLE     503

// HTTP buffer sizes
#define HTTP_MAX_URL_LEN        512
#define HTTP_MAX_HEADER_LEN     1024
#define HTTP_MAX_BODY_LEN       16384
#define HTTP_MAX_HEADERS        32

// HTTP header structure
typedef struct {
    char name[64];
    char value[256];
} http_header_t;

// HTTP request structure
typedef struct {
    char method[8];
    char host[128];
    char path[256];
    uint16_t port;
    http_header_t headers[HTTP_MAX_HEADERS];
    int header_count;
    uint8_t* body;
    uint32_t body_len;
} http_request_t;

// HTTP response structure
typedef struct {
    int status_code;
    char status_text[64];
    http_header_t headers[HTTP_MAX_HEADERS];
    int header_count;
    uint8_t* body;
    uint32_t body_len;
    uint32_t content_length;
    char content_type[128];
} http_response_t;

// URL parsing result
typedef struct {
    char protocol[16];
    char host[128];
    uint16_t port;
    char path[256];
    char query[256];
} url_t;

// HTTP initialization
void http_init(void);

// URL parsing
int url_parse(const char* url_string, url_t* url);

// HTTP request functions
http_request_t* http_request_create(const char* method, const char* url);
void http_request_free(http_request_t* request);
int http_request_add_header(http_request_t* request, const char* name, const char* value);
int http_request_set_body(http_request_t* request, const uint8_t* body, uint32_t len);

// HTTP response functions
http_response_t* http_response_create(void);
void http_response_free(http_response_t* response);
const char* http_response_get_header(http_response_t* response, const char* name);

// HTTP operations
int http_get(const char* url, http_response_t* response);
int http_post(const char* url, const uint8_t* body, uint32_t body_len, http_response_t* response);
int http_send(http_request_t* request, http_response_t* response);

// HTTP download to file
int http_download(const char* url, const char* path);

// HTTP utilities
const char* http_status_text(int status_code);

#endif // HTTP_H
