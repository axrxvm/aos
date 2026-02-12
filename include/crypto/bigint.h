/*
 * === AOS HEADER BEGIN ===
 * include/crypto/bigint.h
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */

#ifndef BIGINT_H
#define BIGINT_H

#include <arch_types.h>

// Big integer for RSA operations (up to 2048 bits)
#define BIGINT_MAX_WORDS 64  // 64 * 32 = 2048 bits

typedef struct {
    uint32_t words[BIGINT_MAX_WORDS];
    uint32_t len;  // Number of significant words
} bigint_t;

// Initialize big integer from bytes (big-endian)
void bigint_from_bytes(bigint_t* bn, const uint8_t* bytes, uint32_t byte_len);

// Convert big integer to bytes (big-endian)
void bigint_to_bytes(const bigint_t* bn, uint8_t* bytes, uint32_t byte_len);

// Set big integer to a small value
void bigint_set(bigint_t* bn, uint32_t value);

// Compare two big integers (returns -1, 0, 1)
int bigint_cmp(const bigint_t* a, const bigint_t* b);

// Addition: result = a + b
void bigint_add(bigint_t* result, const bigint_t* a, const bigint_t* b);

// Subtraction: result = a - b (assumes a >= b)
void bigint_sub(bigint_t* result, const bigint_t* a, const bigint_t* b);

// Multiplication: result = a * b
void bigint_mul(bigint_t* result, const bigint_t* a, const bigint_t* b);

// Modular exponentiation: result = base^exp mod modulus (for RSA)
void bigint_modexp(bigint_t* result, const bigint_t* base, 
                   const bigint_t* exp, const bigint_t* modulus);

// Division: quotient = a / b, remainder = a % b
void bigint_div(bigint_t* quotient, bigint_t* remainder, 
                const bigint_t* a, const bigint_t* b);

// Modular reduction: result = a mod m
void bigint_mod(bigint_t* result, const bigint_t* a, const bigint_t* m);

#endif // BIGINT_H
