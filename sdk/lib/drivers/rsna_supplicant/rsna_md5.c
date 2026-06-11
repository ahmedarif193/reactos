/*
 * PROJECT:     ReactOS Native WiFi - RSNA (WPA2-PSK) supplicant
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     MD5 (RFC 1321) - cleanroom implementation
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 *
 * Little-endian message words are assembled byte-by-byte, so the code is
 * endian independent.
 */

#include "rsna_md5.h"

#define ROTL32(x, n)  (((x) << (n)) | ((x) >> (32 - (n))))

#define MD5_F(x, y, z)  (((x) & (y)) | ((~(x)) & (z)))
#define MD5_G(x, y, z)  (((x) & (z)) | ((y) & (~(z))))
#define MD5_H(x, y, z)  ((x) ^ (y) ^ (z))
#define MD5_I(x, y, z)  ((y) ^ ((x) | (~(z))))

static const rsna_u32 g_Md5K[64] =
{
    0xd76aa478u, 0xe8c7b756u, 0x242070dbu, 0xc1bdceeeu,
    0xf57c0fafu, 0x4787c62au, 0xa8304613u, 0xfd469501u,
    0x698098d8u, 0x8b44f7afu, 0xffff5bb1u, 0x895cd7beu,
    0x6b901122u, 0xfd987193u, 0xa679438eu, 0x49b40821u,
    0xf61e2562u, 0xc040b340u, 0x265e5a51u, 0xe9b6c7aau,
    0xd62f105du, 0x02441453u, 0xd8a1e681u, 0xe7d3fbc8u,
    0x21e1cde6u, 0xc33707d6u, 0xf4d50d87u, 0x455a14edu,
    0xa9e3e905u, 0xfcefa3f8u, 0x676f02d9u, 0x8d2a4c8au,
    0xfffa3942u, 0x8771f681u, 0x6d9d6122u, 0xfde5380cu,
    0xa4beea44u, 0x4bdecfa9u, 0xf6bb4b60u, 0xbebfbc70u,
    0x289b7ec6u, 0xeaa127fau, 0xd4ef3085u, 0x04881d05u,
    0xd9d4d039u, 0xe6db99e5u, 0x1fa27cf8u, 0xc4ac5665u,
    0xf4292244u, 0x432aff97u, 0xab9423a7u, 0xfc93a039u,
    0x655b59c3u, 0x8f0ccc92u, 0xffeff47du, 0x85845dd1u,
    0x6fa87e4fu, 0xfe2ce6e0u, 0xa3014314u, 0x4e0811a1u,
    0xf7537e82u, 0xbd3af235u, 0x2ad7d2bbu, 0xeb86d391u
};

static const rsna_u8 g_Md5S[64] =
{
    7, 12, 17, 22,  7, 12, 17, 22,  7, 12, 17, 22,  7, 12, 17, 22,
    5,  9, 14, 20,  5,  9, 14, 20,  5,  9, 14, 20,  5,  9, 14, 20,
    4, 11, 16, 23,  4, 11, 16, 23,  4, 11, 16, 23,  4, 11, 16, 23,
    6, 10, 15, 21,  6, 10, 15, 21,  6, 10, 15, 21,  6, 10, 15, 21
};

static void Md5Transform(rsna_u32 state[4], const rsna_u8 block[64])
{
    rsna_u32 m[16];
    rsna_u32 a, b, c, d, f, temp;
    int i, g;

    for (i = 0; i < 16; i++)
    {
        m[i] = ((rsna_u32)block[i * 4 + 0]) |
               ((rsna_u32)block[i * 4 + 1] << 8) |
               ((rsna_u32)block[i * 4 + 2] << 16) |
               ((rsna_u32)block[i * 4 + 3] << 24);
    }

    a = state[0];
    b = state[1];
    c = state[2];
    d = state[3];

    for (i = 0; i < 64; i++)
    {
        if (i < 16)
        {
            f = MD5_F(b, c, d);
            g = i;
        }
        else if (i < 32)
        {
            f = MD5_G(b, c, d);
            g = (5 * i + 1) & 15;
        }
        else if (i < 48)
        {
            f = MD5_H(b, c, d);
            g = (3 * i + 5) & 15;
        }
        else
        {
            f = MD5_I(b, c, d);
            g = (7 * i) & 15;
        }

        temp = d;
        d = c;
        c = b;
        b = b + ROTL32((a + f + g_Md5K[i] + m[g]), g_Md5S[i]);
        a = temp;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
}

void RsnaMd5Init(RSNA_MD5_CTX *ctx)
{
    ctx->state[0] = 0x67452301u;
    ctx->state[1] = 0xefcdab89u;
    ctx->state[2] = 0x98badcfeu;
    ctx->state[3] = 0x10325476u;
    ctx->count = 0;
}

void RsnaMd5Update(RSNA_MD5_CTX *ctx, const void *data, rsna_size len)
{
    const rsna_u8 *p = (const rsna_u8 *)data;
    rsna_size have = (rsna_size)(ctx->count % RSNA_MD5_BLOCK_LEN);

    ctx->count += (rsna_u64)len;

    if (have != 0)
    {
        rsna_size need = RSNA_MD5_BLOCK_LEN - have;
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
        Md5Transform(ctx->state, ctx->buffer);
        p += need;
        len -= need;
    }

    while (len >= RSNA_MD5_BLOCK_LEN)
    {
        Md5Transform(ctx->state, p);
        p += RSNA_MD5_BLOCK_LEN;
        len -= RSNA_MD5_BLOCK_LEN;
    }

    {
        rsna_size i;
        for (i = 0; i < len; i++)
            ctx->buffer[i] = p[i];
    }
}

void RsnaMd5Final(RSNA_MD5_CTX *ctx, rsna_u8 digest[RSNA_MD5_DIGEST_LEN])
{
    rsna_u64 bits = ctx->count * 8u;
    rsna_u8  lenbuf[8];
    rsna_u8  pad = 0x80;
    rsna_u8  zero = 0x00;
    int i;

    for (i = 0; i < 8; i++)
        lenbuf[i] = (rsna_u8)((bits >> (8 * i)) & 0xFF);  /* little-endian */

    RsnaMd5Update(ctx, &pad, 1);
    while ((ctx->count % RSNA_MD5_BLOCK_LEN) != 56)
        RsnaMd5Update(ctx, &zero, 1);

    RsnaMd5Update(ctx, lenbuf, 8);

    for (i = 0; i < 4; i++)
    {
        digest[i * 4 + 0] = (rsna_u8)(ctx->state[i] & 0xFF);
        digest[i * 4 + 1] = (rsna_u8)((ctx->state[i] >> 8) & 0xFF);
        digest[i * 4 + 2] = (rsna_u8)((ctx->state[i] >> 16) & 0xFF);
        digest[i * 4 + 3] = (rsna_u8)((ctx->state[i] >> 24) & 0xFF);
    }
}

void RsnaMd5(const void *data, rsna_size len, rsna_u8 digest[RSNA_MD5_DIGEST_LEN])
{
    RSNA_MD5_CTX ctx;
    RsnaMd5Init(&ctx);
    RsnaMd5Update(&ctx, data, len);
    RsnaMd5Final(&ctx, digest);
}
