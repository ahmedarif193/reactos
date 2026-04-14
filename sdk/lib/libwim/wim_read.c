/*
 * Copyright (C) 2026 Ahmed ARIF <arif.ing@outlook.com>
 *
 * wim_read.c - host/user-space adapter over the shared WIM reader core
 *
 */

#include "wim_read.h"
#include "wim_io.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#ifndef _WIN32
#include <utime.h>
#include <unistd.h>
#else
#include <direct.h>
#define mkdir(d, m) _mkdir(d)
#endif

static void*
host_alloc(
    size_t size,
    void* context)
{
    (void)context;
    return malloc(size);
}

static void
host_free(
    void* ptr,
    void* context)
{
    (void)context;
    free(ptr);
}

static int
host_read(
    void* context,
    uint64_t offset,
    void* buffer,
    size_t size)
{
    FILE* file = (FILE*)context;

    if (!file || !buffer)
        return -1;

    if (wim_fseeko(file, (wim_off_t)offset, SEEK_SET) != 0)
        return -1;

    return (fread(buffer, 1, size, file) == size) ? 0 : -1;
}

static int
host_get_file_size(
    FILE* file,
    uint64_t* size)
{
    wim_off_t current;
    wim_off_t end;

    if (!file || !size)
        return -1;

    current = wim_ftello(file);
    if (current < 0)
        return -1;

    if (wim_fseeko(file, 0, SEEK_END) != 0)
        return -1;

    end = wim_ftello(file);
    if (end < 0)
        return -1;

    if (wim_fseeko(file, current, SEEK_SET) != 0)
        return -1;

    *size = (uint64_t)end;
    return 0;
}

static void
wim_sync_core_aliases(
    WimCtx* ctx)
{
    if (!ctx || !ctx->core)
        return;

    ctx->header = ctx->core->Header;
    ctx->blobs = ctx->core->Blobs;
    ctx->blob_count = ctx->core->BlobCount;
    ctx->blob_cap = ctx->core->BlobCapacity;
    ctx->images = ctx->core->Images;
    ctx->image_count = ctx->core->ImageCount;
    ctx->image_infos = ctx->core->ImageInfos;
    ctx->image_info_count = ctx->core->ImageInfoCount;
    ctx->xml_utf8 = ctx->core->XmlUtf8;
    ctx->xml_raw = ctx->core->XmlRaw;
    ctx->xml_raw_size = ctx->core->XmlRawSize;
}

int
wim_open(
    WimCtx* ctx,
    const char* filename)
{
    WimCoreAllocator allocator;
    uint64_t file_size;

    if (!ctx || !filename)
        return -1;

    wim_close(ctx);
    wim_ctx_init(ctx);

    ctx->file = fopen(filename, "rb");
    if (!ctx->file)
    {
        fprintf(stderr, "Error: Cannot open '%s'\n", filename);
        return -1;
    }

    if (host_get_file_size(ctx->file, &file_size) != 0)
    {
        wim_close(ctx);
        return -1;
    }

    ctx->core = (WimCoreCtx*)calloc(1, sizeof(*ctx->core));
    if (!ctx->core)
    {
        wim_close(ctx);
        return -1;
    }

    allocator.Alloc = host_alloc;
    allocator.Free = host_free;
    allocator.Context = NULL;

    if (wimcore_open(ctx->core, &allocator, host_read, ctx->file, file_size) != 0)
    {
        wim_close(ctx);
        return -1;
    }

    wim_sync_core_aliases(ctx);
    ctx->writing = 0;
    return 0;
}

int
wim_select_image(
    WimCtx* ctx,
    int index)
{
    if (!ctx || !ctx->core)
        return -1;

    if (wimcore_select_image(ctx->core, index) != 0)
        return -1;

    wim_sync_core_aliases(ctx);
    return 0;
}

const WimDentry*
wim_get_root(
    const WimCtx* ctx,
    int index)
{
    if (!ctx || !ctx->core)
        return NULL;

    return wimcore_get_root(ctx->core, index);
}

int
wim_read_blob(
    WimCtx* ctx,
    const uint8_t sha1[20],
    uint8_t** data,
    size_t* data_size)
{
    if (!ctx || !ctx->core)
        return -1;

    return wimcore_read_blob(ctx->core, sha1, data, data_size);
}

int
wim_extract_file(
    WimCtx* ctx,
    const WimDentry* d,
    const char* dest_path)
{
    WimSha1Key key;

    if (!ctx || !d || !dest_path)
        return -1;

    memcpy(key.hash, d->sha1, sizeof(key.hash));

    if (wim_sha1_is_zero(&key))
    {
        FILE* f = fopen(dest_path, "wb");
        if (!f)
            return -1;
        fclose(f);
    }
    else
    {
        uint8_t* data = NULL;
        size_t data_size = 0;
        FILE* f;
        int ret = wim_read_blob(ctx, d->sha1, &data, &data_size);

        if (ret != 0)
            return ret;

        f = fopen(dest_path, "wb");
        if (!f)
        {
            free(data);
            return -1;
        }

        if (data_size > 0 && fwrite(data, 1, data_size, f) != data_size)
        {
            fclose(f);
            free(data);
            return -1;
        }

        fclose(f);
        free(data);
    }

#ifndef _WIN32
    if (d->last_write_time != 0 || d->last_access_time != 0)
    {
        struct utimbuf times;
        times.actime = filetime_to_unix(d->last_access_time);
        times.modtime = filetime_to_unix(d->last_write_time);
        utime(dest_path, &times);
    }
#endif

    return 0;
}

static int
is_safe_name(
    const char* name)
{
    if (!name || !name[0])
        return 0;
    if (name[0] == '/')
        return 0;
    if (strchr(name, '/') || strchr(name, '\\'))
        return 0;
    if (strcmp(name, "..") == 0 || strcmp(name, ".") == 0)
        return 0;
    return 1;
}

int
wim_extract_tree(
    WimCtx* ctx,
    const WimDentry* d,
    const char* dest_dir)
{
    size_t i;

    if (!ctx || !d || !dest_dir)
        return -1;

    if (d->attributes & WIM_FILE_ATTRIBUTE_DIRECTORY)
    {
        mkdir(dest_dir, 0755);

        for (i = 0; i < d->child_count; ++i)
        {
            const WimDentry* child = &d->children[i];
            char* child_path;
            size_t path_len;
            int ret;

            if (!child->name_utf8)
                continue;
            if (!is_safe_name(child->name_utf8))
            {
                fprintf(stderr, "Warning: skipping unsafe path component \"%s\"\n",
                        child->name_utf8);
                continue;
            }

            path_len = strlen(dest_dir) + 1 + strlen(child->name_utf8) + 1;
            child_path = (char*)malloc(path_len);
            if (!child_path)
                return -1;

            snprintf(child_path, path_len, "%s/%s", dest_dir, child->name_utf8);
            ret = wim_extract_tree(ctx, child, child_path);
            free(child_path);
            if (ret != 0)
                return ret;
        }
    }
    else
    {
        return wim_extract_file(ctx, d, dest_dir);
    }

    return 0;
}

const char*
wim_get_xml(
    const WimCtx* ctx)
{
    if (!ctx || !ctx->core)
        return NULL;

    return ctx->core->XmlUtf8;
}

int
wim_verify_integrity(
    WimCtx* ctx)
{
    if (!ctx || !ctx->core)
        return -1;

    return wimcore_verify_integrity(ctx->core);
}

void
wim_close(
    WimCtx* ctx)
{
    wim_ctx_free(ctx);
}
