/*
 * Copyright (C) 2026 Ahmed ARIF <arif.ing@outlook.com>
 *
 * wim_format.h - On-disk WIM packed structures (pure C)
 *
 */

#ifndef WIM_FORMAT_H
#define WIM_FORMAT_H

#include <stdint.h>
#include <string.h>

/* ---- Constants ---- */
#define WIM_MAGIC "MSWIM\0\0\0"
#define WIM_MAGIC_LEN 8
#define WIM_HEADER_SIZE 208
#define WIM_VERSION 0x00010D00
#define WIM_LOOKUP_ENTRY_SIZE 50

/* Header flags */
#define WIM_HDR_FLAG_COMPRESSION     0x00000002
#define WIM_HDR_FLAG_READONLY        0x00000004
#define WIM_HDR_FLAG_SPANNED         0x00000008
#define WIM_HDR_FLAG_RP_FIX          0x00000080
#define WIM_HDR_FLAG_COMPRESS_XPRESS 0x00020000
#define WIM_HDR_FLAG_COMPRESS_LZX    0x00040000
#define WIM_HDR_FLAG_COMPRESS_LZMS   0x00080000

/* Resource flags (upper byte of reshdr size_and_flags) */
#define WIM_RESHDR_FLAG_FREE       0x01
#define WIM_RESHDR_FLAG_METADATA   0x02
#define WIM_RESHDR_FLAG_COMPRESSED 0x04
#define WIM_RESHDR_FLAG_SPANNED    0x08

/* Windows file attributes */
#define WIM_FILE_ATTRIBUTE_READONLY  0x00000001
#define WIM_FILE_ATTRIBUTE_HIDDEN    0x00000002
#define WIM_FILE_ATTRIBUTE_SYSTEM    0x00000004
#define WIM_FILE_ATTRIBUTE_DIRECTORY 0x00000010
#define WIM_FILE_ATTRIBUTE_ARCHIVE   0x00000020
#define WIM_FILE_ATTRIBUTE_NORMAL    0x00000080

/* ---- On-disk packed structures ---- */

#pragma pack(push, 1)

typedef struct {                /* 24 bytes */
    uint8_t  size_and_flags[8]; /* low 7 bytes = compressed size, byte 7 = flags */
    uint64_t offset;
    uint64_t original_size;
} WimResHdr;

typedef struct {                /* 208 bytes */
    uint8_t  magic[8];
    uint32_t header_size;
    uint32_t version;
    uint32_t flags;
    uint32_t chunk_size;
    uint8_t  guid[16];
    uint16_t part_number;
    uint16_t total_parts;
    uint32_t image_count;
    WimResHdr lookup_table;
    WimResHdr xml_data;
    WimResHdr boot_metadata;
    uint32_t boot_index;
    WimResHdr integrity;
    uint8_t  reserved[60];
} WimHeader;

typedef struct {                /* 50 bytes */
    WimResHdr reshdr;
    uint16_t part_number;
    uint32_t ref_count;
    uint8_t  sha1[20];
} WimLookupEntry;

#pragma pack(pop)

_Static_assert(sizeof(WimResHdr) == 24, "WimResHdr must be 24 bytes");
_Static_assert(sizeof(WimHeader) == 208, "WimHeader must be 208 bytes");
_Static_assert(sizeof(WimLookupEntry) == 50, "WimLookupEntry must be 50 bytes");

/* ---- WimResHdr helpers ---- */

static inline uint64_t reshdr_get_size(const WimResHdr* r) {
    uint64_t sz = 0;
    memcpy(&sz, r->size_and_flags, 7);
    return sz;
}

static inline uint8_t reshdr_get_flags(const WimResHdr* r) {
    return r->size_and_flags[7];
}

static inline void reshdr_set(WimResHdr* r, uint64_t comp_size, uint8_t flags,
                              uint64_t offset, uint64_t orig_size) {
    memset(r, 0, sizeof(*r));
    memcpy(r->size_and_flags, &comp_size, 7);
    r->size_and_flags[7] = flags;
    r->offset = offset;
    r->original_size = orig_size;
}

#endif /* WIM_FORMAT_H */
