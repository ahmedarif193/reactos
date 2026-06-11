/*
 * PROJECT:     ReactOS Native WiFi - RSNA (WPA2-PSK) supplicant
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     AES Key Wrap / Unwrap (RFC 3394) - cleanroom implementation.
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 *
 * Decrypts the Key Data field of EAPOL-Key message 3 (CCMP descriptor);
 * the KEK (PTK bytes 16..31) is the wrapping key.
 */

#ifndef _RSNA_KEYWRAP_H_
#define _RSNA_KEYWRAP_H_

#include "rsna_types.h"

/*
 * AES Key Wrap (RFC 3394 section 2.2.1).
 *   kek/kekBits : key-encryption key (128/192/256 bits)
 *   in/n        : plaintext, n 64-bit blocks (in length = n*8 bytes)
 *   out         : ciphertext, (n+1)*8 bytes
 * Returns 0 on success.
 */
int RsnaAesWrap(const rsna_u8 *kek, int kekBits,
                const rsna_u8 *in, rsna_size n,
                rsna_u8 *out);

/*
 * AES Key Unwrap (RFC 3394 section 2.2.2).
 *   in/n  : ciphertext, (n+1) 64-bit blocks ((n+1)*8 bytes)
 *   out   : plaintext, n*8 bytes
 * Returns 0 on success (integrity check A == A6A6A6A6A6A6A6A6 passed),
 * non-zero if the integrity check fails (bad KEK / corrupted data).
 */
int RsnaAesUnwrap(const rsna_u8 *kek, int kekBits,
                  const rsna_u8 *in, rsna_size n,
                  rsna_u8 *out);

#endif /* _RSNA_KEYWRAP_H_ */
