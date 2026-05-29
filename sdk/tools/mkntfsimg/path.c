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
fold_wchar(WCHAR c)
{
    if (c >= 'a' && c <= 'z') return (WCHAR)(c - 'a' + 'A');
    return c;
}

static int
cmp_name_w(const WCHAR *a, uint32_t alen, const WCHAR *b, uint32_t blen)
{
    uint32_t n = alen < blen ? alen : blen;
    uint32_t i;
    for (i = 0; i < n; i++)
    {
        WCHAR ca = fold_wchar(a[i]);
        WCHAR cb = fold_wchar(b[i]);
        if (ca < cb) return -1;
        if (ca > cb) return 1;
    }
    if (alen < blen) return -1;
    if (alen > blen) return 1;
    return 0;
}

static NTSTATUS
lookup_in_resident_root(NTFSLX_MFT_RECORD *parent_rec, uint32_t rec_size,
                        const WCHAR *name, uint32_t name_len,
                        uint64_t *out_mft)
{
    static const WCHAR I30[] = { '$','I','3','0' };
    PNTFSLX_ATTR_RECORD attr = ntfsimg_find_attr_named(parent_rec, rec_size,
                                                       NTFSLX_ATTRIBUTE_INDEX_ROOT,
                                                       I30, 4);
    NTFSLX_INDEX_ROOT *ir;
    UCHAR *ientries_base;
    uint32_t ientries_end;
    uint32_t off;

    if (!attr || attr->NonResident) return STATUS_NOT_FOUND;
    ir = (NTFSLX_INDEX_ROOT*)((UCHAR*)attr + attr->Data.Resident.ValueOffset);
    ientries_base = (UCHAR*)&ir->Index;
    ientries_end = ir->Index.IndexLength;
    off = ir->Index.EntriesOffset;

    while (off + sizeof(I30_ENTRY_HEAD) <= ientries_end)
    {
        I30_ENTRY_HEAD *eh = (I30_ENTRY_HEAD*)(ientries_base + off);
        if (eh->Length < sizeof(I30_ENTRY_HEAD)) break;
        if ((eh->Flags & NTFSLX_INDEX_ENTRY_END) == 0)
        {
            PNTFSLX_FILE_NAME_ATTRIBUTE fn = (PNTFSLX_FILE_NAME_ATTRIBUTE)
                (ientries_base + off + sizeof(I30_ENTRY_HEAD));
            const WCHAR *ename = (const WCHAR*)((UCHAR*)fn + sizeof(NTFSLX_FILE_NAME_ATTRIBUTE));
            if (cmp_name_w(name, name_len, ename, fn->FileNameLength) == 0)
            {
                *out_mft = MREF(eh->IndexedFile);
                return STATUS_SUCCESS;
            }
        }
        if (eh->Flags & NTFSLX_INDEX_ENTRY_END) break;
        off += eh->Length;
    }
    return STATUS_NOT_FOUND;
}

/* ntfsimg_lookup_child now lives in lookup.c (handles LARGE_INDEX via $INDEX_ALLOCATION walk). */

static NTSTATUS
build_empty_dir_record(UCHAR *buf, uint32_t rec_size, uint64_t record_number,
                       uint64_t parent_ref, const WCHAR *name, uint32_t name_len,
                       uint64_t ntfs_time)
{
    NTSTATUS st;
    PNTFSLX_ATTR_RECORD attr;
    NTFSLX_INDEX_ROOT *ir;
    uint32_t ir_value_size;
    uint32_t ir_attr_total;
    uint32_t end_off;
    uint32_t hdr_size = 24;
    static const WCHAR I30[] = { '$','I','3','0' };
    uint32_t i30_bytes = 4 * sizeof(WCHAR);
    uint32_t name_off;

    st = ntfsimg_build_file_record(buf, rec_size, record_number, 1, 1);
    if (!NT_SUCCESS(st)) return st;
    st = ntfsimg_append_std_info(buf, rec_size, ntfs_time,
                                 0x10000000 | 0x10);
    if (!NT_SUCCESS(st)) return st;
    st = ntfsimg_append_file_name(buf, rec_size, parent_ref, ntfs_time,
                                  0x10000000 | 0x10, 0, name, name_len,
                                  NTFSLX_FILE_NAME_WIN32);
    if (!NT_SUCCESS(st)) return st;

    {
        NTFSLX_MFT_RECORD *rec = (NTFSLX_MFT_RECORD*)buf;
        uint32_t cur_off = rec->AttributesOffset;
        while (cur_off < rec_size)
        {
            PNTFSLX_ATTR_RECORD a = (PNTFSLX_ATTR_RECORD)(buf + cur_off);
            if (a->Type == NTFSLX_ATTRIBUTE_END) { end_off = cur_off; break; }
            if (a->Length == 0) return STATUS_FILE_CORRUPT_ERROR;
            cur_off += a->Length;
        }
    }

    name_off = hdr_size;
    ir_value_size = sizeof(NTFSLX_INDEX_ROOT) + 16;
    ir_attr_total = (uint32_t)ALIGN8(hdr_size + i30_bytes + ir_value_size);

    if (end_off + ir_attr_total + sizeof(ULONG) > rec_size)
        return STATUS_BUFFER_OVERFLOW;

    attr = (PNTFSLX_ATTR_RECORD)(buf + end_off);
    memset(attr, 0, ir_attr_total);
    attr->Type = NTFSLX_ATTRIBUTE_INDEX_ROOT;
    attr->Length = ir_attr_total;
    attr->NonResident = 0;
    attr->NameLength = 4;
    attr->NameOffset = (USHORT)name_off;
    attr->Flags = 0;
    attr->Instance = ((NTFSLX_MFT_RECORD*)buf)->NextAttributeInstance++;
    memcpy((UCHAR*)attr + name_off, I30, i30_bytes);
    attr->Data.Resident.ValueLength = ir_value_size;
    attr->Data.Resident.ValueOffset = (USHORT)ALIGN8(name_off + i30_bytes);
    attr->Data.Resident.Flags = 0;

    ir = (NTFSLX_INDEX_ROOT*)((UCHAR*)attr + attr->Data.Resident.ValueOffset);
    ir->Type = NTFSLX_ATTRIBUTE_FILE_NAME;
    ir->CollationRule = NTFSLX_COLLATION_FILE_NAME;
    ir->IndexBlockSize = 4096;
    ir->ClustersPerIndexBlock = 1;
    ir->Index.EntriesOffset = 16;
    ir->Index.IndexLength = 16 + 16;
    ir->Index.AllocatedSize = 16 + 16;
    ir->Index.Flags = 0;

    {
        I30_ENTRY_HEAD *term = (I30_ENTRY_HEAD*)((UCHAR*)&ir->Index + 16);
        term->IndexedFile = 0;
        term->Length = 16;
        term->KeyLength = 0;
        term->Flags = NTFSLX_INDEX_ENTRY_END;
        term->Reserved = 0;
    }

    {
        PNTFSLX_ATTR_RECORD new_end = (PNTFSLX_ATTR_RECORD)((UCHAR*)attr + ir_attr_total);
        new_end->Type = NTFSLX_ATTRIBUTE_END;
        new_end->Length = 0;
    }
    ((NTFSLX_MFT_RECORD*)buf)->BytesInUse = end_off + ir_attr_total + (uint32_t)sizeof(ULONG);
    return STATUS_SUCCESS;
}

static NTSTATUS
insert_child_entry_into_parent(NTFSIMG_VOLUME *vol,
                               NTFSIMG_CLUSTER_ALLOCATOR *ca,
                               uint64_t parent_mft,
                               uint64_t child_mft, USHORT child_seq,
                               const WCHAR *name, uint32_t name_len,
                               uint32_t file_attributes, uint64_t data_size,
                               uint64_t ntfs_time)
{
    uint32_t name_bytes = name_len * (uint32_t)sizeof(WCHAR);
    uint32_t fn_size = (uint32_t)sizeof(NTFSLX_FILE_NAME_ATTRIBUTE) + name_bytes;
    uint32_t entry_len = (uint32_t)ALIGN8(sizeof(I30_ENTRY_HEAD) + fn_size);
    UCHAR *entry = calloc(1, entry_len);
    I30_ENTRY_HEAD *h;
    PNTFSLX_FILE_NAME_ATTRIBUTE fn;
    NTSTATUS st;

    if (!entry) return STATUS_INSUFFICIENT_RESOURCES;

    h = (I30_ENTRY_HEAD*)entry;
    h->IndexedFile = MK_MREF(child_mft, child_seq);
    h->Length = (USHORT)entry_len;
    h->KeyLength = (USHORT)fn_size;
    h->Flags = 0;
    h->Reserved = 0;

    fn = (PNTFSLX_FILE_NAME_ATTRIBUTE)(entry + sizeof(I30_ENTRY_HEAD));
    fn->ParentDirectory = MK_MREF(parent_mft, 0);
    fn->CreationTime = ntfs_time;
    fn->LastDataChangeTime = ntfs_time;
    fn->LastMftChangeTime = ntfs_time;
    fn->LastAccessTime = ntfs_time;
    fn->AllocatedSize = (data_size + 7) & ~7ULL;
    fn->DataSize = data_size;
    fn->FileAttributes = file_attributes;
    fn->FileNameLength = (UCHAR)name_len;
    fn->FileNameType = NTFSLX_FILE_NAME_WIN32;
    memcpy((UCHAR*)fn + sizeof(NTFSLX_FILE_NAME_ATTRIBUTE), name, name_bytes);

    st = ntfsimg_dir_insert_ex(vol, ca, parent_mft, entry, entry_len, name, name_len);
    free(entry);
    return st;
}

NTSTATUS
ntfsimg_mkdir(NTFSIMG_VOLUME *vol, NTFSIMG_CLUSTER_ALLOCATOR *ca,
              uint64_t parent_mft_index, const WCHAR *name, uint32_t name_len,
              uint64_t *out_mft_index)
{
    uint64_t new_index;
    UCHAR *record = NULL;
    uint32_t rec_size = vol->bytes_per_mft_record;
    uint64_t now_ntfs;
    NTSTATUS st;

    st = ntfsimg_mft_alloc_or_extend(vol, ca, &new_index);
    if (!NT_SUCCESS(st)) return st;

    record = calloc(1, rec_size);
    if (!record) return STATUS_INSUFFICIENT_RESOURCES;

    now_ntfs = ntfsimg_current_ntfs_time();

    st = build_empty_dir_record(record, rec_size, new_index,
                                MK_MREF(parent_mft_index, 0),
                                name, name_len, now_ntfs);
    if (!NT_SUCCESS(st)) goto out;

    st = ntfsimg_write_mft_record(vol, new_index, (NTFSLX_MFT_RECORD*)record);
    if (!NT_SUCCESS(st)) goto out;

    st = insert_child_entry_into_parent(vol, ca, parent_mft_index, new_index,
                                        ((NTFSLX_MFT_RECORD*)record)->SequenceNumber,
                                        name, name_len,
                                        0x10000000 | 0x10, 0, now_ntfs);
    if (!NT_SUCCESS(st)) goto out;

    *out_mft_index = new_index;

out:
    free(record);
    return st;
}

NTSTATUS
ntfsimg_resolve_path(NTFSIMG_VOLUME *vol, NTFSIMG_CLUSTER_ALLOCATOR *ca,
                     const char *image_path, int create_intermediate,
                     uint64_t *out_parent_mft, char **out_leaf_name)
{
    char *path_copy;
    char *save;
    char *seg;
    uint64_t cur_mft = NTFSLX_FILE_ROOT;
    const char *last_sep;
    char *leaf;
    size_t leaf_len;
    NTSTATUS st = STATUS_SUCCESS;

    while (*image_path == '/' || *image_path == '\\') image_path++;

    last_sep = image_path;
    {
        const char *p;
        for (p = image_path; *p; p++)
            if (*p == '/' || *p == '\\') last_sep = p;
    }

    if (last_sep == image_path)
    {
        *out_parent_mft = NTFSLX_FILE_ROOT;
        leaf_len = strlen(image_path);
        leaf = malloc(leaf_len + 1);
        if (!leaf) return STATUS_INSUFFICIENT_RESOURCES;
        memcpy(leaf, image_path, leaf_len + 1);
        *out_leaf_name = leaf;
        return STATUS_SUCCESS;
    }

    path_copy = malloc((size_t)(last_sep - image_path) + 1);
    if (!path_copy) return STATUS_INSUFFICIENT_RESOURCES;
    memcpy(path_copy, image_path, (size_t)(last_sep - image_path));
    path_copy[last_sep - image_path] = 0;

    save = path_copy;
    while ((seg = save) != NULL && *seg)
    {
        char *slash = strpbrk(save, "/\\");
        WCHAR wseg[256];
        uint32_t wseg_len;
        uint64_t child_mft;

        if (slash) { *slash = 0; save = slash + 1; } else save = NULL;
        if (*seg == 0) continue;

        if (ntfsimg_utf8_to_wchar(seg, (uint32_t)strlen(seg), wseg, 256, &wseg_len) != 0)
        { free(path_copy); return STATUS_INVALID_PARAMETER; }

        st = ntfsimg_lookup_child(vol, cur_mft, wseg, wseg_len, &child_mft);
        if (st == STATUS_NOT_FOUND)
        {
            if (!create_intermediate) { free(path_copy); return st; }
            st = ntfsimg_mkdir(vol, ca, cur_mft, wseg, wseg_len, &child_mft);
            if (!NT_SUCCESS(st)) { free(path_copy); return st; }
        }
        else if (!NT_SUCCESS(st))
        {
            free(path_copy);
            return st;
        }
        cur_mft = child_mft;

        if (!save) break;
    }

    free(path_copy);

    *out_parent_mft = cur_mft;
    last_sep++;
    leaf_len = strlen(last_sep);
    leaf = malloc(leaf_len + 1);
    if (!leaf) return STATUS_INSUFFICIENT_RESOURCES;
    memcpy(leaf, last_sep, leaf_len + 1);
    *out_leaf_name = leaf;
    return STATUS_SUCCESS;
}
