/*
 * Copyright (C) 2026 Ahmed ARIF <arif.ing@outlook.com>
 *
 * xpress_huff.h - XPRESS Huffman (LZ77+Huffman) codec interface (pure C)
 *
 */

#ifndef LIBWIM_XPRESS_HUFF_H
#define LIBWIM_XPRESS_HUFF_H

#include <stdint.h>

typedef enum {
    XPRESS_OK = 0,
    XPRESS_BAD_DATA = -1,
    XPRESS_BUFFER_TOO_SMALL = -2
} XpressStatus;

#ifndef XPRESS_HUFF_DECOMPRESS_ONLY
typedef struct XpressCompressScratch XpressCompressScratch;

XpressCompressScratch* xpress_huff_create_scratch(uint32_t max_input_len);
void xpress_huff_destroy_scratch(XpressCompressScratch* scratch);
int xpress_huff_chunk_may_compress(const uint8_t* in, uint32_t in_len);
#endif

/* Workspace size for static (no-malloc) decompressor: 128KB */
#define XPRESS_DECOMPRESS_WORKSPACE_SIZE (32768 * 4)

#ifndef XPRESS_HUFF_DECOMPRESS_ONLY
XpressStatus xpress_huff_decompress(
    const uint8_t* in, uint32_t in_len,
    uint8_t* out, uint32_t out_len);
#endif

/* Static variant: caller provides workspace (XPRESS_DECOMPRESS_WORKSPACE_SIZE bytes).
 * No malloc/free -- suitable for bootloaders and freestanding environments. */
XpressStatus xpress_huff_decompress_static(
    const uint8_t* in, uint32_t in_len,
    uint8_t* out, uint32_t out_len,
    void* workspace);

#ifndef XPRESS_HUFF_DECOMPRESS_ONLY
XpressStatus xpress_huff_compress_with_scratch(
    const uint8_t* in, uint32_t in_len,
    uint8_t* out, uint32_t out_capacity,
    uint32_t* out_len,
    XpressCompressScratch* scratch);

XpressStatus xpress_huff_compress_prechecked_with_scratch(
    const uint8_t* in, uint32_t in_len,
    uint8_t* out, uint32_t out_capacity,
    uint32_t* out_len,
    XpressCompressScratch* scratch);

XpressStatus xpress_huff_compress(
    const uint8_t* in, uint32_t in_len,
    uint8_t* out, uint32_t out_capacity,
    uint32_t* out_len);
#endif

#endif /* LIBWIM_XPRESS_HUFF_H */
