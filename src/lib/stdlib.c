/*
 * === AOS HEADER BEGIN ===
 * src/lib/stdlib.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */


// Implementations for standard library functions
#include <stdint.h> // For uint32_t
#include <stddef.h> // For size_t

// Maximum values for safety
#define MAX_ITOA_BUFFER 64
#define MAX_ATOI_DIGITS 10

// Function to convert integer to string with base support
void itoa(uint32_t num, char *buf, int base) {
    // Input validation
    if (!buf) return;
    
    int i = 0;
    if (num == 0) {
        buf[i++] = '0';
        buf[i] = '\0';
        return;
    }

    // Only support bases 2-36
    if (base < 2 || base > 36) {
        base = 10;
    }

    // Prevent buffer overrun
    do {
        if (i >= MAX_ITOA_BUFFER - 1) break; // Safety limit
        int digit = num % base;
        buf[i++] = (digit < 10) ? (digit + '0') : (digit - 10 + 'a');
        num /= base;
    } while (num > 0);
    buf[i] = '\0';
    
    // Reverse the string
    int start = 0;
    int end = i - 1;
    while (start < end) {
        char t = buf[start];
        buf[start] = buf[end];
        buf[end] = t;
        start++;
        end--;
    }
}

// Function to convert string to integer with overflow protection
int atoi(const char *str) {
    // Input validation
    if (!str) return 0;
    
    int result = 0;
    int sign = 1;
    int i = 0;
    int digit_count = 0;

    // Handle leading whitespace (optional)
    while (str[i] == ' ' || str[i] == '\t' || str[i] == '\n') {
        i++;
        // Prevent infinite loop on corrupted strings
        if (i > 100) return 0;
    }

    // Handle sign
    if (str[i] == '-') {
        sign = -1;
        i++;
    } else if (str[i] == '+') {
        i++;
    }

    // Convert digits with overflow protection
    while (str[i] >= '0' && str[i] <= '9') {
        int digit = str[i] - '0';
        
        // Check for overflow before multiplying
        if (digit_count >= MAX_ATOI_DIGITS) {
            // Overflow protection: return max/min int
            return (sign == 1) ? 0x7FFFFFFF : -0x7FFFFFFF - 1;
        }
        
        // Check for multiplication overflow
        if (result > (0x7FFFFFFF - digit) / 10) {
            // Would overflow
            return (sign == 1) ? 0x7FFFFFFF : -0x7FFFFFFF - 1;
        }
        
        result = result * 10 + digit;
        i++;
        digit_count++;
    }

    return result * sign;
}
