#include "mkntfsimg.h"

#define ALIGN8(x) (((x) + 7u) & ~7u)

static PNTFSLX_ATTR_RECORD
find_end_slot(UCHAR *buf, uint32_t rec_size, uint32_t *out_offset)
{
    NTFSLX_MFT_RECORD *rec = (NTFSLX_MFT_RECORD*)buf;
    uint32_t offset = rec->AttributesOffset;
    while (offset + sizeof(NTFSLX_ATTR_RECORD) <= rec_size)
    {
        PNTFSLX_ATTR_RECORD a = (PNTFSLX_ATTR_RECORD)(buf + offset);
        if (a->Type == NTFSLX_ATTRIBUTE_END)
        {
            *out_offset = offset;
            return a;
        }
        if (a->Length == 0 || a->Length > rec_size - offset) return NULL;
        offset += a->Length;
    }
    return NULL;
}

NTSTATUS
ntfsimg_build_file_record(UCHAR *buf, uint32_t rec_size, uint64_t record_number,
                          USHORT sequence_number, int is_directory)
{
    NTFSLX_MFT_RECORD *rec = (NTFSLX_MFT_RECORD*)buf;
    uint32_t usa_count = rec_size / 512 + 1;
    uint32_t usa_bytes = usa_count * 2;
    uint32_t attrs_offset;
    uint32_t end_offset;
    PNTFSLX_ATTR_RECORD end_attr;

    memset(buf, 0, rec_size);

    rec->Ntfs.Magic = NTFSLX_RECORD_MAGIC_FILE;
    rec->Ntfs.UsaOffset = (USHORT)sizeof(NTFSLX_MFT_RECORD);
    rec->Ntfs.UsaCount = (USHORT)usa_count;
    rec->Lsn = 0;
    rec->SequenceNumber = sequence_number ? sequence_number : 1;
    rec->LinkCount = 1;
    attrs_offset = (uint32_t)ALIGN8(sizeof(NTFSLX_MFT_RECORD) + usa_bytes);
    rec->AttributesOffset = (USHORT)attrs_offset;
    rec->Flags = NTFSLX_MFT_RECORD_IN_USE | (is_directory ? NTFSLX_MFT_RECORD_IS_DIRECTORY : 0);
    rec->BytesInUse = attrs_offset;
    rec->BytesAllocated = rec_size;
    rec->BaseMftRecord = 0;
    rec->NextAttributeInstance = 0;
    rec->MftRecordNumber = (ULONG)record_number;

    end_offset = attrs_offset;
    end_attr = (PNTFSLX_ATTR_RECORD)(buf + end_offset);
    end_attr->Type = NTFSLX_ATTRIBUTE_END;
    end_attr->Length = 0;

    rec->BytesInUse = end_offset + (uint32_t)sizeof(ULONG);
    return STATUS_SUCCESS;
}

NTSTATUS
ntfsimg_append_std_info(UCHAR *buf, uint32_t rec_size, uint64_t ntfs_time,
                        uint32_t file_attributes)
{
    NTFSLX_MFT_RECORD *rec = (NTFSLX_MFT_RECORD*)buf;
    uint32_t end_off;
    PNTFSLX_ATTR_RECORD end_attr = find_end_slot(buf, rec_size, &end_off);
    uint32_t hdr_size = 24;
    uint32_t value_size = (uint32_t)sizeof(NTFSLX_STANDARD_INFORMATION);
    uint32_t total = (uint32_t)ALIGN8(hdr_size + value_size);
    PNTFSLX_ATTR_RECORD attr;
    PNTFSLX_STANDARD_INFORMATION si;

    if (!end_attr) return STATUS_FILE_CORRUPT_ERROR;
    if (end_off + total + sizeof(ULONG) > rec_size) return STATUS_BUFFER_OVERFLOW;

    attr = (PNTFSLX_ATTR_RECORD)(buf + end_off);
    memset(attr, 0, total);
    attr->Type = NTFSLX_ATTRIBUTE_STANDARD_INFORMATION;
    attr->Length = total;
    attr->NonResident = 0;
    attr->NameLength = 0;
    attr->NameOffset = 0;
    attr->Flags = 0;
    attr->Instance = rec->NextAttributeInstance++;
    attr->Data.Resident.ValueLength = value_size;
    attr->Data.Resident.ValueOffset = (USHORT)hdr_size;
    attr->Data.Resident.Flags = 0;

    si = (PNTFSLX_STANDARD_INFORMATION)((UCHAR*)attr + hdr_size);
    si->CreationTime = ntfs_time;
    si->LastDataChangeTime = ntfs_time;
    si->LastMftChangeTime = ntfs_time;
    si->LastAccessTime = ntfs_time;
    si->FileAttributes = file_attributes;

    {
        PNTFSLX_ATTR_RECORD new_end = (PNTFSLX_ATTR_RECORD)((UCHAR*)attr + total);
        new_end->Type = NTFSLX_ATTRIBUTE_END;
        new_end->Length = 0;
    }
    rec->BytesInUse = end_off + total + (uint32_t)sizeof(ULONG);
    return STATUS_SUCCESS;
}

NTSTATUS
ntfsimg_append_file_name(UCHAR *buf, uint32_t rec_size, uint64_t parent_ref,
                         uint64_t ntfs_time, uint32_t file_attributes,
                         uint64_t data_size, const WCHAR *name, uint32_t name_len,
                         UCHAR name_type)
{
    NTFSLX_MFT_RECORD *rec = (NTFSLX_MFT_RECORD*)buf;
    uint32_t end_off;
    PNTFSLX_ATTR_RECORD end_attr = find_end_slot(buf, rec_size, &end_off);
    uint32_t hdr_size = 24;
    uint32_t name_bytes = name_len * (uint32_t)sizeof(WCHAR);
    uint32_t value_size = (uint32_t)sizeof(NTFSLX_FILE_NAME_ATTRIBUTE) + name_bytes;
    uint32_t total = (uint32_t)ALIGN8(hdr_size + value_size);
    PNTFSLX_ATTR_RECORD attr;
    PNTFSLX_FILE_NAME_ATTRIBUTE fn;

    if (!end_attr) return STATUS_FILE_CORRUPT_ERROR;
    if (end_off + total + sizeof(ULONG) > rec_size) return STATUS_BUFFER_OVERFLOW;
    if (name_len > 255) return STATUS_INVALID_PARAMETER;

    attr = (PNTFSLX_ATTR_RECORD)(buf + end_off);
    memset(attr, 0, total);
    attr->Type = NTFSLX_ATTRIBUTE_FILE_NAME;
    attr->Length = total;
    attr->NonResident = 0;
    attr->NameLength = 0;
    attr->NameOffset = 0;
    attr->Flags = 0;
    attr->Instance = rec->NextAttributeInstance++;
    attr->Data.Resident.ValueLength = value_size;
    attr->Data.Resident.ValueOffset = (USHORT)hdr_size;
    attr->Data.Resident.Flags = NTFSLX_RESIDENT_ATTR_IS_INDEXED;

    fn = (PNTFSLX_FILE_NAME_ATTRIBUTE)((UCHAR*)attr + hdr_size);
    fn->ParentDirectory = parent_ref;
    fn->CreationTime = ntfs_time;
    fn->LastDataChangeTime = ntfs_time;
    fn->LastMftChangeTime = ntfs_time;
    fn->LastAccessTime = ntfs_time;
    fn->AllocatedSize = (data_size + 7) & ~7ULL;
    fn->DataSize = data_size;
    fn->FileAttributes = file_attributes;
    fn->Type.Ea.PackedEaSize = 0;
    fn->Type.Ea.Reserved = 0;
    fn->FileNameLength = (UCHAR)name_len;
    fn->FileNameType = name_type;
    memcpy((UCHAR*)fn + sizeof(NTFSLX_FILE_NAME_ATTRIBUTE), name, name_bytes);

    {
        PNTFSLX_ATTR_RECORD new_end = (PNTFSLX_ATTR_RECORD)((UCHAR*)attr + total);
        new_end->Type = NTFSLX_ATTRIBUTE_END;
        new_end->Length = 0;
    }
    rec->BytesInUse = end_off + total + (uint32_t)sizeof(ULONG);
    return STATUS_SUCCESS;
}

NTSTATUS
ntfsimg_append_data_resident(UCHAR *buf, uint32_t rec_size,
                             const void *data, uint32_t data_len)
{
    NTFSLX_MFT_RECORD *rec = (NTFSLX_MFT_RECORD*)buf;
    uint32_t end_off;
    PNTFSLX_ATTR_RECORD end_attr = find_end_slot(buf, rec_size, &end_off);
    uint32_t hdr_size = 24;
    uint32_t total = (uint32_t)ALIGN8(hdr_size + data_len);
    PNTFSLX_ATTR_RECORD attr;

    if (!end_attr) return STATUS_FILE_CORRUPT_ERROR;
    if (end_off + total + sizeof(ULONG) > rec_size) return STATUS_BUFFER_OVERFLOW;

    attr = (PNTFSLX_ATTR_RECORD)(buf + end_off);
    memset(attr, 0, total);
    attr->Type = NTFSLX_ATTRIBUTE_DATA;
    attr->Length = total;
    attr->NonResident = 0;
    attr->NameLength = 0;
    attr->NameOffset = 0;
    attr->Flags = 0;
    attr->Instance = rec->NextAttributeInstance++;
    attr->Data.Resident.ValueLength = data_len;
    attr->Data.Resident.ValueOffset = (USHORT)hdr_size;
    attr->Data.Resident.Flags = 0;

    if (data_len > 0)
        memcpy((UCHAR*)attr + hdr_size, data, data_len);

    {
        PNTFSLX_ATTR_RECORD new_end = (PNTFSLX_ATTR_RECORD)((UCHAR*)attr + total);
        new_end->Type = NTFSLX_ATTRIBUTE_END;
        new_end->Length = 0;
    }
    rec->BytesInUse = end_off + total + (uint32_t)sizeof(ULONG);
    return STATUS_SUCCESS;
}

NTSTATUS
ntfsimg_finalize_record(UCHAR *buf, uint32_t rec_size)
{
    (void)buf;
    (void)rec_size;
    return STATUS_SUCCESS;
}
