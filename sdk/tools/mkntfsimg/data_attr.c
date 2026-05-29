#include "mkntfsimg.h"

#define ALIGN8(x) (((x) + 7u) & ~7u)

static PNTFSLX_ATTR_RECORD
find_end_attr(UCHAR *buf, uint32_t rec_size, uint32_t *out_offset)
{
    NTFSLX_MFT_RECORD *rec = (NTFSLX_MFT_RECORD*)buf;
    uint32_t offset = rec->AttributesOffset;
    while (offset + sizeof(NTFSLX_ATTR_RECORD) <= rec_size)
    {
        PNTFSLX_ATTR_RECORD a = (PNTFSLX_ATTR_RECORD)(buf + offset);
        if (a->Type == NTFSLX_ATTRIBUTE_END) { *out_offset = offset; return a; }
        if (a->Length == 0 || a->Length > rec_size - offset) return NULL;
        offset += a->Length;
    }
    return NULL;
}

NTSTATUS
ntfsimg_append_data_nonresident(UCHAR *buf, uint32_t rec_size,
                                NTFSIMG_VOLUME *vol, NTFSIMG_CLUSTER_ALLOCATOR *ca,
                                const char *host_path, uint64_t host_size)
{
    NTFSLX_MFT_RECORD *rec = (NTFSLX_MFT_RECORD*)buf;
    uint32_t end_off;
    PNTFSLX_ATTR_RECORD end_attr = find_end_attr(buf, rec_size, &end_off);
    uint32_t non_res_header_size = 64;
    uint32_t bpc = vol->bytes_per_cluster;
    uint64_t needed_clusters = (host_size + bpc - 1) / bpc;
    NTFSLX_RUNLIST_ELEMENT *rl = NULL;
    uint32_t rl_count = 0;
    LONG mp_size;
    uint32_t attr_total;
    PNTFSLX_ATTR_RECORD attr;
    uint32_t mp_offset;
    NTSTATUS st;
    FILE *fp = NULL;
    UCHAR *chunk = NULL;
    uint64_t bytes_left;
    uint64_t cur_vcn = 0;
    uint32_t i;

    if (!end_attr) return STATUS_FILE_CORRUPT_ERROR;
    if (host_size == 0)
    {
        return ntfsimg_append_data_resident(buf, rec_size, NULL, 0);
    }

    st = ntfsimg_cluster_alloc(ca, needed_clusters, &rl, &rl_count);
    if (!NT_SUCCESS(st)) return st;

    mp_size = NtfslxGetSizeForMappingPairs(rl, rl_count, 0, -1);
    if (mp_size <= 0) { free(rl); return STATUS_INSUFFICIENT_RESOURCES; }

    mp_offset = non_res_header_size;
    attr_total = (uint32_t)ALIGN8(mp_offset + (uint32_t)mp_size);
    if (end_off + attr_total + sizeof(ULONG) > rec_size)
    {
        free(rl);
        return STATUS_BUFFER_OVERFLOW;
    }

    attr = (PNTFSLX_ATTR_RECORD)(buf + end_off);
    memset(attr, 0, attr_total);
    attr->Type = NTFSLX_ATTRIBUTE_DATA;
    attr->Length = attr_total;
    attr->NonResident = 1;
    attr->NameLength = 0;
    attr->NameOffset = 0;
    attr->Flags = 0;
    attr->Instance = rec->NextAttributeInstance++;
    attr->Data.NonResident.LowestVcn = 0;
    attr->Data.NonResident.HighestVcn = (LONGLONG)(needed_clusters - 1);
    attr->Data.NonResident.MappingPairsOffset = (USHORT)mp_offset;
    attr->Data.NonResident.CompressionUnit = 0;
    attr->Data.NonResident.AllocatedSize = needed_clusters * bpc;
    attr->Data.NonResident.DataSize = host_size;
    attr->Data.NonResident.InitializedSize = host_size;
    attr->Data.NonResident.CompressedSize = 0;

    st = NtfslxMappingPairsBuild(
        (PUCHAR)attr + mp_offset,
        (LONG)(attr_total - mp_offset),
        rl, rl_count, 0, -1, NULL);
    if (!NT_SUCCESS(st)) { free(rl); return st; }

    {
        PNTFSLX_ATTR_RECORD new_end = (PNTFSLX_ATTR_RECORD)((UCHAR*)attr + attr_total);
        new_end->Type = NTFSLX_ATTRIBUTE_END;
        new_end->Length = 0;
    }
    rec->BytesInUse = end_off + attr_total + (uint32_t)sizeof(ULONG);

    fp = fopen(host_path, "rb");
    if (!fp) { free(rl); return STATUS_NO_SUCH_FILE; }

    chunk = malloc(bpc);
    if (!chunk) { fclose(fp); free(rl); return STATUS_INSUFFICIENT_RESOURCES; }

    bytes_left = host_size;
    for (i = 0; i < rl_count; i++)
    {
        uint64_t run_bytes = (uint64_t)rl[i].Length * bpc;
        uint64_t run_offset = 0;
        while (run_offset < run_bytes && bytes_left > 0)
        {
            uint64_t want = bpc;
            if (want > bytes_left) want = bytes_left;
            size_t got = fread(chunk, 1, (size_t)want, fp);
            if ((uint64_t)got != want) { free(chunk); fclose(fp); free(rl); return STATUS_UNSUCCESSFUL; }
            if (want < bpc) memset(chunk + want, 0, bpc - want);
            st = ntfsimg_io_write(&vol->io,
                                  (uint64_t)rl[i].Lcn * bpc + run_offset,
                                  (want == bpc) ? bpc : (uint32_t)want,
                                  chunk);
            if (!NT_SUCCESS(st)) { free(chunk); fclose(fp); free(rl); return st; }
            run_offset += bpc;
            bytes_left -= want;
        }
        cur_vcn += (uint64_t)rl[i].Length;
    }

    free(chunk);
    fclose(fp);
    free(rl);
    return STATUS_SUCCESS;
}
