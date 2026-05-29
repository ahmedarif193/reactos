#include "mkntfsimg.h"

NTSTATUS
ntfsimg_runlist_vcn_to_lcn(const NTFSLX_RUNLIST_ELEMENT *rl, uint32_t rl_count,
                           int64_t vcn, int64_t *out_lcn)
{
    uint32_t i;
    for (i = 0; i < rl_count; i++)
    {
        if (vcn >= rl[i].Vcn && vcn < rl[i].Vcn + rl[i].Length)
        {
            if (rl[i].Lcn < 0) return STATUS_FILE_CORRUPT_ERROR;
            *out_lcn = rl[i].Lcn + (vcn - rl[i].Vcn);
            return STATUS_SUCCESS;
        }
    }
    return STATUS_NOT_FOUND;
}

NTSTATUS
ntfsimg_read_mapped(NTFSIMG_VOLUME *vol, const NTFSLX_RUNLIST_ELEMENT *rl, uint32_t rl_count,
                    uint64_t byte_offset, uint32_t length, void *buf)
{
    uint8_t *dst = (uint8_t*)buf;
    uint32_t remaining = length;
    while (remaining > 0)
    {
        int64_t vcn = (int64_t)(byte_offset / vol->bytes_per_cluster);
        uint32_t intra = (uint32_t)(byte_offset % vol->bytes_per_cluster);
        uint32_t chunk = vol->bytes_per_cluster - intra;
        int64_t lcn;
        NTSTATUS st;
        if (chunk > remaining) chunk = remaining;
        st = ntfsimg_runlist_vcn_to_lcn(rl, rl_count, vcn, &lcn);
        if (!NT_SUCCESS(st)) return st;
        st = ntfsimg_io_read(&vol->io,
                             (uint64_t)lcn * vol->bytes_per_cluster + intra,
                             chunk, dst);
        if (!NT_SUCCESS(st)) return st;
        dst += chunk;
        byte_offset += chunk;
        remaining -= chunk;
    }
    return STATUS_SUCCESS;
}

NTSTATUS
ntfsimg_write_mapped(NTFSIMG_VOLUME *vol, const NTFSLX_RUNLIST_ELEMENT *rl, uint32_t rl_count,
                     uint64_t byte_offset, uint32_t length, const void *buf)
{
    const uint8_t *src = (const uint8_t*)buf;
    uint32_t remaining = length;
    while (remaining > 0)
    {
        int64_t vcn = (int64_t)(byte_offset / vol->bytes_per_cluster);
        uint32_t intra = (uint32_t)(byte_offset % vol->bytes_per_cluster);
        uint32_t chunk = vol->bytes_per_cluster - intra;
        int64_t lcn;
        NTSTATUS st;
        if (chunk > remaining) chunk = remaining;
        st = ntfsimg_runlist_vcn_to_lcn(rl, rl_count, vcn, &lcn);
        if (!NT_SUCCESS(st)) return st;
        st = ntfsimg_io_write(&vol->io,
                              (uint64_t)lcn * vol->bytes_per_cluster + intra,
                              chunk, src);
        if (!NT_SUCCESS(st)) return st;
        src += chunk;
        byte_offset += chunk;
        remaining -= chunk;
    }
    return STATUS_SUCCESS;
}

static NTSTATUS
apply_post_read_fixup(NTFSLX_RECORD_HEADER *record, uint32_t size)
{
    USHORT usa_count = record->UsaCount;
    USHORT usa_offset = record->UsaOffset;
    USHORT usn;
    USHORT *usa_pos;
    USHORT *data_pos;

    if ((size & 511) != 0 || usa_count == 0) return STATUS_FILE_CORRUPT_ERROR;
    if ((uint32_t)usa_offset + (uint32_t)usa_count * 2 > size) return STATUS_FILE_CORRUPT_ERROR;
    if ((size >> 9) != (uint32_t)(usa_count - 1)) return STATUS_FILE_CORRUPT_ERROR;

    usa_pos = (USHORT*)((uint8_t*)record + usa_offset);
    usn = *usa_pos;
    data_pos = (USHORT*)record + 512 / sizeof(USHORT) - 1;
    while (--usa_count)
    {
        usa_pos++;
        if (*data_pos != usn) return STATUS_FILE_CORRUPT_ERROR;
        *data_pos = *usa_pos;
        data_pos += 512 / sizeof(USHORT);
    }
    return STATUS_SUCCESS;
}

NTSTATUS
ntfsimg_read_mft_record(NTFSIMG_VOLUME *vol, uint64_t record_number,
                        NTFSLX_MFT_RECORD *out_record)
{
    uint32_t rec_size = vol->bytes_per_mft_record;
    uint64_t byte_offset = record_number * rec_size;
    NTSTATUS st;

    if (byte_offset + rec_size > vol->mft_data_size)
        return STATUS_END_OF_FILE;

    st = ntfsimg_read_mapped(vol, vol->mft_runlist, vol->mft_runlist_count,
                             byte_offset, rec_size, out_record);
    if (!NT_SUCCESS(st)) return st;

    if (out_record->Ntfs.Magic != NTFSLX_RECORD_MAGIC_FILE)
    {
        if (out_record->Ntfs.Magic == NTFSLX_RECORD_MAGIC_BAAD)
            return STATUS_FILE_CORRUPT_ERROR;
        return STATUS_NOT_FOUND;
    }

    return apply_post_read_fixup(&out_record->Ntfs, rec_size);
}

NTSTATUS
ntfsimg_write_mft_record(NTFSIMG_VOLUME *vol, uint64_t record_number,
                         NTFSLX_MFT_RECORD *record)
{
    uint32_t rec_size = vol->bytes_per_mft_record;
    uint64_t byte_offset = record_number * rec_size;
    NTFSLX_MFT_RECORD *copy = NULL;
    NTSTATUS st;

    if (byte_offset + rec_size > vol->mft_data_size)
        return STATUS_END_OF_FILE;

    copy = malloc(rec_size);
    if (!copy) return STATUS_INSUFFICIENT_RESOURCES;
    memcpy(copy, record, rec_size);

    st = NtfslxPreWriteMstFixup(&copy->Ntfs, rec_size);
    if (!NT_SUCCESS(st)) { free(copy); return st; }

    st = ntfsimg_write_mapped(vol, vol->mft_runlist, vol->mft_runlist_count,
                              byte_offset, rec_size, copy);

    if (NT_SUCCESS(st) && record_number < 4 && vol->mft_mirr_lcn != 0)
    {
        uint64_t mirror_byte = vol->mft_mirr_lcn * vol->bytes_per_cluster +
                               record_number * rec_size;
        NTSTATUS mst = ntfsimg_io_write(&vol->io, mirror_byte, rec_size, copy);
        if (!NT_SUCCESS(mst))
            fprintf(stderr, "mkntfsimg: warning: mirror write rec=%llu failed 0x%x\n",
                    (unsigned long long)record_number, (unsigned)mst);
    }

    free(copy);
    return st;
}
