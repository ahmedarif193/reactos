#include "mkntfsimg.h"

static uint32_t
decode_clusters_or_bytes(int8_t raw, uint32_t bytes_per_cluster)
{
    if (raw >= 0)
        return (uint32_t)raw * bytes_per_cluster;
    return 1u << (uint32_t)(-raw);
}

static NTSTATUS
load_runlist_from_attr(
    const NTFSLX_ATTR_RECORD *attr,
    const uint8_t *record_end,
    NTFSLX_RUNLIST_ELEMENT **out_runlist,
    uint32_t *out_count,
    uint64_t *out_data_size)
{
    const uint8_t *mp;
    const uint8_t *mp_end;
    uint64_t cur_vcn = 0;
    int64_t prev_lcn = 0;
    NTFSLX_RUNLIST_ELEMENT *rl;
    uint32_t capacity = 16;
    uint32_t count = 0;

    if (attr->NonResident == 0)
        return STATUS_INVALID_PARAMETER;

    mp = (const uint8_t*)attr + attr->Data.NonResident.MappingPairsOffset;
    mp_end = (const uint8_t*)attr + attr->Length;
    if (mp_end > record_end)
        mp_end = record_end;

    rl = calloc(capacity, sizeof(*rl));
    if (!rl) return STATUS_INSUFFICIENT_RESOURCES;

    while (mp < mp_end && *mp != 0)
    {
        uint8_t hdr = *mp++;
        uint32_t len_bytes = hdr & 0x0F;
        uint32_t off_bytes = (hdr >> 4) & 0x0F;
        int64_t run_len = 0;
        int64_t delta_lcn = 0;
        int64_t abs_lcn;
        uint32_t i;

        if (len_bytes == 0 || mp + len_bytes + off_bytes > mp_end)
        {
            free(rl);
            return STATUS_FILE_CORRUPT_ERROR;
        }

        for (i = 0; i < len_bytes; i++)
            run_len |= (int64_t)mp[i] << (i * 8);
        if (len_bytes < 8 && (mp[len_bytes - 1] & 0x80))
            run_len |= (int64_t)(~(uint64_t)0) << (len_bytes * 8);
        mp += len_bytes;

        if (off_bytes > 0)
        {
            for (i = 0; i < off_bytes; i++)
                delta_lcn |= (int64_t)mp[i] << (i * 8);
            if (off_bytes < 8 && (mp[off_bytes - 1] & 0x80))
                delta_lcn |= (int64_t)(~(uint64_t)0) << (off_bytes * 8);
            mp += off_bytes;
            abs_lcn = prev_lcn + delta_lcn;
            prev_lcn = abs_lcn;
        }
        else
        {
            abs_lcn = NTFSLX_LCN_HOLE;
        }

        if (count == capacity)
        {
            NTFSLX_RUNLIST_ELEMENT *bigger;
            capacity *= 2;
            bigger = realloc(rl, capacity * sizeof(*rl));
            if (!bigger) { free(rl); return STATUS_INSUFFICIENT_RESOURCES; }
            rl = bigger;
        }

        rl[count].Vcn = (int64_t)cur_vcn;
        rl[count].Lcn = abs_lcn;
        rl[count].Length = run_len;
        count++;
        cur_vcn += (uint64_t)run_len;
    }

    *out_runlist = rl;
    *out_count = count;
    if (out_data_size)
        *out_data_size = attr->Data.NonResident.DataSize;
    return STATUS_SUCCESS;
}

static const NTFSLX_ATTR_RECORD*
find_attr_in_record(const NTFSLX_MFT_RECORD *rec, uint32_t record_size, uint32_t type)
{
    const uint8_t *base = (const uint8_t*)rec;
    const NTFSLX_ATTR_RECORD *attr;
    uint32_t offset = rec->AttributesOffset;

    while (offset + sizeof(NTFSLX_ATTR_RECORD) <= record_size)
    {
        attr = (const NTFSLX_ATTR_RECORD*)(base + offset);
        if (attr->Type == NTFSLX_ATTRIBUTE_END)
            return NULL;
        if (attr->Length == 0 || attr->Length > record_size - offset)
            return NULL;
        if (attr->Type == type)
            return attr;
        offset += attr->Length;
    }
    return NULL;
}

static NTSTATUS
read_mft_record_via_runlist(
    NTFSIMG_VOLUME *vol,
    const NTFSLX_RUNLIST_ELEMENT *rl,
    uint32_t rl_count,
    uint64_t record_index,
    NTFSLX_MFT_RECORD *out_record)
{
    uint32_t rec_size = vol->bytes_per_mft_record;
    uint64_t byte_offset_in_mft = record_index * rec_size;
    uint64_t cluster_offset = byte_offset_in_mft / vol->bytes_per_cluster;
    uint64_t intra = byte_offset_in_mft % vol->bytes_per_cluster;
    uint32_t i;

    for (i = 0; i < rl_count; i++)
    {
        uint64_t run_start_vcn = (uint64_t)rl[i].Vcn;
        uint64_t run_length    = (uint64_t)rl[i].Length;
        if (cluster_offset >= run_start_vcn &&
            cluster_offset < run_start_vcn + run_length)
        {
            int64_t lcn = rl[i].Lcn + (int64_t)(cluster_offset - run_start_vcn);
            uint64_t disk_byte = (uint64_t)lcn * vol->bytes_per_cluster + intra;
            NTSTATUS st = ntfsimg_io_read(&vol->io, disk_byte, rec_size, out_record);
            if (!NT_SUCCESS(st)) return st;
            if (out_record->Ntfs.Magic != NTFSLX_RECORD_MAGIC_FILE)
                return STATUS_FILE_CORRUPT_ERROR;
            {
                USHORT usa_count = out_record->Ntfs.UsaCount;
                USHORT usa_offset = out_record->Ntfs.UsaOffset;
                USHORT usn;
                USHORT *usa_pos = (USHORT*)((uint8_t*)out_record + usa_offset);
                USHORT *data_pos;
                usn = *usa_pos;
                data_pos = (USHORT*)out_record + 512 / sizeof(USHORT) - 1;
                while (--usa_count)
                {
                    usa_pos++;
                    if (*data_pos != usn)
                        return STATUS_FILE_CORRUPT_ERROR;
                    *data_pos = *usa_pos;
                    data_pos += 512 / sizeof(USHORT);
                }
            }
            return STATUS_SUCCESS;
        }
    }
    return STATUS_NOT_FOUND;
}

NTSTATUS
ntfsimg_volume_open(NTFSIMG_VOLUME *vol, const char *path)
{
    NTSTATUS st;
    NTFSLX_BOOT_SECTOR boot;
    NTFSLX_MFT_RECORD *mft_record_0 = NULL;
    NTFSLX_MFT_RECORD *mft_record_6 = NULL;
    NTFSLX_RUNLIST_ELEMENT *bootstrap_mft_rl = NULL;
    uint32_t bootstrap_rl_count = 1;
    const NTFSLX_ATTR_RECORD *data_attr;

    memset(vol, 0, sizeof(*vol));
    vol->io.fd = -1;

    st = ntfsimg_io_open(&vol->io, path);
    if (!NT_SUCCESS(st)) return st;

    st = ntfsimg_io_read(&vol->io, 0, sizeof(boot), &boot);
    if (!NT_SUCCESS(st)) goto fail;

    if (memcmp(&boot.OemId, "NTFS    ", 8) != 0)
    {
        fprintf(stderr, "mkntfsimg: '%s' is not an NTFS volume (bad OEM ID)\n", path);
        st = STATUS_DISK_CORRUPT_ERROR;
        goto fail;
    }

    vol->bytes_per_sector    = boot.Bpb.BytesPerSector;
    vol->sectors_per_cluster = boot.Bpb.SectorsPerCluster;
    vol->bytes_per_cluster   = vol->bytes_per_sector * vol->sectors_per_cluster;
    vol->bytes_per_mft_record   = decode_clusters_or_bytes(boot.ClustersPerMftRecord, vol->bytes_per_cluster);
    vol->bytes_per_index_record = decode_clusters_or_bytes(boot.ClustersPerIndexRecord, vol->bytes_per_cluster);
    vol->total_sectors = boot.SectorCount;
    vol->total_clusters = vol->total_sectors / vol->sectors_per_cluster;
    vol->mft_lcn       = boot.MftLcn;
    vol->mft_mirr_lcn  = boot.MftMirrLcn;
    vol->serial_number = boot.SerialNumber;
    vol->io.sector_size = vol->bytes_per_sector;

    bootstrap_mft_rl = calloc(1, sizeof(*bootstrap_mft_rl));
    if (!bootstrap_mft_rl) { st = STATUS_INSUFFICIENT_RESOURCES; goto fail; }
    bootstrap_mft_rl[0].Vcn = 0;
    bootstrap_mft_rl[0].Lcn = (LONGLONG)vol->mft_lcn;
    bootstrap_mft_rl[0].Length = 1;

    mft_record_0 = calloc(1, vol->bytes_per_mft_record);
    if (!mft_record_0) { st = STATUS_INSUFFICIENT_RESOURCES; goto fail; }

    st = read_mft_record_via_runlist(vol, bootstrap_mft_rl, bootstrap_rl_count,
                                     NTFSLX_FILE_MFT, mft_record_0);
    if (!NT_SUCCESS(st))
    {
        fprintf(stderr, "mkntfsimg: failed to read $MFT record 0 (0x%x)\n", (unsigned)st);
        goto fail;
    }

    data_attr = find_attr_in_record(mft_record_0, vol->bytes_per_mft_record,
                                    NTFSLX_ATTRIBUTE_DATA);
    if (!data_attr || data_attr->NonResident == 0)
    {
        st = STATUS_FILE_CORRUPT_ERROR;
        goto fail;
    }

    st = load_runlist_from_attr(data_attr,
                                (const uint8_t*)mft_record_0 + vol->bytes_per_mft_record,
                                &vol->mft_runlist, &vol->mft_runlist_count,
                                &vol->mft_data_size);
    if (!NT_SUCCESS(st)) goto fail;

    mft_record_6 = calloc(1, vol->bytes_per_mft_record);
    if (!mft_record_6) { st = STATUS_INSUFFICIENT_RESOURCES; goto fail; }

    st = read_mft_record_via_runlist(vol, vol->mft_runlist, vol->mft_runlist_count,
                                     NTFSLX_FILE_BITMAP, mft_record_6);
    if (!NT_SUCCESS(st))
    {
        fprintf(stderr, "mkntfsimg: failed to read $Bitmap record 6 (0x%x)\n", (unsigned)st);
        goto fail;
    }

    data_attr = find_attr_in_record(mft_record_6, vol->bytes_per_mft_record,
                                    NTFSLX_ATTRIBUTE_DATA);
    if (!data_attr || data_attr->NonResident == 0)
    {
        st = STATUS_FILE_CORRUPT_ERROR;
        goto fail;
    }

    st = load_runlist_from_attr(data_attr,
                                (const uint8_t*)mft_record_6 + vol->bytes_per_mft_record,
                                &vol->bitmap_runlist, &vol->bitmap_runlist_count,
                                &vol->bitmap_data_size);
    if (!NT_SUCCESS(st)) goto fail;

    free(bootstrap_mft_rl);
    free(mft_record_0);
    free(mft_record_6);
    return STATUS_SUCCESS;

fail:
    free(bootstrap_mft_rl);
    free(mft_record_0);
    free(mft_record_6);
    ntfsimg_volume_close(vol);
    return st;
}

void
ntfsimg_volume_close(NTFSIMG_VOLUME *vol)
{
    free(vol->mft_runlist);
    vol->mft_runlist = NULL;
    vol->mft_runlist_count = 0;
    free(vol->bitmap_runlist);
    vol->bitmap_runlist = NULL;
    vol->bitmap_runlist_count = 0;
    ntfsimg_io_close(&vol->io);
}
