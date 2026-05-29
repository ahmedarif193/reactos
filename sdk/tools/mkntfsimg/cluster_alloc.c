#include "mkntfsimg.h"

NTSTATUS
ntfsimg_cluster_alloc_init(NTFSIMG_VOLUME *vol, NTFSIMG_CLUSTER_ALLOCATOR *ca)
{
    uint32_t nbytes = (uint32_t)vol->bitmap_data_size;
    NTSTATUS st;

    memset(ca, 0, sizeof(*ca));
    ca->bitmap = malloc(nbytes);
    if (!ca->bitmap) return STATUS_INSUFFICIENT_RESOURCES;

    st = ntfsimg_read_mapped(vol, vol->bitmap_runlist, vol->bitmap_runlist_count,
                             0, nbytes, ca->bitmap);
    if (!NT_SUCCESS(st)) { free(ca->bitmap); ca->bitmap = NULL; return st; }

    ca->bitmap_bytes = nbytes;
    ca->total_clusters = vol->total_clusters;
    ca->next_search = 0;
    ca->dirty = 0;
    return STATUS_SUCCESS;
}

void
ntfsimg_cluster_alloc_free(NTFSIMG_CLUSTER_ALLOCATOR *ca)
{
    free(ca->bitmap);
    memset(ca, 0, sizeof(*ca));
}

static int
bit_is_set(const UCHAR *bm, uint64_t bit)
{
    return (bm[bit >> 3] & (UCHAR)(1u << (bit & 7))) != 0;
}

static void
bit_set(UCHAR *bm, uint64_t bit)
{
    bm[bit >> 3] |= (UCHAR)(1u << (bit & 7));
}

NTSTATUS
ntfsimg_cluster_alloc(NTFSIMG_CLUSTER_ALLOCATOR *ca, uint64_t needed,
                      NTFSLX_RUNLIST_ELEMENT **out_runlist, uint32_t *out_count)
{
    NTFSLX_RUNLIST_ELEMENT *rl = NULL;
    uint32_t rl_cap = 4;
    uint32_t rl_count = 0;
    uint64_t remaining = needed;
    uint64_t cur_vcn = 0;
    uint64_t search_start = ca->next_search;
    uint64_t scan = search_start;
    int wrapped = 0;

    *out_runlist = NULL;
    *out_count = 0;
    if (needed == 0) return STATUS_SUCCESS;

    rl = calloc(rl_cap, sizeof(*rl));
    if (!rl) return STATUS_INSUFFICIENT_RESOURCES;

    while (remaining > 0)
    {
        uint64_t run_start;
        uint64_t run_len = 0;

        while (scan < ca->total_clusters && bit_is_set(ca->bitmap, scan))
            scan++;
        if (scan >= ca->total_clusters)
        {
            if (wrapped) { free(rl); return STATUS_DISK_FULL; }
            wrapped = 1;
            scan = 0;
            continue;
        }

        run_start = scan;
        while (scan < ca->total_clusters &&
               run_len < remaining &&
               !bit_is_set(ca->bitmap, scan))
        {
            bit_set(ca->bitmap, scan);
            scan++;
            run_len++;
        }

        if (rl_count == rl_cap)
        {
            NTFSLX_RUNLIST_ELEMENT *bigger;
            rl_cap *= 2;
            bigger = realloc(rl, rl_cap * sizeof(*rl));
            if (!bigger) { free(rl); return STATUS_INSUFFICIENT_RESOURCES; }
            rl = bigger;
        }
        rl[rl_count].Vcn = (int64_t)cur_vcn;
        rl[rl_count].Lcn = (int64_t)run_start;
        rl[rl_count].Length = (int64_t)run_len;
        rl_count++;
        cur_vcn += run_len;
        remaining -= run_len;

        if (wrapped && scan >= search_start)
        {
            if (remaining > 0) { free(rl); return STATUS_DISK_FULL; }
            break;
        }
    }

    ca->next_search = scan;
    ca->dirty = 1;
    *out_runlist = rl;
    *out_count = rl_count;
    return STATUS_SUCCESS;
}

NTSTATUS
ntfsimg_cluster_alloc_flush(NTFSIMG_VOLUME *vol, NTFSIMG_CLUSTER_ALLOCATOR *ca)
{
    if (!ca->dirty) return STATUS_SUCCESS;
    NTSTATUS st = ntfsimg_write_mapped(vol, vol->bitmap_runlist, vol->bitmap_runlist_count,
                                       0, ca->bitmap_bytes, ca->bitmap);
    if (NT_SUCCESS(st)) ca->dirty = 0;
    return st;
}
