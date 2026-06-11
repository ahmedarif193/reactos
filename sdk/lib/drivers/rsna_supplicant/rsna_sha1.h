/*
 * PROJECT:     ReactOS Native WiFi - RSNA (WPA2-PSK) supplicant
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     SHA-1 (FIPS 180-4) - cleanroom implementation
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _RSNA_SHA1_H_
#define _RSNA_SHA1_H_

#include "rsna_types.h"

#define RSNA_SHA1_DIGEST_LEN  20
#define RSNA_SHA1_BLOCK_LEN   64

typedef struct _RSNA_SHA1_CTX
{
    rsna_u32 state[5];
    rsna_u64 count;                 /* total bytes hashed */
    rsna_u8  buffer[RSNA_SHA1_BLOCK_LEN];
} RSNA_SHA1_CTX;

void RsnaSha1Init(RSNA_SHA1_CTX *ctx);
void RsnaSha1Update(RSNA_SHA1_CTX *ctx, const void *data, rsna_size len);
void RsnaSha1Final(RSNA_SHA1_CTX *ctx, rsna_u8 digest[RSNA_SHA1_DIGEST_LEN]);

/* One-shot helper. */
void RsnaSha1(const void *data, rsna_size len, rsna_u8 digest[RSNA_SHA1_DIGEST_LEN]);

#endif /* _RSNA_SHA1_H_ */
