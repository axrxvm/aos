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

// Minimal implementation of strcmp
int strcmp(const char *s1, const char *s2) {
    while (*s1 != '\0' && *s1 == *s2) {
        s1++;
        s2++;
    }
    return (*(unsigned char *)s1) - (*(unsigned char *)s2);
}

// Minimal implementation of strncmp
int strncmp(const char *s1, const char *s2, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (s1[i] != s2[i] || s1[i] == '\0' || s2[i] == '\0') {
            return (unsigned char)s1[i] - (unsigned char)s2[i];
        }
    }
    return 0;
}

// Calculate length of string
size_t strlen(const char *s) {
    size_t len = 0;
    while (s[len] != '\0') {
        len++;
    }
    return len;
}

// Copy string from src to dest
char *strcpy(char *dest, const char *src) {
    char *d = dest;
    while ((*d++ = *src++) != '\0') {
        // Copy until null terminator
    }
    return dest;
}

// Copy at most n characters from src to dest
char *strncpy(char *dest, const char *src, size_t n) {
    size_t i;
    for (i = 0; i < n && src[i] != '\0'; i++) {
        dest[i] = src[i];
    }
    for (; i < n; i++) {
        dest[i] = '\0';
    }
    return dest;
}

// Concatenate src to dest
char *strcat(char *dest, const char *src) {
    char *d = dest;
    // Find end of dest
    while (*d != '\0') {
        d++;
    }
    // Copy src to end of dest
    while ((*d++ = *src++) != '\0') {
        // Copy until null terminator
    }
    return dest;
}

// Concatenate at most n characters from src to dest
char *strncat(char *dest, const char *src, size_t n) {
    size_t dest_len = strlen(dest);
    size_t i;
    
    for (i = 0; i < n && src[i] != '\0'; i++) {
        dest[dest_len + i] = src[i];
    }
    dest[dest_len + i] = '\0';
    
    return dest;
}

// Find first occurrence of character in string
char *strchr(const char *s, int c) {
    while (*s != '\0') {
        if (*s == (char)c) {
            return (char *)s;
        }
        s++;
    }
    if ((char)c == '\0') {
        return (char *)s;
    }
    return NULL;
}

// Find last occurrence of character in string
char *strrchr(const char *s, int c) {
    const char *last = NULL;
    while (*s != '\0') {
        if (*s == (char)c) {
            last = s;
        }
        s++;
    }
    if ((char)c == '\0') {
        return (char *)s;
    }
    return (char *)last;
}

// Find substring in string
char *strstr(const char *haystack, const char *needle) {
    if (!*needle) {
        return (char *)haystack;
    }
    
    while (*haystack) {
        const char *h = haystack;
        const char *n = needle;
        
        while (*h && *n && *h == *n) {
            h++;
            n++;
        }
        
        if (!*n) {
            return (char *)haystack;
        }
        
        haystack++;
    }
    
    return NULL;
}

// Copy n bytes from src to dest, handling overlapping memory (robust)
void *memmove(void *dest, const void *src, size_t n) {
    // Null pointer checks
    if (!dest || !src || n == 0) {
        return dest;
    }
    
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;
    
    // Check for same location
    if (d == s) {
        return dest;
    }
    
    if (d < s) {
        // Copy forward (no overlap or dest is before src)
        // Use word-aligned copy if possible
        if (n >= 16 && ((unsigned long)d % 4) == 0 && ((unsigned long)s % 4) == 0) {
            unsigned long *ld = (unsigned long *)d;
            const unsigned long *ls = (const unsigned long *)s;
            
            while (n >= 4) {
                *ld++ = *ls++;
                n -= 4;
            }
            
            d = (unsigned char *)ld;
            s = (const unsigned char *)ls;
        }
        
        while (n-- > 0) {
            *d++ = *s++;
        }
    } else {
        // Copy backward (dest is after src, possible overlap)
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
    if (!dest || !src || n == 0) {
        return dest;
    }
    
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;
    
    // Word-aligned copy for large blocks (if both aligned)
    if (n >= 16 && ((unsigned long)d % 4) == 0 && ((unsigned long)s % 4) == 0) {
        unsigned long *ld = (unsigned long *)d;
        const unsigned long *ls = (const unsigned long *)s;
        
        while (n >= 4) {
            *ld++ = *ls++;
            n -= 4;
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
    if (!s || n == 0) {
        return s;
    }
    
    unsigned char *p = (unsigned char *)s;
    unsigned char uc = (unsigned char)c;
    
    // Word-aligned fill for large blocks (if aligned)
    if (n >= 16 && ((unsigned long)p % 4) == 0) {
        unsigned long pattern = uc;
        pattern |= pattern << 8;
        pattern |= pattern << 16;
        
        unsigned long *lp = (unsigned long *)p;
        
        while (n >= 4) {
            *lp++ = pattern;
            n -= 4;
        }
        
        p = (unsigned char *)lp;
    }
    
    // Fill remaining bytes
    while (n-- > 0) {
        *p++ = uc;
    }
    
    return s;
}

// Compare n bytes of two memory regions
int memcmp(const void *s1, const void *s2, size_t n) {
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

// Simple snprintf implementation - supports %d, %s, %c, %x
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
