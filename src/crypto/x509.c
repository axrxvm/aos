/*
 * === AOS HEADER BEGIN ===
 * src/crypto/x509.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */

#include <crypto/x509.h>
#include <crypto/rsa.h>
#include <string.h>
#include <serial.h>
#include <stdlib.h>
#include <vmm.h>

// ASN.1 Tags
#define ASN1_SEQUENCE           0x30
#define ASN1_INTEGER            0x02
#define ASN1_BIT_STRING         0x03
#define ASN1_OCTET_STRING       0x04
#define ASN1_NULL               0x05
#define ASN1_OID                0x06
#define ASN1_CONTEXT_SPECIFIC_0 0xA0
#define ASN1_CONTEXT_SPECIFIC_3 0xA3

// Get ASN.1 tag and length
int asn1_get_tag_length(const uint8_t* data, uint32_t* tag, uint32_t* length, 
                        uint32_t* header_len) {
    *tag = data[0];
    
    if (data[1] & 0x80) {
        // Long form length
        uint32_t num_octets = data[1] & 0x7F;
        if (num_octets > 4) return -1;
        
        *length = 0;
        for (uint32_t i = 0; i < num_octets; i++) {
            *length = (*length << 8) | data[2 + i];
        }
        *header_len = 2 + num_octets;
    } else {
        // Short form length
        *length = data[1];
        *header_len = 2;
    }
    
    return 0;
}

// Find a sequence in ASN.1 data
int asn1_find_sequence(const uint8_t* data, uint32_t data_len, 
                       const uint8_t** seq_start, uint32_t* seq_len) {
    if (data_len < 2) return -1;
    
    uint32_t tag, length, header_len;
    if (asn1_get_tag_length(data, &tag, &length, &header_len) < 0) {
        return -1;
    }
    
    if (tag != ASN1_SEQUENCE) {
        return -1;
    }
    
    *seq_start = data + header_len;
    *seq_len = length;
    return 0;
}

// Parse ASN.1 integer
int asn1_parse_integer(const uint8_t* data, uint32_t data_len,
                       const uint8_t** int_data, uint32_t* int_len) {
    if (data_len < 2) return -1;
    
    uint32_t tag, length, header_len;
    if (asn1_get_tag_length(data, &tag, &length, &header_len) < 0) {
        return -1;
    }
    
    if (tag != ASN1_INTEGER) {
        return -1;
    }
    
    *int_data = data + header_len;
    *int_len = length;
    
    // Skip leading zero byte if present (for positive numbers)
    if (length > 0 && (*int_data)[0] == 0x00) {
        (*int_data)++;
        (*int_len)--;
    }
    
    return 0;
}

// Parse X.509 certificate and extract RSA public key
// Simplified parser - only extracts RSA public key from SubjectPublicKeyInfo
int x509_parse_certificate(const uint8_t* cert_data, uint32_t cert_len,
                           rsa_public_key_t* public_key) {
    const uint8_t* ptr = cert_data;
    uint32_t remaining = cert_len;
    
    // Certificate ::= SEQUENCE
    const uint8_t* cert_seq;
    uint32_t cert_seq_len;
    if (asn1_find_sequence(ptr, remaining, &cert_seq, &cert_seq_len) < 0) {
        serial_puts("X509: Failed to parse certificate SEQUENCE\n");
        return -1;
    }
    ptr = cert_seq;
    remaining = cert_seq_len;
    
    // TBSCertificate ::= SEQUENCE
    const uint8_t* tbs_seq;
    uint32_t tbs_seq_len;
    if (asn1_find_sequence(ptr, remaining, &tbs_seq, &tbs_seq_len) < 0) {
        serial_puts("X509: Failed to parse TBSCertificate\n");
        return -1;
    }
    ptr = tbs_seq;
    remaining = tbs_seq_len;
    
    // Skip version (CONTEXT [0] EXPLICIT)
    if (ptr[0] == ASN1_CONTEXT_SPECIFIC_0) {
        uint32_t tag = 0, length = 0, header_len = 0;
        asn1_get_tag_length(ptr, &tag, &length, &header_len);
        ptr += header_len + length;
        remaining -= header_len + length;
    }
    
    // Skip serialNumber (INTEGER)
    if (ptr[0] == ASN1_INTEGER) {
        uint32_t tag = 0, length = 0, header_len = 0;
        asn1_get_tag_length(ptr, &tag, &length, &header_len);
        ptr += header_len + length;
        remaining -= header_len + length;
    }
    
    // Skip signature (SEQUENCE)
    if (ptr[0] == ASN1_SEQUENCE) {
        uint32_t tag = 0, length = 0, header_len = 0;
        asn1_get_tag_length(ptr, &tag, &length, &header_len);
        ptr += header_len + length;
        remaining -= header_len + length;
    }
    
    // Skip issuer (SEQUENCE)
    if (ptr[0] == ASN1_SEQUENCE) {
        uint32_t tag = 0, length = 0, header_len = 0;
        asn1_get_tag_length(ptr, &tag, &length, &header_len);
        ptr += header_len + length;
        remaining -= header_len + length;
    }
    
    // Skip validity (SEQUENCE)
    if (ptr[0] == ASN1_SEQUENCE) {
        uint32_t tag = 0, length = 0, header_len = 0;
        asn1_get_tag_length(ptr, &tag, &length, &header_len);
        ptr += header_len + length;
        remaining -= header_len + length;
    }
    
    // Skip subject (SEQUENCE)
    if (ptr[0] == ASN1_SEQUENCE) {
        uint32_t tag = 0, length = 0, header_len = 0;
        asn1_get_tag_length(ptr, &tag, &length, &header_len);
        ptr += header_len + length;
        remaining -= header_len + length;
    }
    
    // SubjectPublicKeyInfo ::= SEQUENCE
    const uint8_t* spki_seq;
    uint32_t spki_seq_len;
    if (asn1_find_sequence(ptr, remaining, &spki_seq, &spki_seq_len) < 0) {
        serial_puts("X509: Failed to parse SubjectPublicKeyInfo\n");
        return -1;
    }
    ptr = spki_seq;
    remaining = spki_seq_len;
    
    // Skip algorithm (SEQUENCE)
    if (ptr[0] == ASN1_SEQUENCE) {
        uint32_t tag = 0, length = 0, header_len = 0;
        asn1_get_tag_length(ptr, &tag, &length, &header_len);
        ptr += header_len + length;
        remaining -= header_len + length;
    }
    
    // SubjectPublicKey ::= BIT STRING
    if (ptr[0] != ASN1_BIT_STRING) {
        serial_puts("X509: Expected BIT STRING for public key\n");
        return -1;
    }
    
    uint32_t tag = 0, length = 0, header_len = 0;
    asn1_get_tag_length(ptr, &tag, &length, &header_len);
    ptr += header_len;
    
    // Skip unused bits byte
    (void)ptr[0];  // unused_bits - suppress warning
    ptr++;
    length--;
    
    // RSAPublicKey ::= SEQUENCE { modulus INTEGER, publicExponent INTEGER }
    const uint8_t* rsa_seq;
    uint32_t rsa_seq_len;
    if (asn1_find_sequence(ptr, length, &rsa_seq, &rsa_seq_len) < 0) {
        serial_puts("X509: Failed to parse RSA public key SEQUENCE\n");
        return -1;
    }
    ptr = rsa_seq;
    
    // Parse modulus
    const uint8_t* modulus_data;
    uint32_t modulus_len;
    if (asn1_parse_integer(ptr, rsa_seq_len, &modulus_data, &modulus_len) < 0) {
        serial_puts("X509: Failed to parse RSA modulus\n");
        return -1;
    }
    
    // Move to exponent
    asn1_get_tag_length(ptr, &tag, &length, &header_len);
    ptr += header_len + length;
    
    // Parse exponent
    const uint8_t* exponent_data;
    uint32_t exponent_len;
    if (asn1_parse_integer(ptr, rsa_seq_len - (ptr - rsa_seq), 
                          &exponent_data, &exponent_len) < 0) {
        serial_puts("X509: Failed to parse RSA exponent\n");
        return -1;
    }
    
    // Initialize RSA public key
    rsa_public_key_init(public_key, modulus_data, modulus_len, 
                       exponent_data, exponent_len);
    
    serial_puts("X509: Extracted RSA public key (");
    char msg[16];
    itoa(modulus_len * 8, msg, 10);
    serial_puts(msg);
    serial_puts("-bit)\n");
    
    return 0;
}
