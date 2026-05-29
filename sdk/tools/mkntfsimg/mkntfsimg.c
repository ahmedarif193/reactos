#include "mkntfsimg.h"

#include <stdarg.h>
#include <sys/stat.h>

static NTSTATUS
add_one_file(NTFSIMG_VOLUME *vol, const char *image_name, const char *host_path)
{
    FILE *fp = NULL;
    long size;
    UCHAR *data = NULL;
    UCHAR *record = NULL;
    UCHAR *bitmap = NULL;
    uint32_t bitmap_bytes = 0;
    uint64_t new_index;
    WCHAR wname[256];
    uint32_t wlen;
    uint64_t now_ntfs;
    uint32_t total_records;
    NTSTATUS st;

    if (ntfsimg_utf8_to_wchar(image_name, (uint32_t)strlen(image_name),
                              wname, 256, &wlen) != 0)
        return STATUS_INVALID_PARAMETER;

    fp = fopen(host_path, "rb");
    if (!fp)
    {
        fprintf(stderr, "mkntfsimg: cannot open host file '%s'\n", host_path);
        return STATUS_NO_SUCH_FILE;
    }
    fseek(fp, 0, SEEK_END);
    size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (size < 0) { fclose(fp); return STATUS_UNSUCCESSFUL; }
    if (size > 700)
    {
        fclose(fp);
        fprintf(stderr, "mkntfsimg: file '%s' size %ld exceeds B2 resident limit (700 bytes); non-resident coming in B3\n",
                host_path, size);
        return STATUS_BUFFER_OVERFLOW;
    }

    data = malloc((size_t)size);
    if (!data) { fclose(fp); return STATUS_INSUFFICIENT_RESOURCES; }
    if ((long)fread(data, 1, (size_t)size, fp) != size)
    { free(data); fclose(fp); return STATUS_UNSUCCESSFUL; }
    fclose(fp);

    st = ntfsimg_mft_bitmap_load(vol, &bitmap, &bitmap_bytes);
    if (!NT_SUCCESS(st)) goto out;

    total_records = (uint32_t)(vol->mft_data_size / vol->bytes_per_mft_record);
    st = ntfsimg_mft_alloc_record(bitmap, bitmap_bytes, total_records, &new_index);
    if (!NT_SUCCESS(st))
    {
        fprintf(stderr, "mkntfsimg: $MFT full (need B3 to extend)\n");
        goto out;
    }

    record = calloc(1, vol->bytes_per_mft_record);
    if (!record) { st = STATUS_INSUFFICIENT_RESOURCES; goto out; }

    now_ntfs = ntfsimg_current_ntfs_time();
    st = ntfsimg_build_file_record(record, vol->bytes_per_mft_record, new_index, 1, 0);
    if (!NT_SUCCESS(st)) goto out;
    st = ntfsimg_append_std_info(record, vol->bytes_per_mft_record, now_ntfs, 0);
    if (!NT_SUCCESS(st)) goto out;
    st = ntfsimg_append_file_name(record, vol->bytes_per_mft_record,
                                  MK_MREF(NTFSLX_FILE_ROOT, 5), now_ntfs,
                                  0, (uint64_t)size, wname, wlen,
                                  NTFSLX_FILE_NAME_WIN32);
    if (!NT_SUCCESS(st)) goto out;
    st = ntfsimg_append_data_resident(record, vol->bytes_per_mft_record,
                                      data, (uint32_t)size);
    if (!NT_SUCCESS(st)) goto out;

    st = ntfsimg_write_mft_record(vol, new_index, (NTFSLX_MFT_RECORD*)record);
    if (!NT_SUCCESS(st)) goto out;

    bitmap[new_index >> 3] |= (UCHAR)(1u << (new_index & 7));
    st = ntfsimg_mft_bitmap_save(vol, bitmap, bitmap_bytes);
    if (!NT_SUCCESS(st)) goto out;

    if (getenv("MKNTFSIMG_SKIP_ROOT_INSERT") == NULL)
    {
        st = ntfsimg_root_add_entry(vol,
                                    MK_MREF(new_index, ((NTFSLX_MFT_RECORD*)record)->SequenceNumber),
                                    wname, wlen, NTFSLX_FILE_NAME_WIN32,
                                    now_ntfs, 0, (uint64_t)size);
        if (!NT_SUCCESS(st)) goto out;
    }

    printf("mkntfsimg: added '%s' -> '%s' as MFT #%llu (%ld bytes resident)\n",
           host_path, image_name, (unsigned long long)new_index, size);

out:
    free(data);
    free(record);
    free(bitmap);
    return st;
}

static void
print_usage(const char *name)
{
    fprintf(stderr,
            "Usage: %s <image> [options]\n"
            "Options:\n"
            "  -addfiles <list>   dst=src manifest to import (unused in skeleton)\n"
            "  -selftest          read-modify-write every MFT record in the volume and verify\n"
            "  -v                 verbose\n",
            name);
}

static NTSTATUS
run_selftest(NTFSIMG_VOLUME *vol)
{
    uint32_t rec_size = vol->bytes_per_mft_record;
    uint32_t record_count = (uint32_t)(vol->mft_data_size / rec_size);
    uint8_t *original = malloc(rec_size);
    uint8_t *reread = malloc(rec_size);
    uint32_t checked = 0;
    uint32_t skipped = 0;
    uint64_t i;
    NTSTATUS st = STATUS_SUCCESS;

    if (!original || !reread) { free(original); free(reread); return STATUS_INSUFFICIENT_RESOURCES; }

    for (i = 0; i < record_count; i++)
    {
        st = ntfsimg_read_mft_record(vol, i, (NTFSLX_MFT_RECORD*)original);
        if (st == STATUS_NOT_FOUND) { skipped++; st = STATUS_SUCCESS; continue; }
        if (!NT_SUCCESS(st))
        {
            fprintf(stderr, "selftest: read rec=%llu failed 0x%x\n",
                    (unsigned long long)i, (unsigned)st);
            break;
        }

        st = ntfsimg_write_mft_record(vol, i, (NTFSLX_MFT_RECORD*)original);
        if (!NT_SUCCESS(st))
        {
            fprintf(stderr, "selftest: write rec=%llu failed 0x%x\n",
                    (unsigned long long)i, (unsigned)st);
            break;
        }

        st = ntfsimg_read_mft_record(vol, i, (NTFSLX_MFT_RECORD*)reread);
        if (!NT_SUCCESS(st))
        {
            fprintf(stderr, "selftest: reread rec=%llu failed 0x%x\n",
                    (unsigned long long)i, (unsigned)st);
            break;
        }

        {
            const NTFSLX_MFT_RECORD *a = (const NTFSLX_MFT_RECORD*)original;
            USHORT usa_off = a->Ntfs.UsaOffset;
            USHORT usa_bytes = (USHORT)(a->Ntfs.UsaCount * 2);
            int mismatch = 0;
            if (memcmp(original, reread, usa_off) != 0) mismatch = 1;
            else if (memcmp(original + usa_off + usa_bytes,
                            reread + usa_off + usa_bytes,
                            rec_size - usa_off - usa_bytes) != 0) mismatch = 1;
            if (mismatch)
            {
                fprintf(stderr, "selftest: rec=%llu content mismatch after RMW\n",
                        (unsigned long long)i);
                st = STATUS_FILE_CORRUPT_ERROR;
                break;
            }
        }
        checked++;
    }

    if (NT_SUCCESS(st))
        st = ntfsimg_io_flush(&vol->io);

    printf("selftest: checked=%u skipped(free)=%u total=%u result=%s\n",
           checked, skipped, record_count,
           NT_SUCCESS(st) ? "OK" : "FAIL");

    free(original);
    free(reread);
    return st;
}

int
main(int argc, char *argv[])
{
    const char *image_path = NULL;
    const char *addfile_image_name = NULL;
    const char *addfile_host_path = NULL;
    const char *addfiles_list_path = NULL;
    NTFSIMG_VOLUME vol;
    NTFSIMG_CLUSTER_ALLOCATOR ca;
    NTSTATUS st;
    int do_selftest = 0;
    int i;

    for (i = 1; i < argc; i++)
    {
        if (argv[i][0] != '-')
        {
            if (!image_path)
                image_path = argv[i];
            else
            {
                fprintf(stderr, "mkntfsimg: unexpected positional '%s'\n", argv[i]);
                print_usage(argv[0]);
                return 1;
            }
        }
        else if (strcmp(argv[i], "-v") == 0)
        {
            g_mkntfsimg_verbose = 1;
        }
        else if (strcmp(argv[i], "-selftest") == 0)
        {
            do_selftest = 1;
        }
        else if (strcmp(argv[i], "-addfiles") == 0 && i + 1 < argc)
        {
            addfiles_list_path = argv[++i];
        }
        else if (strcmp(argv[i], "-addfile") == 0 && i + 2 < argc)
        {
            addfile_image_name = argv[++i];
            addfile_host_path = argv[++i];
        }
        else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0)
        {
            print_usage(argv[0]);
            return 0;
        }
        else
        {
            fprintf(stderr, "mkntfsimg: unknown option '%s'\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    if (!image_path)
    {
        print_usage(argv[0]);
        return 1;
    }

    st = ntfsimg_volume_open(&vol, image_path);
    if (!NT_SUCCESS(st))
    {
        fprintf(stderr, "mkntfsimg: cannot open '%s' (status 0x%x)\n",
                image_path, (unsigned)st);
        return 1;
    }

    printf("mkntfsimg: opened '%s'\n", image_path);
    printf("  bytes/sector      = %u\n", vol.bytes_per_sector);
    printf("  sectors/cluster   = %u\n", vol.sectors_per_cluster);
    printf("  bytes/cluster     = %u\n", vol.bytes_per_cluster);
    printf("  bytes/MFT record  = %u\n", vol.bytes_per_mft_record);
    printf("  bytes/index rec   = %u\n", vol.bytes_per_index_record);
    printf("  total sectors     = %llu\n", (unsigned long long)vol.total_sectors);
    printf("  total clusters    = %llu\n", (unsigned long long)vol.total_clusters);
    printf("  $MFT LCN          = %llu\n", (unsigned long long)vol.mft_lcn);
    printf("  $MFTMirr LCN      = %llu\n", (unsigned long long)vol.mft_mirr_lcn);
    printf("  $MFT data size    = %llu bytes (%u records)\n",
           (unsigned long long)vol.mft_data_size,
           (unsigned)(vol.mft_data_size / vol.bytes_per_mft_record));
    printf("  $MFT runs         = %u\n", vol.mft_runlist_count);
    printf("  $Bitmap data size = %llu bytes\n",
           (unsigned long long)vol.bitmap_data_size);
    printf("  $Bitmap runs      = %u\n", vol.bitmap_runlist_count);
    printf("  serial number     = 0x%016llx\n",
           (unsigned long long)vol.serial_number);

    if (do_selftest)
    {
        st = run_selftest(&vol);
        if (!NT_SUCCESS(st))
        {
            ntfsimg_volume_close(&vol);
            return 1;
        }
    }

    if (addfile_image_name)
    {
        st = add_one_file(&vol, addfile_image_name, addfile_host_path);
        if (!NT_SUCCESS(st))
        {
            fprintf(stderr, "mkntfsimg: add_one_file failed (status 0x%x)\n", (unsigned)st);
            ntfsimg_volume_close(&vol);
            return 1;
        }
        ntfsimg_io_flush(&vol.io);
    }

    if (addfiles_list_path)
    {
        st = ntfsimg_cluster_alloc_init(&vol, &ca);
        if (!NT_SUCCESS(st))
        {
            fprintf(stderr, "mkntfsimg: cluster_alloc_init failed (status 0x%x)\n", (unsigned)st);
            ntfsimg_volume_close(&vol);
            return 1;
        }
        st = ntfsimg_addfiles_list(&vol, &ca, addfiles_list_path);
        ntfsimg_cluster_alloc_free(&ca);
        if (!NT_SUCCESS(st))
        {
            ntfsimg_volume_close(&vol);
            return 1;
        }
        ntfsimg_io_flush(&vol.io);
    }

    ntfsimg_volume_close(&vol);
    return 0;
}
