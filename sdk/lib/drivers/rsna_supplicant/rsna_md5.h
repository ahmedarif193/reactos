/*
 * PROJECT:     ReactOS Native WiFi - RSNA (WPA2-PSK) supplicant
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     MD5 (RFC 1321) - cleanroom implementation
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 *
 * Used by the legacy TKIP key-MIC variant (HMAC-MD5) of the EAPOL
 * 4-Way Handshake.
 */

#ifndef _RSNA_MD5_H_
#define _RSNA_MD5_H_

#include "rsna_types.h"

#define RSNA_MD5_DIGEST_LEN  16
#define RSNA_MD5_BLOCK_LEN   64

typedef struct _RSNA_MD5_CTX
{
    rsna_u32 state[4];
    rsna_u64 count;                 /* total bytes hashed */
    rsna_u8  buffer[RSNA_MD5_BLOCK_LEN];
} RSNA_MD5_CTX;

void RsnaMd5Init(RSNA_MD5_CTX *ctx);
void RsnaMd5Update(RSNA_MD5_CTX *ctx, const void *data, rsna_size len);
void RsnaMd5Final(RSNA_MD5_CTX *ctx, rsna_u8 digest[RSNA_MD5_DIGEST_LEN]);
void RsnaMd5(const void *data, rsna_size len, rsna_u8 digest[RSNA_MD5_DIGEST_LEN]);

#endif /* _RSNA_MD5_H_ */
