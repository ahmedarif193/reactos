#include "mkntfsimg.h"

#define ALIGN8(x) (((x) + 7u) & ~7u)

#pragma pack(push, 1)
typedef struct _ENTRY_HEAD
{
    ULONGLONG IndexedFile;
    USHORT Length;
    USHORT KeyLength;
    USHORT Flags;
    USHORT Reserved;
} ENTRY_HEAD;
#pragma pack(pop)

static WCHAR fold(WCHAR c) { return (c >= 'a' && c <= 'z') ? (WCHAR)(c - 'a' + 'A') : c; }

static int
cmp_fname_dir(const WCHAR *a, uint32_t alen, const WCHAR *b, uint32_t blen)
{
    uint32_t n = alen < blen ? alen : blen;
    uint32_t i;
    for (i = 0; i < n; i++)
    {
        WCHAR ca = fold(a[i]);
        WCHAR cb = fold(b[i]);
        if (ca < cb) return -1;
        if (ca > cb) return 1;
    }
    if (alen < blen) return -1;
    if (alen > blen) return 1;
    return 0;
}

NTSTATUS
ntfsimg_index_resident_insert(NTFSIMG_VOLUME *vol, uint64_t dir_mft_index,
                              const UCHAR *new_entry, uint32_t new_entry_len,
                              const WCHAR *key_name, uint32_t key_name_len)
{
    static const WCHAR I30[] = { '$','I','3','0' };
    uint32_t rec_size = vol->bytes_per_mft_record;
    NTFSLX_MFT_RECORD *rec = malloc(rec_size);
    PNTFSLX_ATTR_RECORD attr;
    NTFSLX_INDEX_ROOT *ir;
    UCHAR *ientries_base;
    uint32_t ientries_end;
    uint32_t off;
    uint32_t inserted = 0;
    UCHAR *rebuilt = NULL;
    uint32_t rebuilt_len = 0;
    uint32_t rebuilt_cap = 0;
    NTSTATUS st;
    uint32_t attr_header_size;
    uint32_t new_attr_total;
    uint32_t old_attr_total;
    uint32_t fixed_prefix;
    uint32_t new_ir_value_size;

    if (!rec) return STATUS_INSUFFICIENT_RESOURCES;
    st = ntfsimg_read_mft_record(vol, dir_mft_index, rec);
    if (!NT_SUCCESS(st)) { free(rec); return st; }

    attr = ntfsimg_find_attr_named(rec, rec_size, NTFSLX_ATTRIBUTE_INDEX_ROOT, I30, 4);
    if (!attr || attr->NonResident) { free(rec); return STATUS_NOT_FOUND; }
    ir = (NTFSLX_INDEX_ROOT*)((UCHAR*)attr + attr->Data.Resident.ValueOffset);
    if (ir->Index.Flags & NTFSLX_LARGE_INDEX) { free(rec); return STATUS_NOT_IMPLEMENTED; }

    ientries_base = (UCHAR*)&ir->Index;
    ientries_end = ir->Index.IndexLength;

    rebuilt_cap = ientries_end + new_entry_len + 64;
    rebuilt = malloc(rebuilt_cap);
    if (!rebuilt) { free(rec); return STATUS_INSUFFICIENT_RESOURCES; }

    off = ir->Index.EntriesOffset;
    while (off + sizeof(ENTRY_HEAD) <= ientries_end)
    {
        ENTRY_HEAD *eh = (ENTRY_HEAD*)(ientries_base + off);
        if (eh->Length == 0 || eh->Length < sizeof(ENTRY_HEAD)) break;
        if (off + eh->Length > ientries_end) break;

        if (!inserted && (eh->Flags & NTFSLX_INDEX_ENTRY_END) == 0)
        {
            PNTFSLX_FILE_NAME_ATTRIBUTE efn = (PNTFSLX_FILE_NAME_ATTRIBUTE)
                (ientries_base + off + sizeof(ENTRY_HEAD));
            const WCHAR *ename = (const WCHAR*)((UCHAR*)efn + sizeof(NTFSLX_FILE_NAME_ATTRIBUTE));
            int c = cmp_fname_dir(key_name, key_name_len, ename, efn->FileNameLength);
            if (c == 0) { free(rebuilt); free(rec); return STATUS_OBJECT_NAME_COLLISION; }
            if (c < 0)
            {
                memcpy(rebuilt + rebuilt_len, new_entry, new_entry_len);
                rebuilt_len += new_entry_len;
                inserted = 1;
            }
        }

        memcpy(rebuilt + rebuilt_len, eh, eh->Length);
        rebuilt_len += eh->Length;

        if (eh->Flags & NTFSLX_INDEX_ENTRY_END) break;
        off += eh->Length;
    }

    if (!inserted)
    {
        uint32_t last_len;
        if (rebuilt_len < sizeof(ENTRY_HEAD)) { free(rebuilt); free(rec); return STATUS_FILE_CORRUPT_ERROR; }
        last_len = ((ENTRY_HEAD*)(rebuilt + rebuilt_len - ((ENTRY_HEAD*)(rebuilt + rebuilt_len - sizeof(ENTRY_HEAD)))->Length))->Length;
        (void)last_len;
        {
            ENTRY_HEAD *end_in_rebuilt = NULL;
            uint32_t scan = ir->Index.EntriesOffset > 0 ? 0 : 0;
            uint32_t cur = 0;
            while (cur < rebuilt_len)
            {
                ENTRY_HEAD *e = (ENTRY_HEAD*)(rebuilt + cur);
                if (e->Flags & NTFSLX_INDEX_ENTRY_END) { end_in_rebuilt = e; break; }
                if (e->Length == 0) break;
                cur += e->Length;
            }
            if (!end_in_rebuilt) { free(rebuilt); free(rec); return STATUS_FILE_CORRUPT_ERROR; }
            {
                uint32_t end_off = (uint32_t)((UCHAR*)end_in_rebuilt - rebuilt);
                memmove(rebuilt + end_off + new_entry_len, rebuilt + end_off,
                        rebuilt_len - end_off);
                memcpy(rebuilt + end_off, new_entry, new_entry_len);
                rebuilt_len += new_entry_len;
            }
            (void)scan;
        }
    }

    attr_header_size = attr->Data.Resident.ValueOffset;
    fixed_prefix = (uint32_t)(((UCHAR*)&ir->Index) - (UCHAR*)ir) + ir->Index.EntriesOffset;
    new_ir_value_size = (uint32_t)ALIGN8(fixed_prefix + rebuilt_len);
    new_attr_total = (uint32_t)ALIGN8(attr_header_size + new_ir_value_size);
    old_attr_total = attr->Length;

    if (new_attr_total > old_attr_total)
    {
        uint32_t grow = new_attr_total - old_attr_total;
        uint32_t after_off = (uint32_t)((UCHAR*)attr - (UCHAR*)rec) + old_attr_total;
        uint32_t trailing = rec->BytesInUse - after_off;
        if (rec->BytesInUse + grow > rec_size)
        {
            free(rebuilt); free(rec);
            return STATUS_BUFFER_OVERFLOW;
        }
        if (trailing > 0)
            memmove((UCHAR*)attr + new_attr_total, (UCHAR*)attr + old_attr_total, trailing);
        rec->BytesInUse += grow;
    }

    attr->Length = new_attr_total;
    attr->Data.Resident.ValueLength = new_ir_value_size;

    ir = (NTFSLX_INDEX_ROOT*)((UCHAR*)attr + attr->Data.Resident.ValueOffset);
    ir->Index.IndexLength = ir->Index.EntriesOffset + rebuilt_len;
    ir->Index.AllocatedSize = ir->Index.IndexLength;
    memcpy((UCHAR*)&ir->Index + ir->Index.EntriesOffset, rebuilt, rebuilt_len);

    st = ntfsimg_write_mft_record(vol, dir_mft_index, rec);
    free(rebuilt);
    free(rec);
    return st;
}

NTSTATUS
ntfsimg_dir_insert(NTFSIMG_VOLUME *vol, uint64_t dir_mft_index,
                   const UCHAR *new_entry, uint32_t new_entry_len,
                   const WCHAR *key_name, uint32_t key_name_len)
{
    NTSTATUS st = ntfsimg_index_resident_insert(vol, dir_mft_index, new_entry,
                                                new_entry_len, key_name, key_name_len);
    if (st == STATUS_NOT_IMPLEMENTED)
    {
        return ntfsimg_index_alloc_insert(vol, dir_mft_index, new_entry,
                                          new_entry_len, key_name, key_name_len);
    }
    return st;
}

NTSTATUS
ntfsimg_dir_insert_ex(NTFSIMG_VOLUME *vol,
                     struct _NTFSIMG_CLUSTER_ALLOCATOR *ca,
                     uint64_t dir_mft_index,
                     const UCHAR *new_entry, uint32_t new_entry_len,
                     const WCHAR *key_name, uint32_t key_name_len)
{
    NTSTATUS st = ntfsimg_dir_insert(vol, dir_mft_index, new_entry,
                                     new_entry_len, key_name, key_name_len);
    if (st == STATUS_BUFFER_OVERFLOW)
    {
        st = ntfsimg_spill_root_to_alloc(vol, ca, dir_mft_index);
        if (!NT_SUCCESS(st)) return st;
        st = ntfsimg_index_alloc_insert(vol, dir_mft_index, new_entry,
                                        new_entry_len, key_name, key_name_len);
    }

    (void)ntfsimg_index_alloc_extend;
    return st;
}
