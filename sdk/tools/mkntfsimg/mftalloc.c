#include "mkntfsimg.h"

static NTSTATUS
load_attr_data(NTFSIMG_VOLUME *vol, PNTFSLX_ATTR_RECORD attr,
               UCHAR **out_buf, uint32_t *out_bytes)
{
    if (attr->NonResident == 0)
    {
        uint32_t n = attr->Data.Resident.ValueLength;
        UCHAR *buf = malloc(n);
        if (!buf) return STATUS_INSUFFICIENT_RESOURCES;
        memcpy(buf, (UCHAR*)attr + attr->Data.Resident.ValueOffset, n);
        *out_buf = buf;
        *out_bytes = n;
        return STATUS_SUCCESS;
    }
    else
    {
        NTFSLX_RUNLIST_ELEMENT *rl = NULL;
        uint32_t rl_count = 0;
        uint32_t n = (uint32_t)attr->Data.NonResident.DataSize;
        uint32_t alloc = (uint32_t)attr->Data.NonResident.AllocatedSize;
        UCHAR *buf;
        NTSTATUS st;
        const uint8_t *mp = (const uint8_t*)attr + attr->Data.NonResident.MappingPairsOffset;
        const uint8_t *mp_end = (const uint8_t*)attr + attr->Length;
        uint64_t cur_vcn = 0;
        int64_t prev_lcn = 0;
        uint32_t capacity = 8;

        rl = calloc(capacity, sizeof(*rl));
        if (!rl) return STATUS_INSUFFICIENT_RESOURCES;

        while (mp < mp_end && *mp != 0)
        {
            uint8_t hdr = *mp++;
            uint32_t len_bytes = hdr & 0x0F;
            uint32_t off_bytes = (hdr >> 4) & 0x0F;
            int64_t run_len = 0, delta_lcn = 0, abs_lcn;
            uint32_t i;
            if (len_bytes == 0 || mp + len_bytes + off_bytes > mp_end)
            { free(rl); return STATUS_FILE_CORRUPT_ERROR; }
            for (i = 0; i < len_bytes; i++) run_len |= (int64_t)mp[i] << (i*8);
            if (len_bytes < 8 && (mp[len_bytes-1] & 0x80))
                run_len |= (int64_t)(~(uint64_t)0) << (len_bytes*8);
            mp += len_bytes;
            if (off_bytes > 0)
            {
                for (i = 0; i < off_bytes; i++) delta_lcn |= (int64_t)mp[i] << (i*8);
                if (off_bytes < 8 && (mp[off_bytes-1] & 0x80))
                    delta_lcn |= (int64_t)(~(uint64_t)0) << (off_bytes*8);
                mp += off_bytes;
                abs_lcn = prev_lcn + delta_lcn;
                prev_lcn = abs_lcn;
            }
            else
                abs_lcn = NTFSLX_LCN_HOLE;
            if (rl_count == capacity)
            {
                NTFSLX_RUNLIST_ELEMENT *bigger;
                capacity *= 2;
                bigger = realloc(rl, capacity*sizeof(*rl));
                if (!bigger) { free(rl); return STATUS_INSUFFICIENT_RESOURCES; }
                rl = bigger;
            }
            rl[rl_count].Vcn = (int64_t)cur_vcn;
            rl[rl_count].Lcn = abs_lcn;
            rl[rl_count].Length = run_len;
            rl_count++;
            cur_vcn += (uint64_t)run_len;
        }

        buf = calloc(1, alloc);
        if (!buf) { free(rl); return STATUS_INSUFFICIENT_RESOURCES; }
        st = ntfsimg_read_mapped(vol, rl, rl_count, 0, n, buf);
        free(rl);
        if (!NT_SUCCESS(st)) { free(buf); return st; }
        *out_buf = buf;
        *out_bytes = n;
        return STATUS_SUCCESS;
    }
}

static NTSTATUS
save_attr_data(NTFSIMG_VOLUME *vol, NTFSLX_MFT_RECORD *rec, PNTFSLX_ATTR_RECORD attr,
               const UCHAR *buf, uint32_t bytes)
{
    if (attr->NonResident == 0)
    {
        if (bytes > attr->Data.Resident.ValueLength) return STATUS_BUFFER_OVERFLOW;
        memcpy((UCHAR*)attr + attr->Data.Resident.ValueOffset, buf, bytes);
        return ntfsimg_write_mft_record(vol, 0, rec);
    }
    else
    {
        NTFSLX_RUNLIST_ELEMENT *rl = NULL;
        uint32_t rl_count = 0;
        const uint8_t *mp = (const uint8_t*)attr + attr->Data.NonResident.MappingPairsOffset;
        const uint8_t *mp_end = (const uint8_t*)attr + attr->Length;
        uint64_t cur_vcn = 0;
        int64_t prev_lcn = 0;
        uint32_t capacity = 8;
        NTSTATUS st;

        rl = calloc(capacity, sizeof(*rl));
        if (!rl) return STATUS_INSUFFICIENT_RESOURCES;

        while (mp < mp_end && *mp != 0)
        {
            uint8_t hdr = *mp++;
            uint32_t len_bytes = hdr & 0x0F;
            uint32_t off_bytes = (hdr >> 4) & 0x0F;
            int64_t run_len = 0, delta_lcn = 0, abs_lcn;
            uint32_t i;
            if (len_bytes == 0 || mp + len_bytes + off_bytes > mp_end)
            { free(rl); return STATUS_FILE_CORRUPT_ERROR; }
            for (i = 0; i < len_bytes; i++) run_len |= (int64_t)mp[i] << (i*8);
            if (len_bytes < 8 && (mp[len_bytes-1] & 0x80))
                run_len |= (int64_t)(~(uint64_t)0) << (len_bytes*8);
            mp += len_bytes;
            if (off_bytes > 0)
            {
                for (i = 0; i < off_bytes; i++) delta_lcn |= (int64_t)mp[i] << (i*8);
                if (off_bytes < 8 && (mp[off_bytes-1] & 0x80))
                    delta_lcn |= (int64_t)(~(uint64_t)0) << (off_bytes*8);
                mp += off_bytes;
                abs_lcn = prev_lcn + delta_lcn;
                prev_lcn = abs_lcn;
            }
            else
                abs_lcn = NTFSLX_LCN_HOLE;
            if (rl_count == capacity)
            {
                NTFSLX_RUNLIST_ELEMENT *bigger;
                capacity *= 2;
                bigger = realloc(rl, capacity*sizeof(*rl));
                if (!bigger) { free(rl); return STATUS_INSUFFICIENT_RESOURCES; }
                rl = bigger;
            }
            rl[rl_count].Vcn = (int64_t)cur_vcn;
            rl[rl_count].Lcn = abs_lcn;
            rl[rl_count].Length = run_len;
            rl_count++;
            cur_vcn += (uint64_t)run_len;
        }

        st = ntfsimg_write_mapped(vol, rl, rl_count, 0, bytes, buf);
        free(rl);
        return st;
    }
}

NTSTATUS
ntfsimg_mft_bitmap_load(NTFSIMG_VOLUME *vol, UCHAR **out_bitmap, uint32_t *out_bytes)
{
    NTFSLX_MFT_RECORD *rec = malloc(vol->bytes_per_mft_record);
    PNTFSLX_ATTR_RECORD attr;
    NTSTATUS st;

    if (!rec) return STATUS_INSUFFICIENT_RESOURCES;
    st = ntfsimg_read_mft_record(vol, 0, rec);
    if (!NT_SUCCESS(st)) { free(rec); return st; }

    attr = ntfsimg_find_attr(rec, vol->bytes_per_mft_record, NTFSLX_ATTRIBUTE_BITMAP);
    if (!attr) { free(rec); return STATUS_NOT_FOUND; }

    st = load_attr_data(vol, attr, out_bitmap, out_bytes);
    free(rec);
    return st;
}

NTSTATUS
ntfsimg_mft_bitmap_save(NTFSIMG_VOLUME *vol, const UCHAR *bitmap, uint32_t bytes)
{
    NTFSLX_MFT_RECORD *rec = malloc(vol->bytes_per_mft_record);
    PNTFSLX_ATTR_RECORD attr;
    NTSTATUS st;

    if (!rec) return STATUS_INSUFFICIENT_RESOURCES;
    st = ntfsimg_read_mft_record(vol, 0, rec);
    if (!NT_SUCCESS(st)) { free(rec); return st; }

    attr = ntfsimg_find_attr(rec, vol->bytes_per_mft_record, NTFSLX_ATTRIBUTE_BITMAP);
    if (!attr) { free(rec); return STATUS_NOT_FOUND; }

    st = save_attr_data(vol, rec, attr, bitmap, bytes);
    free(rec);
    return st;
}

NTSTATUS
ntfsimg_mft_alloc_record(const UCHAR *bitmap, uint32_t bitmap_bytes,
                         uint32_t total_records, uint64_t *out_index)
{
    uint32_t bit;
    uint32_t max_bits = bitmap_bytes * 8;
    if (total_records < max_bits) max_bits = total_records;

    for (bit = NTFSLX_FILE_FIRST_USER; bit < max_bits; bit++)
    {
        uint32_t byte_idx = bit >> 3;
        uint8_t mask = (uint8_t)(1u << (bit & 7));
        if ((bitmap[byte_idx] & mask) == 0)
        {
            *out_index = bit;
            return STATUS_SUCCESS;
        }
    }
    return STATUS_DISK_FULL;
}
