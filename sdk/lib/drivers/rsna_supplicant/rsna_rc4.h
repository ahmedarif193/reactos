/*
 * PROJECT:     ReactOS Native WiFi - RSNA (WPA2-PSK) supplicant
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     RC4 stream cipher - cleanroom implementation.
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 *
 * Used only for the legacy TKIP key-descriptor variant of EAPOL-Key, where
 * the Key Data field is RC4-encrypted with key = (Key IV || EAPOL-Key KEK)
 * and the first 256 keystream bytes are discarded (IEEE 802.11i 8.5.2).
 */

#ifndef _RSNA_RC4_H_
#define _RSNA_RC4_H_

#include "rsna_types.h"

typedef struct _RSNA_RC4_CTX
{
    rsna_u8 s[256];
    rsna_u8 i;
    rsna_u8 j;
} RSNA_RC4_CTX;

void RsnaRc4Init(RSNA_RC4_CTX *ctx, const rsna_u8 *key, rsna_size keyLen);

/* Discard 'count' keystream bytes (TKIP requires skipping the first 256). */
void RsnaRc4Skip(RSNA_RC4_CTX *ctx, rsna_size count);

/* XOR 'len' bytes of keystream into out (in/out may be the same buffer). */
void RsnaRc4Crypt(RSNA_RC4_CTX *ctx, const rsna_u8 *in, rsna_u8 *out, rsna_size len);

#endif /* _RSNA_RC4_H_ */
