/*
 * PROJECT:     ReactOS Native WiFi - RSNA (WPA2-PSK) supplicant
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Key derivation: PBKDF2-HMAC-SHA1 (RFC 2898) and the
 *              IEEE 802.11i PRF (PRF-n) built on HMAC-SHA1.
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _RSNA_KDF_H_
#define _RSNA_KDF_H_

#include "rsna_types.h"

/*
 * PBKDF2-HMAC-SHA1 (RFC 2898 / PKCS#5 v2.0).  For WPA2-PSK:
 *   PMK = PBKDF2(HMAC-SHA1, passphrase, SSID, 4096, 256 bits)
 * Returns 0 on success, non-zero on a parameter error.
 */
int RsnaPbkdf2Sha1(const rsna_u8 *password, rsna_size passwordLen,
                   const rsna_u8 *salt, rsna_size saltLen,
                   rsna_u32 iterations,
                   rsna_u8 *out, rsna_size outLen);

/*
 * IEEE 802.11i PRF (clause 8.5.1.1).  Produces 'bits' bits of output by
 * iterating HMAC-SHA1(key, label || 0x00 || data || counter).
 * Common widths: PRF-384 (CCMP PTK = 48 bytes), PRF-512 (TKIP PTK = 64 bytes).
 */
void RsnaPrf(const rsna_u8 *key, rsna_size keyLen,
             const char *label,
             const rsna_u8 *data, rsna_size dataLen,
             rsna_u8 *out, rsna_size bits);

#endif /* _RSNA_KDF_H_ */
