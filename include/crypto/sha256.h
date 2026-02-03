/*
 * === AOS HEADER BEGIN ===
 * ./include/crypto/sha256.h
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.8.5
 * === AOS HEADER END ===
 */


#ifndef SHA256_H
#define SHA256_H

#include <stdint.h>
#include <stddef.h>

#define SHA256_DIGEST_SIZE 32  // 256 bits / 8 = 32 bytes
#define SHA256_BLOCK_SIZE 64   // 512 bits / 8 = 64 bytes

// SHA-256 context structure
typedef struct {
    uint32_t state[8];        // Internal state (8 x 32-bit words)
    uint64_t count;           // Number of bits processed
    uint8_t buffer[64];       // Input buffer
} sha256_ctx_t;

/**
 * Initialize SHA-256 context
 * @param ctx Context to initialize
 */
void sha256_init(sha256_ctx_t* ctx);

/**
 * Update SHA-256 hash with new data
 * @param ctx Context to update
 * @param data Data to hash
 * @param len Length of data in bytes
 */
void sha256_update(sha256_ctx_t* ctx, const uint8_t* data, size_t len);

/**
 * Finalize SHA-256 hash and produce digest
 * @param ctx Context to finalize
 * @param digest Output buffer (must be at least SHA256_DIGEST_SIZE bytes)
 */
void sha256_final(sha256_ctx_t* ctx, uint8_t* digest);

/**
 * Convenience function: compute SHA-256 hash of data in one call
 * @param data Data to hash
 * @param len Length of data in bytes
 * @param digest Output buffer (must be at least SHA256_DIGEST_SIZE bytes)
 */
void sha256_hash(const uint8_t* data, size_t len, uint8_t* digest);

/**
 * Convert hash digest to hexadecimal string
 * @param digest Hash digest (SHA256_DIGEST_SIZE bytes)
 * @param hex_out Output buffer (must be at least SHA256_DIGEST_SIZE*2+1 bytes)
 */
void sha256_to_hex(const uint8_t* digest, char* hex_out);

#endif // SHA256_H
