/*
 * Copyright (C) 2026 Ahmed ARIF <arif.ing@outlook.com>
 *
 * SHA-1 hash implementation for native-wimage.
 * Adapted from ReactOS sdk/lib/cryptlib/sha1.c
 * Original: Copyright 2004 Filip Navara, based on public domain code by Steve Reid.
 * LGPL-2.1-or-later
 *
 */

#include "sha1.h"

#ifdef HAVE_OPENSSL_SHA1
#include <openssl/sha.h>
#endif

#define rol(value, bits) (((value) << (bits)) | ((value) >> (32 - (bits))))
#define blk0(i) (Block[i] = (rol(Block[i],24)&0xFF00FF00)|(rol(Block[i],8)&0x00FF00FF))
#define blk1(i) (Block[i&15] = rol(Block[(i+13)&15]^Block[(i+8)&15]^Block[(i+2)&15]^Block[i&15],1))
#define f1(x,y,z) (z^(x&(y^z)))
#define f2(x,y,z) (x^y^z)
#define f3(x,y,z) ((x&y)|(z&(x|y)))
#define f4(x,y,z) (x^y^z)
#define R0(v,w,x,y,z,i) z+=f1(w,x,y)+blk0(i)+0x5A827999+rol(v,5);w=rol(w,30);
#define R1(v,w,x,y,z,i) z+=f1(w,x,y)+blk1(i)+0x5A827999+rol(v,5);w=rol(w,30);
#define R2(v,w,x,y,z,i) z+=f2(w,x,y)+blk1(i)+0x6ED9EBA1+rol(v,5);w=rol(w,30);
#define R3(v,w,x,y,z,i) z+=f3(w,x,y)+blk1(i)+0x8F1BBCDC+rol(v,5);w=rol(w,30);
#define R4(v,w,x,y,z,i) z+=f4(w,x,y)+blk1(i)+0xCA62C1D6+rol(v,5);w=rol(w,30);

static void sha1_transform(uint32_t state[5], uint8_t buffer[64])
{
    uint32_t a, b, c, d, e;
    uint32_t* Block = (uint32_t*)buffer;

    a = state[0];
    b = state[1];
    c = state[2];
    d = state[3];
    e = state[4];

    R0(a,b,c,d,e, 0); R0(e,a,b,c,d, 1); R0(d,e,a,b,c, 2); R0(c,d,e,a,b, 3);
    R0(b,c,d,e,a, 4); R0(a,b,c,d,e, 5); R0(e,a,b,c,d, 6); R0(d,e,a,b,c, 7);
    R0(c,d,e,a,b, 8); R0(b,c,d,e,a, 9); R0(a,b,c,d,e,10); R0(e,a,b,c,d,11);
    R0(d,e,a,b,c,12); R0(c,d,e,a,b,13); R0(b,c,d,e,a,14); R0(a,b,c,d,e,15);
    R1(e,a,b,c,d,16); R1(d,e,a,b,c,17); R1(c,d,e,a,b,18); R1(b,c,d,e,a,19);
    R2(a,b,c,d,e,20); R2(e,a,b,c,d,21); R2(d,e,a,b,c,22); R2(c,d,e,a,b,23);
    R2(b,c,d,e,a,24); R2(a,b,c,d,e,25); R2(e,a,b,c,d,26); R2(d,e,a,b,c,27);
    R2(c,d,e,a,b,28); R2(b,c,d,e,a,29); R2(a,b,c,d,e,30); R2(e,a,b,c,d,31);
    R2(d,e,a,b,c,32); R2(c,d,e,a,b,33); R2(b,c,d,e,a,34); R2(a,b,c,d,e,35);
    R2(e,a,b,c,d,36); R2(d,e,a,b,c,37); R2(c,d,e,a,b,38); R2(b,c,d,e,a,39);
    R3(a,b,c,d,e,40); R3(e,a,b,c,d,41); R3(d,e,a,b,c,42); R3(c,d,e,a,b,43);
    R3(b,c,d,e,a,44); R3(a,b,c,d,e,45); R3(e,a,b,c,d,46); R3(d,e,a,b,c,47);
    R3(c,d,e,a,b,48); R3(b,c,d,e,a,49); R3(a,b,c,d,e,50); R3(e,a,b,c,d,51);
    R3(d,e,a,b,c,52); R3(c,d,e,a,b,53); R3(b,c,d,e,a,54); R3(a,b,c,d,e,55);
    R3(e,a,b,c,d,56); R3(d,e,a,b,c,57); R3(c,d,e,a,b,58); R3(b,c,d,e,a,59);
    R4(a,b,c,d,e,60); R4(e,a,b,c,d,61); R4(d,e,a,b,c,62); R4(c,d,e,a,b,63);
    R4(b,c,d,e,a,64); R4(a,b,c,d,e,65); R4(e,a,b,c,d,66); R4(d,e,a,b,c,67);
    R4(c,d,e,a,b,68); R4(b,c,d,e,a,69); R4(a,b,c,d,e,70); R4(e,a,b,c,d,71);
    R4(d,e,a,b,c,72); R4(c,d,e,a,b,73); R4(b,c,d,e,a,74); R4(a,b,c,d,e,75);
    R4(e,a,b,c,d,76); R4(d,e,a,b,c,77); R4(c,d,e,a,b,78); R4(b,c,d,e,a,79);

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;

    a = b = c = d = e = 0;
}

void sha1_init(Sha1Ctx* ctx)
{
    ctx->state[0] = 0x67452301;
    ctx->state[1] = 0xEFCDAB89;
    ctx->state[2] = 0x98BADCFE;
    ctx->state[3] = 0x10325476;
    ctx->state[4] = 0xC3D2E1F0;
    ctx->count[0] = 0;
    ctx->count[1] = 0;
}

void sha1_update(Sha1Ctx* ctx, const uint8_t* data, uint64_t len)
{
    uint32_t buf_used = ctx->count[1] & 63;
    uint32_t len_lo = (uint32_t)len;
    uint32_t len_hi = (uint32_t)(len >> 32);
    ctx->count[1] += len_lo;
    if (ctx->count[1] < len_lo)
        ctx->count[0]++;
    ctx->count[0] += len_hi;

    if (buf_used + len < 64) {
        memcpy(&ctx->buffer[buf_used], data, (size_t)len);
    } else {
        while (buf_used + len >= 64) {
            memcpy(ctx->buffer + buf_used, data, 64 - buf_used);
            data += 64 - buf_used;
            len -= 64 - buf_used;
            sha1_transform(ctx->state, ctx->buffer);
            buf_used = 0;
        }
        memcpy(ctx->buffer + buf_used, data, (size_t)len);
    }
}

void sha1_final(Sha1Ctx* ctx, uint8_t digest[20])
{
    uint32_t buf_used = ctx->count[1] & 63;
    int pad;
    if (buf_used >= 56)
        pad = 56 + 64 - buf_used;
    else
        pad = 56 - buf_used;

    uint32_t len_hi = (ctx->count[0] << 3) | (ctx->count[1] >> 29);
    uint32_t len_lo = (ctx->count[1] << 3);

    uint8_t padbuf[72];
    padbuf[0] = 0x80;
    memset(padbuf + 1, 0, pad - 1);

    /* Append big-endian 64-bit bit count */
    padbuf[pad + 0] = (uint8_t)(len_hi >> 24);
    padbuf[pad + 1] = (uint8_t)(len_hi >> 16);
    padbuf[pad + 2] = (uint8_t)(len_hi >> 8);
    padbuf[pad + 3] = (uint8_t)(len_hi);
    padbuf[pad + 4] = (uint8_t)(len_lo >> 24);
    padbuf[pad + 5] = (uint8_t)(len_lo >> 16);
    padbuf[pad + 6] = (uint8_t)(len_lo >> 8);
    padbuf[pad + 7] = (uint8_t)(len_lo);

    sha1_update(ctx, padbuf, pad + 8);

    /* Output state as big-endian bytes */
    for (int i = 0; i < 5; i++) {
        uint32_t s = ctx->state[i];
        digest[i * 4 + 0] = (uint8_t)(s >> 24);
        digest[i * 4 + 1] = (uint8_t)(s >> 16);
        digest[i * 4 + 2] = (uint8_t)(s >> 8);
        digest[i * 4 + 3] = (uint8_t)(s);
    }

    memset(ctx->buffer, 0, sizeof(ctx->buffer));
    sha1_init(ctx);
}

void sha1_hash(const uint8_t* data, uint64_t len, uint8_t digest[20])
{
#ifdef HAVE_OPENSSL_SHA1
    SHA1(data, (size_t)len, digest);
#else
    Sha1Ctx ctx;
    sha1_init(&ctx);
    sha1_update(&ctx, data, len);
    sha1_final(&ctx, digest);
#endif
}
