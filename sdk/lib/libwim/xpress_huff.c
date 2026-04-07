/*
 * Copyright (C) 2026 Ahmed ARIF <arif.ing@outlook.com>
 *
 * xpress_huff.c - Clean-room XPRESS Huffman (LZ77+Huffman) codec
 *
 * Written from scratch based on the WIM format analysis:
 *   - 512 Huffman symbols (0-255 literals, 256-511 match refs)
 *   - 256-byte packed code-length table (two 4-bit nibbles per byte)
 *   - Canonical Huffman codes, MSB-first bit consumption
 *   - 16-bit LE word refill when buffer < 16 bits
 *   - Inline length extension bytes share the stream pointer
 *
 */

#include "xpress_huff.h"
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

/* Runtime SIMD dispatch on Intel; decompress-only builds stay scalar. */
#if !defined(XPRESS_HUFF_DECOMPRESS_ONLY) && \
    (defined(__x86_64__) || defined(_M_X64))
#  include <emmintrin.h>
#  include <tmmintrin.h>
#  include <immintrin.h>
#  define WIMAGE_HAVE_SSE2  1
#  define WIMAGE_HAVE_SSSE3 1
#  define WIMAGE_HAVE_AVX2  1
#  if defined(__GNUC__) || defined(__clang__)
#    include <cpuid.h>
#  elif defined(_MSC_VER)
#    include <intrin.h>
#  endif

static int wimage_sse2_enabled  = 0;
static int wimage_ssse3_enabled = 0;
static int wimage_avx2_enabled  = 0;

/* PSHUFB masks for small-offset RLE expansion. */
#  if WIMAGE_HAVE_SSSE3
static int8_t wimage_rle_pshufb_masks[16][16][16]
    __attribute__((aligned(16)));

static void wimage_init_rle_masks(void)
{
    for (int moff = 1; moff <= 15; ++moff) {
        for (int phase = 0; phase < moff; ++phase) {
            for (int i = 0; i < 16; ++i) {
                wimage_rle_pshufb_masks[moff][phase][i] =
                    (int8_t)((unsigned)(phase + i) % (unsigned)moff);
            }
        }
    }
}
#  endif

static void wimage_detect_intel(void)
{
    unsigned int eax = 0, ebx = 0, ecx = 0, edx = 0;
#if defined(__GNUC__) || defined(__clang__)
    if (!__get_cpuid(0u, &eax, &ebx, &ecx, &edx))
        return;
#elif defined(_MSC_VER)
    int regs[4] = {0, 0, 0, 0};
    __cpuid(regs, 0);
    eax = (unsigned)regs[0]; ebx = (unsigned)regs[1];
    ecx = (unsigned)regs[2]; edx = (unsigned)regs[3];
#else
    return; /* no portable CPUID available */
#endif
    if (!(ebx == 0x756E6547u && edx == 0x49656E69u && ecx == 0x6C65746Eu))
        return;

    wimage_sse2_enabled = 1;

#if defined(__GNUC__) || defined(__clang__)
    if (__get_cpuid(1u, &eax, &ebx, &ecx, &edx) && (ecx & (1u << 9)))
        wimage_ssse3_enabled = 1;
#elif defined(_MSC_VER)
    {
        int r[4] = {0};
        __cpuid(r, 1);
        if (((unsigned)r[2] & (1u << 9)) != 0)
            wimage_ssse3_enabled = 1;
    }
#endif

#if defined(__GNUC__) || defined(__clang__)
    if (__builtin_cpu_supports("avx2"))
        wimage_avx2_enabled = 1;
#endif

#  if WIMAGE_HAVE_SSSE3
    if (wimage_ssse3_enabled)
        wimage_init_rle_masks();
#  endif
}

#  if defined(__GNUC__) || defined(__clang__)
__attribute__((constructor))
static void wimage_cpuid_init(void) { wimage_detect_intel(); }
#  endif

#  if WIMAGE_HAVE_AVX2 && (defined(__GNUC__) || defined(__clang__))
__attribute__((target("avx2"), hot))
static inline void xpress_decopy_avx2(uint8_t* dst,
                                      const uint8_t* src,
                                      uint32_t n)
{
    while (n >= 32) {
        __m256i v = _mm256_loadu_si256((const __m256i*)src);
        _mm256_storeu_si256((__m256i*)dst, v);
        src += 32; dst += 32; n -= 32;
    }
}
#  endif

#  if WIMAGE_HAVE_SSSE3 && (defined(__GNUC__) || defined(__clang__))
__attribute__((target("ssse3"), noinline))
static uint32_t xpress_rle_expand_ssse3(uint8_t* dst,
                                        const uint8_t* src,
                                        uint32_t moff,
                                        uint32_t copy_len)
{
    uint8_t pat_bytes[16] = {0};
    for (uint32_t i = 0; i < moff; ++i)
        pat_bytes[i] = src[i];
    __m128i pat = _mm_loadu_si128((const __m128i*)pat_bytes);

    uint32_t done = 0;
    int phase = 0;
    while (done + 16 <= copy_len) {
        __m128i mask =
            _mm_load_si128((const __m128i*)wimage_rle_pshufb_masks[moff][phase]);
        _mm_storeu_si128((__m128i*)(dst + done), _mm_shuffle_epi8(pat, mask));
        done += 16;
        phase = (int)(((unsigned)phase + 16u) % (unsigned)moff);
    }
    return done;
}
#  endif /* SSSE3 implementation */
#endif

/*======================================================================
 * Byte-order helpers
 *======================================================================*/

static inline uint16_t rd16(const uint8_t* p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static inline uint32_t rd32(const uint8_t* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline void wr16(uint8_t* p, uint16_t v)
{
    p[0] = (uint8_t)v;  p[1] = (uint8_t)(v >> 8);
}

/*======================================================================
 * Constants
 *======================================================================*/

enum {
    NSYM       = 512,
    MAXCL      = 15,
    TBLBITS    = 15,
    TBLSZ      = 1 << TBLBITS,   /* 32768 */
    HTBL_BYTES = 256
};

/*======================================================================
 * Canonical Huffman - shared by compressor and decompressor
 *======================================================================*/

/* Compute canonical starting codes per length (standard algorithm).
 * count[len] = number of symbols with that code length.
 * next[len]  = next code to assign for that length. */
static void canonical_starts(const uint8_t cl[NSYM], uint32_t next[MAXCL + 1])
{
    uint32_t cnt[MAXCL + 1] = {0};
    for (int i = 0; i < NSYM; i++)
        if (cl[i] > 0 && cl[i] <= MAXCL) cnt[cl[i]]++;

    uint32_t code = 0;
    for (int len = 1; len <= MAXCL; len++) {
        code = (code + cnt[len - 1]) << 1;
        next[len] = code;
    }
}

/*======================================================================
 * DECOMPRESSOR
 *
 * Bit buffer model (MSB-first):
 *   - 'bits' is a 32-bit register; valid data sits at the TOP
 *   - peek N bits:   bits >> (32 - N)
 *   - consume N:     bits <<= N;  nbits -= N
 *   - refill:        bits |= rd16(p) << (16 - nbits);  nbits += 16
 *
 * Decode table is indexed by the top TBLBITS bits of the buffer.
 * For a symbol with canonical code C (length L), fill all entries
 * where the top L bits of the index equal C.
 *======================================================================*/

typedef struct { uint16_t sym; uint8_t len; } DEntry;

static int build_dtable(const uint8_t cl[NSYM], DEntry tbl[TBLSZ])
{
    uint32_t next[MAXCL + 1];
    canonical_starts(cl, next);
    memset(tbl, 0, sizeof(DEntry) * TBLSZ);

    for (int s = 0; s < NSYM; s++) {
        int L = cl[s];
        if (L == 0) continue;
        if (L > MAXCL) return 0;
        uint32_t c = next[L]++;
        /* top-L-bit match in a TBLBITS-wide index */
        uint32_t base = c << (TBLBITS - L);
        uint32_t span = 1u << (TBLBITS - L);
        if (base + span > TBLSZ) return 0;
        for (uint32_t j = 0; j < span; j++) {
            tbl[base + j].sym = (uint16_t)s;
            tbl[base + j].len = (uint8_t)L;
        }
    }
    return 1;
}

/* Core decompressor: tbl must point to TBLSZ DEntry elements */
static XpressStatus decompress_core(
    const uint8_t* in, uint32_t in_len,
    uint8_t* out, uint32_t out_len,
    DEntry* tbl)
{
    uint8_t cl[NSYM];
    for (int i = 0; i < HTBL_BYTES; i++) {
        cl[2 * i]     = in[i] & 0x0F;
        cl[2 * i + 1] = (in[i] >> 4) & 0x0F;
    }

    if (!build_dtable(cl, tbl)) return XPRESS_BAD_DATA;

    const uint8_t* p   = in + HTBL_BYTES;
    const uint8_t* end = in + in_len;

    uint32_t bits = 0;
    int nbits = 0;

#define ENSURE_BITS(n) do { \
    if (nbits < (n) && p + 1 < end) { \
        bits |= (uint32_t)rd16(p) << (16 - nbits); \
        p += 2; nbits += 16; \
    } \
} while(0)

    uint32_t opos = 0;
    while (opos < out_len) {
        ENSURE_BITS(MAXCL);

        uint32_t idx = bits >> (32 - TBLBITS);
        uint16_t sym = tbl[idx].sym;
        int slen     = tbl[idx].len;
        if (slen == 0) return XPRESS_BAD_DATA;
        bits <<= slen;
        nbits -= slen;

        if (sym < 256) {
            out[opos++] = (uint8_t)sym;
        } else {
            int mi = sym - 256;
            int lh = mi & 0xF;
            int ol = mi >> 4;

            ENSURE_BITS(16);

            uint32_t moff;
            if (ol == 0) {
                moff = 1;
            } else {
                moff = (1u << ol) | (bits >> (32 - ol));
                bits <<= ol;
                nbits -= ol;
            }

            uint32_t mlen;
            if (lh < 15) {
                mlen = (uint32_t)lh + 3;
            } else {
                if (p >= end) return XPRESS_BAD_DATA;
                uint8_t xb = *p++;
                if (xb < 255) {
                    mlen = 15 + 3 + xb;
                } else {
                    if (p + 1 >= end) return XPRESS_BAD_DATA;
                    mlen = rd16(p); p += 2;
                    if (mlen == 0) {
                        if (p + 3 >= end) return XPRESS_BAD_DATA;
                        mlen = rd32(p); p += 4;
                    }
                    mlen += 3;
                }
            }

            if (moff > opos) return XPRESS_BAD_DATA;

            /* Clamp mlen to remaining output so we only branch-check once. */
            uint32_t remain = out_len - opos;
            uint32_t copy_len = mlen < remain ? mlen : remain;

#if WIMAGE_HAVE_SSE2
            /* AVX2 for moff>=32, SSE2 for moff>=16, SSSE3 for moff<16. */
#  if WIMAGE_HAVE_AVX2 && (defined(__GNUC__) || defined(__clang__))
            if (wimage_avx2_enabled && moff >= 32 && copy_len >= 32) {
                xpress_decopy_avx2(out + opos, out + opos - moff,
                                   copy_len & ~31u);
                uint32_t done = copy_len & ~31u;
                opos += done;
                uint32_t tail = copy_len - done;
                while (tail >= 16) {
                    __m128i v = _mm_loadu_si128(
                        (const __m128i*)(out + opos - moff));
                    _mm_storeu_si128((__m128i*)(out + opos), v);
                    opos += 16; tail -= 16;
                }
                for (uint32_t j = 0; j < tail; j++) {
                    out[opos] = out[opos - moff];
                    opos++;
                }
            }
            else
#  endif
            if (wimage_sse2_enabled && moff >= 16 && copy_len >= 16) {
                const uint8_t* src = out + opos - moff;
                uint8_t*       dst = out + opos;
                uint32_t n = copy_len;
                while (n >= 16) {
                    __m128i v = _mm_loadu_si128((const __m128i*)src);
                    _mm_storeu_si128((__m128i*)dst, v);
                    src += 16; dst += 16; n -= 16;
                }
                uint32_t done = copy_len - n;
                opos += done;
                for (uint32_t j = 0; j < n; j++) {
                    out[opos] = out[opos - moff];
                    opos++;
                }
            }
#  if WIMAGE_HAVE_SSSE3 && (defined(__GNUC__) || defined(__clang__))
            else if (wimage_ssse3_enabled && moff >= 1 && moff < 16 && copy_len >= 16) {
                uint32_t done = xpress_rle_expand_ssse3(
                    out + opos, out + opos - moff, moff, copy_len);
                opos += done;
                for (uint32_t j = done; j < copy_len; j++) {
                    out[opos] = out[opos - moff];
                    opos++;
                }
            }
#  endif
            else
#endif
            {
                for (uint32_t j = 0; j < copy_len; j++) {
                    out[opos] = out[opos - moff];
                    opos++;
                }
            }
        }
    }

#undef ENSURE_BITS
    return XPRESS_OK;
}

/* Heap-allocating decompressor (original API) */
#ifndef XPRESS_HUFF_DECOMPRESS_ONLY
XpressStatus xpress_huff_decompress(
    const uint8_t* in, uint32_t in_len,
    uint8_t* out, uint32_t out_len)
{
    if (in_len < HTBL_BYTES) return XPRESS_BAD_DATA;

    DEntry* tbl = (DEntry*)malloc(sizeof(DEntry) * TBLSZ);
    if (!tbl) return XPRESS_BAD_DATA;

    XpressStatus st = decompress_core(in, in_len, out, out_len, tbl);
    free(tbl);
    return st;
}
#endif

/* Static decompressor: no malloc, caller provides workspace.
 * workspace must be at least XPRESS_DECOMPRESS_WORKSPACE_SIZE bytes. */
XpressStatus xpress_huff_decompress_static(
    const uint8_t* in, uint32_t in_len,
    uint8_t* out, uint32_t out_len,
    void* workspace)
{
    if (in_len < HTBL_BYTES || !workspace) return XPRESS_BAD_DATA;
    return decompress_core(in, in_len, out, out_len, (DEntry*)workspace);
}

/*======================================================================
 * COMPRESSOR
 *
 * 1) Greedy LZ77 parse with hash table
 * 2) Frequency count -> build length-limited Huffman codes
 * 3) Pack all Huffman codes + offset extra bits into MSB-first bit array
 * 4) Simulate decompressor refill pattern to interleave 16-bit words
 *    and inline length-extension bytes
 *======================================================================*/

#ifndef XPRESS_HUFF_DECOMPRESS_ONLY
/* --- Huffman tree for code-length assignment --- */

typedef struct { uint32_t f; int16_t l, r, sym; } HNode;

static void depths(const HNode* n, int i, uint8_t* cl, int d)
{
    if (i < 0) return;
    if (n[i].sym >= 0) { cl[n[i].sym] = (uint8_t)(d > MAXCL ? MAXCL : d); return; }
    depths(n, n[i].l, cl, d + 1);
    depths(n, n[i].r, cl, d + 1);
}

/* Comparator for sorting leaf nodes by frequency */
static int cmp_hnode(const void* a, const void* b)
{
    uint32_t fa = ((const HNode*)a)->f, fb = ((const HNode*)b)->f;
    if (fa < fb) return -1;
    if (fa > fb) return 1;
    return 0;
}

static void make_lengths(const uint32_t freq[NSYM], uint8_t cl[NSYM])
{
    memset(cl, 0, NSYM);
    int ns = 0;
    int16_t ss[NSYM];
    for (int i = 0; i < NSYM; i++) if (freq[i]) ss[ns++] = (int16_t)i;
    if (ns == 0) { cl[0] = 1; return; }
    if (ns == 1) { cl[ss[0]] = 1; return; }

    int mx = 2 * ns;
    HNode* nd = (HNode*)calloc(mx, sizeof(HNode));
    if (!nd) {
        for (int i = 0; i < ns; i++) cl[ss[i]] = MAXCL;
        return;
    }

    for (int i = 0; i < ns; i++) {
        nd[i].f = freq[ss[i]]; nd[i].sym = ss[i];
        nd[i].l = nd[i].r = -1;
    }
    /* Sort leaves by frequency for O(n) two-queue merge */
    qsort(nd, ns, sizeof(HNode), cmp_hnode);

    /* Two-queue Huffman: q1 = sorted leaves, q2 = internal nodes */
    int q1h = 0;                 /* head of leaf queue */
    int q2h = ns, q2t = ns;     /* head/tail of internal queue (stored after leaves) */
    int nc = ns;

    #define QPOP(idx) do { \
        if (q1h < ns && (q2h >= q2t || nd[q1h].f <= nd[q2h].f)) \
            idx = q1h++; \
        else \
            idx = q2h++; \
    } while(0)

    for (int m = 0; m < ns - 1; m++) {
        int a, b;
        QPOP(a); QPOP(b);
        int ni = nc++;
        nd[ni].f = nd[a].f + nd[b].f;
        nd[ni].l = (int16_t)a; nd[ni].r = (int16_t)b;
        nd[ni].sym = -1;
        q2t = nc; /* new internal node goes to tail of q2 */
    }
    #undef QPOP

    depths(nd, nc - 1, cl, 0);
    free(nd);

    /* Clamp & fix Kraft inequality */
    int fix = 0;
    for (int i = 0; i < NSYM; i++) if (cl[i] > MAXCL) { cl[i] = MAXCL; fix = 1; }
    if (fix) {
        for (int it = 0; it < 2000; it++) {
            uint32_t k = 0;
            for (int i = 0; i < NSYM; i++)
                if (cl[i]) k += 1u << (MAXCL - cl[i]);
            if (k <= (1u << MAXCL)) break;
            int best = -1;
            for (int i = 0; i < NSYM; i++)
                if (cl[i] > 0 && cl[i] < MAXCL)
                    if (best < 0 || cl[i] < cl[best]) best = i;
            if (best < 0) break;
            cl[best]++;
        }
        /* Final Kraft check: if still oversubscribed, fall back to MAXCL for all */
        uint32_t k = 0;
        for (int i = 0; i < NSYM; i++)
            if (cl[i]) k += 1u << (MAXCL - cl[i]);
        if (k > (1u << MAXCL)) {
            for (int i = 0; i < NSYM; i++)
                if (cl[i]) cl[i] = MAXCL;
        }
    }
}

/* Assign canonical codes (MSB-first, right-aligned in uint32_t) */
static void make_codes(const uint8_t cl[NSYM], uint32_t codes[NSYM])
{
    uint32_t next[MAXCL + 1];
    canonical_starts(cl, next);
    memset(codes, 0, NSYM * sizeof(uint32_t));
    for (int i = 0; i < NSYM; i++) {
        if (cl[i] == 0) continue;
        codes[i] = next[cl[i]]++;
    }
}

/* --- LZ77 token ---
 *
 * `sym` is the Huffman symbol for this token, precomputed during LZ77 so we
 * don't re-derive it in the frequency-count pass or the bitstream emit pass.
 * Literals use sym = u.lit (0-255); matches use sym = 256 + (ol << 4) + len_hi.
 * sym >= 256 also serves as the is_match predicate. `ol` is cached for the
 * same reason — hbit() is a serial log2 loop we'd otherwise call twice per
 * match. */
typedef struct {
    uint16_t sym;
    uint8_t  ol;         /* offset-log bits (only valid for matches) */
    uint8_t  _pad;
    union {
        uint8_t lit;
        struct { uint16_t off, len; } m;
    } u;
} Tok;

struct XpressCompressScratch {
    Tok* tok;
    uint32_t* ba;
    uint32_t token_cap;
    uint32_t bit_words;
    /*
     * Generation-counter-based hash chain state:
     *   head_gen[h] == gen means head[h] is valid in the current chunk.
     *   gen is bumped once per compress call; when it overflows to 0 we
     *   zero head_gen[] once and restart at 1. This avoids the 128 KB
     *   head[]/256 KB prev[] memset that previously ran for every chunk.
     *
     * prev[] is intentionally NOT zero-initialized: because chunk_size is
     * constrained to the 65536-entry LZ77 window, prev[p] is always written
     * by HC_INSERT(p) before anything could possibly read it (the chain
     * walk starts from head[hv] and only visits positions already inserted
     * in the current generation).
     */
    int32_t  head[32768];
    int32_t  prev[65536];
    uint16_t head_gen[32768];
    uint16_t gen;
};

static inline int hbit(uint32_t v) { int r = 0; while (v > 1) { v >>= 1; r++; } return r; }

/* Build the XPRESS-Huffman symbol for a match in one place.
 * literals use sym = lit (0-255); matches use sym = 256 + (ol << 4) + len_hi. */
static inline uint16_t match_sym(uint16_t off, uint16_t len, uint8_t* out_ol)
{
    int ol = hbit(off);
    uint32_t lh = (uint32_t)len - 3u;
    if (lh > 15) lh = 15;
    if (out_ol) *out_ol = (uint8_t)ol;
    return (uint16_t)(256 + (ol << 4) + lh);
}

static inline uint32_t h3(const uint8_t* p)
{
    return (((uint32_t)p[0] << 10) ^ ((uint32_t)p[1] << 5) ^ p[2]) & 0x7FFF;
}

/* Fast match extension with SSE2 on Intel x86_64. */
static inline uint32_t match_len(const uint8_t* a, const uint8_t* b, uint32_t max_len)
{
    uint32_t len = 0;

#if WIMAGE_HAVE_SSE2
    /* Intel-only runtime gate (see top of file). */
    if (wimage_sse2_enabled) {
        while (len + 16 <= max_len) {
            __m128i va = _mm_loadu_si128((const __m128i*)(a + len));
            __m128i vb = _mm_loadu_si128((const __m128i*)(b + len));
            uint32_t mask = (uint32_t)_mm_movemask_epi8(_mm_cmpeq_epi8(va, vb));
            if (mask != 0xFFFF) {
                /* Mask has a 1 bit per matching byte (LSB = first byte).
                 * The first zero bit is the first mismatch position. */
                len += (uint32_t)__builtin_ctz(~mask & 0xFFFF);
                return len;
            }
            len += 16;
        }
    }
#endif

    /* 8-byte tail (and the entire loop on non-SSE2 hosts) */
    while (len + 8 <= max_len) {
        uint64_t va, vb;
        memcpy(&va, a + len, 8);
        memcpy(&vb, b + len, 8);
        if (va != vb) {
            uint64_t diff = va ^ vb;
            len += (uint32_t)(__builtin_ctzll(diff) / 8);
            return len < max_len ? len : max_len;
        }
        len += 8;
    }
    while (len < max_len && a[len] == b[len]) len++;
    return len;
}

/*
 * Quick reject for chunks that are very unlikely to benefit from XPRESS.
 * Random-looking chunks dominate the current benchmark and otherwise burn
 * most of the compressor time only to be stored raw in the caller anyway.
 */
static int xpress_huff_likely_incompressible(const uint8_t* in, uint32_t in_len)
{
    enum { HASH_BITS = 10, HASH_SIZE = 1 << HASH_BITS };
    uint16_t last[HASH_SIZE];
    uint32_t repeats = 0;
    uint32_t stride;
    uint32_t repeat_goal;

    if (in_len < 2048)
        return 0;

    stride = in_len >= 16384 ? 8u : 4u;
    repeat_goal = in_len >= 16384 ? 4u : 6u;

    memset(last, 0xFF, sizeof(last));

    for (uint32_t pos = 0; pos + 4 <= in_len; pos += stride) {
        uint32_t word;
        memcpy(&word, in + pos, sizeof(word));

        uint32_t hv = (word * 2654435761u) >> (32 - HASH_BITS);
        uint16_t prev = last[hv];
        last[hv] = (uint16_t)pos;

        if (prev != 0xFFFF && pos >= (uint32_t)prev + stride) {
            uint32_t prev_word;
            memcpy(&prev_word, in + prev, sizeof(prev_word));
            if (prev_word == word && ++repeats >= repeat_goal)
                return 0;
        }
    }

    return 1;
}

int xpress_huff_chunk_may_compress(const uint8_t* in, uint32_t in_len)
{
    return !xpress_huff_likely_incompressible(in, in_len);
}

static int xpress_huff_reserve_tokens(XpressCompressScratch* scratch, uint32_t input_len)
{
    uint32_t need = input_len ? input_len : 1;
    if (scratch->token_cap >= need)
        return 1;

    Tok* tok = (Tok*)realloc(scratch->tok, (size_t)need * sizeof(Tok));
    if (!tok)
        return 0;

    scratch->tok = tok;
    scratch->token_cap = need;
    return 1;
}

static int xpress_huff_reserve_bits(XpressCompressScratch* scratch, uint32_t max_bits)
{
    size_t need_words = ((size_t)max_bits + 31) / 32 + 2;
    if (need_words == 0)
        need_words = 2;
    if (scratch->bit_words >= need_words)
        return 1;

    uint32_t* ba = (uint32_t*)realloc(scratch->ba, need_words * sizeof(uint32_t));
    if (!ba)
        return 0;

    scratch->ba = ba;
    scratch->bit_words = (uint32_t)need_words;
    return 1;
}

XpressCompressScratch* xpress_huff_create_scratch(uint32_t max_input_len)
{
    XpressCompressScratch* scratch =
        (XpressCompressScratch*)calloc(1, sizeof(*scratch));
    if (!scratch)
        return NULL;

    if (!xpress_huff_reserve_tokens(scratch, max_input_len) ||
        !xpress_huff_reserve_bits(scratch, max_input_len * 32u + 64u)) {
        xpress_huff_destroy_scratch(scratch);
        return NULL;
    }

    return scratch;
}

void xpress_huff_destroy_scratch(XpressCompressScratch* scratch)
{
    if (!scratch)
        return;

    free(scratch->ba);
    free(scratch->tok);
    free(scratch);
}

XpressStatus xpress_huff_compress_prechecked_with_scratch(
    const uint8_t* in, uint32_t in_len,
    uint8_t* out, uint32_t out_capacity,
    uint32_t* out_len,
    XpressCompressScratch* scratch)
{
    if (in_len == 0) { *out_len = 0; return XPRESS_OK; }
    if (!scratch)
        return XPRESS_BAD_DATA;

    /* === Pass 1: LZ77 with hash chains + lazy matching === */
    #define WSIZE 65536
    #define MAX_CHAIN 4
    #define GOOD_MATCH 16

    if (!xpress_huff_reserve_tokens(scratch, in_len))
        return XPRESS_BAD_DATA;

    Tok* tok = scratch->tok;
    int32_t* head = scratch->head;
    int32_t* prev = scratch->prev;
    uint16_t* head_gen = scratch->head_gen;

    /* Bump generation counter; on wraparound reset head_gen[] once.
     * gen 0 is the "never written" sentinel so we start at 1. */
    scratch->gen++;
    if (scratch->gen == 0) {
        memset(head_gen, 0, sizeof(uint16_t) * 32768);
        scratch->gen = 1;
    }
    const uint16_t gen_local = scratch->gen;

    /* Insert position into hash chain (guard: need 3 bytes for h3).
     * If head_gen[hv] != gen we treat the bucket as empty. */
    #define HC_INSERT(p) do { \
        if ((p) + 2 < in_len) { \
            uint32_t hv_ = h3(in + (p)); \
            int32_t  oldh_ = (head_gen[hv_] == gen_local) ? head[hv_] : -1; \
            prev[(p) & (WSIZE-1)] = oldh_; \
            head[hv_] = (int32_t)(p); \
            head_gen[hv_] = gen_local; \
        } \
    } while(0)

    /* Find best match at position p, store length in bl_, offset in bo_ */
    #define HC_FIND_BEST(p, bl_, bo_) do { \
        bl_ = 0; bo_ = 0; \
        if ((p) + 2 < in_len) { \
            uint32_t hv_ = h3(in + (p)); \
            int32_t cur_ = (head_gen[hv_] == gen_local) ? head[hv_] : -1; \
            uint32_t mx_ = in_len - (p); \
            if (mx_ > 65535) mx_ = 65535; \
            for (int chain_ = 0; chain_ < MAX_CHAIN && cur_ >= 0; chain_++) { \
                uint32_t d_ = (p) - (uint32_t)cur_; \
                if (d_ > 65535) break; \
                if (d_ >= 1) { \
                    uint32_t ml_ = match_len(in + (p), in + (uint32_t)cur_, mx_); \
                    if (ml_ > bl_) { bl_ = ml_; bo_ = d_; \
                        if (bl_ >= GOOD_MATCH) break; } \
                } \
                cur_ = prev[(uint32_t)cur_ & (WSIZE-1)]; \
            } \
            if (bl_ < 3) { bl_ = 0; bo_ = 0; } \
        } \
    } while(0)

    uint32_t ntok = 0, pos = 0;
    while (pos < in_len) {
        uint32_t bl, bo;
        HC_FIND_BEST(pos, bl, bo);
        HC_INSERT(pos);

        if (bl >= 3) {
            /* Lazy matching: check if next position yields a better match */
            if (pos + 1 < in_len && bl < GOOD_MATCH) {
                uint32_t next_bl, next_bo;
                HC_FIND_BEST(pos + 1, next_bl, next_bo);
                if (next_bl > bl + 1) {
                    /* Emit literal for current pos, use next match instead */
                    Tok* t = &tok[ntok++];
                    t->sym = in[pos];
                    t->ol  = 0;
                    t->u.lit = in[pos];
                    HC_INSERT(pos + 1);
                    pos++;
                    bl = next_bl; bo = next_bo;
                }
            }

            Tok* t = &tok[ntok++];
            t->sym = match_sym((uint16_t)bo, (uint16_t)bl, &t->ol);
            t->u.m.off = (uint16_t)bo;
            t->u.m.len = (uint16_t)bl;

            /* Update hash chain for positions within the match (skip middle for long matches) */
            uint32_t update_limit = bl < 32 ? bl : 32;
            for (uint32_t i = 1; i < update_limit && pos + i + 2 < in_len; i++) {
                HC_INSERT(pos + i);
            }
            pos += bl;
        } else {
            Tok* t = &tok[ntok++];
            t->sym = in[pos];
            t->ol  = 0;
            t->u.lit = in[pos];
            pos++;
        }
    }
    #undef HC_INSERT
    #undef HC_FIND_BEST
    #undef WSIZE
    #undef MAX_CHAIN

    /* === Pass 2: count frequencies using pre-cached sym === */
    uint32_t freq[NSYM] = {0};
    for (uint32_t i = 0; i < ntok; i++)
        freq[tok[i].sym]++;

    uint8_t cl[NSYM];
    make_lengths(freq, cl);

    /* Verify Kraft inequality before encoding: if code lengths are invalid,
     * bail out and let the caller store the chunk raw. */
    {
        uint32_t kraft = 0;
        for (int i = 0; i < NSYM; i++)
            if (cl[i]) kraft += 1u << (MAXCL - cl[i]);
        if (kraft > (1u << MAXCL))
            return XPRESS_BUFFER_TOO_SMALL;
    }

    uint32_t codes[NSYM];
    make_codes(cl, codes);

    /*
     * === Pass 3: fused streaming bit-writer + decoder simulation ===
     *
     * Replaces the old two-pass scheme that (a) packed all bits into ba[]
     * and then (b) walked the token list again simulating the decoder to
     * emit 16-bit words.  We now do both in one walk with a 64-bit bit
     * register:
     *
     *   - `push_tok`  = next token whose bits haven't been added to bit_buf
     *   - `sim_tok`   = next token whose consumption we've simulated
     *   - `bit_buf`   = 0..63 pending bits, MSB-aligned at bit 63
     *   - `dn`        = simulated decoder nbits (bits the decoder holds)
     *
     * When the decoder needs a refill (dn < threshold for the current sim
     * token), we must emit a 16-bit word.  For that we need bit_buf to hold
     * ≥16 bits, so we push producer tokens forward until it does.  Bits
     * consumed by the sim are drained from the stream via EMIT_WORD; the
     * producer can always stay ahead because every token contributes at
     * least 1 bit, so the buffer refills on demand.
     *
     * Length-extension bytes are emitted directly into the byte stream at
     * the exact point in the simulation where the decoder would read them
     * — after the match's offset extras have been consumed, no bit_buf
     * state in flight.  The important invariant is that we only emit a
     * len-ext byte when the last word emitted contained the final bit of
     * the token's offset extras.  The simulation naturally preserves this
     * because we only enter the `match_tail` block after `ENSURE_DN(16)`
     * has drained enough words to cover the match's `clen + ol` bits.
     */
    if (out_capacity < HTBL_BYTES + 4u) {
        return XPRESS_BUFFER_TOO_SMALL;
    }

    /* Write packed Huffman code-length table */
    for (int i = 0; i < HTBL_BYTES; i++)
        out[i] = (uint8_t)(cl[2 * i] | (cl[2 * i + 1] << 4));

    uint8_t* wp = out + HTBL_BYTES;
    uint8_t* we = out + out_capacity;

    uint64_t bit_buf  = 0;   /* pending bits, MSB-aligned */
    int      buf_nbits = 0;  /* valid bits in bit_buf (0..63) */
    int      dn = 0;         /* simulated decoder nbits */
    uint32_t push_tok = 0;   /* next token to push into bit_buf */

    /* Push one token's worth of bits into bit_buf. No-op if producer
     * has already consumed all tokens. */
    #define PUSH_ONE_TOKEN() do { \
        if (push_tok < ntok) { \
            const Tok* _t = &tok[push_tok++]; \
            uint16_t _s = _t->sym; \
            int _clen = cl[_s]; if (_clen == 0) _clen = 1; \
            bit_buf |= ((uint64_t)codes[_s] & ((1ULL << _clen) - 1ULL)) \
                       << (64 - buf_nbits - _clen); \
            buf_nbits += _clen; \
            if (_s >= 256) { \
                int _ol = _t->ol; \
                if (_ol > 0) { \
                    uint32_t _ex = _t->u.m.off & ((1u << _ol) - 1u); \
                    bit_buf |= ((uint64_t)_ex) << (64 - buf_nbits - _ol); \
                    buf_nbits += _ol; \
                } \
            } \
        } \
    } while(0)

    /* Emit the top 16 bits of bit_buf as one 16-bit little-endian word.
     * When buf_nbits < 16 the tail bits are zero (bit_buf is cleared by
     * the shift), which acts as end-of-stream padding the decoder never
     * actually decodes (it stops on output count, not stream position). */
    #define EMIT_WORD() do { \
        if (wp + 2 > we) goto full; \
        uint16_t _w = (uint16_t)(bit_buf >> 48); \
        wr16(wp, _w); wp += 2; \
        bit_buf <<= 16; \
        if (buf_nbits >= 16) buf_nbits -= 16; \
        else                 buf_nbits  = 0; \
        dn += 16; \
    } while(0)

    /* Ensure the simulated decoder has at least `need` bits by emitting
     * words. Producer tokens are pushed opportunistically to keep bit_buf
     * full, but if we run out of tokens mid-sim we still emit (with zero
     * padding) because the decoder's refill is unconditional. */
    #define ENSURE_DN(need) do { \
        while (dn < (need)) { \
            while (buf_nbits < 16 && push_tok < ntok) \
                PUSH_ONE_TOKEN(); \
            EMIT_WORD(); \
        } \
    } while(0)

    for (uint32_t i = 0; i < ntok; i++) {
        const Tok* t = &tok[i];
        uint16_t s = t->sym;
        int clen = cl[s]; if (clen == 0) clen = 1;

        /* ENSURE_BITS(MAXCL=15) before huffman decode */
        ENSURE_DN(MAXCL);
        dn -= clen;

        if (s >= 256) {
            int ol = t->ol;

            /* ENSURE_BITS(16) before offset extras */
            ENSURE_DN(16);
            dn -= ol;

            /* Length extension bytes inlined into the byte stream */
            uint32_t len = t->u.m.len;
            if (len - 3 >= 15) {
                uint32_t el = len - 3 - 15;
                if (el < 255) {
                    if (wp + 1 > we) goto full;
                    *wp++ = (uint8_t)el;
                } else {
                    /* MS-XCA: uint16 value is (match_length - 3), decompressor adds +3 */
                    uint32_t raw = len - 3;
                    if (wp + 3 > we) goto full;
                    *wp++ = 255;
                    *wp++ = (uint8_t)(raw & 0xFF);
                    *wp++ = (uint8_t)((raw >> 8) & 0xFF);
                }
            }
        }
    }

    /* Drain any bits still sitting in bit_buf that haven't been emitted.
     * These are the tail bits of the final token(s); the decoder will
     * consume them either as part of a later ENSURE_BITS refill that
     * never actually reads the symbols (because the output is full), or
     * not at all. Either way we write them out as full 16-bit words. */
    while (push_tok < ntok) {
        PUSH_ONE_TOKEN();
        while (buf_nbits >= 16) EMIT_WORD();
    }
    while (buf_nbits > 0) EMIT_WORD();

    #undef PUSH_ONE_TOKEN
    #undef EMIT_WORD
    #undef ENSURE_DN

    {
        uint32_t cs = (uint32_t)(wp - out);
        if (cs >= in_len) {
            *out_len = in_len;
            return XPRESS_BUFFER_TOO_SMALL; /* caller stores uncompressed */
        }
        *out_len = cs;
        return XPRESS_OK;
    }

full:
    return XPRESS_BUFFER_TOO_SMALL;
}

XpressStatus xpress_huff_compress_with_scratch(
    const uint8_t* in, uint32_t in_len,
    uint8_t* out, uint32_t out_capacity,
    uint32_t* out_len,
    XpressCompressScratch* scratch)
{
    if (!xpress_huff_chunk_may_compress(in, in_len))
        return XPRESS_BUFFER_TOO_SMALL;

    return xpress_huff_compress_prechecked_with_scratch(
        in, in_len, out, out_capacity, out_len, scratch);
}

XpressStatus xpress_huff_compress(
    const uint8_t* in, uint32_t in_len,
    uint8_t* out, uint32_t out_capacity,
    uint32_t* out_len)
{
    XpressStatus st;
    XpressCompressScratch* scratch = xpress_huff_create_scratch(in_len);
    if (!scratch)
        return XPRESS_BAD_DATA;

    st = xpress_huff_compress_with_scratch(in, in_len, out, out_capacity,
                                           out_len, scratch);
    xpress_huff_destroy_scratch(scratch);
    return st;
}
#endif
