#include "mkntfsimg.h"

#define ALIGN8(x) (((x) + 7u) & ~7u)

#pragma pack(push, 1)
typedef struct _I30_ENTRY_HEAD
{
    ULONGLONG IndexedFile;
    USHORT Length;
    USHORT KeyLength;
    USHORT Flags;
    USHORT Reserved;
} I30_ENTRY_HEAD;
#pragma pack(pop)

static WCHAR
ucwchar(WCHAR c)
{
    if (c >= 'a' && c <= 'z') return (WCHAR)(c - 'a' + 'A');
    return c;
}

static int
cmp_fname(const WCHAR *a, uint32_t alen, const WCHAR *b, uint32_t blen)
{
    uint32_t n = alen < blen ? alen : blen;
    uint32_t i;
    for (i = 0; i < n; i++)
    {
        WCHAR ca = ucwchar(a[i]);
        WCHAR cb = ucwchar(b[i]);
        if (ca < cb) return -1;
        if (ca > cb) return 1;
    }
    if (alen < blen) return -1;
    if (alen > blen) return 1;
    for (i = 0; i < n; i++)
    {
        if (a[i] < b[i]) return -1;
        if (a[i] > b[i]) return 1;
    }
    return 0;
}

typedef struct _STAGED_ENTRY
{
    UCHAR *bytes;
    uint32_t length;
} STAGED_ENTRY;

NTSTATUS
ntfsimg_root_add_entry(NTFSIMG_VOLUME *vol, uint64_t file_mft_ref,
                       const WCHAR *name, uint32_t name_len, UCHAR name_type,
                       uint64_t ntfs_time, uint32_t file_attributes,
                       uint64_t data_size)
{
    NTFSLX_MFT_RECORD *root = NULL;
    PNTFSLX_ATTR_RECORD attr;
    NTFSLX_INDEX_ROOT *ir;
    UCHAR *ientries_base;
    uint32_t ientries_end;
    static const WCHAR I30_NAME[] = { '$','I','3','0' };
    STAGED_ENTRY *staged = NULL;
    uint32_t staged_count = 0, staged_cap = 32;
    uint32_t new_entry_len;
    uint32_t name_bytes;
    uint32_t fn_size;
    UCHAR *new_entry_buf = NULL;
    I30_ENTRY_HEAD *new_head;
    PNTFSLX_FILE_NAME_ATTRIBUTE new_fn;
    uint32_t off;
    uint32_t total_len;
    uint32_t old_entries_bytes;
    uint32_t ir_alloc;
    uint32_t attr_header_size;
    int inserted = 0;
    uint32_t new_ir_entries_bytes;
    uint32_t new_ir_value_size;
    uint32_t new_attr_total;
    uint32_t rec_size = vol->bytes_per_mft_record;
    NTSTATUS st;
    uint32_t i;

    name_bytes = name_len * (uint32_t)sizeof(WCHAR);
    fn_size = (uint32_t)sizeof(NTFSLX_FILE_NAME_ATTRIBUTE) + name_bytes;
    new_entry_len = (uint32_t)ALIGN8(sizeof(I30_ENTRY_HEAD) + fn_size);

    new_entry_buf = calloc(1, new_entry_len);
    if (!new_entry_buf) return STATUS_INSUFFICIENT_RESOURCES;

    new_head = (I30_ENTRY_HEAD*)new_entry_buf;
    new_head->IndexedFile = file_mft_ref;
    new_head->Length = (USHORT)new_entry_len;
    new_head->KeyLength = (USHORT)fn_size;
    new_head->Flags = 0;
    new_head->Reserved = 0;

    new_fn = (PNTFSLX_FILE_NAME_ATTRIBUTE)(new_entry_buf + sizeof(I30_ENTRY_HEAD));
    new_fn->ParentDirectory = MK_MREF(NTFSLX_FILE_ROOT, 5);
    new_fn->CreationTime = ntfs_time;
    new_fn->LastDataChangeTime = ntfs_time;
    new_fn->LastMftChangeTime = ntfs_time;
    new_fn->LastAccessTime = ntfs_time;
    new_fn->AllocatedSize = (data_size + 7) & ~7ULL;
    new_fn->DataSize = data_size;
    new_fn->FileAttributes = file_attributes;
    new_fn->FileNameLength = (UCHAR)name_len;
    new_fn->FileNameType = name_type;
    memcpy((UCHAR*)new_fn + sizeof(NTFSLX_FILE_NAME_ATTRIBUTE), name, name_bytes);

    root = malloc(rec_size);
    if (!root) { free(new_entry_buf); return STATUS_INSUFFICIENT_RESOURCES; }

    st = ntfsimg_read_mft_record(vol, NTFSLX_FILE_ROOT, root);
    if (!NT_SUCCESS(st)) { free(new_entry_buf); free(root); return st; }

    attr = ntfsimg_find_attr_named(root, rec_size, NTFSLX_ATTRIBUTE_INDEX_ROOT,
                                   I30_NAME, 4);
    if (!attr || attr->NonResident)
    {
        free(new_entry_buf); free(root);
        return STATUS_NOT_FOUND;
    }

    ir = (NTFSLX_INDEX_ROOT*)((UCHAR*)attr + attr->Data.Resident.ValueOffset);
    ientries_base = (UCHAR*)&ir->Index;
    ientries_end = ir->Index.IndexLength;
    old_entries_bytes = ientries_end;

    if (ir->Index.Flags & 0x01)
    {
        free(new_entry_buf); free(root);
        fprintf(stderr, "mkntfsimg: root $I30 is LARGE_INDEX (already spilled to $INDEX_ALLOCATION); B3 work\n");
        return STATUS_NOT_IMPLEMENTED;
    }

    staged = calloc(staged_cap, sizeof(*staged));
    if (!staged) { free(new_entry_buf); free(root); return STATUS_INSUFFICIENT_RESOURCES; }

    if (g_mkntfsimg_verbose)
    {
        fprintf(stderr, "root_add_entry: IR type=0x%x coll=%u EntriesOffset=%u IndexLength=%u AllocatedSize=%u Flags=0x%x\n",
                ir->Type, ir->CollationRule, ir->Index.EntriesOffset,
                ir->Index.IndexLength, ir->Index.AllocatedSize, ir->Index.Flags);
        fprintf(stderr, "root_add_entry: attr Length=%u Value(off=%u len=%u)\n",
                attr->Length, attr->Data.Resident.ValueOffset, attr->Data.Resident.ValueLength);
    }

    off = ir->Index.EntriesOffset;
    while (off + sizeof(I30_ENTRY_HEAD) <= ientries_end)
    {
        I30_ENTRY_HEAD *eh = (I30_ENTRY_HEAD*)(ientries_base + off);
        if (g_mkntfsimg_verbose)
        {
            PNTFSLX_FILE_NAME_ATTRIBUTE fnp = (PNTFSLX_FILE_NAME_ATTRIBUTE)
                (ientries_base + off + sizeof(I30_ENTRY_HEAD));
            fprintf(stderr, "  entry @%u len=%u key=%u flags=0x%x iref=0x%llx",
                    off, eh->Length, eh->KeyLength, eh->Flags,
                    (unsigned long long)eh->IndexedFile);
            if ((eh->Flags & NTFSLX_INDEX_ENTRY_END) == 0 && eh->KeyLength > 0)
            {
                uint32_t j;
                const WCHAR *nm = (const WCHAR*)((UCHAR*)fnp + sizeof(NTFSLX_FILE_NAME_ATTRIBUTE));
                fprintf(stderr, " name='");
                for (j = 0; j < fnp->FileNameLength && j < 32; j++)
                    fputc(nm[j] < 128 ? (char)nm[j] : '?', stderr);
                fprintf(stderr, "'");
            }
            fprintf(stderr, "\n");
        }
        if (eh->Length == 0) break;
        if (eh->Length < sizeof(I30_ENTRY_HEAD)) break;
        if (off + eh->Length > ientries_end) break;

        if ((eh->Flags & NTFSLX_INDEX_ENTRY_END) == 0 && !inserted)
        {
            PNTFSLX_FILE_NAME_ATTRIBUTE efn = (PNTFSLX_FILE_NAME_ATTRIBUTE)
                (ientries_base + off + sizeof(I30_ENTRY_HEAD));
            const WCHAR *ename = (const WCHAR*)((UCHAR*)efn + sizeof(NTFSLX_FILE_NAME_ATTRIBUTE));
            if (cmp_fname(name, name_len, ename, efn->FileNameLength) == 0)
            {
                free(new_entry_buf); free(root); free(staged);
                return STATUS_OBJECT_NAME_COLLISION;
            }
            if (cmp_fname(name, name_len, ename, efn->FileNameLength) < 0)
            {
                if (staged_count == staged_cap)
                {
                    STAGED_ENTRY *bigger;
                    staged_cap *= 2;
                    bigger = realloc(staged, staged_cap*sizeof(*staged));
                    if (!bigger) { free(new_entry_buf); free(root); free(staged); return STATUS_INSUFFICIENT_RESOURCES; }
                    staged = bigger;
                }
                staged[staged_count].bytes = new_entry_buf;
                staged[staged_count].length = new_entry_len;
                staged_count++;
                inserted = 1;
            }
        }

        if (staged_count == staged_cap)
        {
            STAGED_ENTRY *bigger;
            staged_cap *= 2;
            bigger = realloc(staged, staged_cap*sizeof(*staged));
            if (!bigger) { free(new_entry_buf); free(root); free(staged); return STATUS_INSUFFICIENT_RESOURCES; }
            staged = bigger;
        }
        staged[staged_count].bytes = ientries_base + off;
        staged[staged_count].length = eh->Length;
        staged_count++;

        if (eh->Flags & NTFSLX_INDEX_ENTRY_END) break;
        off += eh->Length;
    }

    if (!inserted)
    {
        if (staged_count == 0)
        {
            free(new_entry_buf); free(root); free(staged);
            return STATUS_FILE_CORRUPT_ERROR;
        }
        if (staged_count + 1 > staged_cap)
        {
            STAGED_ENTRY *bigger;
            staged_cap = staged_count + 8;
            bigger = realloc(staged, staged_cap*sizeof(*staged));
            if (!bigger) { free(new_entry_buf); free(root); free(staged); return STATUS_INSUFFICIENT_RESOURCES; }
            staged = bigger;
        }
        staged[staged_count] = staged[staged_count - 1];
        staged[staged_count - 1].bytes = new_entry_buf;
        staged[staged_count - 1].length = new_entry_len;
        staged_count++;
    }

    total_len = 0;
    for (i = 0; i < staged_count; i++)
        total_len += staged[i].length;

    attr_header_size = attr->Data.Resident.ValueOffset;
    ir_alloc = attr->Data.Resident.ValueLength;
    {
        uint32_t fixed = (uint32_t)(((UCHAR*)&ir->Index) - (UCHAR*)ir) + ir->Index.EntriesOffset;
        new_ir_entries_bytes = total_len;
        new_ir_value_size = (uint32_t)ALIGN8(fixed + new_ir_entries_bytes);
        new_attr_total = (uint32_t)ALIGN8(attr_header_size + new_ir_value_size);
    }

    if (new_attr_total > attr->Length + (rec_size - root->BytesInUse))
    {
        free(new_entry_buf); free(root); free(staged);
        fprintf(stderr, "mkntfsimg: root INDEX_ROOT spill not yet implemented (need B3)\n");
        return STATUS_BUFFER_OVERFLOW;
    }

    {
        uint32_t size_delta = new_attr_total - attr->Length;
        uint32_t after_off;
        uint32_t trailing_bytes;

        after_off = (uint32_t)(((UCHAR*)attr - (UCHAR*)root)) + attr->Length;
        trailing_bytes = root->BytesInUse - after_off;

        if (size_delta != 0 && trailing_bytes > 0)
            memmove((UCHAR*)attr + new_attr_total,
                    (UCHAR*)attr + attr->Length,
                    trailing_bytes);

        root->BytesInUse += size_delta;

        attr->Length = new_attr_total;
        attr->Data.Resident.ValueLength = new_ir_value_size;
    }

    {
        UCHAR *entries_area;
        UCHAR *temp = malloc(new_ir_entries_bytes);
        uint32_t write_off = 0;

        if (!temp)
        {
            free(new_entry_buf); free(root); free(staged);
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        for (i = 0; i < staged_count; i++)
        {
            memcpy(temp + write_off, staged[i].bytes, staged[i].length);
            write_off += staged[i].length;
        }

        ir = (NTFSLX_INDEX_ROOT*)((UCHAR*)attr + attr->Data.Resident.ValueOffset);
        ir->Index.IndexLength = ir->Index.EntriesOffset + new_ir_entries_bytes;
        ir->Index.AllocatedSize = ir->Index.IndexLength;
        entries_area = (UCHAR*)&ir->Index + ir->Index.EntriesOffset;

        memcpy(entries_area, temp, new_ir_entries_bytes);
        free(temp);
    }

    st = ntfsimg_write_mft_record(vol, NTFSLX_FILE_ROOT, root);

    free(new_entry_buf);
    free(root);
    free(staged);

    (void)old_entries_bytes;
    (void)ir_alloc;
    return st;
}
