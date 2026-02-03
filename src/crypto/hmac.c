/*
 * === AOS HEADER BEGIN ===
 * ./src/crypto/hmac.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.8.5
 * === AOS HEADER END ===
 */


/**
 * HMAC Implementation
 * Based on RFC 2104
 */

#include <crypto/hmac.h>
#include <crypto/sha256.h>
#include <string.h>
#include <vmm.h>

#define SHA1_DIGEST_SIZE 20
#define SHA1_BLOCK_SIZE 64

// Simplified SHA-1 implementation for HMAC
typedef struct {
    uint32_t state[5];
    uint64_t count;
    uint8_t buffer[64];
} sha1_ctx_t;

static void sha1_transform(uint32_t state[5], const uint8_t buffer[64]) {
    uint32_t a, b, c, d, e;
    uint32_t w[80];
    
    // Prepare message schedule
    for (int i = 0; i < 16; i++) {
        w[i] = (buffer[i*4] << 24) | (buffer[i*4+1] << 16) | 
               (buffer[i*4+2] << 8) | buffer[i*4+3];
    }
    for (int i = 16; i < 80; i++) {
        uint32_t temp = w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16];
        w[i] = (temp << 1) | (temp >> 31);
    }
    
    a = state[0];
    b = state[1];
    c = state[2];
    d = state[3];
    e = state[4];
    
    // Main loop
    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20) {
            f = (b & c) | ((~b) & d);
            k = 0x5A827999;
        } else if (i < 40) {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1;
        } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDC;
        } else {
            f = b ^ c ^ d;
            k = 0xCA62C1D6;
        }
        
        uint32_t temp = ((a << 5) | (a >> 27)) + f + e + k + w[i];
        e = d;
        d = c;
        c = (b << 30) | (b >> 2);
        b = a;
        a = temp;
    }
    
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
}

static void sha1_init(sha1_ctx_t* ctx) {
    ctx->state[0] = 0x67452301;
    ctx->state[1] = 0xEFCDAB89;
    ctx->state[2] = 0x98BADCFE;
    ctx->state[3] = 0x10325476;
    ctx->state[4] = 0xC3D2E1F0;
    ctx->count = 0;
}

static void sha1_update(sha1_ctx_t* ctx, const uint8_t* data, size_t len) {
    size_t i = 0;
    size_t index = (ctx->count / 8) % 64;
    
    ctx->count += len * 8;
    
    size_t part_len = 64 - index;
    
    if (len >= part_len) {
        memcpy(&ctx->buffer[index], data, part_len);
        sha1_transform(ctx->state, ctx->buffer);
        
        for (i = part_len; i + 63 < len; i += 64) {
            sha1_transform(ctx->state, &data[i]);
        }
        index = 0;
    }
    
    memcpy(&ctx->buffer[index], &data[i], len - i);
}

static void sha1_final(sha1_ctx_t* ctx, uint8_t* digest) {
    uint8_t bits[8];
    size_t index = (ctx->count / 8) % 64;
    size_t pad_len = (index < 56) ? (56 - index) : (120 - index);
    
    for (int i = 0; i < 8; i++) {
        bits[i] = (ctx->count >> (56 - i * 8)) & 0xFF;
    }
    
    uint8_t padding[64];
    padding[0] = 0x80;
    memset(padding + 1, 0, 63);
    
    sha1_update(ctx, padding, pad_len);
    sha1_update(ctx, bits, 8);
    
    for (int i = 0; i < 5; i++) {
        digest[i*4] = (ctx->state[i] >> 24) & 0xFF;
        digest[i*4 + 1] = (ctx->state[i] >> 16) & 0xFF;
        digest[i*4 + 2] = (ctx->state[i] >> 8) & 0xFF;
        digest[i*4 + 3] = ctx->state[i] & 0xFF;
    }
}

void hmac_sha256(const uint8_t* key, size_t key_len,
                 const uint8_t* data, size_t data_len,
                 uint8_t* output) {
    uint8_t k_pad[SHA256_BLOCK_SIZE];
    uint8_t temp_key[SHA256_DIGEST_SIZE];
    const uint8_t* actual_key;
    size_t actual_key_len;
    
    // If key is longer than block size, hash it first
    if (key_len > SHA256_BLOCK_SIZE) {
        sha256_ctx_t ctx;
        sha256_init(&ctx);
        sha256_update(&ctx, key, key_len);
        sha256_final(&ctx, temp_key);
        actual_key = temp_key;
        actual_key_len = SHA256_DIGEST_SIZE;
    } else {
        actual_key = key;
        actual_key_len = key_len;
    }
    
    // Prepare key XOR ipad
    memset(k_pad, 0x36, SHA256_BLOCK_SIZE);
    for (size_t i = 0; i < actual_key_len; i++) {
        k_pad[i] ^= actual_key[i];
    }
    
    // Compute H(K XOR ipad, text)
    sha256_ctx_t ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, k_pad, SHA256_BLOCK_SIZE);
    sha256_update(&ctx, data, data_len);
    sha256_final(&ctx, output);
    
    // Prepare key XOR opad
    memset(k_pad, 0x5c, SHA256_BLOCK_SIZE);
    for (size_t i = 0; i < actual_key_len; i++) {
        k_pad[i] ^= actual_key[i];
    }
    
    // Compute H(K XOR opad, H(K XOR ipad, text))
    uint8_t temp[SHA256_DIGEST_SIZE];
    memcpy(temp, output, SHA256_DIGEST_SIZE);
    
    sha256_init(&ctx);
    sha256_update(&ctx, k_pad, SHA256_BLOCK_SIZE);
    sha256_update(&ctx, temp, SHA256_DIGEST_SIZE);
    sha256_final(&ctx, output);
}

void hmac_sha1(const uint8_t* key, size_t key_len,
               const uint8_t* data, size_t data_len,
               uint8_t* output) {
    uint8_t k_pad[SHA1_BLOCK_SIZE];
    uint8_t temp_key[SHA1_DIGEST_SIZE];
    const uint8_t* actual_key;
    size_t actual_key_len;
    
    // If key is longer than block size, hash it first
    if (key_len > SHA1_BLOCK_SIZE) {
        sha1_ctx_t ctx;
        sha1_init(&ctx);
        sha1_update(&ctx, key, key_len);
        sha1_final(&ctx, temp_key);
        actual_key = temp_key;
        actual_key_len = SHA1_DIGEST_SIZE;
    } else {
        actual_key = key;
        actual_key_len = key_len;
    }
    
    // Prepare key XOR ipad
    memset(k_pad, 0x36, SHA1_BLOCK_SIZE);
    for (size_t i = 0; i < actual_key_len; i++) {
        k_pad[i] ^= actual_key[i];
    }
    
    // Compute H(K XOR ipad, text)
    sha1_ctx_t ctx;
    sha1_init(&ctx);
    sha1_update(&ctx, k_pad, SHA1_BLOCK_SIZE);
    sha1_update(&ctx, data, data_len);
    sha1_final(&ctx, output);
    
    // Prepare key XOR opad
    memset(k_pad, 0x5c, SHA1_BLOCK_SIZE);
    for (size_t i = 0; i < actual_key_len; i++) {
        k_pad[i] ^= actual_key[i];
    }
    
    // Compute H(K XOR opad, H(K XOR ipad, text))
    uint8_t temp[SHA1_DIGEST_SIZE];
    memcpy(temp, output, SHA1_DIGEST_SIZE);
    
    sha1_init(&ctx);
    sha1_update(&ctx, k_pad, SHA1_BLOCK_SIZE);
    sha1_update(&ctx, temp, SHA1_DIGEST_SIZE);
    sha1_final(&ctx, output);
}
