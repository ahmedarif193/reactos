#include "mkntfsimg.h"

#define ALIGN8(x) (((x) + 7u) & ~7u)

#define INDX_HEADER_OFFSET  24u

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

static const WCHAR I30_NAME[4] = { '$','I','3','0' };

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

static NTSTATUS
decode_runlist(PNTFSLX_ATTR_RECORD attr,
               NTFSLX_RUNLIST_ELEMENT **out_rl, uint32_t *out_count)
{
    const uint8_t *mp = (const uint8_t*)attr + attr->Data.NonResident.MappingPairsOffset;
    const uint8_t *mp_end = (const uint8_t*)attr + attr->Length;
    uint64_t cur_vcn = 0;
    int64_t prev_lcn = 0;
    NTFSLX_RUNLIST_ELEMENT *rl = NULL;
    uint32_t rl_count = 0;
    uint32_t capacity = 8;

    rl = calloc(capacity, sizeof(*rl));
    if (!rl) return STATUS_INSUFFICIENT_RESOURCES;

    while (mp < mp_end && *mp != 0)
    {
        uint8_t hdr = *mp++;
        uint32_t len_bytes = hdr & 0x0F;
        uint32_t off_bytes = (hdr >> 4) & 0x0F;
        int64_t run_len = 0, delta_lcn = 0, abs_lcn;
        uint32_t i;

        if (len_bytes == 0 || mp + len_bytes + off_bytes > mp_end)
        {
            free(rl);
            return STATUS_FILE_CORRUPT_ERROR;
        }
        for (i = 0; i < len_bytes; i++)
            run_len |= (int64_t)mp[i] << (i * 8);
        if (len_bytes < 8 && (mp[len_bytes - 1] & 0x80))
            run_len |= (int64_t)(~(uint64_t)0) << (len_bytes * 8);
        mp += len_bytes;
        if (off_bytes > 0)
        {
            for (i = 0; i < off_bytes; i++)
                delta_lcn |= (int64_t)mp[i] << (i * 8);
            if (off_bytes < 8 && (mp[off_bytes - 1] & 0x80))
                delta_lcn |= (int64_t)(~(uint64_t)0) << (off_bytes * 8);
            mp += off_bytes;
            abs_lcn = prev_lcn + delta_lcn;
            prev_lcn = abs_lcn;
        }
        else
        {
            abs_lcn = NTFSLX_LCN_HOLE;
        }

        if (rl_count == capacity)
        {
            NTFSLX_RUNLIST_ELEMENT *bigger;
            capacity *= 2;
            bigger = realloc(rl, capacity * sizeof(*rl));
            if (!bigger) { free(rl); return STATUS_INSUFFICIENT_RESOURCES; }
            rl = bigger;
        }
        rl[rl_count].Vcn = (int64_t)cur_vcn;
        rl[rl_count].Lcn = abs_lcn;
        rl[rl_count].Length = run_len;
        rl_count++;
        cur_vcn += (uint64_t)run_len;
    }

    *out_rl = rl;
    *out_count = rl_count;
    return STATUS_SUCCESS;
}

static NTSTATUS
load_bitmap(NTFSIMG_VOLUME *vol, PNTFSLX_ATTR_RECORD bm_attr,
            UCHAR **out_bits, uint32_t *out_size,
            NTFSLX_RUNLIST_ELEMENT **out_rl, uint32_t *out_rl_count)
{
    *out_bits = NULL;
    *out_size = 0;
    *out_rl = NULL;
    *out_rl_count = 0;

    if (bm_attr->NonResident == 0)
    {
        uint32_t n = bm_attr->Data.Resident.ValueLength;
        UCHAR *buf = malloc(n ? n : 1);
        if (!buf) return STATUS_INSUFFICIENT_RESOURCES;
        if (n) memcpy(buf, (UCHAR*)bm_attr + bm_attr->Data.Resident.ValueOffset, n);
        *out_bits = buf;
        *out_size = n;
        return STATUS_SUCCESS;
    }
    else
    {
        NTFSLX_RUNLIST_ELEMENT *rl = NULL;
        uint32_t rl_count = 0;
        uint32_t n = (uint32_t)bm_attr->Data.NonResident.DataSize;
        UCHAR *buf;
        NTSTATUS st;

        st = decode_runlist(bm_attr, &rl, &rl_count);
        if (!NT_SUCCESS(st)) return st;

        buf = calloc(1, n ? n : 1);
        if (!buf) { free(rl); return STATUS_INSUFFICIENT_RESOURCES; }
        if (n)
        {
            st = ntfsimg_read_mapped(vol, rl, rl_count, 0, n, buf);
            if (!NT_SUCCESS(st)) { free(buf); free(rl); return st; }
        }
        *out_bits = buf;
        *out_size = n;
        *out_rl = rl;
        *out_rl_count = rl_count;
        return STATUS_SUCCESS;
    }
}

static NTSTATUS
save_bitmap(NTFSIMG_VOLUME *vol, NTFSLX_MFT_RECORD *dir_rec, uint64_t dir_index,
            const UCHAR *bits, uint32_t size,
            const NTFSLX_RUNLIST_ELEMENT *rl, uint32_t rl_count)
{
    PNTFSLX_ATTR_RECORD bm_attr;
    bm_attr = ntfsimg_find_attr_named(dir_rec, vol->bytes_per_mft_record,
                                      NTFSLX_ATTRIBUTE_BITMAP, I30_NAME, 4);
    if (!bm_attr) return STATUS_FILE_CORRUPT_ERROR;

    if (bm_attr->NonResident == 0)
    {
        if (size > bm_attr->Data.Resident.ValueLength)
            return STATUS_BUFFER_OVERFLOW;
        memcpy((UCHAR*)bm_attr + bm_attr->Data.Resident.ValueOffset, bits, size);
        return ntfsimg_write_mft_record(vol, dir_index, dir_rec);
    }
    return ntfsimg_write_mapped(vol, rl, rl_count, 0, size, bits);
}

static int
bitmap_find_free_bit(const UCHAR *bits, uint32_t size, uint64_t max_bits, uint64_t *out_bit)
{
    uint64_t bit;
    uint64_t total = (uint64_t)size * 8;
    if (total > max_bits) total = max_bits;
    for (bit = 0; bit < total; bit++)
    {
        uint32_t byte_idx = (uint32_t)(bit >> 3);
        UCHAR mask = (UCHAR)(1u << (bit & 7));
        if ((bits[byte_idx] & mask) == 0)
        {
            *out_bit = bit;
            return 1;
        }
    }
    return 0;
}

static void
bitmap_set_bit(UCHAR *bits, uint64_t bit)
{
    bits[bit >> 3] |= (UCHAR)(1u << (bit & 7));
}

static PNTFSLX_INDEX_HEADER
indx_block_header(UCHAR *block_bytes)
{
    return (PNTFSLX_INDEX_HEADER)(block_bytes + INDX_HEADER_OFFSET);
}

static NTSTATUS
init_indx_block(UCHAR *bytes, uint32_t block_size, uint64_t vcn)
{
    PNTFSLX_INDEX_BLOCK hdr;
    PNTFSLX_INDEX_HEADER ih;
    I30_ENTRY_HEAD *end;
    USHORT usa_count;
    USHORT usa_offset;
    uint32_t entries_offset;

    memset(bytes, 0, block_size);
    hdr = (PNTFSLX_INDEX_BLOCK)bytes;
    hdr->Magic = NTFSLX_RECORD_MAGIC_INDX;
    usa_count = (USHORT)((block_size / 512) + 1);
    usa_offset = (USHORT)(INDX_HEADER_OFFSET + sizeof(NTFSLX_INDEX_HEADER));
    hdr->UsaOffset = usa_offset;
    hdr->UsaCount = usa_count;
    hdr->Lsn = 0;
    hdr->IndexBlockVcn = vcn;

    ih = indx_block_header(bytes);
    entries_offset = (uint32_t)ALIGN8(sizeof(NTFSLX_INDEX_HEADER) +
                                      (uint32_t)usa_count * sizeof(USHORT));
    ih->EntriesOffset = entries_offset;
    ih->AllocatedSize = block_size - INDX_HEADER_OFFSET;
    ih->Flags = 0;

    end = (I30_ENTRY_HEAD*)((UCHAR*)ih + entries_offset);
    end->IndexedFile = 0;
    end->Length = (USHORT)sizeof(I30_ENTRY_HEAD);
    end->KeyLength = 0;
    end->Flags = NTFSLX_INDEX_ENTRY_END;
    end->Reserved = 0;
    ih->IndexLength = entries_offset + (uint32_t)sizeof(I30_ENTRY_HEAD);
    return STATUS_SUCCESS;
}

static int
entry_cmp_to_key(const UCHAR *entry, const WCHAR *key, uint32_t key_len)
{
    const I30_ENTRY_HEAD *eh = (const I30_ENTRY_HEAD*)entry;
    const NTFSLX_FILE_NAME_ATTRIBUTE *fn;
    const WCHAR *nm;
    if (eh->Flags & NTFSLX_INDEX_ENTRY_END) return 1;
    if (eh->KeyLength < (USHORT)sizeof(NTFSLX_FILE_NAME_ATTRIBUTE)) return 1;
    fn = (const NTFSLX_FILE_NAME_ATTRIBUTE*)(entry + sizeof(I30_ENTRY_HEAD));
    nm = (const WCHAR*)((const UCHAR*)fn + sizeof(NTFSLX_FILE_NAME_ATTRIBUTE));
    return cmp_fname(nm, fn->FileNameLength, key, key_len);
}

static ULONGLONG
entry_child_vcn(const UCHAR *entry)
{
    const I30_ENTRY_HEAD *eh = (const I30_ENTRY_HEAD*)entry;
    const UCHAR *vcn_ptr = entry + eh->Length - sizeof(ULONGLONG);
    ULONGLONG v;
    memcpy(&v, vcn_ptr, sizeof(v));
    return v;
}

typedef struct _PATH_LEVEL
{
    int is_root;
    NTFSIMG_INDX indx;
    uint32_t descend_off;
} PATH_LEVEL;

static NTSTATUS
leaf_insert_inplace(PNTFSLX_INDEX_HEADER ih,
                    const UCHAR *new_entry, uint32_t new_entry_len,
                    const WCHAR *key_name, uint32_t key_name_len,
                    int *out_inserted)
{
    UCHAR *base = (UCHAR*)ih;
    uint32_t off = ih->EntriesOffset;
    uint32_t end = ih->IndexLength;
    uint32_t insert_off = off;

    *out_inserted = 0;

    while (off + sizeof(I30_ENTRY_HEAD) <= end)
    {
        I30_ENTRY_HEAD *eh = (I30_ENTRY_HEAD*)(base + off);
        int cmp;
        if (eh->Length == 0) return STATUS_FILE_CORRUPT_ERROR;
        if (eh->Flags & NTFSLX_INDEX_ENTRY_END)
        {
            insert_off = off;
            break;
        }
        cmp = entry_cmp_to_key(base + off, key_name, key_name_len);
        if (cmp == 0) return STATUS_OBJECT_NAME_COLLISION;
        if (cmp > 0)
        {
            insert_off = off;
            break;
        }
        off += eh->Length;
        insert_off = off;
    }

    if (ih->IndexLength + new_entry_len > ih->AllocatedSize)
        return STATUS_DISK_FULL;

    if (end > insert_off)
    {
        memmove(base + insert_off + new_entry_len,
                base + insert_off,
                end - insert_off);
    }
    memcpy(base + insert_off, new_entry, new_entry_len);
    ih->IndexLength += new_entry_len;
    *out_inserted = 1;
    return STATUS_SUCCESS;
}

/*
 * Split an overflowing index body (leaf or internal). Entries sorted by key;
 * first half stays in src_ih, middle entry is returned as separator (stripped
 * of any trailing child-VCN for internal nodes), second half moves to dst_ih.
 * Both blocks end up with a fresh 16-byte END entry except when is_internal=1,
 * in which case each gets a 24-byte END whose child-VCN is set appropriately:
 *   left-END.cv = middle entry's ORIGINAL child-VCN
 *   right-END.cv = ORIGINAL end-entry's child-VCN
 */
static NTSTATUS
split_index_entries(PNTFSLX_INDEX_HEADER src_ih, PNTFSLX_INDEX_HEADER dst_ih,
                    const UCHAR *new_entry, uint32_t new_entry_len,
                    const WCHAR *key_name, uint32_t key_name_len,
                    int is_internal,
                    UCHAR **out_separator, uint32_t *out_sep_len)
{
    UCHAR *src_base = (UCHAR*)src_ih;
    UCHAR *dst_base = (UCHAR*)dst_ih;
    uint32_t src_ents_off = src_ih->EntriesOffset;
    uint32_t src_end_off = src_ih->IndexLength;
    uint32_t dst_ents_off = dst_ih->EntriesOffset;
    uint32_t scan = src_ents_off;
    uint32_t count = 0;
    uint32_t i;
    int inserted_idx = -1;
    uint32_t mid;
    UCHAR **entry_ptrs = NULL;
    uint32_t *entry_lens = NULL;
    UCHAR *tmp_src = NULL;
    UCHAR *tmp_dst = NULL;
    UCHAR *new_copy = NULL;
    uint32_t *offs = NULL;
    uint32_t offs_cap = 64;
    ULONGLONG orig_end_cv = 0;
    int orig_end_has_node = 0;
    I30_ENTRY_HEAD *orig_end_eh;

    *out_separator = NULL;
    *out_sep_len = 0;

    offs = malloc(offs_cap * sizeof(uint32_t));
    if (!offs) return STATUS_INSUFFICIENT_RESOURCES;

    while (scan + sizeof(I30_ENTRY_HEAD) <= src_end_off)
    {
        I30_ENTRY_HEAD *eh = (I30_ENTRY_HEAD*)(src_base + scan);
        int cmp;
        if (eh->Length == 0) { free(offs); return STATUS_FILE_CORRUPT_ERROR; }
        if (eh->Flags & NTFSLX_INDEX_ENTRY_END) break;
        if (count == offs_cap)
        {
            uint32_t *bigger;
            offs_cap *= 2;
            bigger = realloc(offs, offs_cap * sizeof(uint32_t));
            if (!bigger) { free(offs); return STATUS_INSUFFICIENT_RESOURCES; }
            offs = bigger;
        }
        if (inserted_idx < 0)
        {
            cmp = entry_cmp_to_key(src_base + scan, key_name, key_name_len);
            if (cmp == 0) { free(offs); return STATUS_OBJECT_NAME_COLLISION; }
            if (cmp > 0) inserted_idx = (int)count;
        }
        offs[count++] = scan;
        scan += eh->Length;
    }
    if (inserted_idx < 0) inserted_idx = (int)count;

    if (scan >= src_end_off) { free(offs); return STATUS_FILE_CORRUPT_ERROR; }
    orig_end_eh = (I30_ENTRY_HEAD*)(src_base + scan);
    if (!(orig_end_eh->Flags & NTFSLX_INDEX_ENTRY_END))
    { free(offs); return STATUS_FILE_CORRUPT_ERROR; }
    if (orig_end_eh->Flags & NTFSLX_INDEX_ENTRY_NODE)
    {
        orig_end_has_node = 1;
        orig_end_cv = entry_child_vcn(src_base + scan);
    }

    new_copy = malloc(new_entry_len);
    if (!new_copy) { free(offs); return STATUS_INSUFFICIENT_RESOURCES; }
    memcpy(new_copy, new_entry, new_entry_len);

    entry_ptrs = malloc((count + 1) * sizeof(UCHAR*));
    entry_lens = malloc((count + 1) * sizeof(uint32_t));
    if (!entry_ptrs || !entry_lens)
    {
        free(entry_ptrs); free(entry_lens);
        free(new_copy); free(offs);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    {
        uint32_t ins = (uint32_t)inserted_idx;
        uint32_t out = 0;
        for (i = 0; i < count; i++)
        {
            if (i == ins)
            {
                entry_ptrs[out] = new_copy;
                entry_lens[out] = new_entry_len;
                out++;
            }
            entry_ptrs[out] = src_base + offs[i];
            entry_lens[out] = ((I30_ENTRY_HEAD*)(src_base + offs[i]))->Length;
            out++;
        }
        if (ins == count)
        {
            entry_ptrs[out] = new_copy;
            entry_lens[out] = new_entry_len;
        }
    }

    mid = (count + 1) / 2;
    if (mid >= count + 1) mid = count;

    {
        const I30_ENTRY_HEAD *mid_head = (const I30_ENTRY_HEAD*)entry_ptrs[mid];
        uint32_t sep_body = entry_lens[mid];
        if (is_internal && (mid_head->Flags & NTFSLX_INDEX_ENTRY_NODE))
            sep_body -= (uint32_t)sizeof(ULONGLONG);
        {
            UCHAR *sep_copy = malloc(sep_body);
            if (!sep_copy)
            {
                free(entry_ptrs); free(entry_lens);
                free(new_copy); free(offs);
                return STATUS_INSUFFICIENT_RESOURCES;
            }
            memcpy(sep_copy, entry_ptrs[mid], sep_body);
            {
                I30_ENTRY_HEAD *sep_h = (I30_ENTRY_HEAD*)sep_copy;
                sep_h->Length = (USHORT)sep_body;
                sep_h->Flags = (USHORT)(sep_h->Flags & ~NTFSLX_INDEX_ENTRY_NODE);
            }
            *out_separator = sep_copy;
            *out_sep_len = sep_body;
        }
    }

    {
        uint32_t end_entry_size = is_internal ? (uint32_t)(sizeof(I30_ENTRY_HEAD) + sizeof(ULONGLONG))
                                              : (uint32_t)sizeof(I30_ENTRY_HEAD);
        uint32_t left_bytes = src_ents_off + end_entry_size;
        uint32_t right_bytes = dst_ents_off + end_entry_size;
        for (i = 0; i < mid; i++) left_bytes += entry_lens[i];
        for (i = mid + 1; i < count + 1; i++) right_bytes += entry_lens[i];

        if (left_bytes > src_ih->AllocatedSize ||
            right_bytes > dst_ih->AllocatedSize)
        {
            free(*out_separator); *out_separator = NULL;
            free(entry_ptrs); free(entry_lens);
            free(new_copy); free(offs);
            return STATUS_DISK_FULL;
        }
    }

    tmp_src = calloc(1, src_ih->AllocatedSize);
    tmp_dst = calloc(1, dst_ih->AllocatedSize);
    if (!tmp_src || !tmp_dst)
    {
        free(tmp_src); free(tmp_dst);
        free(*out_separator); *out_separator = NULL;
        free(entry_ptrs); free(entry_lens);
        free(new_copy); free(offs);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    {
        uint32_t w = src_ents_off;
        ULONGLONG left_end_cv = 0;
        I30_ENTRY_HEAD *term;
        for (i = 0; i < mid; i++)
        {
            memcpy(tmp_src + w, entry_ptrs[i], entry_lens[i]);
            w += entry_lens[i];
        }
        if (is_internal)
        {
            const I30_ENTRY_HEAD *mid_h = (const I30_ENTRY_HEAD*)entry_ptrs[mid];
            if (mid_h->Flags & NTFSLX_INDEX_ENTRY_NODE)
                left_end_cv = entry_child_vcn(entry_ptrs[mid]);
            else
                left_end_cv = 0;
        }
        term = (I30_ENTRY_HEAD*)(tmp_src + w);
        term->IndexedFile = 0;
        term->KeyLength = 0;
        term->Reserved = 0;
        if (is_internal)
        {
            term->Length = (USHORT)(sizeof(I30_ENTRY_HEAD) + sizeof(ULONGLONG));
            term->Flags = NTFSLX_INDEX_ENTRY_END | NTFSLX_INDEX_ENTRY_NODE;
            memcpy(tmp_src + w + sizeof(I30_ENTRY_HEAD), &left_end_cv, sizeof(ULONGLONG));
            w += (uint32_t)(sizeof(I30_ENTRY_HEAD) + sizeof(ULONGLONG));
        }
        else
        {
            term->Length = (USHORT)sizeof(I30_ENTRY_HEAD);
            term->Flags = NTFSLX_INDEX_ENTRY_END;
            w += (uint32_t)sizeof(I30_ENTRY_HEAD);
        }
        {
            uint32_t saved_alloc = src_ih->AllocatedSize;
            UCHAR saved_flags = src_ih->Flags;
            memcpy(src_base, tmp_src, w);
            if (w < saved_alloc)
                memset(src_base + w, 0, saved_alloc - w);
            src_ih->AllocatedSize = saved_alloc;
            src_ih->IndexLength = w;
            src_ih->EntriesOffset = src_ents_off;
            src_ih->Flags = (UCHAR)(saved_flags | (is_internal ? NTFSLX_LARGE_INDEX : 0));
        }
    }

    {
        uint32_t w = dst_ents_off;
        I30_ENTRY_HEAD *term;
        for (i = mid + 1; i < count + 1; i++)
        {
            memcpy(tmp_dst + w, entry_ptrs[i], entry_lens[i]);
            w += entry_lens[i];
        }
        term = (I30_ENTRY_HEAD*)(tmp_dst + w);
        term->IndexedFile = 0;
        term->KeyLength = 0;
        term->Reserved = 0;
        if (is_internal && orig_end_has_node)
        {
            term->Length = (USHORT)(sizeof(I30_ENTRY_HEAD) + sizeof(ULONGLONG));
            term->Flags = NTFSLX_INDEX_ENTRY_END | NTFSLX_INDEX_ENTRY_NODE;
            memcpy(tmp_dst + w + sizeof(I30_ENTRY_HEAD), &orig_end_cv, sizeof(ULONGLONG));
            w += (uint32_t)(sizeof(I30_ENTRY_HEAD) + sizeof(ULONGLONG));
        }
        else
        {
            term->Length = (USHORT)sizeof(I30_ENTRY_HEAD);
            term->Flags = NTFSLX_INDEX_ENTRY_END;
            w += (uint32_t)sizeof(I30_ENTRY_HEAD);
        }
        {
            uint32_t saved_alloc = dst_ih->AllocatedSize;
            uint32_t saved_flags = dst_ih->Flags;
            memcpy(dst_base, tmp_dst, w);
            if (w < saved_alloc)
                memset(dst_base + w, 0, saved_alloc - w);
            dst_ih->AllocatedSize = saved_alloc;
            dst_ih->IndexLength = w;
            dst_ih->EntriesOffset = dst_ents_off;
            dst_ih->Flags = (UCHAR)(saved_flags | (is_internal ? NTFSLX_LARGE_INDEX : 0));
        }
    }

    free(tmp_src);
    free(tmp_dst);
    free(entry_ptrs);
    free(entry_lens);
    free(new_copy);
    free(offs);
    return STATUS_SUCCESS;
}

static NTSTATUS
insert_into_parent(PNTFSLX_INDEX_HEADER parent_ih, uint32_t parent_alloc_ceiling,
                   const UCHAR *sep, uint32_t sep_raw_len,
                   ULONGLONG right_child_vcn,
                   const WCHAR *key_name, uint32_t key_name_len)
{
    UCHAR *base = (UCHAR*)parent_ih;
    uint32_t off = parent_ih->EntriesOffset;
    uint32_t end = parent_ih->IndexLength;
    uint32_t insert_off = off;
    const I30_ENTRY_HEAD *sep_head_src = (const I30_ENTRY_HEAD*)sep;
    uint32_t sep_body_len;
    uint32_t promoted_len;
    int sep_has_node;

    while (off + sizeof(I30_ENTRY_HEAD) <= end)
    {
        I30_ENTRY_HEAD *eh = (I30_ENTRY_HEAD*)(base + off);
        int cmp;
        if (eh->Length == 0) return STATUS_FILE_CORRUPT_ERROR;
        if (eh->Flags & NTFSLX_INDEX_ENTRY_END)
        {
            insert_off = off;
            break;
        }
        cmp = entry_cmp_to_key(base + off, key_name, key_name_len);
        if (cmp == 0) return STATUS_OBJECT_NAME_COLLISION;
        if (cmp > 0)
        {
            insert_off = off;
            break;
        }
        off += eh->Length;
        insert_off = off;
    }

    sep_has_node = (sep_head_src->Flags & NTFSLX_INDEX_ENTRY_NODE) ? 1 : 0;
    if (sep_has_node)
    {
        sep_body_len = sep_head_src->Length - (uint32_t)sizeof(ULONGLONG);
    }
    else
    {
        sep_body_len = sep_head_src->Length;
    }
    (void)sep_raw_len;
    promoted_len = (uint32_t)ALIGN8(sep_body_len + sizeof(ULONGLONG));
    if (parent_ih->IndexLength + promoted_len > parent_alloc_ceiling)
        return STATUS_DISK_FULL;

    if (end > insert_off)
    {
        memmove(base + insert_off + promoted_len,
                base + insert_off,
                end - insert_off);
    }

    {
        UCHAR *dst = base + insert_off;
        I30_ENTRY_HEAD *new_head;
        memcpy(dst, sep, sep_body_len);
        memset(dst + sep_body_len, 0, promoted_len - sep_body_len);
        new_head = (I30_ENTRY_HEAD*)dst;
        new_head->Length = (USHORT)promoted_len;
        new_head->Flags = (USHORT)(new_head->Flags | NTFSLX_INDEX_ENTRY_NODE);
        memcpy(dst + promoted_len - sizeof(ULONGLONG),
               &right_child_vcn, sizeof(ULONGLONG));
    }
    parent_ih->IndexLength += promoted_len;
    parent_ih->Flags |= NTFSLX_LARGE_INDEX;
    return STATUS_SUCCESS;
}

static NTSTATUS
allocate_new_indx_block(NTFSIMG_VOLUME *vol, NTFSLX_MFT_RECORD *dir_rec,
                        uint64_t dir_index,
                        UCHAR *bitmap_bits, uint32_t bitmap_bytes,
                        const NTFSLX_RUNLIST_ELEMENT *bm_rl, uint32_t bm_rl_count,
                        PNTFSLX_ATTR_RECORD alloc_attr,
                        uint32_t clusters_per_block,
                        uint64_t *out_vcn)
{
    uint64_t free_bit;
    uint64_t max_bits;
    uint64_t total_alloc_clusters;
    NTSTATUS st;

    total_alloc_clusters = alloc_attr->Data.NonResident.AllocatedSize /
                           vol->bytes_per_cluster;
    max_bits = total_alloc_clusters / clusters_per_block;

    if (!bitmap_find_free_bit(bitmap_bits, bitmap_bytes, max_bits, &free_bit))
        return STATUS_DISK_FULL;

    bitmap_set_bit(bitmap_bits, free_bit);
    st = save_bitmap(vol, dir_rec, dir_index, bitmap_bits, bitmap_bytes, bm_rl, bm_rl_count);
    if (!NT_SUCCESS(st)) return st;

    *out_vcn = free_bit * clusters_per_block;
    return STATUS_SUCCESS;
}

NTSTATUS
ntfsimg_index_alloc_insert(NTFSIMG_VOLUME *vol, uint64_t dir_mft_index,
                           const UCHAR *new_entry, uint32_t new_entry_len,
                           const WCHAR *key_name, uint32_t key_name_len)
{
    NTFSLX_MFT_RECORD *dir_rec = NULL;
    PNTFSLX_ATTR_RECORD ir_attr;
    PNTFSLX_ATTR_RECORD alloc_attr;
    PNTFSLX_ATTR_RECORD bm_attr;
    NTFSLX_INDEX_ROOT *ir;
    NTFSLX_RUNLIST_ELEMENT *alloc_rl = NULL;
    uint32_t alloc_rl_count = 0;
    NTFSLX_RUNLIST_ELEMENT *bm_rl = NULL;
    uint32_t bm_rl_count = 0;
    UCHAR *bitmap_bits = NULL;
    uint32_t bitmap_bytes = 0;
    uint32_t rec_size = vol->bytes_per_mft_record;
    uint32_t block_size;
    uint32_t clusters_per_block;
    PATH_LEVEL *path = NULL;
    uint32_t path_depth = 0;
    uint32_t path_cap = 8;
    PNTFSLX_INDEX_HEADER cur_ih;
    NTSTATUS st;
    int inserted;
    uint32_t i;
    UCHAR *sep_bytes = NULL;
    uint32_t sep_len = 0;
    ULONGLONG sep_right_vcn = 0;
    int need_split_up = 0;
    int mft_dirty = 0;

    if (!vol || !new_entry || new_entry_len < sizeof(I30_ENTRY_HEAD))
        return STATUS_INVALID_PARAMETER;

    dir_rec = malloc(rec_size);
    if (!dir_rec) return STATUS_INSUFFICIENT_RESOURCES;
    st = ntfsimg_read_mft_record(vol, dir_mft_index, dir_rec);
    if (!NT_SUCCESS(st)) { free(dir_rec); return st; }

    ir_attr = ntfsimg_find_attr_named(dir_rec, rec_size,
                                      NTFSLX_ATTRIBUTE_INDEX_ROOT, I30_NAME, 4);
    if (!ir_attr || ir_attr->NonResident)
    {
        free(dir_rec);
        return STATUS_NOT_FOUND;
    }
    ir = (NTFSLX_INDEX_ROOT*)((UCHAR*)ir_attr + ir_attr->Data.Resident.ValueOffset);
    if ((ir->Index.Flags & NTFSLX_LARGE_INDEX) == 0)
    {
        free(dir_rec);
        return STATUS_NOT_IMPLEMENTED;
    }

    block_size = ir->IndexBlockSize;
    if (block_size == 0) block_size = vol->bytes_per_index_record;
    clusters_per_block = block_size / vol->bytes_per_cluster;
    if (clusters_per_block == 0) clusters_per_block = 1;

    alloc_attr = ntfsimg_find_attr_named(dir_rec, rec_size,
                                         NTFSLX_ATTRIBUTE_INDEX_ALLOCATION,
                                         I30_NAME, 4);
    if (!alloc_attr || !alloc_attr->NonResident)
    {
        free(dir_rec);
        return STATUS_FILE_CORRUPT_ERROR;
    }
    bm_attr = ntfsimg_find_attr_named(dir_rec, rec_size,
                                      NTFSLX_ATTRIBUTE_BITMAP, I30_NAME, 4);
    if (!bm_attr)
    {
        free(dir_rec);
        return STATUS_FILE_CORRUPT_ERROR;
    }

    st = decode_runlist(alloc_attr, &alloc_rl, &alloc_rl_count);
    if (!NT_SUCCESS(st)) { free(dir_rec); return st; }

    st = load_bitmap(vol, bm_attr, &bitmap_bits, &bitmap_bytes, &bm_rl, &bm_rl_count);
    if (!NT_SUCCESS(st))
    {
        free(alloc_rl); free(dir_rec);
        return st;
    }

    path = calloc(path_cap, sizeof(PATH_LEVEL));
    if (!path)
    {
        free(bitmap_bits); free(bm_rl); free(alloc_rl); free(dir_rec);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Start at the root (resident inside dir MFT record). */
    cur_ih = &ir->Index;
    {
        UCHAR *base = (UCHAR*)cur_ih;
        uint32_t off = cur_ih->EntriesOffset;
        uint32_t end = cur_ih->IndexLength;
        int descend_via_node_vcn = -1;
        uint32_t descend_off = 0;

        while (off + sizeof(I30_ENTRY_HEAD) <= end)
        {
            I30_ENTRY_HEAD *eh = (I30_ENTRY_HEAD*)(base + off);
            int cmp;
            if (eh->Length == 0) { st = STATUS_FILE_CORRUPT_ERROR; goto done; }
            if (eh->Flags & NTFSLX_INDEX_ENTRY_END)
            {
                if (eh->Flags & NTFSLX_INDEX_ENTRY_NODE)
                {
                    descend_via_node_vcn = 1;
                    descend_off = off;
                }
                break;
            }
            cmp = entry_cmp_to_key(base + off, key_name, key_name_len);
            if (cmp == 0) { st = STATUS_OBJECT_NAME_COLLISION; goto done; }
            if (cmp > 0)
            {
                if (eh->Flags & NTFSLX_INDEX_ENTRY_NODE)
                {
                    descend_via_node_vcn = 1;
                    descend_off = off;
                }
                break;
            }
            off += eh->Length;
        }

        path[0].is_root = 1;
        path[0].descend_off = descend_off;
        path_depth = 1;

        if (descend_via_node_vcn < 0)
        {
            /* Root is also the leaf (shouldn't happen with LARGE_INDEX). */
            st = leaf_insert_inplace(cur_ih, new_entry, new_entry_len,
                                     key_name, key_name_len, &inserted);
            if (!NT_SUCCESS(st)) goto done;
            mft_dirty = 1;
            goto write_back;
        }
    }

    /* Descend the B-tree; record every level. */
    for (;;)
    {
        PATH_LEVEL *parent_level = &path[path_depth - 1];
        UCHAR *parent_base;
        ULONGLONG child_vcn;
        I30_ENTRY_HEAD *parent_entry;
        PNTFSLX_INDEX_HEADER parent_ih;
        PNTFSLX_INDEX_HEADER child_ih;
        UCHAR *cbase;
        uint32_t coff, cend;
        int c_descend = -1;
        uint32_t c_descend_off = 0;

        if (parent_level->is_root) parent_ih = &ir->Index;
        else parent_ih = indx_block_header(parent_level->indx.bytes);

        parent_base = (UCHAR*)parent_ih;
        parent_entry = (I30_ENTRY_HEAD*)(parent_base + parent_level->descend_off);
        if (!(parent_entry->Flags & NTFSLX_INDEX_ENTRY_NODE))
        {
            st = STATUS_INTERNAL_ERROR;
            goto done;
        }
        child_vcn = entry_child_vcn(parent_base + parent_level->descend_off);

        if (path_depth == path_cap)
        {
            PATH_LEVEL *bigger;
            path_cap *= 2;
            bigger = realloc(path, path_cap * sizeof(PATH_LEVEL));
            if (!bigger) { st = STATUS_INSUFFICIENT_RESOURCES; goto done; }
            memset(bigger + path_depth, 0, (path_cap - path_depth) * sizeof(PATH_LEVEL));
            path = bigger;
        }

        memset(&path[path_depth], 0, sizeof(PATH_LEVEL));
        path[path_depth].is_root = 0;
        st = ntfsimg_indx_read(vol, alloc_rl, alloc_rl_count, block_size,
                               child_vcn, &path[path_depth].indx);
        if (!NT_SUCCESS(st)) goto done;
        path_depth++;

        child_ih = indx_block_header(path[path_depth - 1].indx.bytes);
        cbase = (UCHAR*)child_ih;
        coff = child_ih->EntriesOffset;
        cend = child_ih->IndexLength;
        while (coff + sizeof(I30_ENTRY_HEAD) <= cend)
        {
            I30_ENTRY_HEAD *eh = (I30_ENTRY_HEAD*)(cbase + coff);
            int cmp;
            if (eh->Length == 0) { st = STATUS_FILE_CORRUPT_ERROR; goto done; }
            if (eh->Flags & NTFSLX_INDEX_ENTRY_END)
            {
                if (eh->Flags & NTFSLX_INDEX_ENTRY_NODE)
                {
                    c_descend = 1;
                    c_descend_off = coff;
                }
                break;
            }
            cmp = entry_cmp_to_key(cbase + coff, key_name, key_name_len);
            if (cmp == 0) { st = STATUS_OBJECT_NAME_COLLISION; goto done; }
            if (cmp > 0)
            {
                if (eh->Flags & NTFSLX_INDEX_ENTRY_NODE)
                {
                    c_descend = 1;
                    c_descend_off = coff;
                }
                break;
            }
            coff += eh->Length;
        }

        path[path_depth - 1].descend_off = c_descend_off;

        if (c_descend < 0)
        {
            /* Reached a leaf. Try an in-place insert first. */
            cur_ih = child_ih;
            st = leaf_insert_inplace(cur_ih, new_entry, new_entry_len,
                                     key_name, key_name_len, &inserted);
            if (NT_SUCCESS(st))
            {
                path[path_depth - 1].indx.dirty = 1;
                goto write_back;
            }
            if (st != STATUS_DISK_FULL) goto done;

            /* Leaf split needed. */
            {
                NTFSIMG_INDX new_indx;
                uint64_t new_vcn = 0;
                PNTFSLX_INDEX_HEADER new_ih;

                memset(&new_indx, 0, sizeof(new_indx));
                st = allocate_new_indx_block(vol, dir_rec, dir_mft_index,
                                             bitmap_bits, bitmap_bytes,
                                             bm_rl, bm_rl_count,
                                             alloc_attr, clusters_per_block,
                                             &new_vcn);
                if (!NT_SUCCESS(st)) goto done;

                new_indx.bytes = calloc(1, block_size);
                if (!new_indx.bytes) { st = STATUS_INSUFFICIENT_RESOURCES; goto done; }
                new_indx.block_size = block_size;
                new_indx.vcn = new_vcn;
                new_indx.dirty = 1;
                init_indx_block(new_indx.bytes, block_size, new_vcn);
                new_ih = indx_block_header(new_indx.bytes);

                st = split_index_entries(cur_ih, new_ih,
                                         new_entry, new_entry_len,
                                         key_name, key_name_len, 0,
                                         &sep_bytes, &sep_len);
                if (!NT_SUCCESS(st))
                {
                    free(new_indx.bytes);
                    goto done;
                }
                st = ntfsimg_indx_write(vol, alloc_rl, alloc_rl_count, &new_indx);
                free(new_indx.bytes);
                if (!NT_SUCCESS(st)) goto done;

                sep_right_vcn = new_vcn;
                path[path_depth - 1].indx.dirty = 1;
                need_split_up = 1;
                break;
            }
        }
    }

    if (need_split_up)
    {
        /* Propagate separator up through parents. */
        while (sep_bytes && path_depth > 0)
        {
            PATH_LEVEL *lvl = &path[path_depth - 1];
            PNTFSLX_INDEX_HEADER lvl_ih;
            uint32_t ceiling;
            NTSTATUS ins_st;

            if (lvl->is_root)
            {
                lvl_ih = &ir->Index;
                ceiling = lvl_ih->AllocatedSize;
                ins_st = insert_into_parent(lvl_ih, ceiling,
                                            sep_bytes, sep_len,
                                            sep_right_vcn,
                                            key_name, key_name_len);
                if (ins_st == STATUS_DISK_FULL)
                {
                    fprintf(stderr, "mkntfsimg: root INDEX_ROOT spill after B-tree overflow not implemented\n");
                    st = STATUS_BUFFER_OVERFLOW;
                    goto done;
                }
                if (!NT_SUCCESS(ins_st)) { st = ins_st; goto done; }
                free(sep_bytes); sep_bytes = NULL; sep_len = 0;
                mft_dirty = 1;
                break;
            }
            else
            {
                lvl_ih = indx_block_header(lvl->indx.bytes);
                ceiling = lvl_ih->AllocatedSize;
                ins_st = insert_into_parent(lvl_ih, ceiling,
                                            sep_bytes, sep_len,
                                            sep_right_vcn,
                                            key_name, key_name_len);
                if (NT_SUCCESS(ins_st))
                {
                    lvl->indx.dirty = 1;
                    free(sep_bytes); sep_bytes = NULL; sep_len = 0;
                    break;
                }
                if (ins_st != STATUS_DISK_FULL) { st = ins_st; goto done; }

                /* Internal-node split. */
                {
                    NTFSIMG_INDX new_indx;
                    uint64_t new_vcn = 0;
                    PNTFSLX_INDEX_HEADER new_ih;
                    UCHAR *new_sep = NULL;
                    uint32_t new_sep_len = 0;

                    memset(&new_indx, 0, sizeof(new_indx));
                    st = allocate_new_indx_block(vol, dir_rec, dir_mft_index,
                                                 bitmap_bits, bitmap_bytes,
                                                 bm_rl, bm_rl_count,
                                                 alloc_attr, clusters_per_block,
                                                 &new_vcn);
                    if (!NT_SUCCESS(st)) goto done;

                    new_indx.bytes = calloc(1, block_size);
                    if (!new_indx.bytes) { st = STATUS_INSUFFICIENT_RESOURCES; goto done; }
                    new_indx.block_size = block_size;
                    new_indx.vcn = new_vcn;
                    new_indx.dirty = 1;
                    init_indx_block(new_indx.bytes, block_size, new_vcn);
                    new_ih = indx_block_header(new_indx.bytes);
                    new_ih->Flags = NTFSLX_LARGE_INDEX;

                    {
                        uint32_t promoted_len = (uint32_t)ALIGN8(sep_len + sizeof(ULONGLONG));
                        UCHAR *promoted = calloc(1, promoted_len);
                        I30_ENTRY_HEAD *ph;
                        if (!promoted)
                        {
                            free(new_indx.bytes);
                            st = STATUS_INSUFFICIENT_RESOURCES;
                            goto done;
                        }
                        memcpy(promoted, sep_bytes, sep_len);
                        ph = (I30_ENTRY_HEAD*)promoted;
                        ph->Length = (USHORT)promoted_len;
                        ph->Flags = (USHORT)(ph->Flags | NTFSLX_INDEX_ENTRY_NODE);
                        memcpy(promoted + promoted_len - sizeof(ULONGLONG),
                               &sep_right_vcn, sizeof(ULONGLONG));

                        st = split_index_entries(lvl_ih, new_ih,
                                                 promoted, promoted_len,
                                                 key_name, key_name_len, 1,
                                                 &new_sep, &new_sep_len);
                        free(promoted);
                    }
                    if (!NT_SUCCESS(st))
                    {
                        free(new_indx.bytes);
                        goto done;
                    }
                    st = ntfsimg_indx_write(vol, alloc_rl, alloc_rl_count, &new_indx);
                    free(new_indx.bytes);
                    if (!NT_SUCCESS(st)) { free(new_sep); goto done; }

                    free(sep_bytes);
                    sep_bytes = new_sep;
                    sep_len = new_sep_len;
                    sep_right_vcn = new_vcn;
                    lvl->indx.dirty = 1;
                    path_depth--;
                }
            }
        }
    }

write_back:
    /* Flush all dirty INDX blocks we touched. */
    for (i = 0; i < path_depth; i++)
    {
        if (path[i].is_root) continue;
        if (path[i].indx.dirty)
        {
            st = ntfsimg_indx_write(vol, alloc_rl, alloc_rl_count, &path[i].indx);
            if (!NT_SUCCESS(st)) goto done;
        }
    }
    if (mft_dirty)
    {
        st = ntfsimg_write_mft_record(vol, dir_mft_index, dir_rec);
        if (!NT_SUCCESS(st)) goto done;
    }
    st = STATUS_SUCCESS;

done:
    if (sep_bytes) free(sep_bytes);
    if (path)
    {
        for (i = 0; i < path_depth; i++)
        {
            if (!path[i].is_root && path[i].indx.bytes)
                ntfsimg_indx_free(&path[i].indx);
        }
        free(path);
    }
    if (bitmap_bits) free(bitmap_bits);
    if (bm_rl) free(bm_rl);
    if (alloc_rl) free(alloc_rl);
    if (dir_rec) free(dir_rec);
    return st;
}
