/*
 * === AOS HEADER BEGIN ===
 * src/crypto/bigint.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */

#include <crypto/bigint.h>
#include <string.h>

// Initialize big integer from bytes (big-endian)
void bigint_from_bytes(bigint_t* bn, const uint8_t* bytes, uint32_t byte_len) {
    memset(bn, 0, sizeof(bigint_t));
    
    if (byte_len == 0) {
        bn->len = 1;
        return;
    }
    
    uint32_t word_count = (byte_len + 3) / 4;
    if (word_count > BIGINT_MAX_WORDS) {
        word_count = BIGINT_MAX_WORDS;
        byte_len = BIGINT_MAX_WORDS * 4;
    }
    
    // Convert bytes to words (big-endian to little-endian words)
    for (uint32_t i = 0; i < byte_len; i++) {
        uint32_t word_idx = (byte_len - 1 - i) / 4;
        uint32_t byte_pos = (byte_len - 1 - i) % 4;
        if (word_idx < BIGINT_MAX_WORDS) {  // Bounds check
            bn->words[word_idx] |= ((uint32_t)bytes[i]) << (byte_pos * 8);
        }
    }
    
    // Set length - find last non-zero word
    bn->len = word_count;
    while (bn->len > 1 && bn->words[bn->len - 1] == 0) {
        bn->len--;
    }
    if (bn->len == 0) bn->len = 1;
}

// Convert big integer to bytes (big-endian)
void bigint_to_bytes(const bigint_t* bn, uint8_t* bytes, uint32_t byte_len) {
    memset(bytes, 0, byte_len);
    
    uint32_t bn_bytes = bn->len * 4;
    if (bn_bytes > byte_len) {
        bn_bytes = byte_len;
    }
    
    for (uint32_t i = 0; i < bn_bytes; i++) {
        uint32_t word_idx = i / 4;
        uint32_t byte_pos = i % 4;
        bytes[byte_len - 1 - i] = (bn->words[word_idx] >> (byte_pos * 8)) & 0xFF;
    }
}

// Set big integer to a small value
void bigint_set(bigint_t* bn, uint32_t value) {
    memset(bn, 0, sizeof(bigint_t));
    bn->words[0] = value;
    bn->len = (value == 0) ? 1 : 1;
}

// Compare two big integers (returns -1 if a<b, 0 if a==b, 1 if a>b)
int bigint_cmp(const bigint_t* a, const bigint_t* b) {
    if (a->len > b->len) return 1;
    if (a->len < b->len) return -1;
    
    for (int i = a->len - 1; i >= 0; i--) {
        if (a->words[i] > b->words[i]) return 1;
        if (a->words[i] < b->words[i]) return -1;
    }
    return 0;
}

// Addition: result = a + b
void bigint_add(bigint_t* result, const bigint_t* a, const bigint_t* b) {
    uint32_t max_len = (a->len > b->len) ? a->len : b->len;
    if (max_len > BIGINT_MAX_WORDS) max_len = BIGINT_MAX_WORDS;
    
    uint64_t carry = 0;
    
    for (uint32_t i = 0; i < max_len || carry; i++) {
        if (i >= BIGINT_MAX_WORDS) break;  // Bounds check
        uint64_t sum = carry;
        if (i < a->len) sum += a->words[i];
        if (i < b->len) sum += b->words[i];
        
        result->words[i] = sum & 0xFFFFFFFF;
        carry = sum >> 32;
    }
    
    result->len = max_len;
    if (carry && result->len < BIGINT_MAX_WORDS) {
        result->words[result->len++] = (uint32_t)carry;
    }
    
    // Normalize
    while (result->len > 1 && result->words[result->len - 1] == 0) {
        result->len--;
    }
}

// Subtraction: result = a - b (assumes a >= b)
void bigint_sub(bigint_t* result, const bigint_t* a, const bigint_t* b) {
    uint64_t borrow = 0;
    
    for (uint32_t i = 0; i < a->len; i++) {
        uint64_t diff = (uint64_t)a->words[i] - borrow;
        if (i < b->len) {
            diff -= b->words[i];
        }
        
        if (diff > a->words[i]) {
            borrow = 1;
            diff += 0x100000000ULL;
        } else {
            borrow = 0;
        }
        
        result->words[i] = diff & 0xFFFFFFFF;
    }
    
    result->len = a->len;
    while (result->len > 1 && result->words[result->len - 1] == 0) {
        result->len--;
    }
}

// Multiplication: result = a * b
void bigint_mul(bigint_t* result, const bigint_t* a, const bigint_t* b) {
    bigint_t temp;
    memset(&temp, 0, sizeof(bigint_t));
    temp.len = 1;
    
    // Bounds check
    if (a->len + b->len > BIGINT_MAX_WORDS) {
        // Result would overflow, clamp to max
        temp.len = BIGINT_MAX_WORDS;
        *result = temp;
        return;
    }
    
    for (uint32_t i = 0; i < a->len && i < BIGINT_MAX_WORDS; i++) {
        uint64_t carry = 0;
        for (uint32_t j = 0; j < b->len || carry; j++) {
            if (i + j >= BIGINT_MAX_WORDS) break;  // Bounds check
            uint64_t product = temp.words[i + j] + carry;
            if (j < b->len) {
                product += (uint64_t)a->words[i] * (uint64_t)b->words[j];
            }
            temp.words[i + j] = product & 0xFFFFFFFF;
            carry = product >> 32;
        }
    }
    
    temp.len = a->len + b->len;
    if (temp.len > BIGINT_MAX_WORDS) temp.len = BIGINT_MAX_WORDS;
    while (temp.len > 1 && temp.words[temp.len - 1] == 0) {
        temp.len--;
    }
    
    *result = temp;
}

// Division: quotient = a / b, remainder = a % b
void bigint_div(bigint_t* quotient, bigint_t* remainder, 
                const bigint_t* a, const bigint_t* b) {
    // Check for division by zero
    if (b->len == 1 && b->words[0] == 0) {
        if (quotient) {
            memset(quotient, 0, sizeof(bigint_t));
            quotient->len = 1;
        }
        if (remainder) {
            memset(remainder, 0, sizeof(bigint_t));
            remainder->len = 1;
        }
        return;
    }
    
    // If a < b, quotient = 0, remainder = a
    if (bigint_cmp(a, b) < 0) {
        if (quotient) {
            memset(quotient, 0, sizeof(bigint_t));
            quotient->len = 1;
        }
        if (remainder) {
            *remainder = *a;
        }
        return;
    }
    
    // Simple long division algorithm
    bigint_t q, r;
    memset(&q, 0, sizeof(bigint_t));
    memset(&r, 0, sizeof(bigint_t));
    q.len = 1;
    r.len = 1;
    
    // Find the highest set bit in a
    int max_bit = -1;
    for (int i = a->len - 1; i >= 0; i--) {
        if (a->words[i] != 0) {
            for (int bit = 31; bit >= 0; bit--) {
                if (a->words[i] & (1U << bit)) {
                    max_bit = i * 32 + bit;
                    goto found_bit;
                }
            }
        }
    }
found_bit:
    
    if (max_bit < 0) {
        // a is zero
        if (quotient) {
            memset(quotient, 0, sizeof(bigint_t));
            quotient->len = 1;
        }
        if (remainder) {
            memset(remainder, 0, sizeof(bigint_t));
            remainder->len = 1;
        }
        return;
    }
    
    // Process from most significant to least significant bit
    for (int i = max_bit; i >= 0; i--) {
        // Left shift r by 1
        uint32_t carry = 0;
        for (uint32_t j = 0; j < r.len || carry; j++) {
            if (j >= BIGINT_MAX_WORDS) break;  // Bounds check
            uint64_t val = ((uint64_t)r.words[j] << 1) | carry;
            r.words[j] = val & 0xFFFFFFFF;
            carry = val >> 32;
            if (j >= r.len && (r.words[j] != 0 || carry)) {
                r.len = j + 1;
            }
        }
        
        // Set LSB of r to bit i of a
        uint32_t word_idx = i / 32;
        uint32_t bit_idx = i % 32;
        if (word_idx < a->len && (a->words[word_idx] & (1U << bit_idx))) {
            r.words[0] |= 1;
        }
        
        // If r >= b, subtract b from r and set bit i of quotient
        if (bigint_cmp(&r, b) >= 0) {
            bigint_sub(&r, &r, b);
            if (word_idx < BIGINT_MAX_WORDS) {  // Bounds check
                q.words[word_idx] |= (1U << bit_idx);
                if (word_idx >= q.len) {
                    q.len = word_idx + 1;
                }
            }
        }
    }
    
    if (quotient) *quotient = q;
    if (remainder) *remainder = r;
}

// Modular reduction: result = a mod m
void bigint_mod(bigint_t* result, const bigint_t* a, const bigint_t* m) {
    if (bigint_cmp(a, m) < 0) {
        *result = *a;
        return;
    }
    bigint_div(NULL, result, a, m);
}

// Modular exponentiation: result = base^exp mod modulus (using square-and-multiply)
void bigint_modexp(bigint_t* result, const bigint_t* base, 
                   const bigint_t* exp, const bigint_t* modulus) {
    bigint_t r, temp;
    bigint_set(&r, 1);
    temp = *base;
    bigint_mod(&temp, &temp, modulus);
    
    // Square-and-multiply algorithm
    for (uint32_t i = 0; i < exp->len; i++) {
        uint32_t word = exp->words[i];
        for (uint32_t bit = 0; bit < 32; bit++) {
            if (word & (1U << bit)) {
                bigint_mul(&r, &r, &temp);
                bigint_mod(&r, &r, modulus);
            }
            bigint_mul(&temp, &temp, &temp);
            bigint_mod(&temp, &temp, modulus);
        }
    }
    
    *result = r;
}
