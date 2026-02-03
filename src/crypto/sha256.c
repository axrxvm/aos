/*
 * === AOS HEADER BEGIN ===
 * ./src/crypto/sha256.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.8.5
 * === AOS HEADER END ===
 */


#include <crypto/sha256.h>
#include <string.h>

// SHA-256 constants (first 32 bits of fractional parts of cube roots of first 64 primes) (ik its too much meth)
static const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

// Bitwise rotation macros
#define ROTR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
#define SHR(x, n) ((x) >> (n))

// SHA-256 functions
#define CH(x, y, z) (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define SIGMA0(x) (ROTR(x, 2) ^ ROTR(x, 13) ^ ROTR(x, 22))
#define SIGMA1(x) (ROTR(x, 6) ^ ROTR(x, 11) ^ ROTR(x, 25))
#define sigma0(x) (ROTR(x, 7) ^ ROTR(x, 18) ^ SHR(x, 3))
#define sigma1(x) (ROTR(x, 17) ^ ROTR(x, 19) ^ SHR(x, 10))

// Helper function to convert uint32_t from big-endian to host byte order
static inline uint32_t be32_to_cpu(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | 
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

// Helper function to convert uint32_t from host byte order to big-endian
static inline void cpu_to_be32(uint8_t* p, uint32_t v) {
    p[0] = (v >> 24) & 0xFF;
    p[1] = (v >> 16) & 0xFF;
    p[2] = (v >> 8) & 0xFF;
    p[3] = v & 0xFF;
}

// Helper function to convert uint64_t from host byte order to big-endian
static inline void cpu_to_be64(uint8_t* p, uint64_t v) {
    p[0] = (v >> 56) & 0xFF;
    p[1] = (v >> 48) & 0xFF;
    p[2] = (v >> 40) & 0xFF;
    p[3] = (v >> 32) & 0xFF;
    p[4] = (v >> 24) & 0xFF;
    p[5] = (v >> 16) & 0xFF;
    p[6] = (v >> 8) & 0xFF;
    p[7] = v & 0xFF;
}

// Process a single 512-bit block
static void sha256_transform(sha256_ctx_t* ctx, const uint8_t* data) {
    uint32_t W[64];
    uint32_t a, b, c, d, e, f, g, h;
    uint32_t T1, T2;
    int i;
    
    // Prepare message schedule
    for (i = 0; i < 16; i++) {
        W[i] = be32_to_cpu(data + i * 4);
    }
    
    for (i = 16; i < 64; i++) {
        W[i] = sigma1(W[i - 2]) + W[i - 7] + sigma0(W[i - 15]) + W[i - 16];
    }
    
    // Initialize working variables
    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];
    e = ctx->state[4];
    f = ctx->state[5];
    g = ctx->state[6];
    h = ctx->state[7];
    
    // Main loop
    for (i = 0; i < 64; i++) {
        T1 = h + SIGMA1(e) + CH(e, f, g) + K[i] + W[i];
        T2 = SIGMA0(a) + MAJ(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + T1;
        d = c;
        c = b;
        b = a;
        a = T1 + T2;
    }
    
    // Add working variables back to state
    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

void sha256_init(sha256_ctx_t* ctx) {
    if (!ctx) return;
    
    // Initialize state with SHA-256 initial hash values
    // (first 32 bits of fractional parts of square roots of first 8 primes)
    ctx->state[0] = 0x6a09e667;
    ctx->state[1] = 0xbb67ae85;
    ctx->state[2] = 0x3c6ef372;
    ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f;
    ctx->state[5] = 0x9b05688c;
    ctx->state[6] = 0x1f83d9ab;
    ctx->state[7] = 0x5be0cd19;
    
    ctx->count = 0;
    memset(ctx->buffer, 0, sizeof(ctx->buffer));
}

void sha256_update(sha256_ctx_t* ctx, const uint8_t* data, size_t len) {
    if (!ctx || !data) return;
    
    size_t buffer_space = SHA256_BLOCK_SIZE - (ctx->count % SHA256_BLOCK_SIZE);
    
    ctx->count += len * 8; // Convert to bits
    
    // If we have data in buffer and new data fills a block
    if (buffer_space <= len) {
        // Fill buffer and process
        size_t buffer_offset = SHA256_BLOCK_SIZE - buffer_space;
        memcpy(ctx->buffer + buffer_offset, data, buffer_space);
        sha256_transform(ctx, ctx->buffer);
        
        data += buffer_space;
        len -= buffer_space;
        
        // Process complete blocks
        while (len >= SHA256_BLOCK_SIZE) {
            sha256_transform(ctx, data);
            data += SHA256_BLOCK_SIZE;
            len -= SHA256_BLOCK_SIZE;
        }
        
        // Store remaining data
        memcpy(ctx->buffer, data, len);
    } else {
        // Just store data in buffer
        size_t buffer_offset = SHA256_BLOCK_SIZE - buffer_space;
        memcpy(ctx->buffer + buffer_offset, data, len);
    }
}

void sha256_final(sha256_ctx_t* ctx, uint8_t* digest) {
    if (!ctx || !digest) return;
    
    uint32_t i;
    uint32_t buffer_offset = (ctx->count / 8) % SHA256_BLOCK_SIZE;
    
    // Add padding bit (0x80)
    ctx->buffer[buffer_offset++] = 0x80;
    
    // If not enough space for length, pad and process block
    if (buffer_offset > 56) {
        memset(ctx->buffer + buffer_offset, 0, SHA256_BLOCK_SIZE - buffer_offset);
        sha256_transform(ctx, ctx->buffer);
        buffer_offset = 0;
    }
    
    // Zero-pad up to length field
    memset(ctx->buffer + buffer_offset, 0, 56 - buffer_offset);
    
    // Append length in bits as 64-bit big-endian
    cpu_to_be64(ctx->buffer + 56, ctx->count);
    
    // Process final block
    sha256_transform(ctx, ctx->buffer);
    
    // Output hash in big-endian
    for (i = 0; i < 8; i++) {
        cpu_to_be32(digest + i * 4, ctx->state[i]);
    }
    
    // Clear context for security
    memset(ctx, 0, sizeof(sha256_ctx_t));
}

void sha256_hash(const uint8_t* data, size_t len, uint8_t* digest) {
    sha256_ctx_t ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, data, len);
    sha256_final(&ctx, digest);
}

void sha256_to_hex(const uint8_t* digest, char* hex_out) {
    if (!digest || !hex_out) return;
    
    static const char hex_chars[] = "0123456789abcdef";
    
    for (int i = 0; i < SHA256_DIGEST_SIZE; i++) {
        hex_out[i * 2] = hex_chars[(digest[i] >> 4) & 0x0F];
        hex_out[i * 2 + 1] = hex_chars[digest[i] & 0x0F];
    }
    hex_out[SHA256_DIGEST_SIZE * 2] = '\0';
}
