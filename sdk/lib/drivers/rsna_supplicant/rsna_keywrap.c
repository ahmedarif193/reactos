/*
 * PROJECT:     ReactOS Native WiFi - RSNA (WPA2-PSK) supplicant
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     AES Key Wrap / Unwrap (RFC 3394) - cleanroom implementation.
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 *
 * Index-based variant of RFC 3394.  The counter t is XORed into the
 * A register big-endian.
 */

#include "rsna_keywrap.h"
#include "rsna_aes.h"

static const rsna_u8 g_DefaultIv[8] =
{
    0xA6, 0xA6, 0xA6, 0xA6, 0xA6, 0xA6, 0xA6, 0xA6
};

int RsnaAesWrap(const rsna_u8 *kek, int kekBits,
                const rsna_u8 *in, rsna_size n,
                rsna_u8 *out)
{
    RSNA_AES_KEY key;
    rsna_u8 a[8];
    rsna_u8 b[16];
    rsna_u8 *r;
    rsna_size i;
    int j, k;

    if (n == 0)
        return -1;
    if (RsnaAesSetEncryptKey(&key, kek, kekBits) != 0)
        return -1;

    /* Initialize A = IV, R[i] = P[i]. */
    for (k = 0; k < 8; k++)
        a[k] = g_DefaultIv[k];
    for (i = 0; i < n * 8; i++)
        out[8 + i] = in[i];

    for (j = 0; j <= 5; j++)
    {
        for (i = 1; i <= n; i++)
        {
            rsna_u64 t = (rsna_u64)(n * (rsna_size)j) + i;

            r = out + i * 8;

            /* B = AES(K, A | R[i]) */
            for (k = 0; k < 8; k++)
                b[k] = a[k];
            for (k = 0; k < 8; k++)
                b[8 + k] = r[k];
            RsnaAesEncrypt(&key, b, b);

            /* A = MSB64(B) ^ t */
            for (k = 0; k < 8; k++)
                a[k] = b[k];
            for (k = 0; k < 8; k++)
                a[7 - k] ^= (rsna_u8)((t >> (8 * k)) & 0xFF);

            /* R[i] = LSB64(B) */
            for (k = 0; k < 8; k++)
                r[k] = b[8 + k];
        }
    }

    for (k = 0; k < 8; k++)
        out[k] = a[k];

    return 0;
}

int RsnaAesUnwrap(const rsna_u8 *kek, int kekBits,
                  const rsna_u8 *in, rsna_size n,
                  rsna_u8 *out)
{
    RSNA_AES_KEY key;
    rsna_u8 a[8];
    rsna_u8 b[16];
    rsna_u8 *r;
    rsna_size i;
    int j, k;

    if (n == 0)
        return -1;
    /* Unwrap uses the AES inverse cipher. */
    if (RsnaAesSetDecryptKey(&key, kek, kekBits) != 0)
        return -1;

    /* A = C[0], R[i] = C[i]. */
    for (k = 0; k < 8; k++)
        a[k] = in[k];
    for (i = 0; i < n * 8; i++)
        out[i] = in[8 + i];

    for (j = 5; j >= 0; j--)
    {
        for (i = n; i >= 1; i--)
        {
            rsna_u64 t = (rsna_u64)(n * (rsna_size)j) + i;

            r = out + (i - 1) * 8;

            /* B = AES-1(K, (A ^ t) | R[i]) */
            for (k = 0; k < 8; k++)
                b[k] = a[k];
            for (k = 0; k < 8; k++)
                b[7 - k] ^= (rsna_u8)((t >> (8 * k)) & 0xFF);
            for (k = 0; k < 8; k++)
                b[8 + k] = r[k];
            RsnaAesDecrypt(&key, b, b);

            /* A = MSB64(B) */
            for (k = 0; k < 8; k++)
                a[k] = b[k];

            /* R[i] = LSB64(B) */
            for (k = 0; k < 8; k++)
                r[k] = b[8 + k];
        }
    }

    /* Integrity check: A must equal the default IV. */
    for (k = 0; k < 8; k++)
    {
        if (a[k] != g_DefaultIv[k])
            return -1;
    }

    return 0;
}
