#ifndef _MKNTFSIMG_NTFS_LAYOUT_H_
#define _MKNTFSIMG_NTFS_LAYOUT_H_

#include "compat.h"

#define NTFSLX_RECORD_MAGIC_FILE  0x454C4946UL
#define NTFSLX_RECORD_MAGIC_INDX  0x58444E49UL
#define NTFSLX_RECORD_MAGIC_BAAD  0x44414142UL
#define NTFSLX_RECORD_MAGIC_RSTR  0x52545352UL
#define NTFSLX_RECORD_MAGIC_RCRD  0x44524352UL
#define NTFSLX_RECORD_MAGIC_CHKD  0x444B4843UL
#define NTFSLX_RECORD_MAGIC_HOLE  0x454C4F48UL
#define NTFSLX_RECORD_MAGIC_EMPTY 0xFFFFFFFFUL

#define NTFSLX_FILE_MFT           0
#define NTFSLX_FILE_MFTMIRR       1
#define NTFSLX_FILE_LOGFILE       2
#define NTFSLX_FILE_VOLUME        3
#define NTFSLX_FILE_ATTRDEF       4
#define NTFSLX_FILE_ROOT          5
#define NTFSLX_FILE_BITMAP        6
#define NTFSLX_FILE_BOOT          7
#define NTFSLX_FILE_BADCLUS       8
#define NTFSLX_FILE_SECURE        9
#define NTFSLX_FILE_UPCASE       10
#define NTFSLX_FILE_EXTEND       11
#define NTFSLX_FILE_FIRST_USER   16

#define NTFSLX_ATTRIBUTE_STANDARD_INFORMATION  0x00000010UL
#define NTFSLX_ATTRIBUTE_ATTRIBUTE_LIST        0x00000020UL
#define NTFSLX_ATTRIBUTE_FILE_NAME             0x00000030UL
#define NTFSLX_ATTRIBUTE_OBJECT_ID             0x00000040UL
#define NTFSLX_ATTRIBUTE_SECURITY_DESCRIPTOR   0x00000050UL
#define NTFSLX_ATTRIBUTE_VOLUME_NAME           0x00000060UL
#define NTFSLX_ATTRIBUTE_VOLUME_INFORMATION    0x00000070UL
#define NTFSLX_ATTRIBUTE_DATA                  0x00000080UL
#define NTFSLX_ATTRIBUTE_INDEX_ROOT            0x00000090UL
#define NTFSLX_ATTRIBUTE_INDEX_ALLOCATION      0x000000A0UL
#define NTFSLX_ATTRIBUTE_BITMAP                0x000000B0UL
#define NTFSLX_ATTRIBUTE_REPARSE_POINT         0x000000C0UL
#define NTFSLX_ATTRIBUTE_EA_INFORMATION        0x000000D0UL
#define NTFSLX_ATTRIBUTE_EA                    0x000000E0UL
#define NTFSLX_ATTRIBUTE_PROPERTY_SET          0x000000F0UL
#define NTFSLX_ATTRIBUTE_LOGGED_UTILITY_STREAM 0x00000100UL
#define NTFSLX_ATTRIBUTE_FIRST_USER_DEFINED    0x00001000UL
#define NTFSLX_ATTRIBUTE_END                   0xFFFFFFFFUL

#define NTFSLX_MFT_RECORD_IN_USE        0x0001
#define NTFSLX_MFT_RECORD_IS_DIRECTORY  0x0002
#define NTFSLX_MFT_RECORD_IS_4          0x0004
#define NTFSLX_MFT_RECORD_IS_VIEW_INDEX 0x0008

#define NTFSLX_ATTR_IS_COMPRESSED     0x0001
#define NTFSLX_ATTR_COMPRESSION_MASK  0x00FF
#define NTFSLX_ATTR_IS_ENCRYPTED      0x4000
#define NTFSLX_ATTR_IS_SPARSE         0x8000
#define NTFSLX_RESIDENT_ATTR_IS_INDEXED 0x01

#define NTFSLX_COLLATION_BINARY              0x00000000UL
#define NTFSLX_COLLATION_FILE_NAME           0x00000001UL
#define NTFSLX_COLLATION_UNICODE_STRING      0x00000002UL
#define NTFSLX_COLLATION_NTOFS_ULONG         0x00000010UL
#define NTFSLX_COLLATION_NTOFS_SID           0x00000011UL
#define NTFSLX_COLLATION_NTOFS_SECURITY_HASH 0x00000012UL
#define NTFSLX_COLLATION_NTOFS_ULONGS        0x00000013UL

#define NTFSLX_FILE_NAME_POSIX          0x00
#define NTFSLX_FILE_NAME_WIN32          0x01
#define NTFSLX_FILE_NAME_DOS            0x02
#define NTFSLX_FILE_NAME_WIN32_AND_DOS  0x03

#define NTFSLX_MFT_REFERENCE_MASK 0x0000FFFFFFFFFFFFULL
#define MREF(Reference)           ((Reference) & NTFSLX_MFT_REFERENCE_MASK)
#define MSEQNO(Reference)         ((USHORT)(((Reference) >> 48) & 0xFFFF))
#define MK_MREF(m, s)             ((ULONGLONG)(((ULONGLONG)(s) << 48) | ((ULONGLONG)(m) & NTFSLX_MFT_REFERENCE_MASK)))

#define NTFSLX_LCN_HOLE          ((LONGLONG)-1)
#define NTFSLX_LCN_DELALLOC      ((LONGLONG)-2)
#define NTFSLX_LCN_RL_NOT_MAPPED ((LONGLONG)-3)
#define NTFSLX_LCN_ENOENT        ((LONGLONG)-4)
#define NTFSLX_LCN_ENOMEM        ((LONGLONG)-5)
#define NTFSLX_LCN_EIO           ((LONGLONG)-6)
#define NTFSLX_LCN_EINVAL        ((LONGLONG)-7)

#define NTFSLX_INDEX_ENTRY_NODE  0x0001
#define NTFSLX_INDEX_ENTRY_END   0x0002

#define NTFSLX_SMALL_INDEX  0x00
#define NTFSLX_LARGE_INDEX  0x01
#define NTFSLX_LEAF_NODE    0x00
#define NTFSLX_INDEX_NODE   0x01
#define NTFSLX_NODE_MASK    0x01

#define NTFSLX_VOLUME_IS_DIRTY            0x0001
#define NTFSLX_VOLUME_RESIZE_LOG_FILE     0x0002
#define NTFSLX_VOLUME_UPGRADE_ON_MOUNT    0x0004
#define NTFSLX_VOLUME_MOUNTED_ON_NT4      0x0008
#define NTFSLX_VOLUME_DELETE_USN_UNDERWAY 0x0010
#define NTFSLX_VOLUME_REPAIR_OBJECT_ID    0x0020
#define NTFSLX_VOLUME_CHKDSK_UNDERWAY     0x4000
#define NTFSLX_VOLUME_MODIFIED_BY_CHKDSK  0x8000

#define NTFSLX_MAXIMUM_FILE_NAME_LENGTH 255

#define NTFSLX_CASE_SENSITIVE 0
#define NTFSLX_IGNORE_CASE    1

#define NTFSLX_FIRST_ZONE 0
#define NTFSLX_MFT_ZONE   0
#define NTFSLX_DATA_ZONE  1
#define NTFSLX_LAST_ZONE  1

#define NTFSLX_TIME_OFFSET ((LONGLONG)(369 * 365 + 89) * 24 * 3600)

#define ROUND_UP(N, S)   ((((N) + (S) - 1) / (S)) * (S))
#define ROUND_DOWN(N, S) ((N) - ((N) % (S)))

#pragma pack(push, 1)

typedef struct _NTFSLX_BIOS_PARAMETER_BLOCK
{
    USHORT BytesPerSector;
    UCHAR  SectorsPerCluster;
    USHORT ReservedSectors;
    UCHAR  Fats;
    USHORT RootEntries;
    USHORT Sectors;
    UCHAR  MediaType;
    USHORT SectorsPerFat;
    USHORT SectorsPerTrack;
    USHORT Heads;
    ULONG  HiddenSectors;
    ULONG  LargeSectors;
} NTFSLX_BIOS_PARAMETER_BLOCK, *PNTFSLX_BIOS_PARAMETER_BLOCK;

typedef struct _NTFSLX_BOOT_SECTOR
{
    UCHAR     Jump[3];
    ULONGLONG OemId;
    NTFSLX_BIOS_PARAMETER_BLOCK Bpb;
    UCHAR     Unused[4];
    ULONGLONG SectorCount;
    ULONGLONG MftLcn;
    ULONGLONG MftMirrLcn;
    CHAR      ClustersPerMftRecord;
    UCHAR     Reserved0[3];
    CHAR      ClustersPerIndexRecord;
    UCHAR     Reserved1[3];
    ULONGLONG SerialNumber;
    ULONG     Checksum;
    UCHAR     BootStrap[426];
    USHORT    EndOfSectorMarker;
} NTFSLX_BOOT_SECTOR, *PNTFSLX_BOOT_SECTOR;

typedef struct _NTFSLX_RECORD_HEADER
{
    ULONG  Magic;
    USHORT UsaOffset;
    USHORT UsaCount;
} NTFSLX_RECORD_HEADER, *PNTFSLX_RECORD_HEADER;

typedef struct _NTFSLX_MFT_RECORD
{
    NTFSLX_RECORD_HEADER Ntfs;
    ULONGLONG Lsn;
    USHORT    SequenceNumber;
    USHORT    LinkCount;
    USHORT    AttributesOffset;
    USHORT    Flags;
    ULONG     BytesInUse;
    ULONG     BytesAllocated;
    ULONGLONG BaseMftRecord;
    USHORT    NextAttributeInstance;
    USHORT    Reserved;
    ULONG     MftRecordNumber;
} NTFSLX_MFT_RECORD, *PNTFSLX_MFT_RECORD;

typedef struct _NTFSLX_ATTR_RECORD
{
    ULONG  Type;
    ULONG  Length;
    UCHAR  NonResident;
    UCHAR  NameLength;
    USHORT NameOffset;
    USHORT Flags;
    USHORT Instance;
    union
    {
        struct
        {
            ULONG  ValueLength;
            USHORT ValueOffset;
            UCHAR  Flags;
            CHAR   Reserved;
        } Resident;
        struct
        {
            ULONGLONG LowestVcn;
            ULONGLONG HighestVcn;
            USHORT    MappingPairsOffset;
            UCHAR     CompressionUnit;
            UCHAR     Reserved[5];
            ULONGLONG AllocatedSize;
            ULONGLONG DataSize;
            ULONGLONG InitializedSize;
            ULONGLONG CompressedSize;
        } NonResident;
    } Data;
} NTFSLX_ATTR_RECORD, *PNTFSLX_ATTR_RECORD;

typedef struct _NTFSLX_STANDARD_INFORMATION
{
    ULONGLONG CreationTime;
    ULONGLONG LastDataChangeTime;
    ULONGLONG LastMftChangeTime;
    ULONGLONG LastAccessTime;
    ULONG     FileAttributes;
    ULONG     MaximumVersions;
    ULONG     VersionNumber;
    ULONG     ClassId;
    ULONG     OwnerId;
    ULONG     SecurityId;
    ULONGLONG QuotaCharged;
    ULONGLONG Usn;
} NTFSLX_STANDARD_INFORMATION, *PNTFSLX_STANDARD_INFORMATION;

typedef struct _NTFSLX_FILE_NAME_ATTRIBUTE
{
    ULONGLONG ParentDirectory;
    ULONGLONG CreationTime;
    ULONGLONG LastDataChangeTime;
    ULONGLONG LastMftChangeTime;
    ULONGLONG LastAccessTime;
    ULONGLONG AllocatedSize;
    ULONGLONG DataSize;
    ULONG     FileAttributes;
    union
    {
        struct
        {
            USHORT PackedEaSize;
            USHORT Reserved;
        } Ea;
        struct
        {
            ULONG ReparsePointTag;
        } Rp;
    } Type;
    UCHAR FileNameLength;
    UCHAR FileNameType;
} NTFSLX_FILE_NAME_ATTRIBUTE, *PNTFSLX_FILE_NAME_ATTRIBUTE;

typedef struct _NTFSLX_ATTR_LIST_ENTRY
{
    ULONG     Type;
    USHORT    Length;
    UCHAR     NameLength;
    UCHAR     NameOffset;
    ULONGLONG LowestVcn;
    ULONGLONG MftReference;
    USHORT    Instance;
} NTFSLX_ATTR_LIST_ENTRY, *PNTFSLX_ATTR_LIST_ENTRY;

typedef struct _NTFSLX_VOLUME_INFORMATION_ATTRIBUTE
{
    ULONGLONG Reserved;
    UCHAR     MajorVersion;
    UCHAR     MinorVersion;
    USHORT    Flags;
} NTFSLX_VOLUME_INFORMATION_ATTRIBUTE, *PNTFSLX_VOLUME_INFORMATION_ATTRIBUTE;

typedef struct _NTFSLX_INDEX_HEADER
{
    ULONG EntriesOffset;
    ULONG IndexLength;
    ULONG AllocatedSize;
    UCHAR Flags;
    UCHAR Reserved[3];
} NTFSLX_INDEX_HEADER, *PNTFSLX_INDEX_HEADER;

typedef struct _NTFSLX_INDEX_ROOT
{
    ULONG Type;
    ULONG CollationRule;
    ULONG IndexBlockSize;
    UCHAR ClustersPerIndexBlock;
    UCHAR Reserved[3];
    NTFSLX_INDEX_HEADER Index;
} NTFSLX_INDEX_ROOT, *PNTFSLX_INDEX_ROOT;

typedef struct _NTFSLX_INDEX_BLOCK
{
    ULONG     Magic;
    USHORT    UsaOffset;
    USHORT    UsaCount;
    ULONGLONG Lsn;
    ULONGLONG IndexBlockVcn;
    NTFSLX_INDEX_HEADER Index;
} NTFSLX_INDEX_BLOCK, *PNTFSLX_INDEX_BLOCK;

typedef struct _NTFSLX_INDEX_ENTRY_HEADER
{
    union
    {
        struct
        {
            ULONGLONG IndexedFile;
        } Dir;
        struct
        {
            USHORT DataOffset;
            USHORT DataLength;
            ULONG  ReservedV;
        } Vi;
    } Data;
    USHORT Length;
    USHORT KeyLength;
    USHORT Flags;
    USHORT Reserved;
} NTFSLX_INDEX_ENTRY_HEADER, *PNTFSLX_INDEX_ENTRY_HEADER;

typedef struct _NTFSLX_RUNLIST_ELEMENT
{
    LONGLONG Vcn;
    LONGLONG Lcn;
    LONGLONG Length;
} NTFSLX_RUNLIST_ELEMENT, *PNTFSLX_RUNLIST_ELEMENT;

#pragma pack(pop)

C_ASSERT(sizeof(NTFSLX_BOOT_SECTOR) == 512);
C_ASSERT(sizeof(NTFSLX_RECORD_HEADER) == 8);
C_ASSERT(sizeof(NTFSLX_MFT_RECORD) == 48);
C_ASSERT(sizeof(NTFSLX_INDEX_BLOCK) == 40);
C_ASSERT(sizeof(NTFSLX_INDEX_ENTRY_HEADER) == 16);

static __inline BOOLEAN
NtfslxIsPowerOfTwo(_In_ ULONG Value)
{
    return (Value != 0) && ((Value & (Value - 1)) == 0);
}

#endif
