/*
 * PROJECT:     ReactOS Native WiFi - RSNA (WPA2-PSK) supplicant
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     AES block cipher (FIPS 197) - cleanroom implementation.
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 *
 * Supports 128/192/256-bit keys.  Only single-block ECB encrypt/decrypt is
 * exposed; higher-level modes (AES key wrap) are built on top of this.
 */

#ifndef _RSNA_AES_H_
#define _RSNA_AES_H_

#include "rsna_types.h"

#define RSNA_AES_BLOCK_SIZE  16
#define RSNA_AES_MAX_ROUNDS  14

typedef struct _RSNA_AES_KEY
{
    rsna_u32 rk[4 * (RSNA_AES_MAX_ROUNDS + 1)];  /* expanded round keys */
    int      rounds;
} RSNA_AES_KEY;

/*
 * Expand a raw key for encryption.  keyBits must be 128, 192 or 256.
 * Returns 0 on success, non-zero on invalid key length.
 */
int RsnaAesSetEncryptKey(RSNA_AES_KEY *key, const rsna_u8 *userKey, int keyBits);

/* Expand a raw key for decryption (inverse cipher schedule). */
int RsnaAesSetDecryptKey(RSNA_AES_KEY *key, const rsna_u8 *userKey, int keyBits);

/* Single 16-byte block ECB encrypt / decrypt. in/out may overlap fully. */
void RsnaAesEncrypt(const RSNA_AES_KEY *key,
                    const rsna_u8 in[RSNA_AES_BLOCK_SIZE],
                    rsna_u8 out[RSNA_AES_BLOCK_SIZE]);

void RsnaAesDecrypt(const RSNA_AES_KEY *key,
                    const rsna_u8 in[RSNA_AES_BLOCK_SIZE],
                    rsna_u8 out[RSNA_AES_BLOCK_SIZE]);

#endif /* _RSNA_AES_H_ */
