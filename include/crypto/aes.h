/*
 * === AOS HEADER BEGIN ===
 * ./include/crypto/aes.h
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.8.5
 * === AOS HEADER END ===
 */


#ifndef AES_H
#define AES_H

#include <stdint.h>
#include <stddef.h>

#define AES_BLOCK_SIZE 16  // 128 bits
#define AES_128_KEY_SIZE 16  // 128 bits

// AES-128 context
typedef struct {
    uint32_t round_keys[44];  // 11 round keys for AES-128 (4 words each)
    uint8_t iv[AES_BLOCK_SIZE];  // Initialization vector for CBC mode
} aes128_ctx_t;

/**
 * Initialize AES-128 context with key
 * @param ctx Context to initialize
 * @param key 16-byte encryption key
 */
void aes128_init(aes128_ctx_t* ctx, const uint8_t* key);

/**
 * Set initialization vector for CBC mode
 * @param ctx AES context
 * @param iv 16-byte initialization vector
 */
void aes128_set_iv(aes128_ctx_t* ctx, const uint8_t* iv);

/**
 * Encrypt a single 16-byte block (ECB mode)
 * @param ctx AES context
 * @param input 16-byte plaintext block
 * @param output 16-byte ciphertext block
 */
void aes128_encrypt_block(aes128_ctx_t* ctx, const uint8_t* input, uint8_t* output);

/**
 * Decrypt a single 16-byte block (ECB mode)
 * @param ctx AES context
 * @param input 16-byte ciphertext block
 * @param output 16-byte plaintext block
 */
void aes128_decrypt_block(aes128_ctx_t* ctx, const uint8_t* input, uint8_t* output);

/**
 * Encrypt data using AES-128-CBC
 * @param ctx AES context (with IV set)
 * @param input Plaintext data (must be multiple of 16 bytes)
 * @param output Ciphertext buffer
 * @param len Length in bytes (must be multiple of 16)
 */
void aes128_cbc_encrypt(aes128_ctx_t* ctx, const uint8_t* input, uint8_t* output, size_t len);

/**
 * Decrypt data using AES-128-CBC
 * @param ctx AES context (with IV set)
 * @param input Ciphertext data (must be multiple of 16 bytes)
 * @param output Plaintext buffer
 * @param len Length in bytes (must be multiple of 16)
 */
void aes128_cbc_decrypt(aes128_ctx_t* ctx, const uint8_t* input, uint8_t* output, size_t len);

#endif // AES_H
