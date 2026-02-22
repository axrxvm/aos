/*
 * === AOS HEADER BEGIN ===
 * src/lib/string.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */


// Implementations for string functions
#include <stddef.h> // For size_t
#include <stdint.h> // For SIZE_MAX

// Define SIZE_MAX if not already defined
#ifndef SIZE_MAX
#define SIZE_MAX ((size_t)-1)
#endif

// Maximum string length to prevent infinite loops (1MB)
#define MAX_STRING_LENGTH (1024 * 1024)

// Minimal implementation of strcmp with null checks
int strcmp(const char *s1, const char *s2) {
    // Null pointer protection
    if (!s1 || !s2) {
        if (s1 == s2) return 0;
        return s1 ? 1 : -1;
    }
    
    // Prevent infinite loops on corrupted strings
    size_t count = 0;
    while (*s1 != '\0' && *s1 == *s2 && count < MAX_STRING_LENGTH) {
        s1++;
        s2++;
        count++;
    }
    return (*(unsigned char *)s1) - (*(unsigned char *)s2);
}

// Minimal implementation of strncmp with null checks
int strncmp(const char *s1, const char *s2, size_t n) {
    // Null pointer protection
    if (!s1 || !s2) {
        if (s1 == s2) return 0;
        return s1 ? 1 : -1;
    }
    
    // Bounds check
    if (n == 0) return 0;
    if (n > MAX_STRING_LENGTH) n = MAX_STRING_LENGTH;
    
    for (size_t i = 0; i < n; i++) {
        if (s1[i] != s2[i] || s1[i] == '\0' || s2[i] == '\0') {
            return (unsigned char)s1[i] - (unsigned char)s2[i];
        }
    }
    return 0;
}

// Calculate length of string with safety checks
size_t strlen(const char *s) {
    // Null pointer protection
    if (!s) return 0;
    
    size_t len = 0;
    // Prevent infinite loops on corrupted strings
    while (s[len] != '\0' && len < MAX_STRING_LENGTH) {
        len++;
    }
    return len;
}

// Copy string from src to dest with safety checks
char *strcpy(char *dest, const char *src) {
    // Null pointer protection
    if (!dest || !src) return dest;
    
    char *d = dest;
    size_t count = 0;
    // Prevent infinite loops and buffer overflows
    while ((*d++ = *src++) != '\0' && count < MAX_STRING_LENGTH) {
        count++;
    }
    
    // Ensure null termination even if we hit the limit
    if (count >= MAX_STRING_LENGTH) {
        *(d - 1) = '\0';
    }
    
    return dest;
}

// Copy at most n characters from src to dest with safety checks
char *strncpy(char *dest, const char *src, size_t n) {
    // Null pointer protection
    if (!dest || !src) return dest;
    
    // Bounds check
    if (n == 0) return dest;
    if (n > MAX_STRING_LENGTH) n = MAX_STRING_LENGTH;
    
    size_t i;
    for (i = 0; i < n && src[i] != '\0'; i++) {
        dest[i] = src[i];
    }
    for (; i < n; i++) {
        dest[i] = '\0';
    }
    return dest;
}

// Concatenate src to dest with safety checks
char *strcat(char *dest, const char *src) {
    // Null pointer protection
    if (!dest || !src) return dest;
    
    char *d = dest;
    size_t count = 0;
    
    // Find end of dest (with bounds check)
    while (*d != '\0' && count < MAX_STRING_LENGTH) {
        d++;
        count++;
    }
    
    // If we hit the limit, string is corrupted or too long
    if (count >= MAX_STRING_LENGTH) return dest;
    
    // Copy src to end of dest (with bounds check)
    count = 0;
    while ((*d++ = *src++) != '\0' && count < MAX_STRING_LENGTH) {
        count++;
    }
    
    // Ensure null termination
    if (count >= MAX_STRING_LENGTH) {
        *(d - 1) = '\0';
    }
    
    return dest;
}

// Concatenate at most n characters from src to dest with safety checks
char *strncat(char *dest, const char *src, size_t n) {
    // Null pointer protection
    if (!dest || !src) return dest;
    
    // Bounds check
    if (n == 0) return dest;
    if (n > MAX_STRING_LENGTH) n = MAX_STRING_LENGTH;
    
    size_t dest_len = strlen(dest);
    
    // Check for potential overflow
    if (dest_len >= MAX_STRING_LENGTH - n) {
        return dest; // Would overflow, abort
    }
    
    size_t i;
    for (i = 0; i < n && src[i] != '\0'; i++) {
        dest[dest_len + i] = src[i];
    }
    dest[dest_len + i] = '\0';
    
    return dest;
}

// Find first occurrence of character in string with safety checks
char *strchr(const char *s, int c) {
    // Null pointer protection
    if (!s) return NULL;
    
    size_t count = 0;
    while (*s != '\0' && count < MAX_STRING_LENGTH) {
        if (*s == (char)c) {
            return (char *)s;
        }
        s++;
        count++;
    }
    if ((char)c == '\0' && count < MAX_STRING_LENGTH) {
        return (char *)s;
    }
    return NULL;
}

// Find last occurrence of character in string with safety checks
char *strrchr(const char *s, int c) {
    // Null pointer protection
    if (!s) return NULL;
    
    const char *last = NULL;
    size_t count = 0;
    while (*s != '\0' && count < MAX_STRING_LENGTH) {
        if (*s == (char)c) {
            last = s;
        }
        s++;
        count++;
    }
    if ((char)c == '\0' && count < MAX_STRING_LENGTH) {
        return (char *)s;
    }
    return (char *)last;
}

// Find substring in string with safety checks
char *strstr(const char *haystack, const char *needle) {
    // Null pointer protection
    if (!haystack || !needle) return NULL;
    
    if (!*needle) {
        return (char *)haystack;
    }
    
    size_t outer_count = 0;
    while (*haystack && outer_count < MAX_STRING_LENGTH) {
        const char *h = haystack;
        const char *n = needle;
        size_t inner_count = 0;
        
        while (*h && *n && *h == *n && inner_count < MAX_STRING_LENGTH) {
            h++;
            n++;
            inner_count++;
        }
        
        if (!*n) {
            return (char *)haystack;
        }
        
        haystack++;
        outer_count++;
    }
    
    return NULL;
}

// Copy n bytes from src to dest, handling overlapping memory (robust)
void *memmove(void *dest, const void *src, size_t n) {
    // Null pointer checks
    if (!dest || !src) {
        return dest;
    }
    
    // Zero-size check
    if (n == 0) {
        return dest;
    }
    
    // Bounds check
    if (n > MAX_STRING_LENGTH) {
        return dest; // Refuse to copy excessive amounts
    }
    
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;
    
    // Check for same location
    if (d == s) {
        return dest;
    }
    
    // Detect overlap and choose copy direction
    if (d < s || d >= (s + n)) {
        // No overlap or dest is before src - copy forward
        // Use word-aligned copy if possible
        if (n >= sizeof(unsigned long) * 4 &&
            ((unsigned long)d % sizeof(unsigned long)) == 0 &&
            ((unsigned long)s % sizeof(unsigned long)) == 0) {
            unsigned long *ld = (unsigned long *)d;
            const unsigned long *ls = (const unsigned long *)s;
            
            while (n >= sizeof(unsigned long)) {
                *ld++ = *ls++;
                n -= sizeof(unsigned long);
            }
            
            d = (unsigned char *)ld;
            s = (const unsigned char *)ls;
        }
        
        while (n-- > 0) {
            *d++ = *s++;
        }
    } else {
        // Overlap detected - copy backward (dest is after src)
        d += n;
        s += n;
        while (n-- > 0) {
            *--d = *--s;
        }
    }
    
    return dest;
}

// Copy n bytes from src to dest (optimized, word-aligned)
void *memcpy(void *dest, const void *src, size_t n) {
    // Null pointer checks
    if (!dest || !src) {
        return dest;
    }
    
    // Zero-size check
    if (n == 0) {
        return dest;
    }
    
    // Bounds check
    if (n > MAX_STRING_LENGTH) {
        return dest; // Refuse to copy excessive amounts
    }
    
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;
    
    // Overlap detection - memcpy should not be used with overlapping regions
    // Check if regions overlap
    if ((d >= s && d < s + n) || (s >= d && s < d + n)) {
        if (d != s) {
            // Regions overlap! Use memmove for safety
            return memmove(dest, src, n);
        }
    }
    
    // Word-aligned copy for large blocks (if both aligned)
    if (n >= sizeof(unsigned long) * 4 &&
        ((unsigned long)d % sizeof(unsigned long)) == 0 &&
        ((unsigned long)s % sizeof(unsigned long)) == 0) {
        unsigned long *ld = (unsigned long *)d;
        const unsigned long *ls = (const unsigned long *)s;
        
        while (n >= sizeof(unsigned long)) {
            *ld++ = *ls++;
            n -= sizeof(unsigned long);
        }
        
        d = (unsigned char *)ld;
        s = (const unsigned char *)ls;
    }
    
    // Copy remaining bytes
    while (n-- > 0) {
        *d++ = *s++;
    }
    
    return dest;
}

// Set n bytes to value c (optimized, word-aligned)
void *memset(void *s, int c, size_t n) {
    // Null pointer check
    if (!s) {
        return s;
    }
    
    // Zero-size check
    if (n == 0) {
        return s;
    }
    
    // Bounds check
    if (n > MAX_STRING_LENGTH) {
        return s; // Refuse to set excessive amounts
    }
    
    unsigned char *p = (unsigned char *)s;
    unsigned char uc = (unsigned char)c;
    
    // Word-aligned fill for large blocks (if aligned)
    if (n >= sizeof(unsigned long) * 4 &&
        ((unsigned long)p % sizeof(unsigned long)) == 0) {
        unsigned long pattern = uc;
        for (size_t i = 1; i < sizeof(unsigned long); i++) {
            pattern = (pattern << 8) | uc;
        }
        
        unsigned long *lp = (unsigned long *)p;
        
        while (n >= sizeof(unsigned long)) {
            *lp++ = pattern;
            n -= sizeof(unsigned long);
        }
        
        p = (unsigned char *)lp;
    }
    
    // Fill remaining bytes
    while (n-- > 0) {
        *p++ = uc;
    }
    
    return s;
}

// Compare n bytes of two memory regions with safety checks
int memcmp(const void *s1, const void *s2, size_t n) {
    // Null pointer protection
    if (!s1 || !s2) {
        if (s1 == s2) return 0;
        return s1 ? 1 : -1;
    }
    
    // Bounds check
    if (n == 0) return 0;
    if (n > MAX_STRING_LENGTH) n = MAX_STRING_LENGTH;
    
    const unsigned char *p1 = (const unsigned char *)s1;
    const unsigned char *p2 = (const unsigned char *)s2;
    
    while (n-- > 0) {
        if (*p1 != *p2) {
            return *p1 - *p2;
        }
        p1++;
        p2++;
    }
    return 0;
}

// Simple snprintf implementation - supports %d, %u, %s, %c, %x
int snprintf(char *str, size_t size, const char *format, ...) {
    if (size == 0) {
        return 0;
    }

    char *out = str;
    size_t remaining = size - 1; // Reserve space for null terminator
    const char *fmt = format;
    __builtin_va_list args;
    __builtin_va_start(args, format);

    while (*fmt && remaining > 0) {
        if (*fmt == '%' && *(fmt + 1) != '\0') {
            fmt++;
            switch (*fmt) {
                case 'd': {
                    int val = __builtin_va_arg(args, int);
                    // Simple itoa-like conversion
                    int num = val;
                    if (num < 0) {
                        if (remaining > 0) {
                            *out++ = '-';
                            remaining--;
                        }
                        num = -num;
                    }
                    if (num == 0) {
                        if (remaining > 0) {
                            *out++ = '0';
                            remaining--;
                        }
                    } else {
                        int divisor = 1;
                        while (num / divisor >= 10) {
                            divisor *= 10;
                        }
                        while (divisor >= 1) {
                            char digit = '0' + (num / divisor);
                            if (remaining > 0) {
                                *out++ = digit;
                                remaining--;
                            }
                            num %= divisor;
                            divisor /= 10;
                        }
                    }
                    break;
                }
                case 'u': {
                    unsigned int num = __builtin_va_arg(args, unsigned int);
                    if (num == 0) {
                        if (remaining > 0) {
                            *out++ = '0';
                            remaining--;
                        }
                    } else {
                        unsigned int divisor = 1;
                        while (num / divisor >= 10) {
                            divisor *= 10;
                        }
                        while (divisor >= 1) {
                            char digit = '0' + (num / divisor);
                            if (remaining > 0) {
                                *out++ = digit;
                                remaining--;
                            }
                            num %= divisor;
                            divisor /= 10;
                        }
                    }
                    break;
                }
                case 's': {
                    const char *str_arg = __builtin_va_arg(args, const char *);
                    while (*str_arg && remaining > 0) {
                        *out++ = *str_arg++;
                        remaining--;
                    }
                    break;
                }
                case 'c': {
                    char c = (char)__builtin_va_arg(args, int);
                    if (remaining > 0) {
                        *out++ = c;
                        remaining--;
                    }
                    break;
                }
                case 'x': {
                    unsigned int val = __builtin_va_arg(args, unsigned int);
                    const char *hex = "0123456789abcdef";
                    int started = 0;
                    for (int i = 0; i < 8; i++) {
                        unsigned char digit = (val >> (28 - i * 4)) & 0xF;
                        if (digit || started || i == 7) {
                            if (remaining > 0) {
                                *out++ = hex[digit];
                                remaining--;
                            }
                            started = 1;
                        }
                    }
                    break;
                }
                case '%':
                    if (remaining > 0) {
                        *out++ = '%';
                        remaining--;
                    }
                    break;
                default:
                    if (remaining > 0) {
                        *out++ = *fmt;
                        remaining--;
                    }
            }
            fmt++;
        } else if (*fmt == '\\' && *(fmt + 1) == 'n') {
            if (remaining > 0) {
                *out++ = '\n';
                remaining--;
            }
            fmt += 2;
        } else {
            if (remaining > 0) {
                *out++ = *fmt;
                remaining--;
            }
            fmt++;
        }
    }

    __builtin_va_end(args);
    *out = '\0';
    return out - str;
}
