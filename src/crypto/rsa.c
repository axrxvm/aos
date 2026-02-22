/*
 * === AOS HEADER BEGIN ===
 * src/crypto/rsa.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */

#include <crypto/rsa.h>
#include <crypto/bigint.h>
#include <crypto/sha256.h>
#include <arch/pit.h>
#include <string.h>
#include <stdlib.h>
#include <vmm.h>

static uint8_t rsa_drbg_state[SHA256_DIGEST_SIZE];
static uint64_t rsa_drbg_counter = 0;
static int rsa_drbg_seeded = 0;

#if defined(ARCH_X86_64) || defined(ARCH_I386)
static int rsa_cpu_has_rdrand(void) {
    uint32_t eax = 1;
    uint32_t ebx = 0;
    uint32_t ecx = 0;
    uint32_t edx = 0;
    asm volatile("cpuid"
                 : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                 : "a"(eax), "c"(0));
    (void)ebx;
    (void)edx;
    return (ecx & (1U << 30)) != 0;
}

static int rsa_rdrand32(uint32_t* out) {
    unsigned char ok = 0;
    if (!out) {
        return 0;
    }
    asm volatile("rdrand %0; setc %1" : "=r"(*out), "=qm"(ok));
    return ok != 0;
}
#endif

static void rsa_drbg_seed(void) {
    uint8_t seed_material[64];
    memset(seed_material, 0, sizeof(seed_material));

    uint32_t ticks = get_tick_count();
    uintptr_t stack_ptr = (uintptr_t)&seed_material;
    uintptr_t state_ptr = (uintptr_t)&rsa_drbg_state;
    uint64_t mix = ((uint64_t)ticks << 32) ^ (uint64_t)stack_ptr ^ (uint64_t)state_ptr;

    memcpy(seed_material, &ticks, sizeof(ticks));
    memcpy(seed_material + 8, &stack_ptr, sizeof(stack_ptr));
    memcpy(seed_material + 24, &state_ptr, sizeof(state_ptr));
    memcpy(seed_material + 40, &mix, sizeof(mix));

#if defined(ARCH_X86_64) || defined(ARCH_I386)
    if (rsa_cpu_has_rdrand()) {
        uint32_t hw = 0;
        for (int i = 0; i < 4; i++) {
            if (!rsa_rdrand32(&hw)) {
                break;
            }
            memcpy(seed_material + 48 + (i * sizeof(hw)), &hw, sizeof(hw));
        }
    }
#endif

    sha256_hash(seed_material, sizeof(seed_material), rsa_drbg_state);
    rsa_drbg_counter = mix ^ (uint64_t)ticks;
    rsa_drbg_seeded = 1;
}

static int rsa_drbg_generate(uint8_t* out, size_t len) {
    if (!out) {
        return -1;
    }

    if (!rsa_drbg_seeded) {
        rsa_drbg_seed();
    }

    size_t produced = 0;
    while (produced < len) {
        uint8_t input[SHA256_DIGEST_SIZE + sizeof(rsa_drbg_counter)];
        memcpy(input, rsa_drbg_state, SHA256_DIGEST_SIZE);
        memcpy(input + SHA256_DIGEST_SIZE, &rsa_drbg_counter, sizeof(rsa_drbg_counter));

        uint8_t block[SHA256_DIGEST_SIZE];
        sha256_hash(input, sizeof(input), block);

        uint8_t state_update[SHA256_DIGEST_SIZE * 2 + sizeof(rsa_drbg_counter)];
        memcpy(state_update, rsa_drbg_state, SHA256_DIGEST_SIZE);
        memcpy(state_update + SHA256_DIGEST_SIZE, block, SHA256_DIGEST_SIZE);
        memcpy(state_update + (SHA256_DIGEST_SIZE * 2), &rsa_drbg_counter, sizeof(rsa_drbg_counter));
        sha256_hash(state_update, sizeof(state_update), rsa_drbg_state);
        rsa_drbg_counter++;

        size_t chunk = (len - produced < SHA256_DIGEST_SIZE) ? (len - produced) : SHA256_DIGEST_SIZE;
        memcpy(out + produced, block, chunk);
        produced += chunk;
    }

    return 0;
}

static int rsa_random_bytes(uint8_t* out, size_t len) {
    if (!out) {
        return -1;
    }

#if defined(ARCH_X86_64) || defined(ARCH_I386)
    if (rsa_cpu_has_rdrand()) {
        size_t produced = 0;
        while (produced < len) {
            uint32_t value = 0;
            int success = 0;
            for (int attempt = 0; attempt < 10; attempt++) {
                if (rsa_rdrand32(&value)) {
                    success = 1;
                    break;
                }
            }
            if (!success) {
                break;
            }

            size_t chunk = (len - produced < sizeof(value)) ? (len - produced) : sizeof(value);
            memcpy(out + produced, &value, chunk);
            produced += chunk;
        }

        if (produced == len) {
            return 0;
        }

        // Mix any produced hardware bytes into software DRBG state before fallback.
        if (produced > 0) {
            if (!rsa_drbg_seeded) {
                rsa_drbg_seed();
            }
            uint8_t reseed_input[SHA256_DIGEST_SIZE + sizeof(uint32_t)];
            memcpy(reseed_input, rsa_drbg_state, SHA256_DIGEST_SIZE);
            memset(reseed_input + SHA256_DIGEST_SIZE, 0, sizeof(uint32_t));
            size_t mix_len = (produced < sizeof(uint32_t)) ? produced : sizeof(uint32_t);
            memcpy(reseed_input + SHA256_DIGEST_SIZE, out, mix_len);
            sha256_hash(reseed_input, sizeof(reseed_input), rsa_drbg_state);
        }

        return rsa_drbg_generate(out + produced, len - produced);
    }
#endif

    return rsa_drbg_generate(out, len);
}

static int rsa_random_nonzero_bytes(uint8_t* out, size_t len) {
    size_t written = 0;
    while (written < len) {
        uint8_t byte = 0;
        if (rsa_random_bytes(&byte, 1) != 0) {
            return -1;
        }
        if (byte != 0) {
            out[written++] = byte;
        }
    }
    return 0;
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
    if (!key || !plaintext || !ciphertext) {
        return -1;
    }
    if (key->key_size < 11) {
        return -1;
    }
    if (plaintext_len > key->key_size - 11) {
        return -1;  // Message too long
    }
    
    // PKCS#1 v1.5 padding: 0x00 || 0x02 || PS || 0x00 || M
    // where PS is random non-zero bytes, length = key_size - plaintext_len - 3
    uint8_t* padded = (uint8_t*)kmalloc(key->key_size);
    if (!padded) {
        return -1;
    }
    
    padded[0] = 0x00;
    padded[1] = 0x02;
    
    // Fill padding string with random non-zero bytes
    uint32_t ps_len = key->key_size - plaintext_len - 3;
    if (rsa_random_nonzero_bytes(padded + 2, ps_len) != 0) {
        memset(padded, 0, key->key_size);
        kfree(padded);
        return -1;
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
    
    memset(padded, 0, key->key_size);
    kfree(padded);
    return 0;
}
