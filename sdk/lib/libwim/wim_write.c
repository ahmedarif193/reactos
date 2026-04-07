/*
 * Copyright (C) 2026 Ahmed ARIF <arif.ing@outlook.com>
 *
 * wim_write.c - WIM writing implementation (pure C)
 *
 * Main hashes and dedups blobs, workers compress chunks, and main drains
 * completed blobs back to the output file.
 */

#include "wim_write.h"
#include "wim_capture.h"
#include "wim_io.h"
#include "sha1.h"
#include "xpress_huff.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifndef _WIN32
#include <unistd.h>
#include <stdatomic.h>
#else
#include <process.h>
#define getpid _getpid
#endif

#ifdef WIMAGE_PROFILE
typedef struct {
    double t_blob_total, t_sha1, t_lookups, t_submit, t_drain_opp, t_drain_fin, t_commit;
    uint64_t n_blobs, n_chunks;
} TimingHarness;
static TimingHarness g_timing;
static inline double wp_now(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}
#  define WP_TS(name) double name = wp_now()
#  define WP_ADD(field, start) g_timing.field += (wp_now() - (start))
#else
#  define WP_TS(name) ((void)0)
#  define WP_ADD(field, start) ((void)0)
#endif

/* Checked fwrite: returns -1 on short write */
static inline int wim_fwrite(const void* buf, size_t size, FILE* f)
{
    return (fwrite(buf, 1, size, f) == size) ? 0 : -1;
}

#ifndef _WIN32
#include <pthread.h>
#endif

/* ================================================================
 *  Multi-threaded chunk compression
 * ================================================================ */

/* Forward decl. */
struct WimBlobInflight;

typedef struct {
    const uint8_t* in;
    uint32_t in_len;
    uint8_t* out;
    uint32_t out_cap;
    uint32_t out_len;
    int      stored_raw;
    int      owns_out;
    int      failed;

    /* NULL on the synchronous path. */
    struct WimBlobInflight* parent;
} ChunkWork;

static void compress_one_chunk(ChunkWork* w, XpressCompressScratch* scratch)
{
    if (w->stored_raw && !w->owns_out)
        return;

    if (!w->out) {
        if (!xpress_huff_chunk_may_compress(w->in, w->in_len)) {
            w->out = (uint8_t*)w->in;
            w->out_cap = w->in_len;
            w->out_len = w->in_len;
            w->stored_raw = 1;
            return;
        }

        w->out = (uint8_t*)malloc(w->in_len + 4096);
        if (!w->out) {
            w->failed = 1;
            return;
        }
        w->out_cap = w->in_len + 4096;
        w->owns_out = 1;
    }

    XpressStatus st;
    if (scratch) {
        st = xpress_huff_compress_prechecked_with_scratch(
            w->in, w->in_len, w->out, w->out_cap, &w->out_len, scratch);
    } else {
        st = xpress_huff_compress(w->in, w->in_len,
                                  w->out, w->out_cap, &w->out_len);
    }
    if (st != XPRESS_OK || w->out_len >= w->in_len) {
        if (w->owns_out) {
            free(w->out);
            w->owns_out = 0;
        }
        w->out = (uint8_t*)w->in;
        w->out_cap = w->in_len;
        w->out_len = w->in_len;
        w->stored_raw = 1;
    } else {
        w->stored_raw = 0;
    }
}

typedef struct WimBlobInflight {
    uint8_t  sha1[20];
    int      is_metadata;
    int      hash_ready;
    uint64_t size;

    ChunkWork* chunks;
    uint64_t   num_chunks;
    uint32_t   chunk_size;

    uint8_t* data;
    void     (*free_fn)(void*, size_t);
    void*    free_arg;

#ifndef _WIN32
    atomic_uint_fast64_t chunks_left;
    atomic_int failed;
#else
    uint64_t chunks_left;
    int failed;
#endif
    uint32_t extra_refs; /* duplicate hits while in flight */
    struct WimBlobInflight* next;    /* completion list */
    struct WimBlobInflight* ht_next; /* inflight hash chain */
} WimBlobInflight;

#ifndef _WIN32
#define WIM_CHUNK_QUEUE_MIN_CAPACITY 32
#define INFLIGHT_HT_BUCKETS          256u /* must be power of 2 */

struct WimThreadPool {
    pthread_t*       threads;
    int              thread_count;

    pthread_mutex_t  qmtx;
    pthread_cond_t   not_empty;
    pthread_cond_t   not_full;
    ChunkWork**      ring;
    uint32_t         ring_cap;
    uint32_t         ring_head;
    uint32_t         ring_count;
    uint32_t         idle_workers;
    int              shutdown;

    pthread_mutex_t  cmtx;
    pthread_cond_t   completion_cv;
    WimBlobInflight* comp_head;
    WimBlobInflight* comp_tail;
    uint32_t         comp_count;

    atomic_uint_fast32_t outstanding;
    WimBlobInflight* inflight_ht[INFLIGHT_HT_BUCKETS];
};

static inline void ring_push_locked(struct WimThreadPool* pool, ChunkWork* w)
{
    uint32_t pos = (pool->ring_head + pool->ring_count) & (pool->ring_cap - 1);
    pool->ring[pos] = w;
    pool->ring_count++;
}

static inline ChunkWork* ring_pop_locked(struct WimThreadPool* pool)
{
    ChunkWork* w = pool->ring[pool->ring_head];
    pool->ring_head = (pool->ring_head + 1) & (pool->ring_cap - 1);
    pool->ring_count--;
    return w;
}

static void* wim_pool_worker_main(void* arg_ptr)
{
    struct WimThreadPool* pool = (struct WimThreadPool*)arg_ptr;

    XpressCompressScratch* scratch = NULL;
    uint32_t scratch_max = 0;

#ifdef WIMAGE_PROFILE
    double t_compress = 0, t_lock = 0, t_alloc = 0;
    uint64_t n_chunks_done = 0;
#endif

    for (;;) {
#ifdef WIMAGE_PROFILE
        WP_TS(tw0);
#endif
        pthread_mutex_lock(&pool->qmtx);
        while (pool->ring_count == 0 && !pool->shutdown) {
            pool->idle_workers++;
            pthread_cond_wait(&pool->not_empty, &pool->qmtx);
            pool->idle_workers--;
        }
        if (pool->ring_count == 0 && pool->shutdown) {
            pthread_mutex_unlock(&pool->qmtx);
            break;
        }
        ChunkWork* w = ring_pop_locked(pool);
        pthread_cond_signal(&pool->not_full);
        pthread_mutex_unlock(&pool->qmtx);
#ifdef WIMAGE_PROFILE
        t_lock += wp_now() - tw0;
#endif

        uint32_t needed = w->parent ? w->parent->chunk_size : w->in_len;
        if (!scratch || scratch_max < needed) {
            if (scratch) xpress_huff_destroy_scratch(scratch);
            scratch = xpress_huff_create_scratch(needed);
            scratch_max = needed;
        }

#ifdef WIMAGE_PROFILE
        WP_TS(tc);
#endif
        compress_one_chunk(w, scratch);
#ifdef WIMAGE_PROFILE
        t_compress += wp_now() - tc;
        n_chunks_done++;
#endif

        WimBlobInflight* parent = w->parent;
        if (w->failed && parent)
            atomic_store_explicit(&parent->failed, 1, memory_order_relaxed);

        if (parent) {
            uint_fast64_t prev = atomic_fetch_sub_explicit(
                &parent->chunks_left, 1, memory_order_acq_rel);
            if (prev == 1) {
                pthread_mutex_lock(&pool->cmtx);
                parent->next = NULL;
                if (pool->comp_tail) {
                    pool->comp_tail->next = parent;
                    pool->comp_tail = parent;
                } else {
                    pool->comp_head = pool->comp_tail = parent;
                }
                pool->comp_count++;
                pthread_cond_signal(&pool->completion_cv);
                pthread_mutex_unlock(&pool->cmtx);
            }
        }
    }

    if (scratch) xpress_huff_destroy_scratch(scratch);
#ifdef WIMAGE_PROFILE
    fprintf(stderr, "[PROF-W] compress=%.4f lock=%.4f chunks=%lu\n",
            t_compress, t_lock, (unsigned long)n_chunks_done);
#endif
    return NULL;
}

static uint32_t round_up_pow2(uint32_t n)
{
    uint32_t r = 1;
    while (r < n) r <<= 1;
    return r;
}

static struct WimThreadPool* wim_pool_create(int thread_count)
{
    if (thread_count <= 1) return NULL;

    struct WimThreadPool* pool = (struct WimThreadPool*)calloc(1, sizeof(*pool));
    if (!pool) return NULL;

    pool->thread_count = thread_count;
    pthread_mutex_init(&pool->qmtx, NULL);
    pthread_mutex_init(&pool->cmtx, NULL);
    pthread_cond_init(&pool->not_empty, NULL);
    pthread_cond_init(&pool->not_full, NULL);
    pthread_cond_init(&pool->completion_cv, NULL);

    /* 16x threads reduced producer stalls in profiling. */
    uint32_t desired = (uint32_t)thread_count * 16;
    if (desired < WIM_CHUNK_QUEUE_MIN_CAPACITY)
        desired = WIM_CHUNK_QUEUE_MIN_CAPACITY;
    pool->ring_cap = round_up_pow2(desired);
    pool->ring = (ChunkWork**)calloc(pool->ring_cap, sizeof(ChunkWork*));
    if (!pool->ring) {
        pthread_cond_destroy(&pool->completion_cv);
        pthread_cond_destroy(&pool->not_full);
        pthread_cond_destroy(&pool->not_empty);
        pthread_mutex_destroy(&pool->cmtx);
        pthread_mutex_destroy(&pool->qmtx);
        free(pool);
        return NULL;
    }

    pool->threads = (pthread_t*)calloc((size_t)thread_count, sizeof(pthread_t));
    if (!pool->threads) {
        free(pool->ring);
        pthread_cond_destroy(&pool->completion_cv);
        pthread_cond_destroy(&pool->not_full);
        pthread_cond_destroy(&pool->not_empty);
        pthread_mutex_destroy(&pool->cmtx);
        pthread_mutex_destroy(&pool->qmtx);
        free(pool);
        return NULL;
    }

    int started = 0;
    for (int i = 0; i < thread_count; i++) {
        if (pthread_create(&pool->threads[i], NULL, wim_pool_worker_main, pool) != 0)
            break;
        started++;
    }

    if (started == 0) {
        free(pool->threads);
        free(pool->ring);
        pthread_cond_destroy(&pool->completion_cv);
        pthread_cond_destroy(&pool->not_full);
        pthread_cond_destroy(&pool->not_empty);
        pthread_mutex_destroy(&pool->cmtx);
        pthread_mutex_destroy(&pool->qmtx);
        free(pool);
        return NULL;
    }

    pool->thread_count = started;
    return pool;
}

static void inflight_free(WimBlobInflight* b);

void wim_pool_destroy(struct WimThreadPool* pool)
{
    if (!pool) return;

    pthread_mutex_lock(&pool->qmtx);
    pool->shutdown = 1;
    pthread_cond_broadcast(&pool->not_empty);
    pthread_mutex_unlock(&pool->qmtx);

    for (int i = 0; i < pool->thread_count; i++)
        pthread_join(pool->threads[i], NULL);

    /* Free remaining inflight records exactly once. */
    pool->comp_head = NULL;
    pool->comp_tail = NULL;
    pool->comp_count = 0;
    for (uint32_t i = 0; i < INFLIGHT_HT_BUCKETS; i++) {
        WimBlobInflight* cur = pool->inflight_ht[i];
        while (cur) {
            WimBlobInflight* next = cur->ht_next;
            inflight_free(cur);
            cur = next;
        }
        pool->inflight_ht[i] = NULL;
    }

    pthread_cond_destroy(&pool->completion_cv);
    pthread_cond_destroy(&pool->not_full);
    pthread_cond_destroy(&pool->not_empty);
    pthread_mutex_destroy(&pool->cmtx);
    pthread_mutex_destroy(&pool->qmtx);
    free(pool->ring);
    free(pool->threads);
    free(pool);
}

static void wim_pool_submit_chunk(struct WimThreadPool* pool, ChunkWork* w)
{
    pthread_mutex_lock(&pool->qmtx);
    while (pool->ring_count == pool->ring_cap)
        pthread_cond_wait(&pool->not_full, &pool->qmtx);
    ring_push_locked(pool, w);
    pthread_cond_signal(&pool->not_empty);
    pthread_mutex_unlock(&pool->qmtx);
}

/* Bulk submit; wake at most the workers that are idle. */
static void wim_pool_submit_chunks(struct WimThreadPool* pool,
                                   ChunkWork** items, uint32_t count)
{
    if (count == 0) return;

    pthread_mutex_lock(&pool->qmtx);
    while (count > 0) {
        if (pool->ring_count == pool->ring_cap) {
            do {
                pthread_cond_wait(&pool->not_full, &pool->qmtx);
            } while (pool->ring_count == pool->ring_cap);
        }
        uint32_t free_slots = pool->ring_cap - pool->ring_count;
        uint32_t to_push = (count < free_slots) ? count : free_slots;
        for (uint32_t i = 0; i < to_push; i++)
            ring_push_locked(pool, items[i]);
        items += to_push;
        count -= to_push;

        uint32_t to_wake = pool->idle_workers;
        if (to_wake > to_push) to_wake = to_push;
        for (uint32_t i = 0; i < to_wake; i++)
            pthread_cond_signal(&pool->not_empty);
    }
    pthread_mutex_unlock(&pool->qmtx);
}

static void wim_pool_begin_blob(struct WimThreadPool* pool)
{
    atomic_fetch_add_explicit(&pool->outstanding, 1, memory_order_release);
}

static WimBlobInflight* wim_pool_pop_all_completed(struct WimThreadPool* pool,
                                                   uint32_t* count_out)
{
    pthread_mutex_lock(&pool->cmtx);
    WimBlobInflight* head = pool->comp_head;
    uint32_t cnt = pool->comp_count;
    pool->comp_head = NULL;
    pool->comp_tail = NULL;
    pool->comp_count = 0;
    atomic_fetch_sub_explicit(&pool->outstanding, cnt, memory_order_acq_rel);
    pthread_mutex_unlock(&pool->cmtx);
    if (count_out) *count_out = cnt;
    return head;
}

static WimBlobInflight* wim_pool_wait_completed(struct WimThreadPool* pool)
{
    pthread_mutex_lock(&pool->cmtx);
    while (pool->comp_head == NULL &&
           atomic_load_explicit(&pool->outstanding, memory_order_acquire) > 0)
        pthread_cond_wait(&pool->completion_cv, &pool->cmtx);
    WimBlobInflight* b = pool->comp_head;
    if (b) {
        pool->comp_head = b->next;
        if (!pool->comp_head) pool->comp_tail = NULL;
        pool->comp_count--;
        atomic_fetch_sub_explicit(&pool->outstanding, 1, memory_order_acq_rel);
        b->next = NULL;
    }
    pthread_mutex_unlock(&pool->cmtx);
    return b;
}

static int wim_pool_has_outstanding(struct WimThreadPool* pool)
{
    return atomic_load_explicit(&pool->outstanding, memory_order_acquire) > 0;
}

static inline uint32_t inflight_hash_bucket(const uint8_t sha1[20])
{
    uint32_t h;
    memcpy(&h, sha1, 4);
    return h & (INFLIGHT_HT_BUCKETS - 1);
}

static void inflight_ht_insert(struct WimThreadPool* pool, WimBlobInflight* b)
{
    uint32_t bk = inflight_hash_bucket(b->sha1);
    b->ht_next = pool->inflight_ht[bk];
    pool->inflight_ht[bk] = b;
}

static WimBlobInflight* inflight_ht_find(struct WimThreadPool* pool,
                                         const uint8_t sha1[20])
{
    uint32_t bk = inflight_hash_bucket(sha1);
    for (WimBlobInflight* cur = pool->inflight_ht[bk]; cur; cur = cur->ht_next) {
        if (memcmp(cur->sha1, sha1, 20) == 0)
            return cur;
    }
    return NULL;
}

static void inflight_ht_remove(struct WimThreadPool* pool, WimBlobInflight* b)
{
    uint32_t bk = inflight_hash_bucket(b->sha1);
    WimBlobInflight** pp = &pool->inflight_ht[bk];
    while (*pp) {
        if (*pp == b) {
            *pp = b->ht_next;
            b->ht_next = NULL;
            return;
        }
        pp = &(*pp)->ht_next;
    }
}

#endif /* !_WIN32 */

/* ================================================================
 *  Blob writing
 * ================================================================ */

#ifndef _WIN32
static int drain_completed_blobs(WimCtx* ctx, int block);
#endif

static inline int blob_is_failed(const WimBlobInflight* b)
{
#ifndef _WIN32
    return atomic_load_explicit(&b->failed, memory_order_relaxed);
#else
    return b->failed;
#endif
}

static inline void free_owned_chunk(ChunkWork* w)
{
    if (w->owns_out && w->out) {
        free(w->out);
        w->out = NULL;
        w->owns_out = 0;
    }
}

#ifndef _WIN32
static void inflight_free(WimBlobInflight* b)
{
    if (!b) return;
    if (b->chunks) {
        for (uint64_t i = 0; i < b->num_chunks; i++)
            free_owned_chunk(&b->chunks[i]);
        free(b->chunks);
    }
    if (b->free_fn && b->data)
        b->free_fn(b->free_arg, (size_t)b->size);
    free(b);
}
#endif

static int commit_blob(WimCtx* ctx, WimBlobInflight* b)
{
    uint64_t blob_offset = (uint64_t)ftello(ctx->file);
    uint64_t written_size = 0;

    if (blob_is_failed(b))
        return -1;

    if (ctx->use_xpress && b->size > 0 && b->num_chunks > 0) {
        int use_64bit = (b->size > 0xFFFFFFFFULL);
        size_t entry_size = use_64bit ? 8 : 4;
        size_t table_size = (size_t)((b->num_chunks - 1) * entry_size);

        uint64_t total_comp = table_size;
        for (uint64_t i = 0; i < b->num_chunks; i++)
            total_comp += b->chunks[i].out_len;

        if (total_comp >= b->size) {
            if (b->size > 0 &&
                wim_fwrite(b->data, (size_t)b->size, ctx->file) != 0)
                return -1;
            written_size = b->size;
        } else {
            if (table_size > 0) {
                uint8_t* table_buf = (uint8_t*)calloc(1, table_size);
                if (!table_buf) return -1;
                uint64_t running_offset = 0;
                for (uint64_t i = 0; i < b->num_chunks - 1; i++) {
                    running_offset += b->chunks[i].out_len;
                    if (use_64bit) {
                        memcpy(table_buf + i * 8, &running_offset, 8);
                    } else {
                        uint32_t val = (uint32_t)running_offset;
                        memcpy(table_buf + i * 4, &val, 4);
                    }
                }
                if (wim_fwrite(table_buf, table_size, ctx->file) != 0) {
                    free(table_buf);
                    return -1;
                }
                free(table_buf);
            }

            /* Coalesce adjacent raw chunks. */
            for (uint64_t i = 0; i < b->num_chunks; ) {
                ChunkWork* ci = &b->chunks[i];
                if (ci->stored_raw && !ci->owns_out) {
                    size_t run_size = ci->in_len;
                    uint64_t j = i + 1;
                    while (j < b->num_chunks) {
                        ChunkWork* cj = &b->chunks[j];
                        ChunkWork* cp = &b->chunks[j - 1];
                        if (!cj->stored_raw || cj->owns_out ||
                            cj->in != cp->in + cp->in_len)
                            break;
                        run_size += cj->in_len;
                        j++;
                    }
                    if (wim_fwrite(ci->in, run_size, ctx->file) != 0)
                        return -1;
                    i = j;
                } else {
                    if (wim_fwrite(ci->out, ci->out_len, ctx->file) != 0)
                        return -1;
                    i++;
                }
            }
            written_size = total_comp;
        }
    } else {
        if (b->size > 0 && b->data &&
            wim_fwrite(b->data, (size_t)b->size, ctx->file) != 0)
            return -1;
        written_size = b->size;
    }

    WimBlob entry;
    memset(&entry, 0, sizeof(entry));
    memcpy(entry.sha1.hash, b->sha1, 20);
    entry.original_size = b->size;
    entry.compressed_size = written_size;
    entry.offset = blob_offset;
    entry.ref_count = 1 + b->extra_refs;
    entry.flags = 0;
    if (b->is_metadata)
        entry.flags |= WIM_RESHDR_FLAG_METADATA;
    if (ctx->use_xpress && written_size != b->size)
        entry.flags |= WIM_RESHDR_FLAG_COMPRESSED;

    if (wim_ctx_add_blob(ctx, &entry) != 0)
        return -1;

    return 0;
}

#ifndef _WIN32
static int drain_completed_blobs(WimCtx* ctx, int block)
{
    if (!ctx->pool) return 0;

    if (block) {
        WimBlobInflight* first = wim_pool_wait_completed(ctx->pool);
        if (!first) return 0;
        inflight_ht_remove(ctx->pool, first);
        int ret = commit_blob(ctx, first);
        inflight_free(first);
        if (ret != 0) return -1;
    }

    WimBlobInflight* head = wim_pool_pop_all_completed(ctx->pool, NULL);
    while (head) {
        WimBlobInflight* next = head->next;
        head->next = NULL;
        inflight_ht_remove(ctx->pool, head);
        int ret = commit_blob(ctx, head);
        inflight_free(head);
        if (ret != 0) return -1;
        head = next;
    }
    return 0;
}
#endif /* !_WIN32 */

/* Return -1 only before ownership transfers to the async path. */
#ifndef _WIN32
static int submit_blob_async(WimCtx* ctx, WimBlobInflight* b)
{
    uint32_t chunk_size = ctx->header.chunk_size;
    uint64_t num_chunks = (b->size + chunk_size - 1) / chunk_size;
    b->chunk_size = chunk_size;
    b->num_chunks = num_chunks;
    b->chunks = (ChunkWork*)calloc((size_t)num_chunks, sizeof(ChunkWork));
    if (!b->chunks) {
        free(b);
        return -1;
    }

    uint64_t in_offset = 0;
    for (uint64_t i = 0; i < num_chunks; i++) {
        uint64_t remaining = b->size - in_offset;
        uint32_t this_chunk = (remaining < chunk_size) ?
                              (uint32_t)remaining : chunk_size;
        b->chunks[i].in = b->data + in_offset;
        b->chunks[i].in_len = this_chunk;
        b->chunks[i].parent = b;
        in_offset += this_chunk;
    }

    inflight_ht_insert(ctx->pool, b);

    atomic_store_explicit(&b->chunks_left, num_chunks, memory_order_release);
    wim_pool_begin_blob(ctx->pool);

#ifdef WIMAGE_PROFILE
    WP_TS(tsub);
    g_timing.n_chunks += num_chunks;
#endif
    {
        enum { WIM_BLOB_PTR_STACK = 64 };
        ChunkWork* stackbuf[WIM_BLOB_PTR_STACK];
        ChunkWork** ptrs;
        int ptrs_on_heap = 0;
        if (num_chunks <= WIM_BLOB_PTR_STACK) {
            ptrs = stackbuf;
        } else {
            ptrs = (ChunkWork**)malloc((size_t)num_chunks * sizeof(ChunkWork*));
            if (!ptrs) {
                for (uint64_t i = 0; i < num_chunks; i++)
                    wim_pool_submit_chunk(ctx->pool, &b->chunks[i]);
                goto submitted;
            }
            ptrs_on_heap = 1;
        }
        for (uint64_t i = 0; i < num_chunks; i++)
            ptrs[i] = &b->chunks[i];
        wim_pool_submit_chunks(ctx->pool, ptrs, (uint32_t)num_chunks);
        if (ptrs_on_heap) free(ptrs);
    }
submitted:;
#ifdef WIMAGE_PROFILE
    WP_ADD(t_submit, tsub);
    WP_TS(tdr);
#endif

    if (drain_completed_blobs(ctx, 0) != 0)
        ctx->write_error = 1;
#ifdef WIMAGE_PROFILE
    WP_ADD(t_drain_opp, tdr);
#endif
    return 0;
}
#endif /* !_WIN32 */

static int write_blob_sync(WimCtx* ctx, const uint8_t* data_ptr, uint64_t size,
                           uint8_t sha1_out[20], int is_metadata)
{
    sha1_hash(data_ptr, size, sha1_out);

    if (!is_metadata) {
        int existing = wim_ctx_find_blob(ctx, sha1_out);
        if (existing >= 0) {
            ctx->blobs[existing].ref_count++;
            return 0;
        }
#ifndef _WIN32
        if (ctx->pool) {
            WimBlobInflight* dup = inflight_ht_find(ctx->pool, sha1_out);
            if (dup) {
                dup->extra_refs++;
                return 0;
            }
        }
#endif
    }

    WimBlobInflight b;
    memset(&b, 0, sizeof(b));
    memcpy(b.sha1, sha1_out, 20);
    b.is_metadata = is_metadata;
    b.size = size;
    b.data = (uint8_t*)data_ptr;  /* we don't own it; free_fn is NULL */
    b.free_fn = NULL;

    if (ctx->use_xpress && size > 0) {
        uint32_t chunk_size = ctx->header.chunk_size;
        uint64_t num_chunks = (size + chunk_size - 1) / chunk_size;
        b.num_chunks = num_chunks;
        b.chunk_size = chunk_size;
        b.chunks = (ChunkWork*)calloc((size_t)num_chunks, sizeof(ChunkWork));
        if (!b.chunks) return -1;

        uint64_t in_offset = 0;
        for (uint64_t i = 0; i < num_chunks; i++) {
            uint64_t remaining = size - in_offset;
            uint32_t this_chunk = (remaining < chunk_size) ?
                                  (uint32_t)remaining : chunk_size;
            b.chunks[i].in = data_ptr + in_offset;
            b.chunks[i].in_len = this_chunk;
            b.chunks[i].parent = NULL;
            in_offset += this_chunk;
        }

        XpressCompressScratch* scratch = xpress_huff_create_scratch(chunk_size);
        for (uint64_t i = 0; i < num_chunks; i++)
            compress_one_chunk(&b.chunks[i], scratch);
        xpress_huff_destroy_scratch(scratch);

        for (uint64_t i = 0; i < num_chunks; i++) {
            if (b.chunks[i].failed) {
                for (uint64_t j = 0; j < num_chunks; j++)
                    free_owned_chunk(&b.chunks[j]);
                free(b.chunks);
                return -1;
            }
        }
    }

    int ret = commit_blob(ctx, &b);
    if (b.chunks) {
        for (uint64_t i = 0; i < b.num_chunks; i++)
            free_owned_chunk(&b.chunks[i]);
        free(b.chunks);
    }
    return ret;
}

/* Capture callback for wim_capture_dir(). */
static int blob_writer_cb(uint8_t* data, uint64_t size,
                          void (*free_fn)(void*, size_t), void* free_arg,
                          uint8_t sha1_out[20], void* user)
{
    WimCtx* ctx = (WimCtx*)user;

    if (ctx->write_error)
        return -1;

#ifndef _WIN32
    if (ctx->pool && ctx->use_xpress && size > 0) {
#ifdef WIMAGE_PROFILE
        WP_TS(t0); g_timing.n_blobs++;
#endif
#ifdef WIMAGE_PROFILE
        { WP_TS(ts); sha1_hash(data, size, sha1_out); WP_ADD(t_sha1, ts); }
#else
        sha1_hash(data, size, sha1_out);
#endif

#ifdef WIMAGE_PROFILE
        WP_TS(tl);
#endif
        int existing = wim_ctx_find_blob(ctx, sha1_out);
        if (existing >= 0) {
            ctx->blobs[existing].ref_count++;
#ifdef WIMAGE_PROFILE
            WP_ADD(t_lookups, tl); WP_ADD(t_blob_total, t0);
#endif
            if (free_fn) free_fn(free_arg, (size_t)size);
            return 0;
        }
        {
            WimBlobInflight* dup = inflight_ht_find(ctx->pool, sha1_out);
            if (dup) {
                dup->extra_refs++;
#ifdef WIMAGE_PROFILE
                WP_ADD(t_lookups, tl); WP_ADD(t_blob_total, t0);
#endif
                if (free_fn) free_fn(free_arg, (size_t)size);
                return 0;
            }
        }
#ifdef WIMAGE_PROFILE
        WP_ADD(t_lookups, tl);
#endif

        WimBlobInflight* b = (WimBlobInflight*)calloc(1, sizeof(*b));
        if (!b) return -1;
        memcpy(b->sha1, sha1_out, 20);
        b->hash_ready = 1;
        b->is_metadata = 0;
        b->size = size;
        b->data = data;
        b->free_fn = free_fn;
        b->free_arg = free_arg;

        int ret = submit_blob_async(ctx, b);
#ifdef WIMAGE_PROFILE
        WP_ADD(t_blob_total, t0);
#endif
        return ret;
    }
#endif

    int ret = write_blob_sync(ctx, data, size, sha1_out, 0);
    if (ret == 0) {
        if (free_fn) free_fn(free_arg, (size_t)size);
    } else {
        ctx->write_error = 1;
    }
    return ret;
}

/* ================================================================
 *  Dentry serialization
 * ================================================================ */

#define DENTRY_FIXED_SIZE 102

static size_t dentry_serialized_size(const WimDentry* d)
{
    uint16_t name_nbytes = (uint16_t)(d->name_utf16_len * 2);
    size_t total = DENTRY_FIXED_SIZE;
    if (name_nbytes > 0)
        total += name_nbytes + 2; /* name + null terminator */
    /* Pad to 8-byte alignment */
    if (total % 8 != 0)
        total += 8 - (total % 8);
    return total;
}

static void serialize_dentry(const WimDentry* d, uint64_t subdir_off_override,
                             uint8_t* buf, size_t padded_len)
{
    memset(buf, 0, padded_len);

    uint16_t name_nbytes = (uint16_t)(d->name_utf16_len * 2);
    size_t offset = 0;

    /* length */
    uint64_t len64 = padded_len;
    memcpy(buf + offset, &len64, 8);                offset += 8;
    /* attributes */
    memcpy(buf + offset, &d->attributes, 4);         offset += 4;
    /* security_id */
    memcpy(buf + offset, &d->security_id, 4);        offset += 4;
    /* subdir_offset */
    memcpy(buf + offset, &subdir_off_override, 8);   offset += 8;
    /* unused1, unused2 */
    offset += 16;
    /* creation_time */
    memcpy(buf + offset, &d->creation_time, 8);      offset += 8;
    /* last_access_time */
    memcpy(buf + offset, &d->last_access_time, 8);   offset += 8;
    /* last_write_time */
    memcpy(buf + offset, &d->last_write_time, 8);    offset += 8;
    /* sha1 */
    memcpy(buf + offset, d->sha1, 20);               offset += 20;
    /* reparse_tag (0) */
    offset += 4;
    /* hard_link_group_id (0) */
    offset += 8;
    /* num_streams */
    uint16_t zero16 = 0;
    memcpy(buf + offset, &zero16, 2);                offset += 2;
    /* short_name_nbytes */
    memcpy(buf + offset, &zero16, 2);                offset += 2;
    /* file_name_nbytes */
    memcpy(buf + offset, &name_nbytes, 2);           offset += 2;

    /* File name (UTF-16LE + null) */
    if (name_nbytes > 0) {
        memcpy(buf + offset, d->name_utf16, name_nbytes);
        offset += name_nbytes;
        memset(buf + offset, 0, 2);
    }
}

typedef struct {
    const WimDentry* parent;
    size_t parent_buf_offset;
} ChildGroup;

static int serialize_dentry_tree(const WimDentry* root, uint8_t** out_buf, size_t* out_size)
{
    /* Security data: total_length=8, num_entries=0 */
    size_t root_size = dentry_serialized_size(root);

    /* BFS: collect directory groups */
    size_t g_cap = 64, g_count = 0;
    ChildGroup* groups = (ChildGroup*)malloc(g_cap * sizeof(ChildGroup));
    if (!groups) return -1;

    size_t pos = 8; /* security data */
    size_t root_offset = pos;
    pos += root_size;

    if (root->attributes & WIM_FILE_ATTRIBUTE_DIRECTORY) {
        if (g_count >= g_cap) {
            g_cap *= 2;
            ChildGroup* tmp = (ChildGroup*)realloc(groups, g_cap * sizeof(ChildGroup));
            if (!tmp) { free(groups); return -1; }
            groups = tmp;
        }
        groups[g_count].parent = root;
        groups[g_count].parent_buf_offset = root_offset;
        g_count++;
    }

    /* Compute group offsets */
    size_t go_cap = 64, go_count = 0;
    size_t* group_offsets = (size_t*)malloc(go_cap * sizeof(size_t));
    if (!group_offsets) { free(groups); return -1; }

    size_t gi = 0;
    while (gi < g_count) {
        const WimDentry* parent = groups[gi].parent;

        if (go_count >= go_cap) {
            go_cap *= 2;
            size_t* tmp_go = (size_t*)realloc(group_offsets, go_cap * sizeof(size_t));
            if (!tmp_go) { free(group_offsets); free(groups); return -1; }
            group_offsets = tmp_go;
        }
        group_offsets[go_count++] = pos;

        for (size_t c = 0; c < parent->child_count; c++) {
            const WimDentry* child = &parent->children[c];
            size_t child_size = dentry_serialized_size(child);
            size_t child_offset = pos;
            pos += child_size;

            if (child->attributes & WIM_FILE_ATTRIBUTE_DIRECTORY) {
                if (g_count >= g_cap) {
                    g_cap *= 2;
                    ChildGroup* tmp_g = (ChildGroup*)realloc(groups, g_cap * sizeof(ChildGroup));
                    if (!tmp_g) { free(groups); free(group_offsets); return -1; }
                    groups = tmp_g;
                }
                groups[g_count].parent = child;
                groups[g_count].parent_buf_offset = child_offset;
                g_count++;
            }
        }
        /* 8-byte terminator */
        pos += 8;
        gi++;
    }

    /* Serialize */
    uint8_t* buf = (uint8_t*)calloc(1, pos);
    if (!buf) { free(groups); free(group_offsets); return -1; }

    /* Security data */
    uint32_t sec_total = 8;
    uint32_t sec_count = 0;
    memcpy(buf, &sec_total, 4);
    memcpy(buf + 4, &sec_count, 4);

    /* Root dentry */
    uint64_t root_subdir = (g_count > 0) ? group_offsets[0] : 0;
    serialize_dentry(root, root_subdir, buf + root_offset, root_size);

    /* Child groups */
    gi = 0;
    size_t dir_index = 1; /* groups[0] is root's children */
    while (gi < go_count) {
        const WimDentry* parent = groups[gi].parent;
        size_t cpos = group_offsets[gi];

        for (size_t c = 0; c < parent->child_count; c++) {
            const WimDentry* child = &parent->children[c];
            size_t child_size = dentry_serialized_size(child);

            uint64_t child_subdir = child->subdir_offset;
            if (child->attributes & WIM_FILE_ATTRIBUTE_DIRECTORY) {
                if (dir_index < go_count)
                    child_subdir = group_offsets[dir_index];
                dir_index++;
            }

            serialize_dentry(child, child_subdir, buf + cpos, child_size);
            cpos += child_size;
        }
        /* 8-byte terminator already zero from calloc */
        gi++;
    }

    free(groups);
    free(group_offsets);

    *out_buf = buf;
    *out_size = pos;
    return 0;
}

/* ================================================================
 *  Write finalization helpers
 * ================================================================ */

static int write_metadata(WimCtx* ctx, int image_idx)
{
    if (image_idx < 0 || image_idx >= (int)ctx->image_count)
        return -1;

    uint8_t* meta_buf = NULL;
    size_t meta_size = 0;
    int ret = serialize_dentry_tree(&ctx->images[image_idx].root, &meta_buf, &meta_size);
    if (ret != 0)
        return ret;

    uint8_t sha1_out[20];
    ret = write_blob_sync(ctx, meta_buf, meta_size, sha1_out, 1);
    free(meta_buf);
    if (ret != 0)
        return ret;

    /* Store metadata blob info */
    int blob_idx = wim_ctx_find_blob(ctx, sha1_out);
    if (blob_idx >= 0) {
        ctx->images[image_idx].metadata_blob = ctx->blobs[blob_idx];
        if (ctx->header.boot_index == (uint32_t)(image_idx + 1))
            reshdr_set(&ctx->header.boot_metadata,
                       ctx->images[image_idx].metadata_blob.compressed_size,
                       ctx->images[image_idx].metadata_blob.flags,
                       ctx->images[image_idx].metadata_blob.offset,
                       ctx->images[image_idx].metadata_blob.original_size);
    }

    return 0;
}

static int write_lookup_table(WimCtx* ctx)
{
    uint64_t offset = (uint64_t)ftello(ctx->file);

    for (size_t i = 0; i < ctx->blob_count; i++) {
        WimLookupEntry entry;
        memset(&entry, 0, sizeof(entry));

        reshdr_set(&entry.reshdr, ctx->blobs[i].compressed_size, ctx->blobs[i].flags,
                   ctx->blobs[i].offset, ctx->blobs[i].original_size);
        entry.part_number = 1;
        entry.ref_count = ctx->blobs[i].ref_count;
        memcpy(entry.sha1, ctx->blobs[i].sha1.hash, 20);

        if (wim_fwrite(&entry, WIM_LOOKUP_ENTRY_SIZE, ctx->file) != 0)
            return -1;
    }

    uint64_t size = (uint64_t)ctx->blob_count * WIM_LOOKUP_ENTRY_SIZE;
    reshdr_set(&ctx->header.lookup_table, size, 0, offset, size);

    return 0;
}

static char* xml_escape(const char* s)
{
    size_t cap = strlen(s) * 2 + 1;
    char* out = (char*)malloc(cap);
    if (!out) return NULL;
    size_t len = 0;
    for (; *s; s++) {
        const char* rep = NULL;
        switch (*s) {
            case '&': rep = "&amp;"; break;
            case '<': rep = "&lt;"; break;
            case '>': rep = "&gt;"; break;
            case '"': rep = "&quot;"; break;
        }
        if (rep) {
            size_t rlen = strlen(rep);
            while (len + rlen + 1 > cap) { cap *= 2; out = (char*)realloc(out, cap); if (!out) return NULL; }
            memcpy(out + len, rep, rlen);
            len += rlen;
        } else {
            if (len + 2 > cap) { cap *= 2; out = (char*)realloc(out, cap); if (!out) return NULL; }
            out[len++] = *s;
        }
    }
    out[len] = '\0';
    return out;
}

static char* generate_xml(WimCtx* ctx)
{
    size_t cap = 4096;
    size_t len = 0;
    char* xml = (char*)malloc(cap);
    if (!xml) return NULL;

#define XML_APPEND(s) do { \
    size_t _slen = strlen(s); \
    while (len + _slen + 1 > cap) { cap *= 2; xml = (char*)realloc(xml, cap); if (!xml) return NULL; } \
    memcpy(xml + len, s, _slen); len += _slen; xml[len] = '\0'; \
} while (0)

    char numbuf[64];

    XML_APPEND("<?xml version=\"1.0\" encoding=\"UTF-16\"?>\n");
    XML_APPEND("<WIM>\n");

    /* Total bytes */
    uint64_t total = 0;
    for (size_t i = 0; i < ctx->blob_count; i++)
        total += ctx->blobs[i].compressed_size;

    snprintf(numbuf, sizeof(numbuf), "%llu", (unsigned long long)total);
    XML_APPEND("<TOTALBYTES>");
    XML_APPEND(numbuf);
    XML_APPEND("</TOTALBYTES>\n");

    for (size_t i = 0; i < ctx->image_count; i++) {
        snprintf(numbuf, sizeof(numbuf), "%u", (unsigned)(i + 1));
        XML_APPEND("<IMAGE INDEX=\"");
        XML_APPEND(numbuf);
        XML_APPEND("\">\n");

        WimImageInfo* info = NULL;
        WimImageInfo empty_info;
        if (i < ctx->image_info_count) {
            info = &ctx->image_infos[i];
        } else {
            memset(&empty_info, 0, sizeof(empty_info));
            info = &empty_info;
        }

        snprintf(numbuf, sizeof(numbuf), "%u", info->dir_count);
        XML_APPEND("<DIRCOUNT>");
        XML_APPEND(numbuf);
        XML_APPEND("</DIRCOUNT>\n");

        snprintf(numbuf, sizeof(numbuf), "%u", info->file_count);
        XML_APPEND("<FILECOUNT>");
        XML_APPEND(numbuf);
        XML_APPEND("</FILECOUNT>\n");

        snprintf(numbuf, sizeof(numbuf), "%llu", (unsigned long long)info->total_bytes);
        XML_APPEND("<TOTALBYTES>");
        XML_APPEND(numbuf);
        XML_APPEND("</TOTALBYTES>\n");

        XML_APPEND("<HARDLINKBYTES>0</HARDLINKBYTES>\n");

        /* CREATIONTIME */
        uint32_t hi = (uint32_t)(info->creation_time >> 32);
        uint32_t lo = (uint32_t)(info->creation_time & 0xFFFFFFFF);
        snprintf(numbuf, sizeof(numbuf), "0x%08X", hi);
        XML_APPEND("<CREATIONTIME><HIGHPART>");
        XML_APPEND(numbuf);
        XML_APPEND("</HIGHPART><LOWPART>");
        snprintf(numbuf, sizeof(numbuf), "0x%08X", lo);
        XML_APPEND(numbuf);
        XML_APPEND("</LOWPART></CREATIONTIME>\n");

        /* LASTMODIFICATIONTIME */
        hi = (uint32_t)(info->modification_time >> 32);
        lo = (uint32_t)(info->modification_time & 0xFFFFFFFF);
        snprintf(numbuf, sizeof(numbuf), "0x%08X", hi);
        XML_APPEND("<LASTMODIFICATIONTIME><HIGHPART>");
        XML_APPEND(numbuf);
        XML_APPEND("</HIGHPART><LOWPART>");
        snprintf(numbuf, sizeof(numbuf), "0x%08X", lo);
        XML_APPEND(numbuf);
        XML_APPEND("</LOWPART></LASTMODIFICATIONTIME>\n");

        if (info->name[0] != '\0') {
            char* esc = xml_escape(info->name);
            if (esc) { XML_APPEND("<NAME>"); XML_APPEND(esc); XML_APPEND("</NAME>\n"); free(esc); }
        }
        if (info->description[0] != '\0') {
            char* esc = xml_escape(info->description);
            if (esc) { XML_APPEND("<DESCRIPTION>"); XML_APPEND(esc); XML_APPEND("</DESCRIPTION>\n"); free(esc); }
        }

        XML_APPEND("</IMAGE>\n");
    }

    XML_APPEND("</WIM>\n");

#undef XML_APPEND

    return xml;
}

static int write_xml_data(WimCtx* ctx)
{
    char* xml = generate_xml(ctx);
    if (!xml) return -1;

    /* Convert to UTF-16LE with BOM */
    uint16_t* utf16 = NULL;
    size_t utf16_len = 0;
    if (utf8_to_utf16le(xml, &utf16, &utf16_len) != 0) {
        free(xml);
        return -1;
    }
    free(xml);

    uint64_t offset = (uint64_t)ftello(ctx->file);

    /* Write BOM */
    uint16_t bom = 0xFEFF;
    if (wim_fwrite(&bom, 2, ctx->file) != 0) { free(utf16); return -1; }

    /* Write UTF-16LE data */
    if (wim_fwrite(utf16, 2 * utf16_len, ctx->file) != 0) { free(utf16); return -1; }

    uint64_t size = 2 + utf16_len * 2;
    reshdr_set(&ctx->header.xml_data, size, 0, offset, size);

    free(utf16);
    return 0;
}

static int write_integrity_table(WimCtx* ctx)
{
    /* Integrity hashes cover from after the header to the start of XML data.
     * XML data and integrity table itself are excluded from the hash range. */
    uint64_t data_end = ctx->header.xml_data.offset;
    uint64_t data_start = WIM_HEADER_SIZE;
    uint64_t data_size = data_end - data_start;

    if (data_size == 0)
        return 0;

    uint32_t chunk_size = 10485760; /* 10 MB */
    uint32_t num_chunks = (uint32_t)((data_size + chunk_size - 1) / chunk_size);

    uint8_t* hashes = (uint8_t*)malloc(num_chunks * 20);
    uint8_t* read_buf = (uint8_t*)malloc(chunk_size);
    if (!hashes || !read_buf) {
        free(hashes);
        free(read_buf);
        return -1;
    }

    fseeko(ctx->file, (off_t)data_start, SEEK_SET);
    uint64_t remaining = data_size;

    for (uint32_t i = 0; i < num_chunks; i++) {
        uint32_t this_chunk = (remaining < chunk_size) ?
                              (uint32_t)remaining : chunk_size;

        if (fread(read_buf, 1, this_chunk, ctx->file) != this_chunk) {
            free(hashes);
            free(read_buf);
            return -1;
        }

        sha1_hash(read_buf, this_chunk, hashes + i * 20);
        remaining -= this_chunk;
    }

    free(read_buf);

    /* Seek to end of file (after XML data) to write integrity table */
    fseeko(ctx->file, 0, SEEK_END);
    uint64_t integ_offset = (uint64_t)ftello(ctx->file);

    /* Table header: size, num_entries, chunk_size */
    uint32_t table_size = 12 + num_chunks * 20;
    if (wim_fwrite(&table_size, 4, ctx->file) != 0 ||
        wim_fwrite(&num_chunks, 4, ctx->file) != 0 ||
        wim_fwrite(&chunk_size, 4, ctx->file) != 0 ||
        wim_fwrite(hashes, num_chunks * 20, ctx->file) != 0) {
        free(hashes);
        return -1;
    }

    free(hashes);

    reshdr_set(&ctx->header.integrity, table_size, 0, integ_offset, table_size);
    return 0;
}

static int write_header(WimCtx* ctx)
{
    fseeko(ctx->file, 0, SEEK_SET);
    if (fwrite(&ctx->header, sizeof(WimHeader), 1, ctx->file) != 1)
        return -1;
    return 0;
}

static void count_dir_file(const WimDentry* d, uint32_t* dirs, uint32_t* files,
                           uint64_t* bytes)
{
    if (d->attributes & WIM_FILE_ATTRIBUTE_DIRECTORY) {
        (*dirs)++;
        for (size_t i = 0; i < d->child_count; i++)
            count_dir_file(&d->children[i], dirs, files, bytes);
    } else {
        (*files)++;
        *bytes += d->file_size;
    }
}

/* ================================================================
 *  Public API
 * ================================================================ */

int wim_create(WimCtx* ctx, const char* filename, int use_xpress)
{
    int num_threads = ctx->num_threads;

    wim_ctx_free(ctx);
    wim_ctx_init(ctx);
    ctx->num_threads = num_threads;

    ctx->file = fopen(filename, "w+b");
    if (!ctx->file) {
        fprintf(stderr, "Error: Cannot create '%s'\n", filename);
        return -1;
    }
    setvbuf(ctx->file, NULL, _IOFBF, 4 << 20);

    memset(&ctx->header, 0, sizeof(ctx->header));
    memcpy(ctx->header.magic, WIM_MAGIC, WIM_MAGIC_LEN);
    ctx->header.header_size = WIM_HEADER_SIZE;
    ctx->header.version = WIM_VERSION;
    ctx->header.flags = 0;
    if (use_xpress) {
        ctx->header.flags |= WIM_HDR_FLAG_COMPRESSION | WIM_HDR_FLAG_COMPRESS_XPRESS;
    }
    ctx->header.chunk_size = use_xpress ? 65536 : 0;

    /* Generate GUID from /dev/urandom, fallback to rand() */
    {
        FILE* urand = fopen("/dev/urandom", "rb");
        if (urand) {
            size_t n = fread(ctx->header.guid, 1, 16, urand);
            fclose(urand);
            if (n < 16)
                memset(ctx->header.guid + n, 0, 16 - n);
        } else {
            srand((unsigned)time(NULL) ^ (unsigned)getpid());
            for (int i = 0; i < 16; i++)
                ctx->header.guid[i] = (uint8_t)(rand() & 0xFF);
        }
    }

    ctx->header.part_number = 1;
    ctx->header.total_parts = 1;
    ctx->header.image_count = 0;

    ctx->writing = 1;
    ctx->use_xpress = use_xpress;

#ifndef _WIN32
    if (use_xpress && num_threads > 1)
        ctx->pool = wim_pool_create(num_threads);
#endif

    /* Write placeholder header */
    uint8_t placeholder[WIM_HEADER_SIZE];
    memset(placeholder, 0, WIM_HEADER_SIZE);
    if (wim_fwrite(placeholder, WIM_HEADER_SIZE, ctx->file) != 0)
        return -1;

    return 0;
}

int wim_capture_tree(WimCtx* ctx, const char* source_dir,
                     const char* image_name, const char* image_desc)
{
    if (!ctx->writing || !ctx->file)
        return -1;

    /* Grow images array */
    size_t new_count = ctx->image_count + 1;
    WimImageData* new_images = (WimImageData*)realloc(ctx->images,
                                                       new_count * sizeof(WimImageData));
    if (!new_images) return -1;
    ctx->images = new_images;

    WimImageData* img = &ctx->images[ctx->image_count];
    memset(img, 0, sizeof(*img));
    wim_dentry_init(&img->root);
    img->metadata_loaded = 1;

    int ret = wim_capture_dir(source_dir, &img->root, blob_writer_cb, ctx);
    if (ret != 0 || ctx->write_error) {
        wim_dentry_free(&img->root);
        memset(img, 0, sizeof(*img));
        if (ret != 0)
            return ret;
        return -1;
    }

    /* Grow image_infos array */
    WimImageInfo* new_infos = (WimImageInfo*)realloc(ctx->image_infos,
                                                      new_count * sizeof(WimImageInfo));
    if (!new_infos) {
        wim_dentry_free(&img->root);
        memset(img, 0, sizeof(*img));
        return -1;
    }
    ctx->image_infos = new_infos;

    WimImageInfo* info = &ctx->image_infos[ctx->image_count];
    memset(info, 0, sizeof(*info));
    if (image_name)
        snprintf(info->name, sizeof(info->name), "%s", image_name);
    if (image_desc)
        snprintf(info->description, sizeof(info->description), "%s", image_desc);

    info->dir_count = 0;
    info->file_count = 0;
    info->total_bytes = 0;
    count_dir_file(&img->root, &info->dir_count, &info->file_count, &info->total_bytes);
    info->creation_time = unix_to_filetime(time(NULL));
    info->modification_time = info->creation_time;

    ctx->image_count = new_count;
    ctx->image_info_count = new_count;
    ctx->header.image_count = (uint32_t)new_count;

    return 0;
}

int wim_finalize(WimCtx* ctx, int write_integrity)
{
    if (!ctx->writing || !ctx->file || ctx->write_error)
        return -1;

#ifndef _WIN32
    if (ctx->pool) {
#ifdef WIMAGE_PROFILE
        WP_TS(tdf);
#endif
        while (wim_pool_has_outstanding(ctx->pool)) {
            if (drain_completed_blobs(ctx, 1) != 0) {
                ctx->write_error = 1;
                return -1;
            }
        }
#ifdef WIMAGE_PROFILE
        WP_ADD(t_drain_fin, tdf);
#endif
    }
#endif

    /* Write metadata for each image */
    for (size_t i = 0; i < ctx->image_count; i++) {
        int ret = write_metadata(ctx, (int)i);
        if (ret != 0)
            return ret;
    }

    /* Write lookup table */
    int ret = write_lookup_table(ctx);
    if (ret != 0)
        return ret;

    /* Write XML data */
    ret = write_xml_data(ctx);
    if (ret != 0)
        return ret;

    /* Write integrity table if requested */
    if (write_integrity) {
        ret = write_integrity_table(ctx);
        if (ret != 0)
            return ret;
    }

    /* Write final header */
    ret = write_header(ctx);
    if (ret != 0)
        return ret;

    fclose(ctx->file);
    ctx->file = NULL;
    ctx->writing = 0;

#ifdef WIMAGE_PROFILE
    fprintf(stderr, "[PROF] blobs=%lu chunks=%lu\n",
            (unsigned long)g_timing.n_blobs, (unsigned long)g_timing.n_chunks);
    fprintf(stderr, "[PROF] blob_total=%.4f sha1=%.4f lookups=%.4f submit=%.4f drain_opp=%.4f drain_fin=%.4f\n",
            g_timing.t_blob_total, g_timing.t_sha1, g_timing.t_lookups,
            g_timing.t_submit, g_timing.t_drain_opp, g_timing.t_drain_fin);
#endif
    return 0;
}
