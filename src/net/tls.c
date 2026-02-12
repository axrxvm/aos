/*
 * === AOS HEADER BEGIN ===
 * src/net/tls.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */


/**
 * TLS/SSL Client Implementation
 * TLS 1.2 client with AES-128-CBC encryption
 * 
 * Supports:
 * - TLS 1.2 handshake
 * - RSA key exchange
 * - AES-128-CBC with HMAC-SHA1/SHA256
 */

#include <net/tls.h>
#include <net/tcp.h>
#include <crypto/sha256.h>
#include <crypto/aes.h>
#include <crypto/hmac.h>
#include <crypto/rsa.h>
#include <crypto/x509.h>
#include <string.h>
#include <stdlib.h>
#include <vmm.h>
#include <serial.h>
#include <arch/i386/pit.h>

#define TLS_RECV_BUFFER_SIZE    16384
#define TLS_MAX_RECORD_SIZE     16384
#define TLS_HANDSHAKE_TIMEOUT   15000

// Simple random number generation (using PIT for entropy)
static uint32_t random_seed = 0x12345678;

static void init_random(void) {
    random_seed ^= get_tick_count();
}

static uint32_t random_next(void) {
    random_seed = (random_seed * 1103515245 + 12345) & 0x7fffffff;
    return random_seed;
}

void tls_random_bytes(uint8_t* buffer, uint32_t len) {
    for (uint32_t i = 0; i < len; i++) {
        if (i % 4 == 0) {
            random_next();
        }
        buffer[i] = (random_seed >> ((i % 4) * 8)) & 0xFF;
    }
}


// TLS PRF (Pseudo-Random Function) for key derivation


static void tls_prf_p_hash(const uint8_t* secret, size_t secret_len,
                           const uint8_t* seed, size_t seed_len,
                           uint8_t* output, size_t output_len,
                           int use_sha256) {
    uint8_t a[32];  // A(i) - max size for SHA256
    uint8_t temp[32 + 64];  // HMAC output + seed
    size_t hash_len = use_sha256 ? 32 : 20;
    size_t offset = 0;
    
    // A(1) = HMAC(secret, seed)
    if (use_sha256) {
        hmac_sha256(secret, secret_len, seed, seed_len, a);
    } else {
        hmac_sha1(secret, secret_len, seed, seed_len, a);
    }
    
    while (offset < output_len) {
        // HMAC(secret, A(i) + seed)
        memcpy(temp, a, hash_len);
        memcpy(temp + hash_len, seed, seed_len);
        
        uint8_t result[32];
        if (use_sha256) {
            hmac_sha256(secret, secret_len, temp, hash_len + seed_len, result);
        } else {
            hmac_sha1(secret, secret_len, temp, hash_len + seed_len, result);
        }
        
        size_t to_copy = (output_len - offset < hash_len) ? (output_len - offset) : hash_len;
        memcpy(output + offset, result, to_copy);
        offset += to_copy;
        
        // A(i+1) = HMAC(secret, A(i))
        if (offset < output_len) {
            if (use_sha256) {
                hmac_sha256(secret, secret_len, a, hash_len, a);
            } else {
                hmac_sha1(secret, secret_len, a, hash_len, a);
            }
        }
    }
}

static void tls_prf(const uint8_t* secret, size_t secret_len,
                    const char* label,
                    const uint8_t* seed, size_t seed_len,
                    uint8_t* output, size_t output_len) {
    // TLS 1.2 PRF uses only SHA-256 based P_hash
    uint8_t full_seed[128];
    size_t label_len = strlen(label);
    
    memcpy(full_seed, label, label_len);
    memcpy(full_seed + label_len, seed, seed_len);
    
    tls_prf_p_hash(secret, secret_len, full_seed, label_len + seed_len,
                   output, output_len, 1);  // Use SHA-256
}

static void derive_keys(tls_session_t* session) {
    // Derive master secret from pre-master secret
    uint8_t seed[64];
    memcpy(seed, session->client_random, 32);
    memcpy(seed + 32, session->server_random, 32);
    
    tls_prf(session->pre_master_secret, 48, "master secret",
            seed, 64, session->master_secret, 48);
    
    // Derive key material from master secret
    // Seed is server_random + client_random for key derivation
    memcpy(seed, session->server_random, 32);
    memcpy(seed + 32, session->client_random, 32);
    
    uint8_t key_block[128];
    size_t key_material_len = 0;
    
    // Determine how much key material we need based on cipher suite
    if (session->cipher_suite == TLS_RSA_WITH_AES_128_CBC_SHA) {
        // MAC key (20 bytes) + enc key (16 bytes) + IV (16 bytes) per side
        key_material_len = 2 * (20 + 16 + 16);  // 104 bytes
    } else if (session->cipher_suite == TLS_RSA_WITH_AES_128_CBC_SHA256) {
        // MAC key (32 bytes) + enc key (16 bytes) + IV (16 bytes) per side
        key_material_len = 2 * (32 + 16 + 16);  // 128 bytes
    } else {
        // NULL cipher or unknown - no keys needed
        return;
    }
    
    tls_prf(session->master_secret, 48, "key expansion",
            seed, 64, key_block, key_material_len);
    
    // Split key material
    size_t mac_key_len = (session->cipher_suite == TLS_RSA_WITH_AES_128_CBC_SHA256) ? 32 : 20;
    size_t enc_key_len = 16;  // AES-128
    size_t iv_len = 16;  // AES block size
    
    size_t offset = 0;
    memcpy(session->client_write_mac_key, key_block + offset, mac_key_len);
    offset += mac_key_len;
    memcpy(session->server_write_mac_key, key_block + offset, mac_key_len);
    offset += mac_key_len;
    memcpy(session->client_write_key, key_block + offset, enc_key_len);
    offset += enc_key_len;
    memcpy(session->server_write_key, key_block + offset, enc_key_len);
    offset += enc_key_len;
    memcpy(session->client_write_iv, key_block + offset, iv_len);
    offset += iv_len;
    memcpy(session->server_write_iv, key_block + offset, iv_len);
    
    serial_puts("TLS: Keys derived successfully\n");
}


// Initialization


void tls_init(void) {
    serial_puts("Initializing TLS...\n");
    init_random();
    serial_puts("TLS initialized (simplified TLS 1.2 client)\n");
}


// Helper Functions

/*
 * Send encrypted handshake message
 */
static int tls_send_encrypted_handshake(tls_session_t* session, const uint8_t* data, size_t len) {
    if (!session || !data || len == 0) {
        return -1;
    }
    
    // Handshake messages need MAC and encryption when encryption_enabled=1
    if (!session->encryption_enabled) {
        return tls_send_record(session, TLS_CONTENT_HANDSHAKE, data, len);
    }
    
    // With encryption: add padding, compute MAC, encrypt (same as tls_send)
    size_t mac_len = (session->cipher_suite == TLS_RSA_WITH_AES_128_CBC_SHA256) ? 32 : 20;
    size_t block_size = 16;  // AES block size
    
    // Calculate padding needed
    size_t total_len = len + mac_len + 1;  // data + MAC + padding_length byte
    size_t padding_len = (block_size - (total_len % block_size)) % block_size;
    total_len += padding_len;
    
    uint8_t* plaintext = (uint8_t*)kmalloc(total_len);
    if (!plaintext) {
        return -1;
    }
    
    // Build plaintext: data + MAC + padding + padding_length
    memcpy(plaintext, data, len);
    
    // Compute MAC over sequence number + TLS header + data
    uint8_t mac_input[13 + TLS_MAX_RECORD_SIZE];
    size_t mac_input_len = 0;
    
    // Sequence number (8 bytes, big endian)
    for (int i = 7; i >= 0; i--) {
        mac_input[mac_input_len++] = (session->client_seq_num >> (i * 8)) & 0xFF;
    }
    
    // TLS header: type (1) + version (2) + length (2)
    mac_input[mac_input_len++] = TLS_CONTENT_HANDSHAKE;
    mac_input[mac_input_len++] = (session->version >> 8) & 0xFF;
    mac_input[mac_input_len++] = session->version & 0xFF;
    mac_input[mac_input_len++] = (len >> 8) & 0xFF;
    mac_input[mac_input_len++] = len & 0xFF;
    
    // Data
    memcpy(mac_input + mac_input_len, data, len);
    mac_input_len += len;
    
    // Compute MAC
    if (session->cipher_suite == TLS_RSA_WITH_AES_128_CBC_SHA256) {
        hmac_sha256(session->client_write_mac_key, 32, mac_input, mac_input_len,
                    plaintext + len);
    } else {
        hmac_sha1(session->client_write_mac_key, 20, mac_input, mac_input_len,
                  plaintext + len);
    }
    
    // Add padding
    for (size_t i = 0; i <= padding_len; i++) {
        plaintext[len + mac_len + i] = padding_len;
    }
    
    // Encrypt
    uint8_t* ciphertext = (uint8_t*)kmalloc(total_len);
    if (!ciphertext) {
        kfree(plaintext);
        return -1;
    }
    
    aes128_cbc_encrypt((aes128_ctx_t*)session->enc_ctx, plaintext, ciphertext, total_len);
    
    // Send encrypted record
    int result = tls_send_record(session, TLS_CONTENT_HANDSHAKE, ciphertext, total_len);
    
    kfree(plaintext);
    kfree(ciphertext);
    
    if (result >= 0) {
        session->client_seq_num++;
    }
    
    return result;
}


// Session Management


tls_session_t* tls_session_create(tcp_socket_t* socket, const char* hostname) {
    if (!socket || socket->state != TCP_ESTABLISHED) {
        serial_puts("TLS: Invalid socket or not connected\n");
        return NULL;
    }
    
    tls_session_t* session = (tls_session_t*)kmalloc(sizeof(tls_session_t));
    if (!session) {
        serial_puts("TLS: Failed to allocate session\n");
        return NULL;
    }
    
    memset(session, 0, sizeof(tls_session_t));
    session->socket = socket;
    session->state = TLS_STATE_INIT;
    session->version = TLS_VERSION_1_2;
    session->verify_certificate = 0;  // Disabled by default for bare-metal
    
    // Allocate receive buffer
    session->recv_buffer = (uint8_t*)kmalloc(TLS_RECV_BUFFER_SIZE);
    if (!session->recv_buffer) {
        kfree(session);
        return NULL;
    }
    session->recv_buffer_size = TLS_RECV_BUFFER_SIZE;
    
    // Allocate handshake message buffer for verify_data calculation
    session->handshake_messages = (uint8_t*)kmalloc(8192);
    if (!session->handshake_messages) {
        kfree(session->recv_buffer);
        kfree(session);
        return NULL;
    }
    
    // Store hostname for SNI
    if (hostname) {
        session->hostname = (char*)kmalloc(strlen(hostname) + 1);
        if (session->hostname) {
            strcpy(session->hostname, hostname);
        }
    }
    
    // Generate client random
    tls_random_bytes(session->client_random, 32);
    
    // Set first 4 bytes to timestamp (TLS convention)
    uint32_t timestamp = get_tick_count();
    session->client_random[0] = (timestamp >> 24) & 0xFF;
    session->client_random[1] = (timestamp >> 16) & 0xFF;
    session->client_random[2] = (timestamp >> 8) & 0xFF;
    session->client_random[3] = timestamp & 0xFF;
    
    return session;
}

void tls_session_free(tls_session_t* session) {
    if (!session) return;
    
    if (session->enc_ctx) {
        kfree(session->enc_ctx);
    }
    if (session->dec_ctx) {
        kfree(session->dec_ctx);
    }
    if (session->recv_buffer) {
        kfree(session->recv_buffer);
    }
    if (session->handshake_messages) {
        kfree(session->handshake_messages);
    }
    if (session->hostname) {
        kfree(session->hostname);
    }
    kfree(session);
}

void tls_set_verify(tls_session_t* session, uint8_t verify) {
    if (session) {
        session->verify_certificate = verify;
    }
}


// Low-level TLS record I/O


int tls_send_record(tls_session_t* session, uint8_t content_type,
                    const uint8_t* data, uint32_t len) {
    if (!session || !data || len > TLS_MAX_RECORD_SIZE) {
        return -1;
    }
    
    // Build TLS record header
    tls_record_header_t header;
    header.content_type = content_type;
    header.version = __builtin_bswap16(session->version);
    header.length = __builtin_bswap16(len);
    
    // Send header
    if (tcp_socket_send(session->socket, (uint8_t*)&header, 
                       sizeof(tls_record_header_t)) < 0) {
        return -1;
    }
    
    // Send data
    if (tcp_socket_send(session->socket, data, len) < 0) {
        return -1;
    }
    
    return len;
}

int tls_recv_record(tls_session_t* session, uint8_t* content_type,
                    uint8_t* buffer, uint32_t max_len) {
    if (!session || !buffer) {
        return -1;
    }
    
    // Receive record header (5 bytes) with longer timeout for application data
    tls_record_header_t header;
    uint32_t timeout = (session->state == TLS_STATE_ESTABLISHED) ? 30000 : TLS_HANDSHAKE_TIMEOUT;
    
    int received = tcp_socket_recv_blocking(session->socket, (uint8_t*)&header,
                                           sizeof(tls_record_header_t), timeout);
    if (received != sizeof(tls_record_header_t)) {
        if (session->state == TLS_STATE_ESTABLISHED) {
            // This might be normal - server closed connection
            return 0;
        }
        serial_puts("TLS: Failed to receive record header\n");
        return -1;
    }
    
    // Parse header
    if (content_type) {
        *content_type = header.content_type;
    }
    
    uint16_t length = __builtin_bswap16(header.length);
    
    if (length > max_len) {
        serial_puts("TLS: Record too large: ");
        char msg[16];
        itoa(length, msg, 10);
        serial_puts(msg);
        serial_puts(" bytes (max buffer: ");
        itoa(max_len, msg, 10);
        serial_puts(msg);
        serial_puts(")\n");
        return -1;
    }
    
    if (length > TLS_MAX_RECORD_SIZE) {
        serial_puts("TLS: Record exceeds TLS limit\n");
        return -1;
    }
    
    // Receive record data - must get all of it
    size_t total_received = 0;
    while (total_received < length) {
        received = tcp_socket_recv_blocking(session->socket, buffer + total_received, 
                                           length - total_received, timeout);
        if (received <= 0) {
            serial_puts("TLS: Failed to receive record data\n");
            return -1;
        }
        total_received += received;
    }
    
    return total_received;
}


// Handshake message builders


static int build_client_hello(tls_session_t* session, uint8_t* buffer, uint32_t max_len) {
    (void)max_len;  // Reserved for future bounds checking
    uint8_t* ptr = buffer;
    
    // Handshake header
    *ptr++ = TLS_HANDSHAKE_CLIENT_HELLO;
    
    // Length placeholder (fill later)
    uint8_t* length_ptr = ptr;
    ptr += 3;
    
    // Client version
    *ptr++ = (session->version >> 8) & 0xFF;
    *ptr++ = session->version & 0xFF;
    
    // Client random (32 bytes)
    memcpy(ptr, session->client_random, 32);
    ptr += 32;
    
    // Session ID length (0 for new session)
    *ptr++ = 0;
    
    // Cipher suites length
    *ptr++ = 0x00;
    *ptr++ = 0x06;  // 3 cipher suites * 2 bytes
    
    // Cipher suites (support minimal set)
    *ptr++ = (TLS_RSA_WITH_AES_128_CBC_SHA256 >> 8) & 0xFF;
    *ptr++ = TLS_RSA_WITH_AES_128_CBC_SHA256 & 0xFF;
    *ptr++ = (TLS_RSA_WITH_AES_128_CBC_SHA >> 8) & 0xFF;
    *ptr++ = TLS_RSA_WITH_AES_128_CBC_SHA & 0xFF;
    *ptr++ = (TLS_RSA_WITH_NULL_SHA256 >> 8) & 0xFF;
    *ptr++ = TLS_RSA_WITH_NULL_SHA256 & 0xFF;
    
    // Compression methods (only null)
    *ptr++ = 0x01;
    *ptr++ = 0x00;
    
    // Extensions length
    uint8_t* ext_length_ptr = ptr;
    ptr += 2;
    
    // SNI extension (Server Name Indication)
    if (session->hostname) {
        uint16_t hostname_len = strlen(session->hostname);
        
        // Extension type: server_name (0x0000)
        *ptr++ = 0x00;
        *ptr++ = 0x00;
        
        // Extension length
        uint16_t ext_len = 5 + hostname_len;
        *ptr++ = (ext_len >> 8) & 0xFF;
        *ptr++ = ext_len & 0xFF;
        
        // Server name list length
        uint16_t list_len = 3 + hostname_len;
        *ptr++ = (list_len >> 8) & 0xFF;
        *ptr++ = list_len & 0xFF;
        
        // Server name type (0 = hostname)
        *ptr++ = 0x00;
        
        // Hostname length
        *ptr++ = (hostname_len >> 8) & 0xFF;
        *ptr++ = hostname_len & 0xFF;
        
        // Hostname
        memcpy(ptr, session->hostname, hostname_len);
        ptr += hostname_len;
    }
    
    // Fill in extensions length
    uint16_t ext_len = ptr - ext_length_ptr - 2;
    ext_length_ptr[0] = (ext_len >> 8) & 0xFF;
    ext_length_ptr[1] = ext_len & 0xFF;
    
    // Fill in message length
    uint32_t msg_len = ptr - length_ptr - 3;
    length_ptr[0] = (msg_len >> 16) & 0xFF;
    length_ptr[1] = (msg_len >> 8) & 0xFF;
    length_ptr[2] = msg_len & 0xFF;
    
    return ptr - buffer;
}


// Handshake processing


static int parse_server_hello(tls_session_t* session, const uint8_t* data, uint32_t len) {
    if (len < 38) {  // Minimum server hello size
        return -1;
    }
    
    const uint8_t* ptr = data;
    
    // Skip handshake header (already parsed)
    if (*ptr++ != TLS_HANDSHAKE_SERVER_HELLO) {
        return -1;
    }
    
    // Message length (skip for now)
    ptr += 3;
    
    // Server version
    uint16_t server_version = (ptr[0] << 8) | ptr[1];
    ptr += 2;
    
    if (server_version > session->version) {
        serial_puts("TLS: Server version too high\n");
        return -1;
    }
    session->version = server_version;
    
    // Server random
    memcpy(session->server_random, ptr, 32);
    ptr += 32;
    
    // Session ID
    uint8_t session_id_len = *ptr++;
    if (session_id_len > 32) {
        return -1;
    }
    session->session_id_len = session_id_len;
    if (session_id_len > 0) {
        memcpy(session->session_id, ptr, session_id_len);
        ptr += session_id_len;
    }
    
    // Cipher suite
    session->cipher_suite = (ptr[0] << 8) | ptr[1];
    ptr += 2;
    
    // Compression method
    uint8_t compression = *ptr++;
    if (compression != 0) {
        serial_puts("TLS: Server selected compression (not supported)\n");
        return -1;
    }
    
    char msg[128];
    serial_puts("TLS: ServerHello received, cipher suite: 0x");
    itoa(session->cipher_suite, msg, 16);
    serial_puts(msg);
    serial_puts("\n");
    
    return 0;
}


// Simplified handshake - just establishes connection without full crypto


int tls_handshake(tls_session_t* session) {
    if (!session || session->state != TLS_STATE_INIT) {
        return -1;
    }
    
    serial_puts("TLS: Starting handshake...\n");
    
    // Build and send ClientHello
    uint8_t* hello_buffer = (uint8_t*)kmalloc(1024);
    if (!hello_buffer) {
        return -1;
    }
    
    int hello_len = build_client_hello(session, hello_buffer, 1024);
    if (hello_len < 0) {
        kfree(hello_buffer);
        return -1;
    }
    
    // Save for handshake hash
    if (session->handshake_messages_len + hello_len <= 8192) {
        memcpy(session->handshake_messages + session->handshake_messages_len,
               hello_buffer, hello_len);
        session->handshake_messages_len += hello_len;
    }
    
    if (tls_send_record(session, TLS_CONTENT_HANDSHAKE, hello_buffer, hello_len) < 0) {
        serial_puts("TLS: Failed to send ClientHello\n");
        kfree(hello_buffer);
        return -1;
    }
    
    kfree(hello_buffer);
    session->state = TLS_STATE_CLIENT_HELLO_SENT;
    serial_puts("TLS: ClientHello sent\n");
    
    // Receive ServerHello
    uint8_t recv_buffer[4096];
    uint8_t content_type;
    
    int received = tls_recv_record(session, &content_type, recv_buffer, sizeof(recv_buffer));
    if (received < 0) {
        serial_puts("TLS: Failed to receive ServerHello\n");
        return -1;
    }
    
    // Handle TLS alerts during handshake
    if (content_type == TLS_CONTENT_ALERT) {
        if (received >= 2) {
            uint8_t level = recv_buffer[0];
            uint8_t description = recv_buffer[1];
            serial_puts("TLS: Server sent alert during handshake\n");
            serial_puts("TLS: Alert level: ");
            char msg[16];
            itoa(level, msg, 10);
            serial_puts(msg);
            serial_puts(" (");
            serial_puts(level == TLS_ALERT_WARNING ? "warning" : "fatal");
            serial_puts(")\n");
            serial_puts("TLS: Alert description: ");
            itoa(description, msg, 10);
            serial_puts(msg);
            serial_puts(" (");
            switch (description) {
                case TLS_ALERT_CLOSE_NOTIFY: serial_puts("close_notify"); break;
                case TLS_ALERT_HANDSHAKE_FAILURE: serial_puts("handshake_failure"); break;
                case TLS_ALERT_BAD_CERTIFICATE: serial_puts("bad_certificate"); break;
                case TLS_ALERT_PROTOCOL_VERSION: serial_puts("protocol_version"); break;
                case TLS_ALERT_INSUFFICIENT_SECURITY: serial_puts("insufficient_security"); break;
                case TLS_ALERT_INTERNAL_ERROR: serial_puts("internal_error"); break;
                case TLS_ALERT_DECODE_ERROR: serial_puts("decode_error"); break;
                default: serial_puts("unknown"); break;
            }
            serial_puts(")\n");
            serial_puts("TLS: This likely means the server requires cipher suites\n");
            serial_puts("TLS: or TLS features not supported by this implementation.\n");
        }
        return -1;
    }
    
    if (content_type != TLS_CONTENT_HANDSHAKE) {
        serial_puts("TLS: Unexpected content type: ");
        char msg[16];
        itoa(content_type, msg, 10);
        serial_puts(msg);
        serial_puts("\n");
        return -1;
    }
    
    // Save for handshake hash
    if (session->handshake_messages_len + received <= 8192) {
        memcpy(session->handshake_messages + session->handshake_messages_len,
               recv_buffer, received);
        session->handshake_messages_len += received;
    }
    
    if (parse_server_hello(session, recv_buffer, received) < 0) {
        serial_puts("TLS: Invalid ServerHello\n");
        return -1;
    }
    
    session->state = TLS_STATE_SERVER_HELLO_RECEIVED;
    
    // For RSA key exchange, generate pre-master secret
    // In a real implementation, we'd receive server certificate and use RSA
    // For this simplified version, we'll use a random pre-master secret
    tls_random_bytes(session->pre_master_secret, 48);
    session->pre_master_secret[0] = (session->version >> 8) & 0xFF;
    session->pre_master_secret[1] = session->version & 0xFF;
    
    // Derive keys from pre-master secret
    derive_keys(session);
    
    // Initialize encryption contexts
    if (session->cipher_suite == TLS_RSA_WITH_AES_128_CBC_SHA ||
        session->cipher_suite == TLS_RSA_WITH_AES_128_CBC_SHA256) {
        // Allocate and initialize AES contexts
        session->enc_ctx = kmalloc(sizeof(aes128_ctx_t));
        session->dec_ctx = kmalloc(sizeof(aes128_ctx_t));
        
        if (session->enc_ctx && session->dec_ctx) {
            aes128_init((aes128_ctx_t*)session->enc_ctx, session->client_write_key);
            aes128_set_iv((aes128_ctx_t*)session->enc_ctx, session->client_write_iv);
            
            aes128_init((aes128_ctx_t*)session->dec_ctx, session->server_write_key);
            aes128_set_iv((aes128_ctx_t*)session->dec_ctx, session->server_write_iv);
            
            serial_puts("TLS: Encryption initialized (AES-128-CBC)\n");
        }
    }
    
    // Receive Certificate message and extract RSA public key
    serial_puts("TLS: Receiving Certificate message...\n");
    uint8_t* cert_buffer = (uint8_t*)kmalloc(8192);  // Allocate large buffer for certificate
    if (!cert_buffer) {
        serial_puts("TLS: Failed to allocate certificate buffer\n");
        return -1;
    }
    
    received = tls_recv_record(session, &content_type, cert_buffer, 8192);
    if (received < 0 || content_type != TLS_CONTENT_HANDSHAKE) {
        serial_puts("TLS: Failed to receive Certificate\n");
        kfree(cert_buffer);
        return -1;
    }
    
    // Save Certificate to handshake messages
    if (session->handshake_messages_len + received <= 8192) {
        memcpy(session->handshake_messages + session->handshake_messages_len,
               cert_buffer, received);
        session->handshake_messages_len += received;
    }
    
    // Check it's a Certificate message (type 11)
    rsa_public_key_t server_key;
    if (received > 0 && cert_buffer[0] == TLS_HANDSHAKE_CERTIFICATE) {
        serial_puts("TLS: Certificate received (");
        char msg[16];
        itoa(received, msg, 10);
        serial_puts(msg);
        serial_puts(" bytes)\n");
        
        // Parse certificate chain
        // Format: type(1) + length(3) + certificates_length(3) + cert1_length(3) + cert1_data
        const uint8_t* ptr = cert_buffer + 4;  // Skip handshake header
        // Skip certificates length
        ptr += 3;
        
        // Get first certificate length
        uint32_t cert_len = (ptr[0] << 16) | (ptr[1] << 8) | ptr[2];
        ptr += 3;
        
        // Parse X.509 certificate to extract RSA public key
        if (x509_parse_certificate(ptr, cert_len, &server_key) < 0) {
            serial_puts("TLS: Failed to parse server certificate\n");
            kfree(cert_buffer);
            return -1;
        }
    } else {
        serial_puts("TLS: Invalid Certificate message\n");
        kfree(cert_buffer);
        return -1;
    }
    kfree(cert_buffer);
    
    // Receive ServerHelloDone message
    serial_puts("TLS: Receiving ServerHelloDone...\n");
    uint8_t server_done[64];
    received = tls_recv_record(session, &content_type, server_done, sizeof(server_done));
    if (received < 0 || content_type != TLS_CONTENT_HANDSHAKE) {
        serial_puts("TLS: Failed to receive ServerHelloDone\n");
        return -1;
    }
    
    // Save ServerHelloDone to handshake messages
    if (session->handshake_messages_len + received <= 8192) {
        memcpy(session->handshake_messages + session->handshake_messages_len,
               server_done, received);
        session->handshake_messages_len += received;
    }
    
    if (received > 0 && server_done[0] == TLS_HANDSHAKE_SERVER_HELLO_DONE) {
        serial_puts("TLS: ServerHelloDone received\n");
    }
    
    // Now send ClientKeyExchange with RSA-encrypted pre-master secret
    serial_puts("TLS: About to send ClientKeyExchange...\n");
    
    // RSA-encrypt the pre-master secret
    uint8_t* encrypted_pms = (uint8_t*)kmalloc(server_key.key_size);
    if (!encrypted_pms) {
        serial_puts("TLS: Failed to allocate buffer for encrypted pre-master secret\n");
        return -1;
    }
    
    if (rsa_public_encrypt(&server_key, session->pre_master_secret, 48, encrypted_pms) < 0) {
        serial_puts("TLS: RSA encryption failed\n");
        kfree(encrypted_pms);
        return -1;
    }
    
    serial_puts("TLS: Pre-master secret encrypted with RSA (");
    char msg[16];
    itoa(server_key.key_size * 8, msg, 10);
    serial_puts(msg);
    serial_puts("-bit key)\n");
    
    // Build ClientKeyExchange message
    // Format: type (1) + length (3) + encrypted_pms_length (2) + encrypted_pms
    uint32_t cke_len = 4 + 2 + server_key.key_size;
    uint8_t* client_key_exchange = (uint8_t*)kmalloc(cke_len);
    if (!client_key_exchange) {
        kfree(encrypted_pms);
        return -1;
    }
    
    client_key_exchange[0] = TLS_HANDSHAKE_CLIENT_KEY_EXCHANGE;
    uint32_t payload_len = 2 + server_key.key_size;
    client_key_exchange[1] = (payload_len >> 16) & 0xFF;
    client_key_exchange[2] = (payload_len >> 8) & 0xFF;
    client_key_exchange[3] = payload_len & 0xFF;
    
    // Add encrypted pre-master secret length (2 bytes)
    client_key_exchange[4] = (server_key.key_size >> 8) & 0xFF;
    client_key_exchange[5] = server_key.key_size & 0xFF;
    
    // Add encrypted pre-master secret
    memcpy(client_key_exchange + 6, encrypted_pms, server_key.key_size);
    
    // Save ClientKeyExchange to handshake messages (for Finished verification)
    if (session->handshake_messages_len + cke_len <= 8192) {
        memcpy(session->handshake_messages + session->handshake_messages_len,
               client_key_exchange, cke_len);
        session->handshake_messages_len += cke_len;
    }
    
    if (tls_send_record(session, TLS_CONTENT_HANDSHAKE, client_key_exchange, cke_len) < 0) {
        serial_puts("TLS: Failed to send ClientKeyExchange\n");
        kfree(client_key_exchange);
        kfree(encrypted_pms);
        return -1;
    }
    serial_puts("TLS: ClientKeyExchange sent (RSA-encrypted)\n");
    
    kfree(client_key_exchange);
    kfree(encrypted_pms);
    
    // Send ChangeCipherSpec
    uint8_t change_cipher_spec = 0x01;
    if (tls_send_record(session, TLS_CONTENT_CHANGE_CIPHER_SPEC, 
                       &change_cipher_spec, 1) < 0) {
        serial_puts("TLS: Failed to send ChangeCipherSpec\n");
        return -1;
    }
    serial_puts("TLS: ChangeCipherSpec sent\n");
    
    // Now encryption is enabled for subsequent messages
    session->encryption_enabled = 1;
    
    // IMPORTANT: Reset sequence number when enabling encryption
    // TLS spec requires sequence numbers to start at 0 for encrypted records
    session->client_seq_num = 0;
    
    // Build Finished message with proper verify_data
    // verify_data = PRF(master_secret, "client finished", SHA256(handshake_messages))
    
    // First, hash all handshake messages sent and received so far
    uint8_t handshake_hash[32];
    sha256_hash(session->handshake_messages, session->handshake_messages_len, handshake_hash);
    
    // Generate verify_data using TLS PRF
    uint8_t verify_data[12];
    const char* label = "client finished";
    tls_prf(session->master_secret, 48, label, handshake_hash, 32, verify_data, 12);
    
    // Build Finished message: type (1) + length (3) + verify_data (12)
    uint8_t finished_msg[16];
    finished_msg[0] = TLS_HANDSHAKE_FINISHED;
    finished_msg[1] = 0x00;
    finished_msg[2] = 0x00;
    finished_msg[3] = 0x0C;  // Length: 12 bytes
    memcpy(finished_msg + 4, verify_data, 12);
    
    // Send encrypted Finished as a handshake record
    if (tls_send_encrypted_handshake(session, finished_msg, 16) < 0) {
        serial_puts("TLS: Failed to send Finished\n");
        return -1;
    }
    serial_puts("TLS: Finished sent (encrypted with verify_data)\n");
    
    // Receive server's ChangeCipherSpec (or alert if handshake failed)
    uint8_t server_ccs[256];
    uint8_t ccs_type;
    received = tls_recv_record(session, &ccs_type, server_ccs, sizeof(server_ccs));
    
    if (received < 0) {
        serial_puts("TLS: Failed to receive server response\n");
        serial_puts("TLS: Connection may have been rejected by server\n");
        return -1;
    }
    
    // Check if server sent an alert instead of ChangeCipherSpec
    if (ccs_type == TLS_CONTENT_ALERT) {
        if (received >= 2) {
            uint8_t level = server_ccs[0];
            uint8_t description = server_ccs[1];
            serial_puts("TLS: Server rejected handshake with alert\n");
            serial_puts("TLS: Alert level: ");
            char msg[16];
            itoa(level, msg, 10);
            serial_puts(msg);
            serial_puts(", description: ");
            itoa(description, msg, 10);
            serial_puts(msg);
            serial_puts(" (");
            switch (description) {
                case TLS_ALERT_CLOSE_NOTIFY: serial_puts("close_notify"); break;
                case TLS_ALERT_UNEXPECTED_MESSAGE: serial_puts("unexpected_message"); break;
                case TLS_ALERT_BAD_RECORD_MAC: serial_puts("bad_record_mac"); break;
                case TLS_ALERT_DECRYPTION_FAILED: serial_puts("decryption_failed"); break;
                case TLS_ALERT_HANDSHAKE_FAILURE: serial_puts("handshake_failure"); break;
                case TLS_ALERT_BAD_CERTIFICATE: serial_puts("bad_certificate"); break;
                case TLS_ALERT_DECODE_ERROR: serial_puts("decode_error"); break;
                default: serial_puts("unknown"); break;
            }
            serial_puts(")\n");
        }
        serial_puts("TLS: Server rejected the handshake\n");
        serial_puts("TLS: This may be due to unsupported cipher suites or protocol version\n");
        return -1;
    }
    
    if (ccs_type != TLS_CONTENT_CHANGE_CIPHER_SPEC) {
        serial_puts("TLS: Expected ChangeCipherSpec but got content type: ");
        char msg[16];
        itoa(ccs_type, msg, 10);
        serial_puts(msg);
        serial_puts("\n");
        return -1;
    }
    
    serial_puts("TLS: Server ChangeCipherSpec received\n");
    
    // Receive server's Finished (encrypted)
    // This will likely fail because our keys don't match the server's
    uint8_t server_finished[256];
    received = tls_recv(session, server_finished, sizeof(server_finished));
    if (received < 0) {
        serial_puts("TLS: Could not decrypt server Finished message\n");
        serial_puts("TLS: Key mismatch - RSA key exchange not implemented\n");
        return -1;
    }
    
    serial_puts("TLS: Handshake complete with encryption enabled\n");
    serial_puts("TLS: WARNING: Keys may not match - connection may fail\n");
    
    session->state = TLS_STATE_ESTABLISHED;
    
    return 0;
}


// Application data I/O


int tls_send(tls_session_t* session, const uint8_t* data, uint32_t len) {
    if (!session || session->state != TLS_STATE_ESTABLISHED) {
        return -1;
    }
    
    if (!session->encryption_enabled) {
        // No encryption - just send as application data
        return tls_send_record(session, TLS_CONTENT_APPLICATION_DATA, data, len);
    }
    
    // With encryption: add padding, compute MAC, encrypt
    size_t mac_len = (session->cipher_suite == TLS_RSA_WITH_AES_128_CBC_SHA256) ? 32 : 20;
    size_t block_size = 16;  // AES block size
    
    // Calculate padding needed
    size_t total_len = len + mac_len + 1;  // data + MAC + padding_length byte
    size_t padding_len = (block_size - (total_len % block_size)) % block_size;
    total_len += padding_len;
    
    uint8_t* plaintext = (uint8_t*)kmalloc(total_len);
    if (!plaintext) {
        return -1;
    }
    
    // Build plaintext: data + MAC + padding + padding_length
    memcpy(plaintext, data, len);
    
    // Compute MAC over sequence number + TLS header + data
    uint8_t mac_input[13 + TLS_MAX_RECORD_SIZE];
    size_t mac_input_len = 0;
    
    // Sequence number (8 bytes, big endian)
    for (int i = 7; i >= 0; i--) {
        mac_input[mac_input_len++] = (session->client_seq_num >> (i * 8)) & 0xFF;
    }
    
    // TLS header: type (1) + version (2) + length (2)
    mac_input[mac_input_len++] = TLS_CONTENT_APPLICATION_DATA;
    mac_input[mac_input_len++] = (session->version >> 8) & 0xFF;
    mac_input[mac_input_len++] = session->version & 0xFF;
    mac_input[mac_input_len++] = (len >> 8) & 0xFF;
    mac_input[mac_input_len++] = len & 0xFF;
    
    // Data
    memcpy(mac_input + mac_input_len, data, len);
    mac_input_len += len;
    
    // Compute MAC
    if (session->cipher_suite == TLS_RSA_WITH_AES_128_CBC_SHA256) {
        hmac_sha256(session->client_write_mac_key, 32, mac_input, mac_input_len,
                    plaintext + len);
    } else {
        hmac_sha1(session->client_write_mac_key, 20, mac_input, mac_input_len,
                  plaintext + len);
    }
    
    // Add padding
    for (size_t i = 0; i <= padding_len; i++) {
        plaintext[len + mac_len + i] = padding_len;
    }
    
    // Encrypt
    uint8_t* ciphertext = (uint8_t*)kmalloc(total_len);
    if (!ciphertext) {
        kfree(plaintext);
        return -1;
    }
    
    aes128_cbc_encrypt((aes128_ctx_t*)session->enc_ctx, plaintext, ciphertext, total_len);
    
    // Send encrypted record
    int result = tls_send_record(session, TLS_CONTENT_APPLICATION_DATA, ciphertext, total_len);
    
    kfree(plaintext);
    kfree(ciphertext);
    
    if (result >= 0) {
        session->client_seq_num++;
    }
    
    return result;
}

int tls_recv(tls_session_t* session, uint8_t* buffer, uint32_t max_len) {
    if (!session || session->state != TLS_STATE_ESTABLISHED) {
        return -1;
    }
    
    // Receive application data record
    uint8_t content_type;
    uint8_t* temp_buffer = (uint8_t*)kmalloc(TLS_MAX_RECORD_SIZE);
    if (!temp_buffer) {
        return -1;
    }
    
    int received = tls_recv_record(session, &content_type, temp_buffer, TLS_MAX_RECORD_SIZE);
    
    if (received < 0) {
        kfree(temp_buffer);
        return -1;
    }
    
    if (received == 0) {
        // No data available (connection closed cleanly)
        kfree(temp_buffer);
        return 0;
    }
    
    // Handle different content types
    if (content_type == TLS_CONTENT_ALERT) {
        // Parse alert
        if (received >= 2) {
            uint8_t level = temp_buffer[0];
            uint8_t description = temp_buffer[1];
            serial_puts("TLS: Received alert - level: ");
            char msg[16];
            itoa(level, msg, 10);
            serial_puts(msg);
            serial_puts(", description: ");
            itoa(description, msg, 10);
            serial_puts(msg);
            serial_puts("\n");
            
            if (description == 10) {
                serial_puts("TLS: unexpected_message - handshake protocol error\n");
            }
        }
        session->state = TLS_STATE_ERROR;
        kfree(temp_buffer);
        return -1;
    }
    
    if (content_type != TLS_CONTENT_APPLICATION_DATA) {
        kfree(temp_buffer);
        return -1;
    }
    
    if (!session->encryption_enabled) {
        // No encryption - just return the data
        size_t to_copy = ((uint32_t)received < max_len) ? (uint32_t)received : max_len;
        memcpy(buffer, temp_buffer, to_copy);
        kfree(temp_buffer);
        return to_copy;
    }
    
    // Decrypt the data
    uint8_t* plaintext = (uint8_t*)kmalloc(received);
    if (!plaintext) {
        kfree(temp_buffer);
        return -1;
    }
    
    aes128_cbc_decrypt((aes128_ctx_t*)session->dec_ctx, temp_buffer, plaintext, received);
    
    // Extract data length (remove MAC and padding)
    size_t mac_len = (session->cipher_suite == TLS_RSA_WITH_AES_128_CBC_SHA256) ? 32 : 20;
    uint8_t padding_len = plaintext[received - 1];
    
    int data_len = received - mac_len - padding_len - 1;
    if (data_len <= 0 || data_len > received) {
        serial_puts("TLS: Invalid padding or MAC\n");
        kfree(plaintext);
        kfree(temp_buffer);
        return -1;
    }
    
    // Verify MAC
    uint8_t mac_input[13 + TLS_MAX_RECORD_SIZE];
    size_t mac_input_len = 0;
    
    // Sequence number
    for (int i = 7; i >= 0; i--) {
        mac_input[mac_input_len++] = (session->server_seq_num >> (i * 8)) & 0xFF;
    }
    
    // TLS header
    mac_input[mac_input_len++] = TLS_CONTENT_APPLICATION_DATA;
    mac_input[mac_input_len++] = (session->version >> 8) & 0xFF;
    mac_input[mac_input_len++] = session->version & 0xFF;
    mac_input[mac_input_len++] = (data_len >> 8) & 0xFF;
    mac_input[mac_input_len++] = data_len & 0xFF;
    
    // Data
    memcpy(mac_input + mac_input_len, plaintext, data_len);
    mac_input_len += data_len;
    
    uint8_t computed_mac[32];
    if (session->cipher_suite == TLS_RSA_WITH_AES_128_CBC_SHA256) {
        hmac_sha256(session->server_write_mac_key, 32, mac_input, mac_input_len, computed_mac);
    } else {
        hmac_sha1(session->server_write_mac_key, 20, mac_input, mac_input_len, computed_mac);
    }
    
    // Compare MAC
    if (memcmp(computed_mac, plaintext + data_len, mac_len) != 0) {
        serial_puts("TLS: MAC verification failed!\n");
        kfree(plaintext);
        kfree(temp_buffer);
        return -1;
    }
    
    // Copy decrypted data to output buffer
    size_t to_copy = ((uint32_t)data_len < max_len) ? (uint32_t)data_len : max_len;
    memcpy(buffer, plaintext, to_copy);
    
    kfree(plaintext);
    kfree(temp_buffer);
    
    session->server_seq_num++;
    
    return to_copy;
}


// Session termination


void tls_session_close(tls_session_t* session) {
    if (!session || session->state == TLS_STATE_CLOSED) {
        return;
    }
    
    // Send close_notify alert
    uint8_t alert[2] = { TLS_ALERT_WARNING, TLS_ALERT_CLOSE_NOTIFY };
    tls_send_record(session, TLS_CONTENT_ALERT, alert, 2);
    
    session->state = TLS_STATE_CLOSED;
    serial_puts("TLS: Connection closed\n");
}
