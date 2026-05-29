#include "mkntfsimg.h"

#define ALIGN8(x) (((x) + 7u) & ~7u)

static const WCHAR IAE_I30[] = { '$','I','3','0' };

static NTSTATUS
decode_rl(PNTFSLX_ATTR_RECORD attr,
          NTFSLX_RUNLIST_ELEMENT **out_rl, uint32_t *out_count)
{
    const uint8_t *mp = (const uint8_t*)attr + attr->Data.NonResident.MappingPairsOffset;
    const uint8_t *mp_end = (const uint8_t*)attr + attr->Length;
    uint64_t cur_vcn = 0;
    int64_t prev_lcn = 0;
    NTFSLX_RUNLIST_ELEMENT *rl;
    uint32_t capacity = 8;
    uint32_t count = 0;

    rl = calloc(capacity, sizeof(*rl));
    if (!rl) return STATUS_INSUFFICIENT_RESOURCES;
    while (mp < mp_end && *mp != 0)
    {
        uint8_t hdr = *mp++;
        uint32_t len_bytes = hdr & 0x0F;
        uint32_t off_bytes = (hdr >> 4) & 0x0F;
        int64_t run_len = 0, delta = 0, abs_lcn;
        uint32_t i;
        if (len_bytes == 0 || mp + len_bytes + off_bytes > mp_end) { free(rl); return STATUS_FILE_CORRUPT_ERROR; }
        for (i = 0; i < len_bytes; i++) run_len |= (int64_t)mp[i] << (i*8);
        if (len_bytes < 8 && (mp[len_bytes-1] & 0x80))
            run_len |= (int64_t)(~(uint64_t)0) << (len_bytes*8);
        mp += len_bytes;
        if (off_bytes > 0)
        {
            for (i = 0; i < off_bytes; i++) delta |= (int64_t)mp[i] << (i*8);
            if (off_bytes < 8 && (mp[off_bytes-1] & 0x80))
                delta |= (int64_t)(~(uint64_t)0) << (off_bytes*8);
            mp += off_bytes;
            abs_lcn = prev_lcn + delta;
            prev_lcn = abs_lcn;
        }
        else abs_lcn = NTFSLX_LCN_HOLE;
        if (count == capacity)
        {
            NTFSLX_RUNLIST_ELEMENT *bigger;
            capacity *= 2;
            bigger = realloc(rl, capacity*sizeof(*rl));
            if (!bigger) { free(rl); return STATUS_INSUFFICIENT_RESOURCES; }
            rl = bigger;
        }
        rl[count].Vcn = (int64_t)cur_vcn;
        rl[count].Lcn = abs_lcn;
        rl[count].Length = run_len;
        count++;
        cur_vcn += (uint64_t)run_len;
    }
    *out_rl = rl;
    *out_count = count;
    return STATUS_SUCCESS;
}

static NTSTATUS
merge_rl(NTFSLX_RUNLIST_ELEMENT *old, uint32_t old_count,
         NTFSLX_RUNLIST_ELEMENT *add, uint32_t add_count,
         NTFSLX_RUNLIST_ELEMENT **out, uint32_t *out_count)
{
    uint32_t cap = old_count + add_count + 2;
    NTFSLX_RUNLIST_ELEMENT *m = calloc(cap, sizeof(*m));
    uint32_t n = 0;
    uint32_t i;
    int64_t next_vcn = 0;

    if (!m) return STATUS_INSUFFICIENT_RESOURCES;
    for (i = 0; i < old_count; i++) m[n++] = old[i];
    if (n > 0) next_vcn = m[n-1].Vcn + m[n-1].Length;

    for (i = 0; i < add_count; i++)
    {
        if (n > 0 && m[n-1].Lcn >= 0 && add[i].Lcn >= 0 &&
            m[n-1].Lcn + m[n-1].Length == add[i].Lcn)
        {
            m[n-1].Length += add[i].Length;
            next_vcn += add[i].Length;
        }
        else
        {
            m[n].Vcn = next_vcn;
            m[n].Lcn = add[i].Lcn;
            m[n].Length = add[i].Length;
            next_vcn += add[i].Length;
            n++;
        }
    }
    *out = m;
    *out_count = n;
    return STATUS_SUCCESS;
}

static PNTFSLX_ATTR_RECORD
find_named_attr(NTFSLX_MFT_RECORD *rec, uint32_t rec_size, uint32_t type,
                const WCHAR *name, uint32_t name_len, uint32_t *out_off)
{
    uint8_t *base = (uint8_t*)rec;
    uint32_t offset = rec->AttributesOffset;
    while (offset + sizeof(NTFSLX_ATTR_RECORD) <= rec_size)
    {
        PNTFSLX_ATTR_RECORD a = (PNTFSLX_ATTR_RECORD)(base + offset);
        if (a->Type == NTFSLX_ATTRIBUTE_END) return NULL;
        if (a->Length == 0 || a->Length > rec_size - offset) return NULL;
        if (a->Type == type && a->NameLength == name_len)
        {
            if (name_len == 0 ||
                memcmp((uint8_t*)a + a->NameOffset, name, name_len * sizeof(WCHAR)) == 0)
            {
                if (out_off) *out_off = offset;
                return a;
            }
        }
        offset += a->Length;
    }
    return NULL;
}

NTSTATUS
ntfsimg_index_alloc_extend(NTFSIMG_VOLUME *vol,
                           struct _NTFSIMG_CLUSTER_ALLOCATOR *ca,
                           uint64_t dir_mft_index,
                           uint32_t extra_blocks)
{
    uint32_t rec_size = vol->bytes_per_mft_record;
    NTFSLX_MFT_RECORD *rec = malloc(rec_size);
    PNTFSLX_ATTR_RECORD alloc_attr, bm_attr;
    uint32_t alloc_off, bm_off;
    NTFSLX_RUNLIST_ELEMENT *old_rl = NULL;
    uint32_t old_rl_count = 0;
    NTFSLX_RUNLIST_ELEMENT *new_rl = NULL;
    uint32_t new_rl_count = 0;
    NTFSLX_RUNLIST_ELEMENT *merged_rl = NULL;
    uint32_t merged_rl_count = 0;
    uint32_t block_size;
    uint32_t clusters_per_block;
    uint64_t extra_clusters;
    uint64_t new_alloc_bytes;
    uint64_t new_total_blocks;
    uint32_t new_bitmap_bytes;
    LONG mp_size;
    uint32_t new_mp_offset;
    uint32_t new_attr_length;
    int32_t alloc_delta;
    NTSTATUS st;

    if (!rec) return STATUS_INSUFFICIENT_RESOURCES;
    st = ntfsimg_read_mft_record(vol, dir_mft_index, rec);
    if (!NT_SUCCESS(st)) { free(rec); return st; }

    alloc_attr = find_named_attr(rec, rec_size, NTFSLX_ATTRIBUTE_INDEX_ALLOCATION,
                                 IAE_I30, 4, &alloc_off);
    if (!alloc_attr || !alloc_attr->NonResident) { free(rec); return STATUS_NOT_FOUND; }

    bm_attr = find_named_attr(rec, rec_size, NTFSLX_ATTRIBUTE_BITMAP,
                              IAE_I30, 4, &bm_off);
    if (!bm_attr) { free(rec); return STATUS_NOT_FOUND; }

    {
        PNTFSLX_ATTR_RECORD ir_attr = find_named_attr(rec, rec_size,
                                                      NTFSLX_ATTRIBUTE_INDEX_ROOT,
                                                      IAE_I30, 4, NULL);
        if (!ir_attr) { free(rec); return STATUS_NOT_FOUND; }
        NTFSLX_INDEX_ROOT *ir = (NTFSLX_INDEX_ROOT*)((UCHAR*)ir_attr +
                                                     ir_attr->Data.Resident.ValueOffset);
        block_size = ir->IndexBlockSize;
        if (block_size == 0) block_size = vol->bytes_per_index_record;
    }
    clusters_per_block = block_size / vol->bytes_per_cluster;
    if (clusters_per_block == 0) clusters_per_block = 1;

    extra_clusters = (uint64_t)extra_blocks * clusters_per_block;

    st = decode_rl(alloc_attr, &old_rl, &old_rl_count);
    if (!NT_SUCCESS(st)) { free(rec); return st; }

    st = ntfsimg_cluster_alloc(ca, extra_clusters, &new_rl, &new_rl_count);
    if (!NT_SUCCESS(st)) { free(old_rl); free(rec); return st; }

    st = merge_rl(old_rl, old_rl_count, new_rl, new_rl_count,
                  &merged_rl, &merged_rl_count);
    free(old_rl); free(new_rl);
    if (!NT_SUCCESS(st)) { free(rec); return st; }

    mp_size = NtfslxGetSizeForMappingPairs(merged_rl, merged_rl_count, 0, -1);
    if (mp_size <= 0) { free(merged_rl); free(rec); return STATUS_INSUFFICIENT_RESOURCES; }

    new_mp_offset = alloc_attr->Data.NonResident.MappingPairsOffset;
    new_attr_length = (uint32_t)ALIGN8(new_mp_offset + (uint32_t)mp_size);
    alloc_delta = (int32_t)new_attr_length - (int32_t)alloc_attr->Length;

    new_alloc_bytes = (uint64_t)(merged_rl[merged_rl_count - 1].Vcn +
                                 merged_rl[merged_rl_count - 1].Length) *
                      vol->bytes_per_cluster;
    new_total_blocks = new_alloc_bytes / block_size;
    new_bitmap_bytes = (uint32_t)((new_total_blocks + 7) / 8);
    new_bitmap_bytes = (uint32_t)(((new_bitmap_bytes + 7) / 8) * 8);

    {
        int32_t total_delta = alloc_delta;
        uint32_t bm_grow = 0;
        int bm_grew = 0;

        if (bm_attr->NonResident == 0)
        {
            uint32_t vo = bm_attr->Data.Resident.ValueOffset;
            uint32_t vl = bm_attr->Data.Resident.ValueLength;
            if (new_bitmap_bytes > vl)
            {
                uint32_t new_vl = new_bitmap_bytes;
                uint32_t new_bm_attr_len = (uint32_t)ALIGN8(vo + new_vl);
                bm_grow = new_bm_attr_len - bm_attr->Length;
                total_delta += (int32_t)bm_grow;
                bm_grew = 1;
                (void)bm_grew;
            }
        }

        if ((int32_t)rec->BytesInUse + total_delta + (int32_t)sizeof(ULONG) > (int32_t)rec_size)
        {
            free(merged_rl); free(rec);
            return STATUS_BUFFER_OVERFLOW;
        }

        if (alloc_delta != 0)
        {
            uint32_t tail_off = alloc_off + alloc_attr->Length;
            uint32_t tail_bytes = rec->BytesInUse - tail_off;
            if (tail_bytes > 0)
                memmove((UCHAR*)rec + tail_off + alloc_delta,
                        (UCHAR*)rec + tail_off, tail_bytes);
            rec->BytesInUse = (uint32_t)((int32_t)rec->BytesInUse + alloc_delta);
            if (bm_off > alloc_off) bm_off = (uint32_t)((int32_t)bm_off + alloc_delta);
            alloc_attr = (PNTFSLX_ATTR_RECORD)((UCHAR*)rec + alloc_off);
            bm_attr = (PNTFSLX_ATTR_RECORD)((UCHAR*)rec + bm_off);
        }

        alloc_attr->Length = new_attr_length;
        alloc_attr->Data.NonResident.HighestVcn =
            merged_rl[merged_rl_count - 1].Vcn +
            merged_rl[merged_rl_count - 1].Length - 1;
        alloc_attr->Data.NonResident.AllocatedSize = new_alloc_bytes;
        alloc_attr->Data.NonResident.DataSize = new_alloc_bytes;
        alloc_attr->Data.NonResident.InitializedSize = new_alloc_bytes;

        st = NtfslxMappingPairsBuild(
            (UCHAR*)alloc_attr + new_mp_offset,
            (LONG)(new_attr_length - new_mp_offset),
            merged_rl, merged_rl_count, 0, -1, NULL);
        if (!NT_SUCCESS(st)) { free(merged_rl); free(rec); return st; }

        if (bm_attr->NonResident == 0 && bm_grow > 0)
        {
            uint32_t vo = bm_attr->Data.Resident.ValueOffset;
            uint32_t vl = bm_attr->Data.Resident.ValueLength;
            uint32_t new_vl = new_bitmap_bytes;
            uint32_t new_bm_attr_len = (uint32_t)ALIGN8(vo + new_vl);
            uint32_t bm_tail_off = bm_off + bm_attr->Length;
            uint32_t bm_tail_bytes = rec->BytesInUse - bm_tail_off;
            if (bm_tail_bytes > 0)
                memmove((UCHAR*)rec + bm_tail_off + bm_grow,
                        (UCHAR*)rec + bm_tail_off, bm_tail_bytes);
            rec->BytesInUse += bm_grow;
            bm_attr = (PNTFSLX_ATTR_RECORD)((UCHAR*)rec + bm_off);
            bm_attr->Length = new_bm_attr_len;
            bm_attr->Data.Resident.ValueLength = new_vl;
            memset((UCHAR*)bm_attr + vo + vl, 0, new_vl - vl);
        }
        else if (bm_attr->NonResident != 0)
        {
            uint64_t bm_alloc = bm_attr->Data.NonResident.AllocatedSize;
            if ((uint64_t)new_bitmap_bytes > bm_alloc)
            {
                free(merged_rl); free(rec);
                fprintf(stderr, "mkntfsimg: $I30:$BITMAP non-resident cluster growth not implemented\n");
                return STATUS_BUFFER_OVERFLOW;
            }
            if ((uint64_t)new_bitmap_bytes > bm_attr->Data.NonResident.DataSize)
            {
                bm_attr->Data.NonResident.DataSize = new_bitmap_bytes;
                bm_attr->Data.NonResident.InitializedSize = new_bitmap_bytes;
            }
        }
    }

    st = ntfsimg_write_mft_record(vol, dir_mft_index, rec);
    free(merged_rl);
    free(rec);
    return st;
}
