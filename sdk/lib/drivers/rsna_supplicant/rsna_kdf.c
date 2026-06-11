/*
 * PROJECT:     ReactOS Native WiFi - RSNA (WPA2-PSK) supplicant
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     PBKDF2-HMAC-SHA1 (RFC 2898) and IEEE 802.11i PRF.
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "rsna_kdf.h"
#include "rsna_hmac.h"

static rsna_size RsnaStrLen(const char *s)
{
    rsna_size n = 0;
    while (s[n] != '\0')
        n++;
    return n;
}

/* ------------------------------------------------------------------ */
/* PBKDF2-HMAC-SHA1 (RFC 2898 section 5.2)                            */
/* ------------------------------------------------------------------ */

/*
 * Compute one PBKDF2 "block" F(P, S, c, i) and XOR it into 'digest'.
 * F = U_1 ^ U_2 ^ ... ^ U_c
 *   U_1 = PRF(P, S || INT(i))      (INT is 4-byte big-endian block index)
 *   U_j = PRF(P, U_{j-1})
 */
static void Pbkdf2F(const rsna_u8 *password, rsna_size passwordLen,
                    const rsna_u8 *salt, rsna_size saltLen,
                    rsna_u32 iterations, rsna_u32 blockIndex,
                    rsna_u8 digest[RSNA_SHA1_DIGEST_LEN])
{
    rsna_u8 u[RSNA_SHA1_DIGEST_LEN];
    rsna_u8 counter[4];
    const rsna_u8 *addr[2];
    rsna_size len[2];
    rsna_u32 j;
    int k;

    counter[0] = (rsna_u8)((blockIndex >> 24) & 0xFF);
    counter[1] = (rsna_u8)((blockIndex >> 16) & 0xFF);
    counter[2] = (rsna_u8)((blockIndex >> 8) & 0xFF);
    counter[3] = (rsna_u8)(blockIndex & 0xFF);

    /* U_1 = HMAC(password, salt || INT(blockIndex)) */
    addr[0] = salt;
    len[0] = saltLen;
    addr[1] = counter;
    len[1] = sizeof(counter);
    RsnaHmacSha1Vector(password, passwordLen, 2, addr, len, u);

    for (k = 0; k < RSNA_SHA1_DIGEST_LEN; k++)
        digest[k] = u[k];

    /* U_2 .. U_c */
    for (j = 1; j < iterations; j++)
    {
        RsnaHmacSha1(password, passwordLen, u, RSNA_SHA1_DIGEST_LEN, u);
        for (k = 0; k < RSNA_SHA1_DIGEST_LEN; k++)
            digest[k] ^= u[k];
    }
}

int RsnaPbkdf2Sha1(const rsna_u8 *password, rsna_size passwordLen,
                   const rsna_u8 *salt, rsna_size saltLen,
                   rsna_u32 iterations,
                   rsna_u8 *out, rsna_size outLen)
{
    rsna_u8 digest[RSNA_SHA1_DIGEST_LEN];
    rsna_u32 blockIndex;
    rsna_size pos;

    if (out == RSNA_NULL || iterations == 0)
        return -1;

    blockIndex = 1;
    pos = 0;
    while (pos < outLen)
    {
        rsna_size take = outLen - pos;
        rsna_size k;

        if (take > RSNA_SHA1_DIGEST_LEN)
            take = RSNA_SHA1_DIGEST_LEN;

        Pbkdf2F(password, passwordLen, salt, saltLen, iterations, blockIndex, digest);

        for (k = 0; k < take; k++)
            out[pos + k] = digest[k];

        pos += take;
        blockIndex++;
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/* IEEE 802.11i PRF (clause 8.5.1.1)                                  */
/* ------------------------------------------------------------------ */

/*
 * PRF(K, A, B, Len):
 *   R = R || HMAC-SHA1(K, A || 0x00 || B || i)   for i = 0 .. ceil(Len/160)-1
 * A is the NUL-terminated label, B the binary data, i a single octet.
 */
void RsnaPrf(const rsna_u8 *key, rsna_size keyLen,
             const char *label,
             const rsna_u8 *data, rsna_size dataLen,
             rsna_u8 *out, rsna_size bits)
{
    rsna_u8 hash[RSNA_SHA1_DIGEST_LEN];
    rsna_u8 sep = 0x00;
    rsna_u8 counter;
    const rsna_u8 *addr[4];
    rsna_size len[4];
    rsna_size labelLen = RsnaStrLen(label);
    rsna_size outBytes = (bits + 7) / 8;
    rsna_size pos = 0;

    addr[0] = (const rsna_u8 *)label;
    len[0]  = labelLen;
    addr[1] = &sep;
    len[1]  = 1;
    addr[2] = data;
    len[2]  = dataLen;
    addr[3] = &counter;
    len[3]  = 1;

    counter = 0;
    while (pos < outBytes)
    {
        rsna_size take = outBytes - pos;
        rsna_size k;

        if (take > RSNA_SHA1_DIGEST_LEN)
            take = RSNA_SHA1_DIGEST_LEN;

        RsnaHmacSha1Vector(key, keyLen, 4, addr, len, hash);

        for (k = 0; k < take; k++)
            out[pos + k] = hash[k];

        pos += take;
        counter++;
    }
}
