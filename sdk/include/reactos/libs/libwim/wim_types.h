#ifndef WIM_TYPES_H
#define WIM_TYPES_H

#include "wimcore.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <pthread.h>
#endif

/* Forward decl; definition lives in wim_write.c (keeps pthread types out
 * of the public header for Windows/single-threaded builds). */
struct WimThreadPool;

#ifndef _WIN32
/* Defined in wim_write.c; called by wim_ctx_free() when the ctx owns
 * a pool.  Kept as an extern so wim_types.h doesn't need to pull in
 * pthread definitions. */
void wim_pool_destroy(struct WimThreadPool* pool);
#endif

static inline int wim_sha1_cmp(const WimSha1Key* a, const WimSha1Key* b) {
    return memcmp(a->hash, b->hash, 20);
}
static inline int wim_sha1_is_zero(const WimSha1Key* k) {
    uint8_t z[20] = {0};
    return memcmp(k->hash, z, 20) == 0;
}

/* SHA-1 hash table for O(1) blob dedup lookup */
#define WIM_BLOB_HT_EMPTY ((uint32_t)-1)

typedef struct {
    uint32_t* slots;   /* blob index per slot, WIM_BLOB_HT_EMPTY = unused */
    size_t    capacity;
} WimBlobHT;

/* Main WIM context (replaces CWimImage class) */
typedef struct {
    FILE*       file;
    WimHeader   header;
    int         writing;
    int         use_xpress;
    int         write_error; /* sticky async write failure after ownership transfer */
    int         num_threads; /* 0 or 1 = single-threaded */

    /* Blob table */
    WimBlob*    blobs;
    size_t      blob_count;
    size_t      blob_cap;
    WimBlobHT   blob_ht;

    /* Images */
    WimImageData* images;
    size_t        image_count;

    /* Image infos */
    WimImageInfo* image_infos;
    size_t        image_info_count;

    /* XML */
    char*    xml_utf8;
    uint8_t* xml_raw;
    size_t   xml_raw_size;

    /* Shared freestanding read core state, when opened for reading. */
    WimCoreCtx* core;

    /* Async compression pool and in-flight dedup state. */
    struct WimThreadPool* pool;
} WimCtx;

/* Dynamic array helpers */
static inline void wim_dentry_init(WimDentry* d) {
    memset(d, 0, sizeof(*d));
    d->security_id = -1;
}

static inline int wim_dentry_add_child(WimDentry* parent, WimDentry child) {
    if (parent->child_count >= parent->child_cap) {
        size_t newcap = parent->child_cap ? parent->child_cap * 2 : 8;
        WimDentry* tmp = (WimDentry*)realloc(parent->children, newcap * sizeof(WimDentry));
        if (!tmp) return -1;
        parent->children = tmp;
        parent->child_cap = newcap;
    }
    parent->children[parent->child_count++] = child;
    return 0;
}

static inline void wim_dentry_free(WimDentry* d) {
    free(d->name_utf8);
    free(d->name_utf16);
    for (size_t i = 0; i < d->child_count; i++)
        wim_dentry_free(&d->children[i]);
    free(d->children);
    memset(d, 0, sizeof(*d));
}

static inline void wim_ctx_init(WimCtx* ctx) {
    memset(ctx, 0, sizeof(*ctx));
}

/* Blob hash table helpers */
static inline uint32_t wim_blob_ht_hash(const uint8_t sha1[20]) {
    uint32_t h;
    memcpy(&h, sha1, 4);
    return h;
}

static inline int wim_blob_ht_grow(WimBlobHT* ht, const WimBlob* blobs, size_t blob_count) {
    size_t newcap = ht->capacity ? ht->capacity * 2 : 256;
    uint32_t* newslots = (uint32_t*)malloc(newcap * sizeof(uint32_t));
    if (!newslots) return -1;
    memset(newslots, 0xFF, newcap * sizeof(uint32_t)); /* fill with EMPTY */
    /* Re-insert all existing entries */
    for (size_t i = 0; i < blob_count; i++) {
        uint32_t idx = wim_blob_ht_hash(blobs[i].sha1.hash) & (uint32_t)(newcap - 1);
        while (newslots[idx] != WIM_BLOB_HT_EMPTY)
            idx = (idx + 1) & (uint32_t)(newcap - 1);
        newslots[idx] = (uint32_t)i;
    }
    free(ht->slots);
    ht->slots = newslots;
    ht->capacity = newcap;
    return 0;
}

/* Blob table: add entry, find by SHA-1 */
static inline int wim_ctx_add_blob(WimCtx* ctx, const WimBlob* blob) {
    if (ctx->blob_count >= ctx->blob_cap) {
        size_t newcap = ctx->blob_cap ? ctx->blob_cap * 2 : 64;
        WimBlob* tmp = (WimBlob*)realloc(ctx->blobs, newcap * sizeof(WimBlob));
        if (!tmp) return -1;
        ctx->blobs = tmp;
        ctx->blob_cap = newcap;
    }
    /* Grow hash table at 75% load */
    if (ctx->blob_ht.capacity == 0 || ctx->blob_count * 4 >= ctx->blob_ht.capacity * 3) {
        if (wim_blob_ht_grow(&ctx->blob_ht, ctx->blobs, ctx->blob_count) != 0)
            return -1;
    }
    /* Insert into hash table */
    uint32_t idx = wim_blob_ht_hash(blob->sha1.hash) & (uint32_t)(ctx->blob_ht.capacity - 1);
    while (ctx->blob_ht.slots[idx] != WIM_BLOB_HT_EMPTY)
        idx = (idx + 1) & (uint32_t)(ctx->blob_ht.capacity - 1);
    ctx->blob_ht.slots[idx] = (uint32_t)ctx->blob_count;

    ctx->blobs[ctx->blob_count++] = *blob;
    return 0;
}

static inline int wim_ctx_find_blob(const WimCtx* ctx, const uint8_t sha1[20]) {
    if (ctx->blob_ht.capacity == 0)
        return -1;
    uint32_t idx = wim_blob_ht_hash(sha1) & (uint32_t)(ctx->blob_ht.capacity - 1);
    while (ctx->blob_ht.slots[idx] != WIM_BLOB_HT_EMPTY) {
        uint32_t bi = ctx->blob_ht.slots[idx];
        if (memcmp(ctx->blobs[bi].sha1.hash, sha1, 20) == 0)
            return (int)bi;
        idx = (idx + 1) & (uint32_t)(ctx->blob_ht.capacity - 1);
    }
    return -1;
}

static inline void wim_ctx_free(WimCtx* ctx) {
    if (ctx->core) {
        if (ctx->file) { fclose(ctx->file); ctx->file = NULL; }
        wimcore_close(ctx->core);
        free(ctx->core);
        memset(ctx, 0, sizeof(*ctx));
        return;
    }

    if (ctx->file) { fclose(ctx->file); ctx->file = NULL; }
#ifndef _WIN32
    if (ctx->pool) {
        wim_pool_destroy(ctx->pool);
        ctx->pool = NULL;
    }
#endif
    free(ctx->blobs);
    free(ctx->blob_ht.slots);
    if (ctx->images) {
        for (size_t i = 0; i < ctx->image_count; i++)
            wim_dentry_free(&ctx->images[i].root);
        free(ctx->images);
    }
    free(ctx->image_infos);
    free(ctx->xml_utf8);
    free(ctx->xml_raw);
    memset(ctx, 0, sizeof(*ctx));
}

#endif /* WIM_TYPES_H */
