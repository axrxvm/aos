/*
 * === AOS HEADER BEGIN ===
 * ./src/lib/stdlib.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.8.5
 * === AOS HEADER END ===
 */


// Implementations for standard library functions
#include <stdint.h> // For uint32_t

// Function to convert integer to string with base support
void itoa(uint32_t num, char *buf, int base) {
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

    do {
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

// Function to convert string to integer
int atoi(const char *str) {
    int result = 0;
    int sign = 1;
    int i = 0;

    // Handle leading whitespace (optional)
    while (str[i] == ' ' || str[i] == '\t' || str[i] == '\n') {
        i++;
    }

    // Handle sign
    if (str[i] == '-') {
        sign = -1;
        i++;
    } else if (str[i] == '+') {
        i++;
    }

    // Convert digits
    while (str[i] >= '0' && str[i] <= '9') {
        result = result * 10 + (str[i] - '0');
        i++;
    }

    return result * sign;
}
