#include "mkntfsimg.h"

#define ALIGN8(x) (((x) + 7u) & ~7u)
#define INDX_HEADER_OFFSET 24u

static const WCHAR SP_I30[] = { '$','I','3','0' };

#pragma pack(push, 1)
typedef struct _SP_ENTRY
{
    ULONGLONG IndexedFile;
    USHORT Length;
    USHORT KeyLength;
    USHORT Flags;
    USHORT Reserved;
} SP_ENTRY;
#pragma pack(pop)

static PNTFSLX_ATTR_RECORD
find_named(NTFSLX_MFT_RECORD *rec, uint32_t rec_size, uint32_t type,
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

static uint32_t
find_end_off(NTFSLX_MFT_RECORD *rec, uint32_t rec_size)
{
    uint8_t *base = (uint8_t*)rec;
    uint32_t offset = rec->AttributesOffset;
    while (offset + sizeof(ULONG) <= rec_size)
    {
        PNTFSLX_ATTR_RECORD a = (PNTFSLX_ATTR_RECORD)(base + offset);
        if (a->Type == NTFSLX_ATTRIBUTE_END) return offset;
        if (offset + sizeof(NTFSLX_ATTR_RECORD) > rec_size) return 0;
        if (a->Length == 0) return 0;
        offset += a->Length;
    }
    return 0;
}

NTSTATUS
ntfsimg_spill_root_to_alloc(NTFSIMG_VOLUME *vol,
                            struct _NTFSIMG_CLUSTER_ALLOCATOR *ca,
                            uint64_t dir_mft_index)
{
    uint32_t rec_size = vol->bytes_per_mft_record;
    NTFSLX_MFT_RECORD *rec = malloc(rec_size);
    PNTFSLX_ATTR_RECORD ir_attr;
    uint32_t ir_off;
    NTFSLX_INDEX_ROOT *ir;
    UCHAR *ient_base;
    uint32_t ient_end;
    uint32_t off;
    UCHAR *entries_buf = NULL;
    uint32_t entries_len = 0;
    uint32_t block_size;
    uint32_t clusters_per_block;
    UCHAR *indx = NULL;
    PNTFSLX_INDEX_HEADER indx_ih;
    PNTFSLX_INDEX_BLOCK indx_hdr;
    uint32_t usa_count;
    NTFSLX_RUNLIST_ELEMENT *alloc_rl = NULL;
    uint32_t alloc_rl_count = 0;
    LONG mp_size;
    NTSTATUS st;
    uint32_t new_root_value_size;
    uint32_t new_root_attr_total;
    uint32_t alloc_attr_total;
    uint32_t bm_attr_total;
    uint32_t insertion_off;
    int32_t size_delta;
    uint32_t hdr_size = 24;
    uint32_t i30_bytes = 4 * (uint32_t)sizeof(WCHAR);

    if (!rec) return STATUS_INSUFFICIENT_RESOURCES;

    st = ntfsimg_read_mft_record(vol, dir_mft_index, rec);
    if (!NT_SUCCESS(st)) { free(rec); return st; }

    ir_attr = find_named(rec, rec_size, NTFSLX_ATTRIBUTE_INDEX_ROOT, SP_I30, 4, &ir_off);
    if (!ir_attr || ir_attr->NonResident) { free(rec); return STATUS_NOT_FOUND; }
    ir = (NTFSLX_INDEX_ROOT*)((UCHAR*)ir_attr + ir_attr->Data.Resident.ValueOffset);
    if (ir->Index.Flags & NTFSLX_LARGE_INDEX) { free(rec); return STATUS_SUCCESS; }

    block_size = ir->IndexBlockSize;
    if (block_size == 0) block_size = vol->bytes_per_index_record;
    clusters_per_block = block_size / vol->bytes_per_cluster;
    if (clusters_per_block == 0) clusters_per_block = 1;

    ient_base = (UCHAR*)&ir->Index;
    ient_end = ir->Index.IndexLength;
    entries_buf = malloc(ient_end);
    if (!entries_buf) { free(rec); return STATUS_INSUFFICIENT_RESOURCES; }

    off = ir->Index.EntriesOffset;
    while (off + sizeof(SP_ENTRY) <= ient_end)
    {
        SP_ENTRY *eh = (SP_ENTRY*)(ient_base + off);
        if (eh->Length == 0 || eh->Length < sizeof(SP_ENTRY)) break;
        if (off + eh->Length > ient_end) break;
        if ((eh->Flags & NTFSLX_INDEX_ENTRY_END) == 0)
        {
            memcpy(entries_buf + entries_len, eh, eh->Length);
            entries_len += eh->Length;
        }
        if (eh->Flags & NTFSLX_INDEX_ENTRY_END) break;
        off += eh->Length;
    }

    indx = calloc(1, block_size);
    if (!indx) { free(entries_buf); free(rec); return STATUS_INSUFFICIENT_RESOURCES; }

    usa_count = block_size / 512 + 1;
    indx_hdr = (PNTFSLX_INDEX_BLOCK)indx;
    indx_hdr->Magic = NTFSLX_RECORD_MAGIC_INDX;
    indx_hdr->UsaOffset = (USHORT)sizeof(NTFSLX_INDEX_BLOCK);
    indx_hdr->UsaCount = (USHORT)usa_count;
    indx_hdr->Lsn = 0;
    indx_hdr->IndexBlockVcn = 0;

    indx_ih = (PNTFSLX_INDEX_HEADER)(indx + INDX_HEADER_OFFSET);
    indx_ih->EntriesOffset = (uint32_t)ALIGN8(sizeof(NTFSLX_INDEX_HEADER) +
                                              (uint32_t)(usa_count * 2));
    if (indx_ih->EntriesOffset < 40) indx_ih->EntriesOffset = 40;
    indx_ih->Flags = 0;

    {
        uint32_t cap = block_size - INDX_HEADER_OFFSET;
        uint32_t entries_area = cap - indx_ih->EntriesOffset;
        if (entries_len + 16 > entries_area)
        {
            free(indx); free(entries_buf); free(rec);
            return STATUS_BUFFER_OVERFLOW;
        }
        memcpy((UCHAR*)indx_ih + indx_ih->EntriesOffset, entries_buf, entries_len);
        {
            SP_ENTRY *term = (SP_ENTRY*)((UCHAR*)indx_ih + indx_ih->EntriesOffset + entries_len);
            term->IndexedFile = 0;
            term->Length = 16;
            term->KeyLength = 0;
            term->Flags = NTFSLX_INDEX_ENTRY_END;
            term->Reserved = 0;
        }
        indx_ih->IndexLength = indx_ih->EntriesOffset + entries_len + 16;
        indx_ih->AllocatedSize = (uint32_t)ALIGN8(cap);
    }
    free(entries_buf);

    st = ntfsimg_cluster_alloc(ca, clusters_per_block, &alloc_rl, &alloc_rl_count);
    if (!NT_SUCCESS(st)) { free(indx); free(rec); return st; }

    {
        NTFSIMG_INDX blk;
        blk.bytes = indx;
        blk.block_size = block_size;
        blk.vcn = 0;
        blk.dirty = 1;
        st = ntfsimg_indx_write(vol, alloc_rl, alloc_rl_count, &blk);
    }
    free(indx);
    if (!NT_SUCCESS(st)) { free(alloc_rl); free(rec); return st; }

    mp_size = NtfslxGetSizeForMappingPairs(alloc_rl, alloc_rl_count, 0, -1);
    if (mp_size <= 0) { free(alloc_rl); free(rec); return STATUS_INSUFFICIENT_RESOURCES; }

    new_root_value_size = (uint32_t)ALIGN8(sizeof(NTFSLX_INDEX_ROOT) + 16 + 8);
    new_root_attr_total = (uint32_t)ALIGN8(hdr_size + i30_bytes + new_root_value_size);

    alloc_attr_total = (uint32_t)ALIGN8(64 + i30_bytes + (uint32_t)mp_size);
    bm_attr_total = (uint32_t)ALIGN8(hdr_size + i30_bytes + 8);

    size_delta = (int32_t)new_root_attr_total - (int32_t)ir_attr->Length +
                 (int32_t)alloc_attr_total + (int32_t)bm_attr_total;

    {
        uint32_t end_off = find_end_off(rec, rec_size);
        if (end_off == 0 ||
            (int32_t)rec->BytesInUse + size_delta + (int32_t)sizeof(ULONG) > (int32_t)rec_size)
        {
            free(alloc_rl); free(rec);
            return STATUS_BUFFER_OVERFLOW;
        }

        {
            uint32_t after_ir = ir_off + ir_attr->Length;
            uint32_t trailing = rec->BytesInUse - after_ir;
            int32_t ir_delta = (int32_t)new_root_attr_total - (int32_t)ir_attr->Length;
            if (ir_delta != 0 && trailing > 0)
                memmove((UCHAR*)rec + after_ir + ir_delta, (UCHAR*)rec + after_ir, trailing);
            rec->BytesInUse = (uint32_t)((int32_t)rec->BytesInUse + ir_delta);
            ir_attr = (PNTFSLX_ATTR_RECORD)((UCHAR*)rec + ir_off);
            ir_attr->Length = new_root_attr_total;
            ir_attr->Data.Resident.ValueLength = new_root_value_size;
            ir = (NTFSLX_INDEX_ROOT*)((UCHAR*)ir_attr + ir_attr->Data.Resident.ValueOffset);
        }

        ir->Type = NTFSLX_ATTRIBUTE_FILE_NAME;
        ir->CollationRule = NTFSLX_COLLATION_FILE_NAME;
        ir->IndexBlockSize = block_size;
        ir->ClustersPerIndexBlock = (CHAR)clusters_per_block;
        ir->Index.EntriesOffset = 16;
        ir->Index.IndexLength = 16 + 24;
        ir->Index.AllocatedSize = 16 + 24;
        ir->Index.Flags = NTFSLX_LARGE_INDEX;
        {
            SP_ENTRY *term = (SP_ENTRY*)((UCHAR*)&ir->Index + 16);
            term->IndexedFile = 0;
            term->Length = 24;
            term->KeyLength = 0;
            term->Flags = NTFSLX_INDEX_ENTRY_END | NTFSLX_INDEX_ENTRY_NODE;
            term->Reserved = 0;
            *(ULONGLONG*)((UCHAR*)term + 16) = 0;
        }
    }

    {
        uint32_t insert_off = ir_off + new_root_attr_total;
        PNTFSLX_ATTR_RECORD alloc_attr;
        insertion_off = insert_off;

        memmove((UCHAR*)rec + insert_off + alloc_attr_total + bm_attr_total,
                (UCHAR*)rec + insert_off,
                rec->BytesInUse - insert_off);

        alloc_attr = (PNTFSLX_ATTR_RECORD)((UCHAR*)rec + insert_off);
        memset(alloc_attr, 0, alloc_attr_total);
        alloc_attr->Type = NTFSLX_ATTRIBUTE_INDEX_ALLOCATION;
        alloc_attr->Length = alloc_attr_total;
        alloc_attr->NonResident = 1;
        alloc_attr->NameLength = 4;
        alloc_attr->NameOffset = 64;
        alloc_attr->Instance = rec->NextAttributeInstance++;
        memcpy((UCHAR*)alloc_attr + 64, SP_I30, i30_bytes);
        alloc_attr->Data.NonResident.LowestVcn = 0;
        alloc_attr->Data.NonResident.HighestVcn = clusters_per_block - 1;
        alloc_attr->Data.NonResident.MappingPairsOffset = (USHORT)(64 + i30_bytes);
        alloc_attr->Data.NonResident.AllocatedSize = block_size;
        alloc_attr->Data.NonResident.DataSize = block_size;
        alloc_attr->Data.NonResident.InitializedSize = block_size;
        st = NtfslxMappingPairsBuild(
            (UCHAR*)alloc_attr + alloc_attr->Data.NonResident.MappingPairsOffset,
            (LONG)(alloc_attr_total - alloc_attr->Data.NonResident.MappingPairsOffset),
            alloc_rl, alloc_rl_count, 0, -1, NULL);
        if (!NT_SUCCESS(st)) { free(alloc_rl); free(rec); return st; }
        rec->BytesInUse += alloc_attr_total;
    }

    {
        uint32_t bm_off = insertion_off + alloc_attr_total;
        PNTFSLX_ATTR_RECORD bm_attr = (PNTFSLX_ATTR_RECORD)((UCHAR*)rec + bm_off);
        memset(bm_attr, 0, bm_attr_total);
        bm_attr->Type = NTFSLX_ATTRIBUTE_BITMAP;
        bm_attr->Length = bm_attr_total;
        bm_attr->NonResident = 0;
        bm_attr->NameLength = 4;
        bm_attr->NameOffset = hdr_size;
        bm_attr->Instance = rec->NextAttributeInstance++;
        memcpy((UCHAR*)bm_attr + hdr_size, SP_I30, i30_bytes);
        bm_attr->Data.Resident.ValueLength = 8;
        bm_attr->Data.Resident.ValueOffset = (USHORT)(hdr_size + i30_bytes);
        *(UCHAR*)((UCHAR*)bm_attr + bm_attr->Data.Resident.ValueOffset) = 0x01;
        rec->BytesInUse += bm_attr_total;
    }

    free(alloc_rl);

    st = ntfsimg_write_mft_record(vol, dir_mft_index, rec);
    free(rec);
    return st;
}
