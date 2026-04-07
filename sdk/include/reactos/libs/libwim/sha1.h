/*
 * Copyright (C) 2026 Ahmed ARIF <arif.ing@outlook.com>
 *
 * sha1.h - SHA-1 hash interface (pure C)
 *
 */

#ifndef LIBWIM_SHA1_H
#define LIBWIM_SHA1_H

#include <stdint.h>
#include <string.h>

typedef struct {
    uint8_t  buffer[64];
    uint32_t state[5];
    uint32_t count[2];
} Sha1Ctx;

void sha1_init(Sha1Ctx* ctx);
void sha1_update(Sha1Ctx* ctx, const uint8_t* data, uint64_t len);
void sha1_final(Sha1Ctx* ctx, uint8_t digest[20]);
void sha1_hash(const uint8_t* data, uint64_t len, uint8_t digest[20]);

#endif /* LIBWIM_SHA1_H */
