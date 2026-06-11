/*
 * PROJECT:     ReactOS Native WiFi - RSNA (WPA2-PSK) supplicant
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     HMAC-SHA1 and HMAC-MD5 (RFC 2104) - cleanroom implementation
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "rsna_hmac.h"

/* ------------------------------------------------------------------ */
/* HMAC-SHA1                                                          */
/* ------------------------------------------------------------------ */

void RsnaHmacSha1Vector(const rsna_u8 *key, rsna_size keyLen,
                        rsna_size numElem,
                        const rsna_u8 *const *addr, const rsna_size *len,
                        rsna_u8 mac[RSNA_SHA1_DIGEST_LEN])
{
    RSNA_SHA1_CTX ctx;
    rsna_u8 k_pad[RSNA_SHA1_BLOCK_LEN];
    rsna_u8 tk[RSNA_SHA1_DIGEST_LEN];
    rsna_u8 inner[RSNA_SHA1_DIGEST_LEN];
    rsna_size i;

    /* If the key is longer than the block size, hash it down. */
    if (keyLen > RSNA_SHA1_BLOCK_LEN)
    {
        RsnaSha1(key, keyLen, tk);
        key = tk;
        keyLen = RSNA_SHA1_DIGEST_LEN;
    }

    /* Inner pad. */
    for (i = 0; i < RSNA_SHA1_BLOCK_LEN; i++)
        k_pad[i] = 0x36;
    for (i = 0; i < keyLen; i++)
        k_pad[i] ^= key[i];

    RsnaSha1Init(&ctx);
    RsnaSha1Update(&ctx, k_pad, RSNA_SHA1_BLOCK_LEN);
    for (i = 0; i < numElem; i++)
        RsnaSha1Update(&ctx, addr[i], len[i]);
    RsnaSha1Final(&ctx, inner);

    /* Outer pad. */
    for (i = 0; i < RSNA_SHA1_BLOCK_LEN; i++)
        k_pad[i] = 0x5c;
    for (i = 0; i < keyLen; i++)
        k_pad[i] ^= key[i];

    RsnaSha1Init(&ctx);
    RsnaSha1Update(&ctx, k_pad, RSNA_SHA1_BLOCK_LEN);
    RsnaSha1Update(&ctx, inner, RSNA_SHA1_DIGEST_LEN);
    RsnaSha1Final(&ctx, mac);
}

void RsnaHmacSha1(const rsna_u8 *key, rsna_size keyLen,
                  const rsna_u8 *data, rsna_size dataLen,
                  rsna_u8 mac[RSNA_SHA1_DIGEST_LEN])
{
    const rsna_u8 *addr[1];
    rsna_size len[1];

    addr[0] = data;
    len[0] = dataLen;
    RsnaHmacSha1Vector(key, keyLen, 1, addr, len, mac);
}

/* ------------------------------------------------------------------ */
/* HMAC-MD5                                                           */
/* ------------------------------------------------------------------ */

void RsnaHmacMd5(const rsna_u8 *key, rsna_size keyLen,
                 const rsna_u8 *data, rsna_size dataLen,
                 rsna_u8 mac[RSNA_MD5_DIGEST_LEN])
{
    RSNA_MD5_CTX ctx;
    rsna_u8 k_pad[RSNA_MD5_BLOCK_LEN];
    rsna_u8 tk[RSNA_MD5_DIGEST_LEN];
    rsna_u8 inner[RSNA_MD5_DIGEST_LEN];
    rsna_size i;

    if (keyLen > RSNA_MD5_BLOCK_LEN)
    {
        RsnaMd5(key, keyLen, tk);
        key = tk;
        keyLen = RSNA_MD5_DIGEST_LEN;
    }

    for (i = 0; i < RSNA_MD5_BLOCK_LEN; i++)
        k_pad[i] = 0x36;
    for (i = 0; i < keyLen; i++)
        k_pad[i] ^= key[i];

    RsnaMd5Init(&ctx);
    RsnaMd5Update(&ctx, k_pad, RSNA_MD5_BLOCK_LEN);
    RsnaMd5Update(&ctx, data, dataLen);
    RsnaMd5Final(&ctx, inner);

    for (i = 0; i < RSNA_MD5_BLOCK_LEN; i++)
        k_pad[i] = 0x5c;
    for (i = 0; i < keyLen; i++)
        k_pad[i] ^= key[i];

    RsnaMd5Init(&ctx);
    RsnaMd5Update(&ctx, k_pad, RSNA_MD5_BLOCK_LEN);
    RsnaMd5Update(&ctx, inner, RSNA_MD5_DIGEST_LEN);
    RsnaMd5Final(&ctx, mac);
}
