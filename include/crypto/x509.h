/*
 * === AOS HEADER BEGIN ===
 * include/crypto/x509.h
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */

#ifndef X509_H
#define X509_H

#include <arch_types.h>
#include <crypto/rsa.h>

// Simplified X.509 certificate parser (for extracting RSA public key only)

// Parse X.509 certificate and extract RSA public key
// Returns 0 on success, -1 on error
int x509_parse_certificate(const uint8_t* cert_data, uint32_t cert_len,
                           rsa_public_key_t* public_key);

// ASN.1/DER helper functions
int asn1_get_tag_length(const uint8_t* data, uint32_t* tag, uint32_t* length, 
                        uint32_t* header_len);
int asn1_find_sequence(const uint8_t* data, uint32_t data_len, 
                       const uint8_t** seq_start, uint32_t* seq_len);
int asn1_parse_integer(const uint8_t* data, uint32_t data_len,
                       const uint8_t** int_data, uint32_t* int_len);

#endif // X509_H
