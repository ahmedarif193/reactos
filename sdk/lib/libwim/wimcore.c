/*
 * Copyright (C) 2026 Ahmed ARIF <arif.ing@outlook.com>
 *
 * wimcore.c - freestanding WIM reader core shared by host tools and freeldr
 *
 */

#include "wimcore.h"
#include "sha1.h"
#include "xpress_huff.h"

#include <stdlib.h>
#include <string.h>

#define WIMCORE_DEFAULT_CHUNK_SIZE 32768u
#define WIMCORE_DENTRY_FIXED_SIZE  102u

static void
core_zero(
    void* ptr,
    size_t size)
{
    memset(ptr, 0, size);
}

static void*
core_alloc(
    WimCoreCtx* ctx,
    size_t size)
{
    if (!ctx || !ctx->Allocator.Alloc || size == 0)
        return NULL;

    return ctx->Allocator.Alloc(size, ctx->Allocator.Context);
}

static void
core_free(
    WimCoreCtx* ctx,
    void* ptr)
{
    if (!ctx || !ctx->Allocator.Free || !ptr)
        return;

    ctx->Allocator.Free(ptr, ctx->Allocator.Context);
}

static const uint8_t*
core_get_pointer(
    WimCoreCtx* ctx,
    uint64_t offset,
    size_t size)
{
    if (!ctx || !ctx->MemoryBase)
        return NULL;
    if (offset > ctx->ImageSize || size > (size_t)(ctx->ImageSize - offset))
        return NULL;
    return ctx->MemoryBase + offset;
}

static int
core_read_at(
    WimCoreCtx* ctx,
    uint64_t offset,
    void* buffer,
    size_t size)
{
    const uint8_t* ptr;

    if (!ctx || !buffer)
        return -1;
    if (offset > ctx->ImageSize || size > (size_t)(ctx->ImageSize - offset))
        return -1;

    ptr = core_get_pointer(ctx, offset, size);
    if (ptr)
    {
        memcpy(buffer, ptr, size);
        return 0;
    }

    if (!ctx->Read)
        return -1;

    return ctx->Read(ctx->ReadContext, offset, buffer, size);
}

static uint32_t
core_read_u32(
    const uint8_t* p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint16_t
core_read_u16(
    const uint8_t* p)
{
    return (uint16_t)p[0] |
           ((uint16_t)p[1] << 8);
}

static uint64_t
core_read_u64(
    const uint8_t* p)
{
    return (uint64_t)p[0] |
           ((uint64_t)p[1] << 8) |
           ((uint64_t)p[2] << 16) |
           ((uint64_t)p[3] << 24) |
           ((uint64_t)p[4] << 32) |
           ((uint64_t)p[5] << 40) |
           ((uint64_t)p[6] << 48) |
           ((uint64_t)p[7] << 56);
}

static void
core_dentry_init(
    WimDentry* dentry)
{
    if (!dentry)
        return;

    core_zero(dentry, sizeof(*dentry));
    dentry->security_id = -1;
}

static void
core_dentry_free(
    WimCoreCtx* ctx,
    WimDentry* dentry)
{
    size_t i;

    if (!dentry)
        return;

    core_free(ctx, dentry->name_utf8);
    core_free(ctx, dentry->name_utf16);
    for (i = 0; i < dentry->child_count; ++i)
        core_dentry_free(ctx, &dentry->children[i]);
    core_free(ctx, dentry->children);

    core_zero(dentry, sizeof(*dentry));
}

static int
core_add_child(
    WimCoreCtx* ctx,
    WimDentry* parent,
    const WimDentry* child)
{
    WimDentry* new_children;
    size_t new_cap;

    if (!ctx || !parent || !child)
        return -1;

    if (parent->child_count >= parent->child_cap)
    {
        new_cap = parent->child_cap ? parent->child_cap * 2 : 8;
        new_children = (WimDentry*)core_alloc(ctx, new_cap * sizeof(WimDentry));
        if (!new_children)
            return -1;

        if (parent->children && parent->child_count)
            memcpy(new_children, parent->children, parent->child_count * sizeof(WimDentry));

        core_free(ctx, parent->children);
        parent->children = new_children;
        parent->child_cap = new_cap;
    }

    parent->children[parent->child_count++] = *child;
    return 0;
}

static int
core_utf16le_to_utf8(
    WimCoreCtx* ctx,
    const uint16_t* utf16,
    size_t len,
    char** out)
{
    char* buffer;
    size_t pos = 0;
    size_t i;

    if (!ctx || !out)
        return -1;

    *out = NULL;

    buffer = (char*)core_alloc(ctx, len * 3 + 1);
    if (!buffer)
        return -1;

    for (i = 0; i < len; ++i)
    {
        uint16_t c = utf16[i];

        if (c == 0)
            break;

        if (c < 0x80)
        {
            buffer[pos++] = (char)c;
        }
        else if (c < 0x800)
        {
            buffer[pos++] = (char)(0xC0 | (c >> 6));
            buffer[pos++] = (char)(0x80 | (c & 0x3F));
        }
        else
        {
            buffer[pos++] = (char)(0xE0 | (c >> 12));
            buffer[pos++] = (char)(0x80 | ((c >> 6) & 0x3F));
            buffer[pos++] = (char)(0x80 | (c & 0x3F));
        }
    }

    buffer[pos] = '\0';
    *out = buffer;
    return 0;
}

static int
core_add_blob(
    WimCoreCtx* ctx,
    const WimBlob* blob)
{
    WimBlob* new_blobs;
    size_t new_cap;

    if (!ctx || !blob)
        return -1;

    if (ctx->BlobCount >= ctx->BlobCapacity)
    {
        new_cap = ctx->BlobCapacity ? ctx->BlobCapacity * 2 : 64;
        new_blobs = (WimBlob*)core_alloc(ctx, new_cap * sizeof(WimBlob));
        if (!new_blobs)
            return -1;

        if (ctx->Blobs && ctx->BlobCount)
            memcpy(new_blobs, ctx->Blobs, ctx->BlobCount * sizeof(WimBlob));

        core_free(ctx, ctx->Blobs);
        ctx->Blobs = new_blobs;
        ctx->BlobCapacity = new_cap;
    }

    ctx->Blobs[ctx->BlobCount++] = *blob;
    return 0;
}

static int
core_find_blob(
    const WimCoreCtx* ctx,
    const uint8_t sha1[20])
{
    size_t i;

    if (!ctx || !sha1)
        return -1;

    for (i = 0; i < ctx->BlobCount; ++i)
    {
        if (memcmp(ctx->Blobs[i].sha1.hash, sha1, 20) == 0)
            return (int)i;
    }

    return -1;
}

static int
core_read_header(
    WimCoreCtx* ctx)
{
    WimHeader header;

    if (core_read_at(ctx, 0, &header, sizeof(header)) != 0)
        return -1;

    if (memcmp(header.magic, WIM_MAGIC, WIM_MAGIC_LEN) != 0)
        return -1;
    if (header.header_size != WIM_HEADER_SIZE)
        return -1;
    if (header.version != WIM_VERSION)
        return -1;

    ctx->Header = header;
    return 0;
}

static int
core_read_resource_into(
    WimCoreCtx* ctx,
    uint64_t offset,
    uint64_t compressed_size,
    uint64_t original_size,
    uint8_t flags,
    uint8_t* out_data,
    size_t out_size,
    WimCoreProgressFn progress,
    void* progress_context,
    const char* current_path)
{
    const uint8_t* resource_base;
    uint8_t* resource_copy = NULL;
    uint8_t* resource_owned = NULL;
    uint32_t chunk_size;
    uint64_t num_chunks;
    int use_64bit;
    size_t entry_size;
    size_t table_size;
    const uint8_t* table_buf;
    const uint8_t* comp_data;
    uint64_t comp_data_size;
    uint64_t prev_offset = 0;
    uint64_t out_offset = 0;
    uint64_t i;

    if (!ctx || !out_data || out_size != (size_t)original_size)
        return -1;

    if (compressed_size == 0 && original_size == 0)
        return 0;

    resource_base = core_get_pointer(ctx, offset, (size_t)compressed_size);
    if (!resource_base)
    {
        resource_copy = (uint8_t*)core_alloc(ctx, (size_t)compressed_size);
        if (!resource_copy)
            return -1;
        if (core_read_at(ctx, offset, resource_copy, (size_t)compressed_size) != 0)
        {
            core_free(ctx, resource_copy);
            return -1;
        }
        resource_base = resource_copy;
        resource_owned = resource_copy;
    }

    if ((flags & WIM_RESHDR_FLAG_COMPRESSED) == 0 || compressed_size == original_size)
    {
        memcpy(out_data, resource_base, (size_t)original_size);
        if (progress && original_size)
            progress(progress_context, original_size, current_path);
        core_free(ctx, resource_owned);
        return 0;
    }

    chunk_size = ctx->Header.chunk_size ? ctx->Header.chunk_size : WIMCORE_DEFAULT_CHUNK_SIZE;
    num_chunks = (original_size + chunk_size - 1) / chunk_size;
    if (num_chunks == 0)
    {
        core_free(ctx, resource_owned);
        return -1;
    }

    use_64bit = (original_size > 0xFFFFFFFFULL);
    entry_size = use_64bit ? 8 : 4;
    table_size = (size_t)((num_chunks - 1) * entry_size);
    if (table_size > (size_t)compressed_size)
    {
        core_free(ctx, resource_owned);
        return -1;
    }

    table_buf = resource_base;
    comp_data = resource_base + table_size;
    comp_data_size = compressed_size - table_size;

    for (i = 0; i < num_chunks; ++i)
    {
        uint64_t chunk_start = prev_offset;
        uint64_t chunk_end;
        uint64_t chunk_len;
        uint64_t remaining = original_size - out_offset;
        uint32_t this_chunk = (remaining < chunk_size) ? (uint32_t)remaining : chunk_size;

        if (i + 1 < num_chunks)
        {
            if (use_64bit)
                chunk_end = core_read_u64(table_buf + (i * entry_size));
            else
                chunk_end = core_read_u32(table_buf + (i * entry_size));
        }
        else
        {
            chunk_end = comp_data_size;
        }

        if (chunk_end < chunk_start || chunk_end > comp_data_size)
        {
            core_free(ctx, resource_owned);
            return -1;
        }

        chunk_len = chunk_end - chunk_start;
        if (chunk_len == this_chunk)
        {
            memcpy(out_data + out_offset, comp_data + chunk_start, this_chunk);
        }
        else
        {
            XpressStatus status = xpress_huff_decompress_static(
                comp_data + chunk_start,
                (uint32_t)chunk_len,
                out_data + out_offset,
                this_chunk,
                ctx->DecompressWorkspace);
            if (status != XPRESS_OK)
            {
                core_free(ctx, resource_owned);
                return -1;
            }
        }

        if (progress && this_chunk)
            progress(progress_context, this_chunk, current_path);

        out_offset += this_chunk;
        prev_offset = chunk_end;
    }

    core_free(ctx, resource_owned);
    return 0;
}

static int
core_read_resource_alloc(
    WimCoreCtx* ctx,
    uint64_t offset,
    uint64_t compressed_size,
    uint64_t original_size,
    uint8_t flags,
    uint8_t** out_data,
    size_t* out_size)
{
    uint8_t* buffer;

    if (!out_data || !out_size)
        return -1;

    *out_data = NULL;
    *out_size = 0;

    buffer = (uint8_t*)core_alloc(ctx, (size_t)original_size);
    if (!buffer)
        return -1;

    if (core_read_resource_into(ctx,
                                offset,
                                compressed_size,
                                original_size,
                                flags,
                                buffer,
                                (size_t)original_size,
                                NULL,
                                NULL,
                                NULL) != 0)
    {
        core_free(ctx, buffer);
        return -1;
    }

    *out_data = buffer;
    *out_size = (size_t)original_size;
    return 0;
}

static int
core_read_lookup_table(
    WimCoreCtx* ctx)
{
    uint64_t table_comp_size = reshdr_get_size(&ctx->Header.lookup_table);
    uint64_t table_orig_size = ctx->Header.lookup_table.original_size;
    uint8_t table_flags = reshdr_get_flags(&ctx->Header.lookup_table);
    uint8_t* table_data;
    size_t table_size;
    size_t count;
    size_t i;

    if (table_orig_size == 0)
        return 0;

    if (core_read_resource_alloc(ctx,
                                 ctx->Header.lookup_table.offset,
                                 table_comp_size,
                                 table_orig_size,
                                 table_flags,
                                 &table_data,
                                 &table_size) != 0)
    {
        return -1;
    }

    count = table_size / WIM_LOOKUP_ENTRY_SIZE;
    for (i = 0; i < count; ++i)
    {
        WimLookupEntry entry;
        WimBlob blob;

        memcpy(&entry, table_data + (i * WIM_LOOKUP_ENTRY_SIZE), sizeof(entry));
        core_zero(&blob, sizeof(blob));
        memcpy(blob.sha1.hash, entry.sha1, sizeof(blob.sha1.hash));
        blob.compressed_size = reshdr_get_size(&entry.reshdr);
        blob.offset = entry.reshdr.offset;
        blob.original_size = entry.reshdr.original_size;
        blob.ref_count = entry.ref_count;
        blob.flags = reshdr_get_flags(&entry.reshdr);

        if (core_add_blob(ctx, &blob) != 0)
        {
            core_free(ctx, table_data);
            return -1;
        }
    }

    core_free(ctx, table_data);
    return 0;
}

static const char*
core_find_tag_value(
    const char* block,
    size_t block_len,
    const char* tag,
    size_t* value_len)
{
    char open_tag[64];
    char close_tag[64];
    const char* start;
    const char* end;

    if (!block || !tag || !value_len)
        return NULL;

    memset(open_tag, 0, sizeof(open_tag));
    memset(close_tag, 0, sizeof(close_tag));
    memcpy(open_tag, "<", 1);
    memcpy(open_tag + 1, tag, strlen(tag));
    memcpy(open_tag + 1 + strlen(tag), ">", 1);
    memcpy(close_tag, "</", 2);
    memcpy(close_tag + 2, tag, strlen(tag));
    memcpy(close_tag + 2 + strlen(tag), ">", 1);

    start = strstr(block, open_tag);
    if (!start || (size_t)(start - block) >= block_len)
        return NULL;
    start += strlen(open_tag);

    end = strstr(start, close_tag);
    if (!end || (size_t)(end - block) >= block_len)
        return NULL;

    *value_len = (size_t)(end - start);
    return start;
}

static void
core_parse_filetime_xml(
    const char* block,
    size_t block_len,
    uint64_t* out_value)
{
    size_t high_len;
    size_t low_len;
    const char* high_part;
    const char* low_part;
    char buffer[32];
    size_t n;
    uint32_t high;
    uint32_t low;

    if (!block || !out_value)
        return;

    high_part = core_find_tag_value(block, block_len, "HIGHPART", &high_len);
    low_part = core_find_tag_value(block, block_len, "LOWPART", &low_len);
    if (!high_part || !low_part)
        return;

    n = (high_len < sizeof(buffer) - 1) ? high_len : sizeof(buffer) - 1;
    memcpy(buffer, high_part, n);
    buffer[n] = '\0';
    high = (uint32_t)strtoul(buffer, NULL, 0);

    n = (low_len < sizeof(buffer) - 1) ? low_len : sizeof(buffer) - 1;
    memcpy(buffer, low_part, n);
    buffer[n] = '\0';
    low = (uint32_t)strtoul(buffer, NULL, 0);

    *out_value = ((uint64_t)high << 32) | low;
}

static int
core_read_xml_data(
    WimCoreCtx* ctx)
{
    uint64_t xml_comp_size = reshdr_get_size(&ctx->Header.xml_data);
    uint64_t xml_orig_size = ctx->Header.xml_data.original_size;
    uint8_t xml_flags = reshdr_get_flags(&ctx->Header.xml_data);
    const uint8_t* xml_source;
    size_t xml_utf16_len;
    size_t xml_offset = 0;

    if (xml_comp_size == 0)
        return 0;

    if (core_read_resource_alloc(ctx,
                                 ctx->Header.xml_data.offset,
                                 xml_comp_size,
                                 xml_orig_size ? xml_orig_size : xml_comp_size,
                                 xml_flags,
                                 &ctx->XmlRaw,
                                 &ctx->XmlRawSize) != 0)
    {
        return -1;
    }

    xml_source = ctx->XmlRaw;
    if (ctx->XmlRawSize >= 2 && xml_source[0] == 0xFF && xml_source[1] == 0xFE)
        xml_offset = 2;

    xml_utf16_len = (ctx->XmlRawSize - xml_offset) / sizeof(uint16_t);
    if (xml_utf16_len != 0)
    {
        ctx->XmlUtf8 = NULL;
        if (core_utf16le_to_utf8(ctx,
                                 (const uint16_t*)(xml_source + xml_offset),
                                 xml_utf16_len,
                                 &ctx->XmlUtf8) != 0)
        {
            return -1;
        }
    }

    return 0;
}

static int
core_parse_xml_image_infos(
    WimCoreCtx* ctx)
{
    const char* xml;
    const char* pos;
    const char* scan;
    size_t image_count = 0;
    size_t index = 0;

    if (!ctx || !ctx->XmlUtf8)
        return 0;

    xml = ctx->XmlUtf8;
    scan = xml;
    while ((scan = strstr(scan, "<IMAGE ")) != NULL)
    {
        ++image_count;
        scan += 7;
    }

    if (image_count == 0)
        return 0;

    ctx->ImageInfos = (WimImageInfo*)core_alloc(ctx, image_count * sizeof(WimImageInfo));
    if (!ctx->ImageInfos)
        return -1;
    core_zero(ctx->ImageInfos, image_count * sizeof(WimImageInfo));

    pos = xml;
    while ((pos = strstr(pos, "<IMAGE ")) != NULL && index < image_count)
    {
        const char* end = strstr(pos, "</IMAGE>");
        size_t block_len;
        WimImageInfo* info;
        size_t value_len;
        const char* value;

        if (!end)
            break;

        block_len = (size_t)(end - pos + 8);
        info = &ctx->ImageInfos[index];

        value = core_find_tag_value(pos, block_len, "DIRCOUNT", &value_len);
        if (value)
            info->dir_count = (uint32_t)strtoul(value, NULL, 10);

        value = core_find_tag_value(pos, block_len, "FILECOUNT", &value_len);
        if (value)
            info->file_count = (uint32_t)strtoul(value, NULL, 10);

        value = core_find_tag_value(pos, block_len, "TOTALBYTES", &value_len);
        if (value)
            info->total_bytes = strtoull(value, NULL, 10);

        value = core_find_tag_value(pos, block_len, "NAME", &value_len);
        if (value)
        {
            size_t copy_len = (value_len < sizeof(info->name) - 1) ? value_len : sizeof(info->name) - 1;
            memcpy(info->name, value, copy_len);
            info->name[copy_len] = '\0';
        }

        value = core_find_tag_value(pos, block_len, "DESCRIPTION", &value_len);
        if (value)
        {
            size_t copy_len = (value_len < sizeof(info->description) - 1) ? value_len : sizeof(info->description) - 1;
            memcpy(info->description, value, copy_len);
            info->description[copy_len] = '\0';
        }

        value = core_find_tag_value(pos, block_len, "CREATIONTIME", &value_len);
        if (value)
            core_parse_filetime_xml(value, value_len, &info->creation_time);

        value = core_find_tag_value(pos, block_len, "LASTMODIFICATIONTIME", &value_len);
        if (value)
            core_parse_filetime_xml(value, value_len, &info->modification_time);

        ++index;
        pos = end + 8;
    }

    ctx->ImageInfoCount = index;
    return 0;
}

static int
core_deserialize_one_dentry(
    WimCoreCtx* ctx,
    const uint8_t* buffer,
    uint64_t buffer_length,
    uint64_t offset,
    WimDentry* dentry,
    uint64_t* next_offset)
{
    const uint8_t* p;
    uint64_t entry_length;
    uint16_t num_streams;
    uint16_t short_name_nbytes;
    uint16_t file_name_nbytes;
    size_t off = 8;

    if (!ctx || !buffer || !dentry || !next_offset || offset + 8 > buffer_length)
        return 0;

    entry_length = core_read_u64(buffer + offset);
    if (entry_length == 0)
    {
        *next_offset = 0;
        return 0;
    }

    if (entry_length < WIMCORE_DENTRY_FIXED_SIZE || offset + entry_length > buffer_length)
        return 0;

    core_dentry_init(dentry);
    p = buffer + offset;

    dentry->attributes = core_read_u32(p + off);      off += 4;
    dentry->security_id = (int32_t)core_read_u32(p + off); off += 4;
    dentry->subdir_offset = core_read_u64(p + off);   off += 8;
    off += 16;
    dentry->creation_time = core_read_u64(p + off);   off += 8;
    dentry->last_access_time = core_read_u64(p + off); off += 8;
    dentry->last_write_time = core_read_u64(p + off); off += 8;
    memcpy(dentry->sha1, p + off, 20);                off += 20;
    off += 4;
    off += 8;

    num_streams = core_read_u16(p + off);             off += 2;
    short_name_nbytes = core_read_u16(p + off);       off += 2;
    file_name_nbytes = core_read_u16(p + off);        off += 2;
    (void)num_streams;
    (void)short_name_nbytes;

    if (file_name_nbytes > 0 && off + file_name_nbytes <= entry_length)
    {
        size_t utf16_count = file_name_nbytes / sizeof(uint16_t);

        dentry->name_utf16 = (uint16_t*)core_alloc(ctx, utf16_count * sizeof(uint16_t));
        if (!dentry->name_utf16)
            return -1;

        memcpy(dentry->name_utf16, p + off, file_name_nbytes);
        dentry->name_utf16_len = utf16_count;
        if (core_utf16le_to_utf8(ctx, dentry->name_utf16, utf16_count, &dentry->name_utf8) != 0)
            return -1;
    }

    *next_offset = offset + entry_length;
    return 1;
}

static int
core_deserialize_dentry_tree(
    WimCoreCtx* ctx,
    const uint8_t* buffer,
    uint64_t length,
    WimDentry* root)
{
    uint32_t security_total_length;
    uint64_t dentry_start;
    uint64_t next_offset;
    WimDentry** queue;
    size_t queue_cap = 64;
    size_t queue_head = 0;
    size_t queue_tail = 0;

    if (!ctx || !buffer || !root || length < 8)
        return -1;

    security_total_length = core_read_u32(buffer);
    dentry_start = security_total_length;
    if (dentry_start % 8 != 0)
        dentry_start += 8 - (dentry_start % 8);
    if (dentry_start >= length)
        return -1;

    if (core_deserialize_one_dentry(ctx, buffer, length, dentry_start, root, &next_offset) <= 0)
        return -1;

    queue = (WimDentry**)core_alloc(ctx, queue_cap * sizeof(WimDentry*));
    if (!queue)
        return -1;

    if ((root->attributes & WIM_FILE_ATTRIBUTE_DIRECTORY) && root->subdir_offset > 0)
        queue[queue_tail++] = root;

    while (queue_head < queue_tail)
    {
        WimDentry* parent = queue[queue_head++];
        uint64_t offset = parent->subdir_offset;

        while (offset < length)
        {
            WimDentry child;
            int status;

            core_dentry_init(&child);
            status = core_deserialize_one_dentry(ctx, buffer, length, offset, &child, &next_offset);
            if (status <= 0)
                break;

            if (core_add_child(ctx, parent, &child) != 0)
            {
                core_dentry_free(ctx, &child);
                core_free(ctx, queue);
                return -1;
            }

            offset = next_offset;
        }

        if (parent->child_count != 0)
        {
            size_t i;
            for (i = 0; i < parent->child_count; ++i)
            {
                WimDentry* child = &parent->children[i];
                WimDentry** new_queue;

                if ((child->attributes & WIM_FILE_ATTRIBUTE_DIRECTORY) == 0 || child->subdir_offset == 0)
                    continue;

                if (queue_tail >= queue_cap)
                {
                    queue_cap *= 2;
                    new_queue = (WimDentry**)core_alloc(ctx, queue_cap * sizeof(WimDentry*));
                    if (!new_queue)
                    {
                        core_free(ctx, queue);
                        return -1;
                    }
                    memcpy(new_queue, queue, queue_tail * sizeof(WimDentry*));
                    core_free(ctx, queue);
                    queue = new_queue;
                }

                queue[queue_tail++] = child;
            }
        }
    }

    core_free(ctx, queue);
    return 0;
}

static int
core_populate_dentry_sizes(
    WimCoreCtx* ctx,
    WimDentry* dentry)
{
    size_t i;

    if (!ctx || !dentry)
        return -1;

    if ((dentry->attributes & WIM_FILE_ATTRIBUTE_DIRECTORY) == 0)
    {
        static const uint8_t zero_sha1[20] = { 0 };
        int blob_index;

        if (memcmp(dentry->sha1, zero_sha1, sizeof(zero_sha1)) == 0)
        {
            dentry->file_size = 0;
            return 0;
        }

        blob_index = core_find_blob(ctx, dentry->sha1);
        if (blob_index < 0)
            return -1;

        dentry->file_size = ctx->Blobs[blob_index].original_size;
        return 0;
    }

    for (i = 0; i < dentry->child_count; ++i)
    {
        if (core_populate_dentry_sizes(ctx, &dentry->children[i]) != 0)
            return -1;
    }

    return 0;
}

static int
core_read_metadata(
    WimCoreCtx* ctx,
    int image_index)
{
    WimImageData* image;
    uint8_t* buffer;
    size_t buffer_size;

    if (!ctx || image_index < 0 || image_index >= (int)ctx->ImageCount)
        return -1;

    image = &ctx->Images[image_index];
    if (image->metadata_loaded)
        return 0;

    if (core_read_resource_alloc(ctx,
                                 image->metadata_blob.offset,
                                 image->metadata_blob.compressed_size,
                                 image->metadata_blob.original_size,
                                 image->metadata_blob.flags,
                                 &buffer,
                                 &buffer_size) != 0)
    {
        return -1;
    }

    if (core_deserialize_dentry_tree(ctx, buffer, buffer_size, &image->root) != 0 ||
        core_populate_dentry_sizes(ctx, &image->root) != 0)
    {
        core_free(ctx, buffer);
        return -1;
    }

    image->metadata_loaded = 1;
    core_free(ctx, buffer);
    return 0;
}

static int
core_create_image_array(
    WimCoreCtx* ctx)
{
    size_t metadata_count = 0;
    size_t i;
    size_t index = 0;

    for (i = 0; i < ctx->BlobCount; ++i)
    {
        if (ctx->Blobs[i].flags & WIM_RESHDR_FLAG_METADATA)
            ++metadata_count;
    }

    if (metadata_count == 0)
        return 0;

    ctx->Images = (WimImageData*)core_alloc(ctx, metadata_count * sizeof(WimImageData));
    if (!ctx->Images)
        return -1;
    core_zero(ctx->Images, metadata_count * sizeof(WimImageData));

    for (i = 0; i < ctx->BlobCount; ++i)
    {
        if ((ctx->Blobs[i].flags & WIM_RESHDR_FLAG_METADATA) == 0)
            continue;

        core_dentry_init(&ctx->Images[index].root);
        ctx->Images[index].metadata_blob = ctx->Blobs[i];
        ctx->Images[index].metadata_loaded = 0;
        ++index;
    }

    ctx->ImageCount = metadata_count;
    return 0;
}

static int
core_query_stats_recursive(
    const WimDentry* dentry,
    WimCoreImageStats* stats)
{
    size_t i;

    if (!dentry || !stats)
        return -1;

    if (dentry->attributes & WIM_FILE_ATTRIBUTE_DIRECTORY)
    {
        for (i = 0; i < dentry->child_count; ++i)
        {
            const WimDentry* child = &dentry->children[i];
            if (child->attributes & WIM_FILE_ATTRIBUTE_DIRECTORY)
                ++stats->DirectoryCount;
            else
            {
                ++stats->FileCount;
                stats->FileBytes += child->file_size;
            }

            if (core_query_stats_recursive(child, stats) != 0)
                return -1;
        }
    }

    return 0;
}

void
wimcore_init(
    WimCoreCtx* ctx)
{
    if (!ctx)
        return;

    core_zero(ctx, sizeof(*ctx));
}

int
wimcore_open(
    WimCoreCtx* ctx,
    const WimCoreAllocator* allocator,
    WimCoreReadFn read_routine,
    void* read_context,
    uint64_t image_size)
{
    if (!ctx || !allocator || !allocator->Alloc || !allocator->Free || !read_routine || image_size < WIM_HEADER_SIZE)
        return -1;

    wimcore_close(ctx);
    wimcore_init(ctx);

    ctx->Allocator = *allocator;
    ctx->Read = read_routine;
    ctx->ReadContext = read_context;
    ctx->ImageSize = image_size;

    ctx->DecompressWorkspace = core_alloc(ctx, XPRESS_DECOMPRESS_WORKSPACE_SIZE);
    if (!ctx->DecompressWorkspace)
        return -1;

    if (core_read_header(ctx) != 0 ||
        core_read_lookup_table(ctx) != 0 ||
        core_read_xml_data(ctx) != 0 ||
        core_parse_xml_image_infos(ctx) != 0 ||
        core_create_image_array(ctx) != 0)
    {
        wimcore_close(ctx);
        return -1;
    }

    return 0;
}

int
wimcore_open_memory(
    WimCoreCtx* ctx,
    const WimCoreAllocator* allocator,
    const void* image_base,
    uint64_t image_size)
{
    if (!ctx || !allocator || !image_base || image_size < WIM_HEADER_SIZE)
        return -1;

    wimcore_close(ctx);
    wimcore_init(ctx);

    ctx->Allocator = *allocator;
    ctx->MemoryBase = (const uint8_t*)image_base;
    ctx->ImageSize = image_size;

    ctx->DecompressWorkspace = core_alloc(ctx, XPRESS_DECOMPRESS_WORKSPACE_SIZE);
    if (!ctx->DecompressWorkspace)
        return -1;

    if (core_read_header(ctx) != 0 ||
        core_read_lookup_table(ctx) != 0 ||
        core_read_xml_data(ctx) != 0 ||
        core_parse_xml_image_infos(ctx) != 0 ||
        core_create_image_array(ctx) != 0)
    {
        wimcore_close(ctx);
        return -1;
    }

    return 0;
}

int
wimcore_select_image(
    WimCoreCtx* ctx,
    int index)
{
    if (!ctx || index < 1 || index > (int)ctx->ImageCount)
        return -1;

    return core_read_metadata(ctx, index - 1);
}

const WimDentry*
wimcore_get_root(
    const WimCoreCtx* ctx,
    int index)
{
    if (!ctx || index < 1 || index > (int)ctx->ImageCount)
        return NULL;
    if (!ctx->Images[index - 1].metadata_loaded)
        return NULL;
    return &ctx->Images[index - 1].root;
}

int
wimcore_read_blob(
    WimCoreCtx* ctx,
    const uint8_t sha1[20],
    uint8_t** data,
    size_t* data_size)
{
    int blob_index;
    WimBlob* blob;
    uint8_t* buffer;

    if (!ctx || !sha1 || !data || !data_size)
        return -1;

    *data = NULL;
    *data_size = 0;

    blob_index = core_find_blob(ctx, sha1);
    if (blob_index < 0)
        return -1;

    blob = &ctx->Blobs[blob_index];
    buffer = (uint8_t*)core_alloc(ctx, (size_t)blob->original_size);
    if (!buffer)
        return -1;

    if (core_read_resource_into(ctx,
                                blob->offset,
                                blob->compressed_size,
                                blob->original_size,
                                blob->flags,
                                buffer,
                                (size_t)blob->original_size,
                                NULL,
                                NULL,
                                NULL) != 0)
    {
        core_free(ctx, buffer);
        return -1;
    }

    *data = buffer;
    *data_size = (size_t)blob->original_size;
    return 0;
}

int
wimcore_read_blob_into(
    WimCoreCtx* ctx,
    const uint8_t sha1[20],
    uint8_t* data,
    size_t data_size,
    WimCoreProgressFn progress,
    void* progress_context,
    const char* current_path)
{
    int blob_index;
    WimBlob* blob;

    if (!ctx || !sha1 || !data)
        return -1;

    blob_index = core_find_blob(ctx, sha1);
    if (blob_index < 0)
        return -1;

    blob = &ctx->Blobs[blob_index];
    if ((size_t)blob->original_size != data_size)
        return -1;

    return core_read_resource_into(ctx,
                                   blob->offset,
                                   blob->compressed_size,
                                   blob->original_size,
                                   blob->flags,
                                   data,
                                   data_size,
                                   progress,
                                   progress_context,
                                   current_path);
}

int
wimcore_query_image_stats(
    WimCoreCtx* ctx,
    int index,
    WimCoreImageStats* stats)
{
    const WimDentry* root;

    if (!ctx || !stats)
        return -1;

    core_zero(stats, sizeof(*stats));

    if (wimcore_select_image(ctx, index) != 0)
        return -1;

    root = wimcore_get_root(ctx, index);
    if (!root)
        return -1;

    return core_query_stats_recursive(root, stats);
}

int
wimcore_verify_integrity(
    WimCoreCtx* ctx)
{
    uint64_t integrity_size;
    uint64_t data_start;
    uint64_t data_size;
    uint8_t* integrity_data;
    size_t integrity_data_size;
    uint32_t table_size;
    uint32_t entry_count;
    uint32_t chunk_size;
    const uint8_t* stored_hashes;
    uint8_t* read_buffer;
    uint64_t remaining;
    uint64_t offset;
    uint32_t i;

    if (!ctx)
        return -1;

    integrity_size = reshdr_get_size(&ctx->Header.integrity);
    if (integrity_size == 0)
        return -1;

    if (core_read_resource_alloc(ctx,
                                 ctx->Header.integrity.offset,
                                 integrity_size,
                                 ctx->Header.integrity.original_size ? ctx->Header.integrity.original_size : integrity_size,
                                 reshdr_get_flags(&ctx->Header.integrity),
                                 &integrity_data,
                                 &integrity_data_size) != 0)
    {
        return -1;
    }

    if (integrity_data_size < 12)
    {
        core_free(ctx, integrity_data);
        return -1;
    }

    table_size = core_read_u32(integrity_data);
    entry_count = core_read_u32(integrity_data + 4);
    chunk_size = core_read_u32(integrity_data + 8);
    if (table_size < 12 || integrity_data_size < (size_t)(12 + (entry_count * 20)))
    {
        core_free(ctx, integrity_data);
        return -1;
    }

    stored_hashes = integrity_data + 12;
    read_buffer = (uint8_t*)core_alloc(ctx, chunk_size);
    if (!read_buffer)
    {
        core_free(ctx, integrity_data);
        return -1;
    }

    data_start = WIM_HEADER_SIZE;
    data_size = ctx->Header.xml_data.offset - data_start;
    remaining = data_size;
    offset = data_start;

    for (i = 0; i < entry_count; ++i)
    {
        uint32_t this_chunk = (remaining < chunk_size) ? (uint32_t)remaining : chunk_size;
        uint8_t computed[20];

        if (core_read_at(ctx, offset, read_buffer, this_chunk) != 0)
        {
            core_free(ctx, read_buffer);
            core_free(ctx, integrity_data);
            return -1;
        }

        sha1_hash(read_buffer, this_chunk, computed);
        if (memcmp(computed, stored_hashes + (i * 20), 20) != 0)
        {
            core_free(ctx, read_buffer);
            core_free(ctx, integrity_data);
            return -1;
        }

        offset += this_chunk;
        remaining -= this_chunk;
    }

    core_free(ctx, read_buffer);
    core_free(ctx, integrity_data);
    return 0;
}

void
wimcore_close(
    WimCoreCtx* ctx)
{
    size_t i;

    if (!ctx)
        return;

    core_free(ctx, ctx->Blobs);
    if (ctx->Images)
    {
        for (i = 0; i < ctx->ImageCount; ++i)
            core_dentry_free(ctx, &ctx->Images[i].root);
    }
    core_free(ctx, ctx->Images);
    core_free(ctx, ctx->ImageInfos);
    core_free(ctx, ctx->XmlUtf8);
    core_free(ctx, ctx->XmlRaw);
    core_free(ctx, ctx->DecompressWorkspace);

    core_zero(ctx, sizeof(*ctx));
}
