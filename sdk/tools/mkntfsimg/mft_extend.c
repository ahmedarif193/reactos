#include "mkntfsimg.h"

#define ALIGN8(x) (((x) + 7u) & ~7u)

static NTSTATUS
merge_runs(NTFSLX_RUNLIST_ELEMENT *old, uint32_t old_count,
           NTFSLX_RUNLIST_ELEMENT *add, uint32_t add_count,
           NTFSLX_RUNLIST_ELEMENT **out_rl, uint32_t *out_count)
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

    *out_rl = m;
    *out_count = n;
    return STATUS_SUCCESS;
}

static int
find_attr_off(NTFSLX_MFT_RECORD *rec, uint32_t rec_size, uint32_t type, uint32_t *out_off)
{
    uint8_t *base = (uint8_t*)rec;
    uint32_t offset = rec->AttributesOffset;
    while (offset + sizeof(NTFSLX_ATTR_RECORD) <= rec_size)
    {
        PNTFSLX_ATTR_RECORD a = (PNTFSLX_ATTR_RECORD)(base + offset);
        if (a->Type == NTFSLX_ATTRIBUTE_END) return 0;
        if (a->Length == 0 || a->Length > rec_size - offset) return 0;
        if (a->Type == type) { *out_off = offset; return 1; }
        offset += a->Length;
    }
    return 0;
}

NTSTATUS
ntfsimg_mft_extend(NTFSIMG_VOLUME *vol, NTFSIMG_CLUSTER_ALLOCATOR *ca,
                   uint32_t extra_records)
{
    uint32_t rec_size = vol->bytes_per_mft_record;
    uint32_t bpc = vol->bytes_per_cluster;
    uint32_t recs_per_cluster = bpc / rec_size;
    uint32_t extra_clusters;
    NTFSLX_RUNLIST_ELEMENT *new_rl = NULL;
    uint32_t new_rl_count = 0;
    NTFSLX_RUNLIST_ELEMENT *merged_rl = NULL;
    uint32_t merged_rl_count = 0;
    NTFSLX_MFT_RECORD *rec0 = NULL;
    uint32_t data_off, bitmap_off;
    PNTFSLX_ATTR_RECORD data_attr;
    PNTFSLX_ATTR_RECORD bm_attr;
    uint64_t old_data_size;
    uint64_t new_data_size;
    uint32_t old_total_records;
    uint32_t new_total_records;
    uint32_t old_rl_count;
    uint32_t i;
    UCHAR *blank_rec = NULL;
    LONG mp_size;
    uint32_t new_mp_offset;
    uint32_t new_attr_length;
    NTSTATUS st;

    if (recs_per_cluster == 0) recs_per_cluster = 1;
    extra_clusters = (extra_records + recs_per_cluster - 1) / recs_per_cluster;
    if (extra_clusters == 0) extra_clusters = 1;

    st = ntfsimg_cluster_alloc(ca, extra_clusters, &new_rl, &new_rl_count);
    if (!NT_SUCCESS(st)) return st;

    rec0 = malloc(rec_size);
    if (!rec0) { free(new_rl); return STATUS_INSUFFICIENT_RESOURCES; }

    st = ntfsimg_read_mft_record(vol, NTFSLX_FILE_MFT, rec0);
    if (!NT_SUCCESS(st)) { free(rec0); free(new_rl); return st; }

    if (!find_attr_off(rec0, rec_size, NTFSLX_ATTRIBUTE_DATA, &data_off))
    { free(rec0); free(new_rl); return STATUS_FILE_CORRUPT_ERROR; }
    data_attr = (PNTFSLX_ATTR_RECORD)((UCHAR*)rec0 + data_off);
    if (!data_attr->NonResident)
    { free(rec0); free(new_rl); return STATUS_FILE_CORRUPT_ERROR; }

    old_data_size = data_attr->Data.NonResident.AllocatedSize;
    old_total_records = (uint32_t)(old_data_size / rec_size);

    merged_rl = NULL;
    old_rl_count = vol->mft_runlist_count;
    st = merge_runs(vol->mft_runlist, old_rl_count,
                    new_rl, new_rl_count,
                    &merged_rl, &merged_rl_count);
    free(new_rl);
    if (!NT_SUCCESS(st)) { free(rec0); return st; }

    mp_size = NtfslxGetSizeForMappingPairs(merged_rl, merged_rl_count, 0, -1);
    if (mp_size <= 0) { free(merged_rl); free(rec0); return STATUS_INSUFFICIENT_RESOURCES; }

    new_mp_offset = data_attr->Data.NonResident.MappingPairsOffset;
    new_attr_length = (uint32_t)ALIGN8(new_mp_offset + (uint32_t)mp_size);

    {
        uint32_t old_len = data_attr->Length;
        int32_t delta = (int32_t)new_attr_length - (int32_t)old_len;
        uint32_t tail_off = data_off + old_len;
        uint32_t tail_bytes = rec0->BytesInUse - tail_off;
        if ((int32_t)rec0->BytesInUse + delta + (int32_t)sizeof(ULONG) > (int32_t)rec_size)
        { free(merged_rl); free(rec0); return STATUS_BUFFER_OVERFLOW; }
        if (delta != 0 && tail_bytes > 0)
            memmove((UCHAR*)rec0 + tail_off + delta, (UCHAR*)rec0 + tail_off, tail_bytes);
        rec0->BytesInUse += delta;
    }

    new_data_size = old_data_size + (uint64_t)extra_clusters * bpc;
    new_total_records = (uint32_t)(new_data_size / rec_size);

    data_attr->Length = new_attr_length;
    data_attr->Data.NonResident.HighestVcn = (int64_t)(merged_rl[merged_rl_count - 1].Vcn +
                                                       merged_rl[merged_rl_count - 1].Length - 1);
    data_attr->Data.NonResident.AllocatedSize = new_data_size;
    data_attr->Data.NonResident.DataSize = new_data_size;
    data_attr->Data.NonResident.InitializedSize = new_data_size;

    st = NtfslxMappingPairsBuild(
        (UCHAR*)data_attr + new_mp_offset,
        (LONG)(new_attr_length - new_mp_offset),
        merged_rl, merged_rl_count, 0, -1, NULL);
    if (!NT_SUCCESS(st)) { free(merged_rl); free(rec0); return st; }

    if (!find_attr_off(rec0, rec_size, NTFSLX_ATTRIBUTE_BITMAP, &bitmap_off))
    { free(merged_rl); free(rec0); return STATUS_FILE_CORRUPT_ERROR; }
    bm_attr = (PNTFSLX_ATTR_RECORD)((UCHAR*)rec0 + bitmap_off);

    {
        uint32_t new_bits_needed = new_total_records;
        uint32_t new_bytes_needed = (new_bits_needed + 7) / 8;
        new_bytes_needed = (uint32_t)(((new_bytes_needed + 7) / 8) * 8);

        if (bm_attr->NonResident == 0)
        {
            uint32_t vo = bm_attr->Data.Resident.ValueOffset;
            uint32_t vl = bm_attr->Data.Resident.ValueLength;
            if (new_bytes_needed > vl)
            {
                uint32_t grow = new_bytes_needed - vl;
                uint32_t hdr_and_pad = (uint32_t)ALIGN8(vo + new_bytes_needed);
                int32_t delta = (int32_t)hdr_and_pad - (int32_t)bm_attr->Length;
                uint32_t tail_off = bitmap_off + bm_attr->Length;
                uint32_t tail_bytes = rec0->BytesInUse - tail_off;
                if ((int32_t)rec0->BytesInUse + delta + (int32_t)sizeof(ULONG) > (int32_t)rec_size)
                { free(merged_rl); free(rec0); return STATUS_BUFFER_OVERFLOW; }
                if (delta != 0 && tail_bytes > 0)
                    memmove((UCHAR*)rec0 + tail_off + delta, (UCHAR*)rec0 + tail_off, tail_bytes);
                rec0->BytesInUse += delta;
                bm_attr = (PNTFSLX_ATTR_RECORD)((UCHAR*)rec0 + bitmap_off);
                bm_attr->Length = hdr_and_pad;
                bm_attr->Data.Resident.ValueLength = new_bytes_needed;
                memset((UCHAR*)bm_attr + vo + vl, 0, grow);
            }
        }
        else
        {
            uint64_t bm_alloc = bm_attr->Data.NonResident.AllocatedSize;
            uint64_t bm_data = bm_attr->Data.NonResident.DataSize;
            if ((uint64_t)new_bytes_needed > bm_alloc)
            {
                free(merged_rl); free(rec0);
                fprintf(stderr, "mkntfsimg: $MFT:$BITMAP cluster extension not implemented (needed=%u, alloc=%llu)\n",
                        new_bytes_needed, (unsigned long long)bm_alloc);
                return STATUS_BUFFER_OVERFLOW;
            }
            if ((uint64_t)new_bytes_needed > bm_data)
            {
                bm_attr->Data.NonResident.DataSize = new_bytes_needed;
                bm_attr->Data.NonResident.InitializedSize = new_bytes_needed;
            }
        }
    }

    st = ntfsimg_write_mft_record(vol, NTFSLX_FILE_MFT, rec0);
    if (!NT_SUCCESS(st)) { free(merged_rl); free(rec0); return st; }

    free(vol->mft_runlist);
    vol->mft_runlist = merged_rl;
    vol->mft_runlist_count = merged_rl_count;
    vol->mft_data_size = new_data_size;

    blank_rec = calloc(1, rec_size);
    if (!blank_rec) { free(rec0); return STATUS_INSUFFICIENT_RESOURCES; }

    {
        PNTFSLX_RECORD_HEADER h = (PNTFSLX_RECORD_HEADER)blank_rec;
        h->Magic = NTFSLX_RECORD_MAGIC_FILE;
        h->UsaOffset = (USHORT)sizeof(NTFSLX_MFT_RECORD);
        h->UsaCount = (USHORT)(rec_size / 512 + 1);
    }
    {
        NTFSLX_MFT_RECORD *mr = (NTFSLX_MFT_RECORD*)blank_rec;
        mr->SequenceNumber = 0;
        mr->LinkCount = 0;
        mr->AttributesOffset = (USHORT)ALIGN8(sizeof(NTFSLX_MFT_RECORD) + mr->Ntfs.UsaCount * 2);
        mr->Flags = 0;
        mr->BytesInUse = mr->AttributesOffset + (uint32_t)sizeof(ULONG);
        mr->BytesAllocated = rec_size;
        {
            PNTFSLX_ATTR_RECORD end = (PNTFSLX_ATTR_RECORD)(blank_rec + mr->AttributesOffset);
            end->Type = NTFSLX_ATTRIBUTE_END;
            end->Length = 0;
        }
    }

    for (i = old_total_records; i < new_total_records; i++)
    {
        NTFSLX_MFT_RECORD *mr = (NTFSLX_MFT_RECORD*)blank_rec;
        mr->MftRecordNumber = i;
        st = ntfsimg_write_mft_record(vol, i, mr);
        if (!NT_SUCCESS(st)) { free(blank_rec); free(rec0); return st; }
    }

    free(blank_rec);
    free(rec0);
    return STATUS_SUCCESS;
}

NTSTATUS
ntfsimg_mft_alloc_or_extend(NTFSIMG_VOLUME *vol, NTFSIMG_CLUSTER_ALLOCATOR *ca,
                            uint64_t *out_index)
{
    UCHAR *bitmap = NULL;
    uint32_t bitmap_bytes = 0;
    uint32_t total_records;
    NTSTATUS st;

    st = ntfsimg_mft_bitmap_load(vol, &bitmap, &bitmap_bytes);
    if (!NT_SUCCESS(st)) return st;

    total_records = (uint32_t)(vol->mft_data_size / vol->bytes_per_mft_record);
    st = ntfsimg_mft_alloc_record(bitmap, bitmap_bytes, total_records, out_index);

    if (st == STATUS_DISK_FULL)
    {
        free(bitmap);
        st = ntfsimg_mft_extend(vol, ca, 256);
        if (!NT_SUCCESS(st)) return st;

        st = ntfsimg_mft_bitmap_load(vol, &bitmap, &bitmap_bytes);
        if (!NT_SUCCESS(st)) return st;

        total_records = (uint32_t)(vol->mft_data_size / vol->bytes_per_mft_record);
        st = ntfsimg_mft_alloc_record(bitmap, bitmap_bytes, total_records, out_index);
        if (!NT_SUCCESS(st)) { free(bitmap); return st; }
    }
    else if (!NT_SUCCESS(st))
    {
        free(bitmap);
        return st;
    }

    bitmap[*out_index >> 3] |= (UCHAR)(1u << (*out_index & 7));
    st = ntfsimg_mft_bitmap_save(vol, bitmap, bitmap_bytes);
    free(bitmap);
    return st;
}
