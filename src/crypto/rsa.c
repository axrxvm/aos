/*
 * === AOS HEADER BEGIN ===
 * ./src/crypto/rsa.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.8.5
 * === AOS HEADER END ===
 */

#include <crypto/rsa.h>
#include <crypto/bigint.h>
#include <string.h>
#include <stdlib.h>
#include <vmm.h>

// Simple PRNG for padding (not cryptographically secure, but sufficient for demo)
static uint32_t rsa_rand_seed = 0x12345678;

static uint8_t rsa_rand_byte(void) {
    rsa_rand_seed = rsa_rand_seed * 1103515245 + 12345;
    return (rsa_rand_seed >> 16) & 0xFF;
}

// Initialize RSA public key from raw bytes
void rsa_public_key_init(rsa_public_key_t* key, 
                         const uint8_t* modulus, uint32_t modulus_len,
                         const uint8_t* exponent, uint32_t exponent_len) {
    bigint_from_bytes(&key->modulus, modulus, modulus_len);
    bigint_from_bytes(&key->exponent, exponent, exponent_len);
    key->key_size = modulus_len;
}

// RSA public key encryption with PKCS#1 v1.5 padding
// plaintext_len must be <= key_size - 11
// ciphertext buffer must be key_size bytes
int rsa_public_encrypt(const rsa_public_key_t* key,
                       const uint8_t* plaintext, uint32_t plaintext_len,
                       uint8_t* ciphertext) {
    if (plaintext_len > key->key_size - 11) {
        return -1;  // Message too long
    }
    
    // PKCS#1 v1.5 padding: 0x00 || 0x02 || PS || 0x00 || M
    // where PS is random non-zero bytes, length = key_size - plaintext_len - 3
    uint8_t* padded = (uint8_t*)kmalloc(key->key_size);
    if (!padded) {
        return -1;
    }
    
    // Seed random number generator with some entropy
    rsa_rand_seed ^= (uint32_t)plaintext_len;
    
    padded[0] = 0x00;
    padded[1] = 0x02;
    
    // Fill padding string with random non-zero bytes
    uint32_t ps_len = key->key_size - plaintext_len - 3;
    for (uint32_t i = 0; i < ps_len; i++) {
        uint8_t byte;
        do {
            byte = rsa_rand_byte();
        } while (byte == 0);
        padded[2 + i] = byte;
    }
    
    padded[2 + ps_len] = 0x00;
    memcpy(padded + 2 + ps_len + 1, plaintext, plaintext_len);
    
    // Convert padded message to big integer
    bigint_t m, c;
    bigint_from_bytes(&m, padded, key->key_size);
    
    // Perform RSA encryption: c = m^e mod n
    bigint_modexp(&c, &m, &key->exponent, &key->modulus);
    
    // Convert result to bytes
    bigint_to_bytes(&c, ciphertext, key->key_size);
    
    kfree(padded);
    return 0;
}
