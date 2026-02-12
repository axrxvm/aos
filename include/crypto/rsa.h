/*
 * === AOS HEADER BEGIN ===
 * include/crypto/rsa.h
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */

#ifndef RSA_H
#define RSA_H

#include <arch_types.h>
#include <crypto/bigint.h>

// RSA public key structure
typedef struct {
    bigint_t modulus;   // n (typically 1024 or 2048 bits)
    bigint_t exponent;  // e (typically 65537)
    uint32_t key_size;  // Key size in bytes (128 for 1024-bit, 256 for 2048-bit)
} rsa_public_key_t;

// Initialize RSA public key from raw bytes
void rsa_public_key_init(rsa_public_key_t* key, 
                         const uint8_t* modulus, uint32_t modulus_len,
                         const uint8_t* exponent, uint32_t exponent_len);

// RSA public key encryption with PKCS#1 v1.5 padding
// plaintext_len must be <= key_size - 11
// ciphertext buffer must be key_size bytes
int rsa_public_encrypt(const rsa_public_key_t* key,
                       const uint8_t* plaintext, uint32_t plaintext_len,
                       uint8_t* ciphertext);

#endif // RSA_H
