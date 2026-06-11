/*
 * PROJECT:     ReactOS Native WiFi - RSNA (WPA2-PSK) supplicant
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     RC4 stream cipher - cleanroom implementation.
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "rsna_rc4.h"

void RsnaRc4Init(RSNA_RC4_CTX *ctx, const rsna_u8 *key, rsna_size keyLen)
{
    int i;
    rsna_u8 j;
    rsna_u8 t;

    for (i = 0; i < 256; i++)
        ctx->s[i] = (rsna_u8)i;

    j = 0;
    for (i = 0; i < 256; i++)
    {
        j = (rsna_u8)(j + ctx->s[i] + key[i % keyLen]);
        t = ctx->s[i];
        ctx->s[i] = ctx->s[j];
        ctx->s[j] = t;
    }

    ctx->i = 0;
    ctx->j = 0;
}

void RsnaRc4Crypt(RSNA_RC4_CTX *ctx, const rsna_u8 *in, rsna_u8 *out, rsna_size len)
{
    rsna_size n;
    rsna_u8 t;

    for (n = 0; n < len; n++)
    {
        ctx->i = (rsna_u8)(ctx->i + 1);
        ctx->j = (rsna_u8)(ctx->j + ctx->s[ctx->i]);

        t = ctx->s[ctx->i];
        ctx->s[ctx->i] = ctx->s[ctx->j];
        ctx->s[ctx->j] = t;

        t = (rsna_u8)(ctx->s[ctx->i] + ctx->s[ctx->j]);
        out[n] = (rsna_u8)(in[n] ^ ctx->s[t]);
    }
}

void RsnaRc4Skip(RSNA_RC4_CTX *ctx, rsna_size count)
{
    rsna_size n;
    rsna_u8 t;

    for (n = 0; n < count; n++)
    {
        ctx->i = (rsna_u8)(ctx->i + 1);
        ctx->j = (rsna_u8)(ctx->j + ctx->s[ctx->i]);
        t = ctx->s[ctx->i];
        ctx->s[ctx->i] = ctx->s[ctx->j];
        ctx->s[ctx->j] = t;
    }
}
