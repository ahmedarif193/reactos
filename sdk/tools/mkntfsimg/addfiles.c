#include "mkntfsimg.h"

#include <sys/stat.h>

#define LINE_CAP 4096

NTSTATUS
ntfsimg_add_dir(NTFSIMG_VOLUME *vol, NTFSIMG_CLUSTER_ALLOCATOR *ca,
                const char *image_path)
{
    uint64_t parent_mft;
    char *leaf = NULL;
    WCHAR wleaf[256];
    uint32_t wleaf_len;
    uint64_t dummy;
    NTSTATUS st = ntfsimg_resolve_path(vol, ca, image_path, 1, &parent_mft, &leaf);
    if (!NT_SUCCESS(st)) return st;
    if (!*leaf) { free(leaf); return STATUS_SUCCESS; }

    if (ntfsimg_utf8_to_wchar(leaf, (uint32_t)strlen(leaf), wleaf, 256, &wleaf_len) != 0)
    { free(leaf); return STATUS_INVALID_PARAMETER; }

    st = ntfsimg_lookup_child(vol, parent_mft, wleaf, wleaf_len, &dummy);
    if (NT_SUCCESS(st)) { free(leaf); return STATUS_SUCCESS; }

    st = ntfsimg_mkdir(vol, ca, parent_mft, wleaf, wleaf_len, &dummy);
    free(leaf);
    return st;
}

NTSTATUS
ntfsimg_add_file(NTFSIMG_VOLUME *vol, NTFSIMG_CLUSTER_ALLOCATOR *ca,
                 const char *image_path, const char *host_path)
{
    uint64_t parent_mft;
    char *leaf = NULL;
    WCHAR wleaf[256];
    uint32_t wleaf_len;
    struct stat stbuf;
    uint64_t host_size;
    UCHAR *record = NULL;
    uint64_t new_index;
    uint32_t rec_size = vol->bytes_per_mft_record;
    uint64_t now_ntfs;
    NTSTATUS st;
    uint32_t resident_threshold = 700;

    if (stat(host_path, &stbuf) != 0) return STATUS_NO_SUCH_FILE;
    host_size = (uint64_t)stbuf.st_size;

    st = ntfsimg_resolve_path(vol, ca, image_path, 1, &parent_mft, &leaf);
    if (!NT_SUCCESS(st)) return st;
    if (ntfsimg_utf8_to_wchar(leaf, (uint32_t)strlen(leaf), wleaf, 256, &wleaf_len) != 0)
    { free(leaf); return STATUS_INVALID_PARAMETER; }

    st = ntfsimg_mft_alloc_or_extend(vol, ca, &new_index);
    if (!NT_SUCCESS(st)) goto out;

    record = calloc(1, rec_size);
    if (!record) { st = STATUS_INSUFFICIENT_RESOURCES; goto out; }

    now_ntfs = ntfsimg_current_ntfs_time();
    st = ntfsimg_build_file_record(record, rec_size, new_index, 1, 0);
    if (!NT_SUCCESS(st)) goto out;
    st = ntfsimg_append_std_info(record, rec_size, now_ntfs, 0);
    if (!NT_SUCCESS(st)) goto out;
    st = ntfsimg_append_file_name(record, rec_size,
                                  MK_MREF(parent_mft, 0), now_ntfs,
                                  0, host_size, wleaf, wleaf_len,
                                  NTFSLX_FILE_NAME_WIN32);
    if (!NT_SUCCESS(st)) goto out;

    if (host_size <= resident_threshold)
    {
        UCHAR *data = NULL;
        if (host_size > 0)
        {
            FILE *fp = fopen(host_path, "rb");
            if (!fp) { st = STATUS_NO_SUCH_FILE; goto out; }
            data = malloc((size_t)host_size);
            if (!data) { fclose(fp); st = STATUS_INSUFFICIENT_RESOURCES; goto out; }
            if (fread(data, 1, (size_t)host_size, fp) != host_size)
            { free(data); fclose(fp); st = STATUS_UNSUCCESSFUL; goto out; }
            fclose(fp);
        }
        st = ntfsimg_append_data_resident(record, rec_size, data, (uint32_t)host_size);
        free(data);
        if (!NT_SUCCESS(st)) goto out;
    }
    else
    {
        st = ntfsimg_append_data_nonresident(record, rec_size, vol, ca,
                                             host_path, host_size);
        if (!NT_SUCCESS(st)) goto out;
    }

    st = ntfsimg_write_mft_record(vol, new_index, (NTFSLX_MFT_RECORD*)record);
    if (!NT_SUCCESS(st)) goto out;

    {
        uint32_t name_bytes = wleaf_len * (uint32_t)sizeof(WCHAR);
        uint32_t fn_size = (uint32_t)sizeof(NTFSLX_FILE_NAME_ATTRIBUTE) + name_bytes;
        uint32_t entry_len = (fn_size + 16 + 7) & ~7u;
        UCHAR *entry = calloc(1, entry_len);
        PNTFSLX_FILE_NAME_ATTRIBUTE fn;
        if (!entry) { st = STATUS_INSUFFICIENT_RESOURCES; goto out; }
        *(ULONGLONG*)entry = MK_MREF(new_index, ((NTFSLX_MFT_RECORD*)record)->SequenceNumber);
        *(USHORT*)(entry + 8) = (USHORT)entry_len;
        *(USHORT*)(entry + 10) = (USHORT)fn_size;
        *(USHORT*)(entry + 12) = 0;
        *(USHORT*)(entry + 14) = 0;
        fn = (PNTFSLX_FILE_NAME_ATTRIBUTE)(entry + 16);
        fn->ParentDirectory = MK_MREF(parent_mft, 0);
        fn->CreationTime = now_ntfs;
        fn->LastDataChangeTime = now_ntfs;
        fn->LastMftChangeTime = now_ntfs;
        fn->LastAccessTime = now_ntfs;
        fn->AllocatedSize = (host_size + 7) & ~7ULL;
        fn->DataSize = host_size;
        fn->FileAttributes = 0;
        fn->FileNameLength = (UCHAR)wleaf_len;
        fn->FileNameType = NTFSLX_FILE_NAME_WIN32;
        memcpy(entry + 16 + sizeof(NTFSLX_FILE_NAME_ATTRIBUTE), wleaf, name_bytes);

        st = ntfsimg_dir_insert_ex(vol, ca, parent_mft, entry, entry_len, wleaf, wleaf_len);
        free(entry);
    }

out:
    free(record);
    free(leaf);
    return st;
}

static char *
trim(char *s)
{
    char *end;
    while (*s == ' ' || *s == '\t' || *s == '\r') s++;
    end = s + strlen(s);
    while (end > s && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n'))
        *--end = 0;
    return s;
}

NTSTATUS
ntfsimg_addfiles_list(NTFSIMG_VOLUME *vol, NTFSIMG_CLUSTER_ALLOCATOR *ca,
                      const char *list_path)
{
    FILE *fp = fopen(list_path, "r");
    char line[LINE_CAP];
    uint32_t added = 0;
    uint32_t dirs = 0;
    NTSTATUS st = STATUS_SUCCESS;

    if (!fp)
    {
        fprintf(stderr, "mkntfsimg: cannot open list '%s'\n", list_path);
        return STATUS_NO_SUCH_FILE;
    }

    while (fgets(line, sizeof(line), fp))
    {
        char *entry = trim(line);
        char *eq;
        if (*entry == 0 || *entry == '#') continue;
        eq = strchr(entry, '=');
        if (!eq)
        {
            st = ntfsimg_add_dir(vol, ca, entry);
            if (!NT_SUCCESS(st))
            {
                fprintf(stderr, "mkntfsimg: [#%u] add_dir '%s' failed 0x%x\n",
                        added + dirs, entry, (unsigned)st);
                break;
            }
            dirs++;
            continue;
        }
        *eq = 0;
        {
            char *dst = trim(entry);
            char *src = trim(eq + 1);
            struct stat stbuf;
            if (stat(src, &stbuf) != 0)
            {
                fprintf(stderr, "mkntfsimg: stat '%s': missing\n", src);
                st = STATUS_NO_SUCH_FILE;
                break;
            }
            if (S_ISDIR(stbuf.st_mode))
            {
                st = ntfsimg_add_dir(vol, ca, dst);
                if (!NT_SUCCESS(st))
                {
                    fprintf(stderr, "mkntfsimg: add_dir '%s' failed 0x%x\n", dst, (unsigned)st);
                    break;
                }
                dirs++;
            }
            else
            {
                st = ntfsimg_add_file(vol, ca, dst, src);
                if (!NT_SUCCESS(st))
                {
                    fprintf(stderr, "mkntfsimg: add_file '%s' (<= '%s') failed 0x%x\n",
                            dst, src, (unsigned)st);
                    break;
                }
                added++;
            }
        }
    }

    fclose(fp);

    if (NT_SUCCESS(st))
    {
        st = ntfsimg_cluster_alloc_flush(vol, ca);
        printf("mkntfsimg: added %u files, %u dirs\n", added, dirs);
    }
    return st;
}
