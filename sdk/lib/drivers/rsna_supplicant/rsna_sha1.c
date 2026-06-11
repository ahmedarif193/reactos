/*
 * PROJECT:     ReactOS Native WiFi - RSNA (WPA2-PSK) supplicant
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     SHA-1 (FIPS 180-4) - cleanroom implementation
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 *
 * Big-endian message words are assembled byte-by-byte, so the code is
 * endian independent.
 */

#include "rsna_sha1.h"

#define ROTL32(x, n)  (((x) << (n)) | ((x) >> (32 - (n))))

static void Sha1Transform(rsna_u32 state[5], const rsna_u8 block[64])
{
    rsna_u32 w[80];
    rsna_u32 a, b, c, d, e, f, k, temp;
    int i;

    /* Prepare the message schedule (big-endian words). */
    for (i = 0; i < 16; i++)
    {
        w[i] = ((rsna_u32)block[i * 4 + 0] << 24) |
               ((rsna_u32)block[i * 4 + 1] << 16) |
               ((rsna_u32)block[i * 4 + 2] << 8)  |
               ((rsna_u32)block[i * 4 + 3]);
    }
    for (i = 16; i < 80; i++)
    {
        temp = w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16];
        w[i] = ROTL32(temp, 1);
    }

    a = state[0];
    b = state[1];
    c = state[2];
    d = state[3];
    e = state[4];

    for (i = 0; i < 80; i++)
    {
        if (i < 20)
        {
            f = (b & c) | ((~b) & d);
            k = 0x5A827999u;
        }
        else if (i < 40)
        {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1u;
        }
        else if (i < 60)
        {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDCu;
        }
        else
        {
            f = b ^ c ^ d;
            k = 0xCA62C1D6u;
        }

        temp = ROTL32(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = ROTL32(b, 30);
        b = a;
        a = temp;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
}

void RsnaSha1Init(RSNA_SHA1_CTX *ctx)
{
    ctx->state[0] = 0x67452301u;
    ctx->state[1] = 0xEFCDAB89u;
    ctx->state[2] = 0x98BADCFEu;
    ctx->state[3] = 0x10325476u;
    ctx->state[4] = 0xC3D2E1F0u;
    ctx->count = 0;
}

void RsnaSha1Update(RSNA_SHA1_CTX *ctx, const void *data, rsna_size len)
{
    const rsna_u8 *p = (const rsna_u8 *)data;
    rsna_size have = (rsna_size)(ctx->count % RSNA_SHA1_BLOCK_LEN);

    ctx->count += (rsna_u64)len;

    if (have != 0)
    {
        rsna_size need = RSNA_SHA1_BLOCK_LEN - have;
        if (len < need)
        {
            rsna_size i;
            for (i = 0; i < len; i++)
                ctx->buffer[have + i] = p[i];
            return;
        }
        {
            rsna_size i;
            for (i = 0; i < need; i++)
                ctx->buffer[have + i] = p[i];
        }
        Sha1Transform(ctx->state, ctx->buffer);
        p += need;
        len -= need;
    }

    while (len >= RSNA_SHA1_BLOCK_LEN)
    {
        Sha1Transform(ctx->state, p);
        p += RSNA_SHA1_BLOCK_LEN;
        len -= RSNA_SHA1_BLOCK_LEN;
    }

    {
        rsna_size i;
        for (i = 0; i < len; i++)
            ctx->buffer[i] = p[i];
    }
}

void RsnaSha1Final(RSNA_SHA1_CTX *ctx, rsna_u8 digest[RSNA_SHA1_DIGEST_LEN])
{
    rsna_u64 bits = ctx->count * 8u;
    rsna_u8  lenbuf[8];
    rsna_u8  pad = 0x80;
    rsna_u8  zero = 0x00;
    int i;

    for (i = 0; i < 8; i++)
        lenbuf[i] = (rsna_u8)((bits >> (56 - 8 * i)) & 0xFF);

    RsnaSha1Update(ctx, &pad, 1);
    while ((ctx->count % RSNA_SHA1_BLOCK_LEN) != 56)
        RsnaSha1Update(ctx, &zero, 1);

    RsnaSha1Update(ctx, lenbuf, 8);

    for (i = 0; i < 5; i++)
    {
        digest[i * 4 + 0] = (rsna_u8)((ctx->state[i] >> 24) & 0xFF);
        digest[i * 4 + 1] = (rsna_u8)((ctx->state[i] >> 16) & 0xFF);
        digest[i * 4 + 2] = (rsna_u8)((ctx->state[i] >> 8) & 0xFF);
        digest[i * 4 + 3] = (rsna_u8)(ctx->state[i] & 0xFF);
    }
}

void RsnaSha1(const void *data, rsna_size len, rsna_u8 digest[RSNA_SHA1_DIGEST_LEN])
{
    RSNA_SHA1_CTX ctx;
    RsnaSha1Init(&ctx);
    RsnaSha1Update(&ctx, data, len);
    RsnaSha1Final(&ctx, digest);
}
