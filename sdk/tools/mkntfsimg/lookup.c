#include "mkntfsimg.h"

#define INDX_HEADER_OFFSET 24u

#pragma pack(push, 1)
typedef struct _LK_ENTRY
{
    ULONGLONG IndexedFile;
    USHORT Length;
    USHORT KeyLength;
    USHORT Flags;
    USHORT Reserved;
} LK_ENTRY;
#pragma pack(pop)

static WCHAR lk_fold(WCHAR c) { return (c >= 'a' && c <= 'z') ? (WCHAR)(c - 'a' + 'A') : c; }

static int
lk_cmp(const WCHAR *a, uint32_t alen, const WCHAR *b, uint32_t blen)
{
    uint32_t n = alen < blen ? alen : blen;
    uint32_t i;
    for (i = 0; i < n; i++)
    {
        WCHAR ca = lk_fold(a[i]);
        WCHAR cb = lk_fold(b[i]);
        if (ca < cb) return -1;
        if (ca > cb) return 1;
    }
    if (alen < blen) return -1;
    if (alen > blen) return 1;
    return 0;
}

static NTSTATUS
lk_decode_rl(PNTFSLX_ATTR_RECORD attr,
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

static int
scan_entries(const UCHAR *entries_base, uint32_t entries_end,
             uint32_t start_off,
             const WCHAR *name, uint32_t name_len,
             uint64_t *out_mft, uint64_t *out_child_vcn, int *out_descend)
{
    uint32_t off = start_off;
    *out_descend = 0;
    while (off + sizeof(LK_ENTRY) <= entries_end)
    {
        LK_ENTRY *eh = (LK_ENTRY*)(entries_base + off);
        if (eh->Length < sizeof(LK_ENTRY)) return 0;
        if (off + eh->Length > entries_end) return 0;

        if ((eh->Flags & NTFSLX_INDEX_ENTRY_END) == 0)
        {
            PNTFSLX_FILE_NAME_ATTRIBUTE fn = (PNTFSLX_FILE_NAME_ATTRIBUTE)
                (entries_base + off + sizeof(LK_ENTRY));
            const WCHAR *ename = (const WCHAR*)((UCHAR*)fn + sizeof(NTFSLX_FILE_NAME_ATTRIBUTE));
            int c = lk_cmp(name, name_len, ename, fn->FileNameLength);
            if (c == 0)
            {
                *out_mft = MREF(eh->IndexedFile);
                return 1;
            }
            if (c < 0)
            {
                if (eh->Flags & NTFSLX_INDEX_ENTRY_NODE)
                {
                    *out_child_vcn = *(ULONGLONG*)(entries_base + off + eh->Length - 8);
                    *out_descend = 1;
                }
                return 0;
            }
        }
        else
        {
            if (eh->Flags & NTFSLX_INDEX_ENTRY_NODE)
            {
                *out_child_vcn = *(ULONGLONG*)(entries_base + off + eh->Length - 8);
                *out_descend = 1;
            }
            return 0;
        }
        off += eh->Length;
    }
    return 0;
}

NTSTATUS
ntfsimg_lookup_child(NTFSIMG_VOLUME *vol, uint64_t parent_mft_index,
                     const WCHAR *name, uint32_t name_len,
                     uint64_t *out_mft_index)
{
    static const WCHAR I30[] = { '$','I','3','0' };
    uint32_t rec_size = vol->bytes_per_mft_record;
    NTFSLX_MFT_RECORD *rec = malloc(rec_size);
    PNTFSLX_ATTR_RECORD ir_attr;
    PNTFSLX_ATTR_RECORD alloc_attr;
    NTFSLX_INDEX_ROOT *ir;
    NTFSLX_RUNLIST_ELEMENT *alloc_rl = NULL;
    uint32_t alloc_rl_count = 0;
    uint32_t block_size;
    NTSTATUS st;

    if (!rec) return STATUS_INSUFFICIENT_RESOURCES;
    st = ntfsimg_read_mft_record(vol, parent_mft_index, rec);
    if (!NT_SUCCESS(st)) { free(rec); return st; }

    ir_attr = ntfsimg_find_attr_named(rec, rec_size, NTFSLX_ATTRIBUTE_INDEX_ROOT, I30, 4);
    if (!ir_attr || ir_attr->NonResident) { free(rec); return STATUS_NOT_FOUND; }
    ir = (NTFSLX_INDEX_ROOT*)((UCHAR*)ir_attr + ir_attr->Data.Resident.ValueOffset);

    {
        int found, descend;
        uint64_t child_vcn = 0;
        found = scan_entries((UCHAR*)&ir->Index, ir->Index.IndexLength,
                             ir->Index.EntriesOffset,
                             name, name_len, out_mft_index, &child_vcn, &descend);
        if (found) { free(rec); return STATUS_SUCCESS; }
        if (!descend || (ir->Index.Flags & NTFSLX_LARGE_INDEX) == 0)
        { free(rec); return STATUS_NOT_FOUND; }

        alloc_attr = ntfsimg_find_attr_named(rec, rec_size,
                                             NTFSLX_ATTRIBUTE_INDEX_ALLOCATION, I30, 4);
        if (!alloc_attr || !alloc_attr->NonResident)
        { free(rec); return STATUS_FILE_CORRUPT_ERROR; }

        st = lk_decode_rl(alloc_attr, &alloc_rl, &alloc_rl_count);
        if (!NT_SUCCESS(st)) { free(rec); return st; }

        block_size = ir->IndexBlockSize;
        if (block_size == 0) block_size = vol->bytes_per_index_record;

        while (descend)
        {
            NTFSIMG_INDX indx;
            PNTFSLX_INDEX_HEADER ih;
            uint64_t next_vcn = 0;
            st = ntfsimg_indx_read(vol, alloc_rl, alloc_rl_count,
                                   block_size, child_vcn, &indx);
            if (!NT_SUCCESS(st)) { free(alloc_rl); free(rec); return st; }
            ih = (PNTFSLX_INDEX_HEADER)(indx.bytes + INDX_HEADER_OFFSET);
            found = scan_entries((UCHAR*)ih, ih->IndexLength,
                                 ih->EntriesOffset,
                                 name, name_len, out_mft_index,
                                 &next_vcn, &descend);
            ntfsimg_indx_free(&indx);
            if (found) { free(alloc_rl); free(rec); return STATUS_SUCCESS; }
            if (!descend) { free(alloc_rl); free(rec); return STATUS_NOT_FOUND; }
            child_vcn = next_vcn;
        }
    }

    free(alloc_rl);
    free(rec);
    return STATUS_NOT_FOUND;
}
