#include "mkntfsimg.h"

static NTSTATUS
indx_apply_post_read_fixup(NTFSLX_RECORD_HEADER *record, uint32_t size)
{
    USHORT usa_count = record->UsaCount;
    USHORT usa_offset = record->UsaOffset;
    USHORT usn;
    USHORT *usa_pos;
    USHORT *data_pos;

    if ((size & 511) != 0 || usa_count == 0) return STATUS_FILE_CORRUPT_ERROR;
    if ((uint32_t)usa_offset + (uint32_t)usa_count * 2 > size) return STATUS_FILE_CORRUPT_ERROR;
    if ((size >> 9) != (uint32_t)(usa_count - 1)) return STATUS_FILE_CORRUPT_ERROR;

    usa_pos = (USHORT*)((uint8_t*)record + usa_offset);
    usn = *usa_pos;
    data_pos = (USHORT*)record + 512 / sizeof(USHORT) - 1;
    while (--usa_count)
    {
        usa_pos++;
        if (*data_pos != usn) return STATUS_FILE_CORRUPT_ERROR;
        *data_pos = *usa_pos;
        data_pos += 512 / sizeof(USHORT);
    }
    return STATUS_SUCCESS;
}

NTSTATUS
ntfsimg_indx_read(NTFSIMG_VOLUME *vol, const NTFSLX_RUNLIST_ELEMENT *rl,
                  uint32_t rl_count, uint32_t block_size, uint64_t vcn,
                  NTFSIMG_INDX *out_block)
{
    UCHAR *bytes = NULL;
    PNTFSLX_INDEX_BLOCK header;
    NTSTATUS st;

    if (!vol || !rl || !out_block || block_size == 0) return STATUS_INVALID_PARAMETER;

    bytes = malloc(block_size);
    if (!bytes) return STATUS_INSUFFICIENT_RESOURCES;

    st = ntfsimg_read_mapped(vol, rl, rl_count,
                             vcn * vol->bytes_per_cluster, block_size, bytes);
    if (!NT_SUCCESS(st)) { free(bytes); return st; }

    header = (PNTFSLX_INDEX_BLOCK)bytes;
    if (header->Magic != NTFSLX_RECORD_MAGIC_INDX)
    {
        free(bytes);
        if (header->Magic == NTFSLX_RECORD_MAGIC_BAAD) return STATUS_FILE_CORRUPT_ERROR;
        return STATUS_NOT_FOUND;
    }

    st = indx_apply_post_read_fixup((PNTFSLX_RECORD_HEADER)bytes, block_size);
    if (!NT_SUCCESS(st)) { free(bytes); return st; }

    out_block->bytes = bytes;
    out_block->block_size = block_size;
    out_block->vcn = vcn;
    out_block->dirty = 0;
    return STATUS_SUCCESS;
}

NTSTATUS
ntfsimg_indx_write(NTFSIMG_VOLUME *vol, const NTFSLX_RUNLIST_ELEMENT *rl,
                   uint32_t rl_count, NTFSIMG_INDX *block)
{
    UCHAR *copy = NULL;
    NTSTATUS st;

    if (!vol || !rl || !block || !block->bytes || block->block_size == 0)
        return STATUS_INVALID_PARAMETER;

    copy = malloc(block->block_size);
    if (!copy) return STATUS_INSUFFICIENT_RESOURCES;
    memcpy(copy, block->bytes, block->block_size);

    st = NtfslxPreWriteMstFixup((PNTFSLX_RECORD_HEADER)copy, block->block_size);
    if (!NT_SUCCESS(st)) { free(copy); return st; }

    st = ntfsimg_write_mapped(vol, rl, rl_count,
                              block->vcn * vol->bytes_per_cluster,
                              block->block_size, copy);
    free(copy);
    if (!NT_SUCCESS(st)) return st;

    block->dirty = 0;
    return STATUS_SUCCESS;
}

void
ntfsimg_indx_free(NTFSIMG_INDX *block)
{
    if (!block) return;
    if (block->bytes) free(block->bytes);
    block->bytes = NULL;
    block->block_size = 0;
    block->vcn = 0;
    block->dirty = 0;
}
