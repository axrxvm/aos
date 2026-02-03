/*
 * === AOS HEADER BEGIN ===
 * ./include/net/tls.h
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.8.5
 * === AOS HEADER END ===
 */


#ifndef TLS_H
#define TLS_H

#include <stdint.h>
#include <net/tcp.h>

// TLS version constants
#define TLS_VERSION_1_0     0x0301
#define TLS_VERSION_1_1     0x0302
#define TLS_VERSION_1_2     0x0303
#define TLS_VERSION_1_3     0x0304

// TLS content types
#define TLS_CONTENT_CHANGE_CIPHER_SPEC  20
#define TLS_CONTENT_ALERT               21
#define TLS_CONTENT_HANDSHAKE           22
#define TLS_CONTENT_APPLICATION_DATA    23

// TLS handshake message types
#define TLS_HANDSHAKE_HELLO_REQUEST         0
#define TLS_HANDSHAKE_CLIENT_HELLO          1
#define TLS_HANDSHAKE_SERVER_HELLO          2
#define TLS_HANDSHAKE_CERTIFICATE           11
#define TLS_HANDSHAKE_SERVER_KEY_EXCHANGE   12
#define TLS_HANDSHAKE_CERTIFICATE_REQUEST   13
#define TLS_HANDSHAKE_SERVER_HELLO_DONE     14
#define TLS_HANDSHAKE_CERTIFICATE_VERIFY    15
#define TLS_HANDSHAKE_CLIENT_KEY_EXCHANGE   16
#define TLS_HANDSHAKE_FINISHED              20

// TLS alert levels
#define TLS_ALERT_WARNING   1
#define TLS_ALERT_FATAL     2

// TLS alert descriptions
#define TLS_ALERT_CLOSE_NOTIFY              0
#define TLS_ALERT_UNEXPECTED_MESSAGE        10
#define TLS_ALERT_BAD_RECORD_MAC            20
#define TLS_ALERT_DECRYPTION_FAILED         21
#define TLS_ALERT_RECORD_OVERFLOW           22
#define TLS_ALERT_HANDSHAKE_FAILURE         40
#define TLS_ALERT_BAD_CERTIFICATE           42
#define TLS_ALERT_UNSUPPORTED_CERTIFICATE   43
#define TLS_ALERT_CERTIFICATE_REVOKED       44
#define TLS_ALERT_CERTIFICATE_EXPIRED       45
#define TLS_ALERT_CERTIFICATE_UNKNOWN       46
#define TLS_ALERT_ILLEGAL_PARAMETER         47
#define TLS_ALERT_UNKNOWN_CA                48
#define TLS_ALERT_ACCESS_DENIED             49
#define TLS_ALERT_DECODE_ERROR              50
#define TLS_ALERT_DECRYPT_ERROR             51
#define TLS_ALERT_PROTOCOL_VERSION          70
#define TLS_ALERT_INSUFFICIENT_SECURITY     71
#define TLS_ALERT_INTERNAL_ERROR            80
#define TLS_ALERT_USER_CANCELED             90
#define TLS_ALERT_NO_RENEGOTIATION          100

// Cipher suites - we support only a minimal set for bare-metal compatibility
#define TLS_NULL_WITH_NULL_NULL                 0x0000
#define TLS_RSA_WITH_NULL_SHA                   0x0002
#define TLS_RSA_WITH_NULL_SHA256                0x003B
#define TLS_RSA_WITH_AES_128_CBC_SHA            0x002F
#define TLS_RSA_WITH_AES_256_CBC_SHA            0x0035
#define TLS_RSA_WITH_AES_128_CBC_SHA256         0x003C

// TLS states
typedef enum {
    TLS_STATE_INIT = 0,
    TLS_STATE_CLIENT_HELLO_SENT,
    TLS_STATE_SERVER_HELLO_RECEIVED,
    TLS_STATE_CERTIFICATE_RECEIVED,
    TLS_STATE_KEY_EXCHANGE_RECEIVED,
    TLS_STATE_HELLO_DONE_RECEIVED,
    TLS_STATE_CHANGE_CIPHER_SPEC_SENT,
    TLS_STATE_FINISHED_SENT,
    TLS_STATE_ESTABLISHED,
    TLS_STATE_CLOSED,
    TLS_STATE_ERROR
} tls_state_t;

// TLS record header (5 bytes)
typedef struct {
    uint8_t content_type;
    uint16_t version;
    uint16_t length;
} __attribute__((packed)) tls_record_header_t;

// TLS session structure
typedef struct {
    tcp_socket_t* socket;           // Underlying TCP socket
    tls_state_t state;              // Current TLS state
    uint16_t version;               // Negotiated TLS version
    uint16_t cipher_suite;          // Negotiated cipher suite
    uint8_t session_id[32];         // Session ID
    uint8_t session_id_len;         // Session ID length
    
    // Random values
    uint8_t client_random[32];      // Client random
    uint8_t server_random[32];      // Server random
    
    // Master secret and keys
    uint8_t pre_master_secret[48];
    uint8_t master_secret[48];
    uint8_t client_write_mac_key[32];
    uint8_t server_write_mac_key[32];
    uint8_t client_write_key[32];
    uint8_t server_write_key[32];
    uint8_t client_write_iv[16];
    uint8_t server_write_iv[16];
    
    // Encryption context
    void* enc_ctx;  // AES context for encryption
    void* dec_ctx;  // AES context for decryption
    uint8_t encryption_enabled;
    
    // Sequence numbers for replay protection
    uint64_t client_seq_num;
    uint64_t server_seq_num;
    
    // Server certificate (simplified - just store hash for now)
    uint8_t server_cert_hash[32];
    uint8_t cert_verified;
    
    // Buffers
    uint8_t* recv_buffer;           // Receive buffer for TLS records
    uint32_t recv_buffer_size;
    uint32_t recv_buffer_used;
    
    uint8_t* handshake_messages;    // Buffer for handshake message hashing
    uint32_t handshake_messages_len;
    
    char* hostname;                 // Server hostname for SNI
    uint8_t verify_certificate;     // Whether to verify certificates
} tls_session_t;

// TLS API functions

/**
 * Initialize TLS subsystem
 */
void tls_init(void);

/**
 * Create a new TLS session
 * @param socket Connected TCP socket
 * @param hostname Server hostname (for SNI)
 * @return TLS session or NULL on failure
 */
tls_session_t* tls_session_create(tcp_socket_t* socket, const char* hostname);

/**
 * Perform TLS handshake
 * @param session TLS session
 * @return 0 on success, -1 on failure
 */
int tls_handshake(tls_session_t* session);

/**
 * Send data over TLS connection
 * @param session TLS session
 * @param data Data to send
 * @param len Length of data
 * @return Number of bytes sent or -1 on error
 */
int tls_send(tls_session_t* session, const uint8_t* data, uint32_t len);

/**
 * Receive data from TLS connection
 * @param session TLS session
 * @param buffer Buffer to receive data into
 * @param max_len Maximum bytes to receive
 * @return Number of bytes received or -1 on error
 */
int tls_recv(tls_session_t* session, uint8_t* buffer, uint32_t max_len);

/**
 * Close TLS session
 * @param session TLS session to close
 */
void tls_session_close(tls_session_t* session);

/**
 * Free TLS session
 * @param session TLS session to free
 */
void tls_session_free(tls_session_t* session);

/**
 * Set certificate verification mode
 * @param session TLS session
 * @param verify 1 to enable verification, 0 to disable (insecure!)
 */
void tls_set_verify(tls_session_t* session, uint8_t verify);

// Internal helper functions (exposed for testing)

/**
 * Generate random bytes
 * @param buffer Buffer to fill with random data
 * @param len Number of random bytes to generate
 */
void tls_random_bytes(uint8_t* buffer, uint32_t len);

/**
 * Send a TLS record
 * @param session TLS session
 * @param content_type Content type
 * @param data Data to send
 * @param len Length of data
 * @return 0 on success, -1 on failure
 */
int tls_send_record(tls_session_t* session, uint8_t content_type, 
                    const uint8_t* data, uint32_t len);

/**
 * Receive a TLS record
 * @param session TLS session
 * @param content_type Expected content type (or 0 for any)
 * @param buffer Buffer to receive data
 * @param max_len Maximum buffer size
 * @return Bytes received or -1 on error
 */
int tls_recv_record(tls_session_t* session, uint8_t* content_type,
                    uint8_t* buffer, uint32_t max_len);

#endif // TLS_H
