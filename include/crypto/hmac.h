/*
 * === AOS HEADER BEGIN ===
 * include/crypto/hmac.h
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */


#ifndef HMAC_H
#define HMAC_H

#include <stdint.h>
#include <stddef.h>
#include <crypto/sha256.h>

#define HMAC_SHA256_DIGEST_SIZE SHA256_DIGEST_SIZE

/**
 * Compute HMAC-SHA256
 * @param key HMAC key
 * @param key_len Key length in bytes
 * @param data Data to authenticate
 * @param data_len Data length in bytes
 * @param output Output buffer (must be at least 32 bytes)
 */
void hmac_sha256(const uint8_t* key, size_t key_len,
                 const uint8_t* data, size_t data_len,
                 uint8_t* output);

/**
 * Compute HMAC-SHA1 (for TLS 1.2 compatibility)
 * @param key HMAC key
 * @param key_len Key length in bytes
 * @param data Data to authenticate
 * @param data_len Data length in bytes
 * @param output Output buffer (must be at least 20 bytes)
 */
void hmac_sha1(const uint8_t* key, size_t key_len,
               const uint8_t* data, size_t data_len,
               uint8_t* output);

#endif // HMAC_H
