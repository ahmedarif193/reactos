#include "mkntfsimg.h"

#include <time.h>

PNTFSLX_ATTR_RECORD
ntfsimg_find_attr(NTFSLX_MFT_RECORD *rec, uint32_t rec_size, uint32_t type)
{
    uint8_t *base = (uint8_t*)rec;
    uint32_t offset = rec->AttributesOffset;
    PNTFSLX_ATTR_RECORD attr;

    while (offset + sizeof(NTFSLX_ATTR_RECORD) <= rec_size)
    {
        attr = (PNTFSLX_ATTR_RECORD)(base + offset);
        if (attr->Type == NTFSLX_ATTRIBUTE_END) return NULL;
        if (attr->Length == 0 || attr->Length > rec_size - offset) return NULL;
        if (attr->Type == type && attr->NameLength == 0) return attr;
        offset += attr->Length;
    }
    return NULL;
}

PNTFSLX_ATTR_RECORD
ntfsimg_find_attr_named(NTFSLX_MFT_RECORD *rec, uint32_t rec_size,
                        uint32_t type, const WCHAR *name, uint32_t name_len)
{
    uint8_t *base = (uint8_t*)rec;
    uint32_t offset = rec->AttributesOffset;
    PNTFSLX_ATTR_RECORD attr;

    while (offset + sizeof(NTFSLX_ATTR_RECORD) <= rec_size)
    {
        attr = (PNTFSLX_ATTR_RECORD)(base + offset);
        if (attr->Type == NTFSLX_ATTRIBUTE_END) return NULL;
        if (attr->Length == 0 || attr->Length > rec_size - offset) return NULL;
        if (attr->Type == type && attr->NameLength == name_len)
        {
            if (name_len == 0)
                return attr;
            const WCHAR *attr_name = (const WCHAR*)(base + offset + attr->NameOffset);
            if (memcmp(attr_name, name, name_len * sizeof(WCHAR)) == 0)
                return attr;
        }
        offset += attr->Length;
    }
    return NULL;
}

uint64_t
ntfsimg_current_ntfs_time(void)
{
    time_t now = time(NULL);
    return ((uint64_t)now + 11644473600ULL) * 10000000ULL;
}

int
ntfsimg_utf8_to_wchar(const char *utf8, uint32_t utf8_bytes,
                      WCHAR *out_wchar, uint32_t out_capacity, uint32_t *out_len)
{
    uint32_t i = 0, j = 0;
    while (i < utf8_bytes)
    {
        uint32_t cp;
        uint8_t b = (uint8_t)utf8[i];
        if ((b & 0x80) == 0)
        {
            cp = b;
            i += 1;
        }
        else if ((b & 0xE0) == 0xC0 && i + 1 < utf8_bytes)
        {
            cp = ((b & 0x1F) << 6) | ((uint8_t)utf8[i+1] & 0x3F);
            i += 2;
        }
        else if ((b & 0xF0) == 0xE0 && i + 2 < utf8_bytes)
        {
            cp = ((b & 0x0F) << 12) | (((uint8_t)utf8[i+1] & 0x3F) << 6) |
                 ((uint8_t)utf8[i+2] & 0x3F);
            i += 3;
        }
        else
        {
            return -1;
        }
        if (cp > 0xFFFF) return -1;
        if (j >= out_capacity) return -1;
        out_wchar[j++] = (WCHAR)cp;
    }
    *out_len = j;
    return 0;
}
