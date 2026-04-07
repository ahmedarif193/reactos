/*
 * Copyright (C) 2026 Ahmed ARIF <arif.ing@outlook.com>
 *
 * wimcore.h - freestanding WIM reader core shared by host tools and freeldr
 *
 */

#ifndef LIBWIM_WIMCORE_H
#define LIBWIM_WIMCORE_H

#include "wim_format.h"

#include <stddef.h>
#include <stdint.h>

/* SHA-1 key for blob lookup */
typedef struct {
    uint8_t hash[20];
} WimSha1Key;

/* Blob entry */
typedef struct {
    WimSha1Key sha1;
    uint64_t original_size;
    uint64_t compressed_size;
    uint64_t offset;
    uint32_t ref_count;
    uint8_t  flags;
} WimBlob;

/* Directory entry (tree node) */
typedef struct WimDentry {
    char*     name_utf8;       /* heap-allocated */
    uint16_t* name_utf16;      /* heap-allocated */
    size_t    name_utf16_len;  /* count of uint16_t */
    uint32_t  attributes;
    uint64_t  creation_time;
    uint64_t  last_access_time;
    uint64_t  last_write_time;
    uint8_t   sha1[20];
    uint64_t  file_size;
    int32_t   security_id;
    uint64_t  subdir_offset;

    struct WimDentry* children;   /* heap-allocated array */
    size_t child_count;
    size_t child_cap;
} WimDentry;

/* Image info (from XML) */
typedef struct {
    char     name[256];
    char     description[256];
    uint32_t dir_count;
    uint32_t file_count;
    uint64_t total_bytes;
    uint64_t creation_time;
    uint64_t modification_time;
} WimImageInfo;

/* Per-image data */
typedef struct {
    WimDentry root;
    int       metadata_loaded;
    WimBlob   metadata_blob;
} WimImageData;

typedef void* (*WimCoreAllocFn)(size_t size, void* context);
typedef void  (*WimCoreFreeFn)(void* ptr, void* context);
typedef int   (*WimCoreReadFn)(void* context, uint64_t offset, void* buffer, size_t size);
typedef void  (*WimCoreProgressFn)(void* context, uint64_t bytes_advanced, const char* current_path);

typedef struct {
    WimCoreAllocFn Alloc;
    WimCoreFreeFn  Free;
    void*          Context;
} WimCoreAllocator;

typedef struct {
    uint64_t FileBytes;
    uint32_t FileCount;
    uint32_t DirectoryCount;
} WimCoreImageStats;

typedef struct {
    const uint8_t*     MemoryBase; /* Optional direct backing store */
    uint64_t           ImageSize;
    WimCoreReadFn      Read;
    void*              ReadContext;
    WimCoreAllocator   Allocator;
    WimHeader          Header;
    WimBlob*           Blobs;
    size_t             BlobCount;
    size_t             BlobCapacity;
    WimImageData*      Images;
    size_t             ImageCount;
    WimImageInfo*      ImageInfos;
    size_t             ImageInfoCount;
    char*              XmlUtf8;
    uint8_t*           XmlRaw;
    size_t             XmlRawSize;
    void*              DecompressWorkspace;
} WimCoreCtx;

void
wimcore_init(
    WimCoreCtx* ctx);

int
wimcore_open(
    WimCoreCtx* ctx,
    const WimCoreAllocator* allocator,
    WimCoreReadFn read_routine,
    void* read_context,
    uint64_t image_size);

int
wimcore_open_memory(
    WimCoreCtx* ctx,
    const WimCoreAllocator* allocator,
    const void* image_base,
    uint64_t image_size);

int
wimcore_select_image(
    WimCoreCtx* ctx,
    int index);

const WimDentry*
wimcore_get_root(
    const WimCoreCtx* ctx,
    int index);

int
wimcore_read_blob(
    WimCoreCtx* ctx,
    const uint8_t sha1[20],
    uint8_t** data,
    size_t* data_size);

int
wimcore_read_blob_into(
    WimCoreCtx* ctx,
    const uint8_t sha1[20],
    uint8_t* data,
    size_t data_size,
    WimCoreProgressFn progress,
    void* progress_context,
    const char* current_path);

int
wimcore_query_image_stats(
    WimCoreCtx* ctx,
    int index,
    WimCoreImageStats* stats);

int
wimcore_verify_integrity(
    WimCoreCtx* ctx);

void
wimcore_close(
    WimCoreCtx* ctx);

#endif /* LIBWIM_WIMCORE_H */
