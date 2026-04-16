/*
 * NTFS on-disk format structures for the ntfslx ReactOS driver.
 *
 * Clean-room implementation based on public NTFS documentation and
 * Microsoft's published on-disk format specifications.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef _NTFSLX_LAYOUT_H_
#define _NTFSLX_LAYOUT_H_

/*
 * ============================================================================
 * Magic identifiers for multi-sector protected records
 * ============================================================================
 */
#define NTFSLX_RECORD_MAGIC_RSTR  0x52545352UL  /* "RSTR" - restart page */
#define NTFSLX_RECORD_MAGIC_RCRD  0x44524352UL  /* "RCRD" - log record page */
#define NTFSLX_RECORD_MAGIC_CHKD  0x444B4843UL  /* "CHKD" - chkdsk modified */
#define NTFSLX_RECORD_MAGIC_HOLE  0x454C4F48UL  /* "HOLE" - sparse hole */
#define NTFSLX_RECORD_MAGIC_EMPTY 0xFFFFFFFFUL  /* uninitialized page */

/*
 * ============================================================================
 * System file MFT record numbers
 * ============================================================================
 */
#define NTFSLX_FILE_MFTMIRR       1
#define NTFSLX_FILE_LOGFILE       2
#define NTFSLX_FILE_ATTRDEF       4
#define NTFSLX_FILE_BITMAP        6
#define NTFSLX_FILE_BOOT          7
#define NTFSLX_FILE_BADCLUS       8
#define NTFSLX_FILE_SECURE        9
#define NTFSLX_FILE_UPCASE       10
#define NTFSLX_FILE_EXTEND       11
#define NTFSLX_FILE_FIRST_USER   16

/*
 * ============================================================================
 * Attribute type codes (32-bit)
 * ============================================================================
 */
#define NTFSLX_ATTRIBUTE_STANDARD_INFORMATION  0x00000010UL
#define NTFSLX_ATTRIBUTE_OBJECT_ID             0x00000040UL
#define NTFSLX_ATTRIBUTE_SECURITY_DESCRIPTOR   0x00000050UL
#define NTFSLX_ATTRIBUTE_BITMAP                0x000000B0UL
#define NTFSLX_ATTRIBUTE_REPARSE_POINT         0x000000C0UL
#define NTFSLX_ATTRIBUTE_EA_INFORMATION        0x000000D0UL
#define NTFSLX_ATTRIBUTE_EA                    0x000000E0UL
#define NTFSLX_ATTRIBUTE_PROPERTY_SET          0x000000F0UL
#define NTFSLX_ATTRIBUTE_LOGGED_UTILITY_STREAM 0x00000100UL
#define NTFSLX_ATTRIBUTE_FIRST_USER_DEFINED    0x00001000UL

/*
 * ============================================================================
 * MFT record flags (16-bit)
 * ============================================================================
 */
#define NTFSLX_MFT_RECORD_IS_4          0x0004
#define NTFSLX_MFT_RECORD_IS_VIEW_INDEX 0x0008

/*
 * ============================================================================
 * Attribute record flags (16-bit)
 * ============================================================================
 */
#define NTFSLX_ATTR_IS_COMPRESSED     0x0001
#define NTFSLX_ATTR_COMPRESSION_MASK  0x00FF
#define NTFSLX_ATTR_IS_ENCRYPTED      0x4000
#define NTFSLX_ATTR_IS_SPARSE         0x8000

/*
 * Resident attribute flags (8-bit)
 */
#define NTFSLX_RESIDENT_ATTR_IS_INDEXED  0x01

/*
 * ============================================================================
 * Collation rules (32-bit)
 * ============================================================================
 */
#define NTFSLX_COLLATION_BINARY              0x00000000UL
#define NTFSLX_COLLATION_FILE_NAME           0x00000001UL
#define NTFSLX_COLLATION_UNICODE_STRING      0x00000002UL
#define NTFSLX_COLLATION_NTOFS_ULONG         0x00000010UL
#define NTFSLX_COLLATION_NTOFS_SID           0x00000011UL
#define NTFSLX_COLLATION_NTOFS_SECURITY_HASH 0x00000012UL
#define NTFSLX_COLLATION_NTOFS_ULONGS        0x00000013UL

/*
 * ============================================================================
 * Filename namespace values (8-bit)
 * ============================================================================
 */
#define NTFSLX_FILE_NAME_POSIX          0x00
#define NTFSLX_FILE_NAME_WIN32          0x01
#define NTFSLX_FILE_NAME_DOS            0x02
#define NTFSLX_FILE_NAME_WIN32_AND_DOS  0x03

/*
 * ============================================================================
 * MFT reference helpers
 * ============================================================================
 */
#define MSEQNO(Reference) ((USHORT)(((Reference) >> 48) & 0xFFFF))
#define MK_MREF(m, s)     ((ULONGLONG)(((ULONGLONG)(s) << 48) | \
                           ((ULONGLONG)(m) & NTFSLX_MFT_REFERENCE_MASK)))

/*
 * ============================================================================
 * LCN special values (additional)
 * ============================================================================
 */
#define NTFSLX_LCN_DELALLOC  ((LONGLONG)-2)
#define NTFSLX_LCN_ENOMEM    ((LONGLONG)-5)
#define NTFSLX_LCN_EIO       ((LONGLONG)-6)
#define NTFSLX_LCN_EINVAL    ((LONGLONG)-7)

/*
 * ============================================================================
 * Index entry flags (16-bit)
 * ============================================================================
 */
#define NTFSLX_INDEX_ENTRY_NODE  0x0001
#define NTFSLX_INDEX_ENTRY_END   0x0002

/*
 * ============================================================================
 * Index header flags (8-bit)
 * ============================================================================
 */
#define NTFSLX_SMALL_INDEX  0x00  /* fits in root only */
#define NTFSLX_LARGE_INDEX  0x01  /* INDEX_ALLOCATION present */
#define NTFSLX_LEAF_NODE    0x00
#define NTFSLX_INDEX_NODE   0x01
#define NTFSLX_NODE_MASK    0x01

/*
 * ============================================================================
 * Volume information flags (16-bit)
 * ============================================================================
 */
#define NTFSLX_VOLUME_IS_DIRTY            0x0001
#define NTFSLX_VOLUME_RESIZE_LOG_FILE     0x0002
#define NTFSLX_VOLUME_UPGRADE_ON_MOUNT    0x0004
#define NTFSLX_VOLUME_MOUNTED_ON_NT4      0x0008
#define NTFSLX_VOLUME_DELETE_USN_UNDERWAY 0x0010
#define NTFSLX_VOLUME_REPAIR_OBJECT_ID    0x0020
#define NTFSLX_VOLUME_CHKDSK_UNDERWAY     0x4000
#define NTFSLX_VOLUME_MODIFIED_BY_CHKDSK  0x8000
#define NTFSLX_VOLUME_FLAGS_MASK          0xC03F
#define NTFSLX_VOLUME_MUST_MOUNT_RO_MASK  0xC027

/*
 * ============================================================================
 * File attribute flags (32-bit)
 *
 * Most are already in ntifs.h via FILE_ATTRIBUTE_*, but we define the
 * NTFS-specific duplicated index bits here.
 * ============================================================================
 */
#define NTFSLX_FILE_ATTR_DUP_FILE_NAME_INDEX_PRESENT  0x10000000UL
#define NTFSLX_FILE_ATTR_DUP_VIEW_INDEX_PRESENT       0x20000000UL

/*
 * ============================================================================
 * Attribute definition flags (32-bit)
 * ============================================================================
 */
#define NTFSLX_ATTR_DEF_INDEXABLE       0x00000002UL
#define NTFSLX_ATTR_DEF_MULTIPLE        0x00000004UL
#define NTFSLX_ATTR_DEF_NOT_ZERO        0x00000008UL
#define NTFSLX_ATTR_DEF_INDEXED_UNIQUE  0x00000010UL
#define NTFSLX_ATTR_DEF_NAMED_UNIQUE    0x00000020UL
#define NTFSLX_ATTR_DEF_RESIDENT        0x00000040UL
#define NTFSLX_ATTR_DEF_ALWAYS_LOG      0x00000080UL

/*
 * ============================================================================
 * LogFile constants
 * ============================================================================
 */
#define NTFSLX_MAX_LOG_FILE_SIZE    0x100000000ULL
#define NTFSLX_DEFAULT_LOG_PAGE_SIZE 4096
#define NTFSLX_MIN_LOG_RECORD_PAGES  48

#define NTFSLX_LOGFILE_NO_CLIENT     0xFFFF

/*
 * Restart area flags
 */
#define NTFSLX_RESTART_VOLUME_IS_CLEAN 0x0002

/*
 * ============================================================================
 * Cluster allocation zones
 * ============================================================================
 */
#define NTFSLX_FIRST_ZONE  0
#define NTFSLX_MFT_ZONE    0
#define NTFSLX_DATA_ZONE   1
#define NTFSLX_LAST_ZONE   1

/*
 * ============================================================================
 * NTFS time offset: difference in 100ns intervals between
 * 1601-01-01 and 1970-01-01
 * ============================================================================
 */
#define NTFSLX_TIME_OFFSET  ((LONGLONG)(369 * 365 + 89) * 24 * 3600)

/*
 * Maximum filename length in characters
 */
#define NTFSLX_MAXIMUM_FILE_NAME_LENGTH 255

/*
 * Reparse tag values
 */
#define NTFSLX_IO_REPARSE_TAG_MOUNT_POINT  0xA0000003UL
#define NTFSLX_IO_REPARSE_TAG_SYMLINK      0xA000000CUL
#define NTFSLX_IO_REPARSE_TAG_LX_SYMLINK   0xA000001DUL
#define NTFSLX_IO_REPARSE_TAG_AF_UNIX      0x80000023UL
#define NTFSLX_IO_REPARSE_TAG_LX_FIFO      0x80000024UL
#define NTFSLX_IO_REPARSE_TAG_LX_CHR       0x80000025UL
#define NTFSLX_IO_REPARSE_TAG_LX_BLK       0x80000026UL

/*
 * EA flags
 */
#define NTFSLX_EA_NEED_EA  0x80

/*
 * ============================================================================
 * On-disk structures - all packed
 * ============================================================================
 */
#include <pshpack1.h>

/*
 * $STANDARD_INFORMATION attribute (type 0x10).
 * Always resident, present in all base MFT records.
 */
typedef struct _NTFSLX_STANDARD_INFORMATION
{
    ULONGLONG CreationTime;
    ULONGLONG LastDataChangeTime;
    ULONGLONG LastMftChangeTime;
    ULONGLONG LastAccessTime;
    ULONG     FileAttributes;
    /* NTFS 3.0+ extended fields follow */
    ULONG     MaximumVersions;
    ULONG     VersionNumber;
    ULONG     ClassId;
    ULONG     OwnerId;
    ULONG     SecurityId;
    ULONGLONG QuotaCharged;
    ULONGLONG Usn;
} NTFSLX_STANDARD_INFORMATION, *PNTFSLX_STANDARD_INFORMATION;

/*
 * $ATTRIBUTE_LIST entry (type 0x20).
 * Variable-length, 8-byte aligned.
 */
typedef struct _NTFSLX_ATTR_LIST_ENTRY
{
    ULONG     Type;
    USHORT    Length;
    UCHAR     NameLength;
    UCHAR     NameOffset;
    ULONGLONG LowestVcn;
    ULONGLONG MftReference;
    USHORT    Instance;
    /* WCHAR Name[] follows at NameOffset */
} NTFSLX_ATTR_LIST_ENTRY, *PNTFSLX_ATTR_LIST_ENTRY;

/*
 * $FILE_NAME attribute (type 0x30).
 * Always resident.
 */
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
            ULONG  ReparsePointTag;
        } Rp;
    } Type;
    UCHAR     FileNameLength;
    UCHAR     FileNameType;
    /* WCHAR FileName[] follows */
} NTFSLX_FILE_NAME_ATTRIBUTE, *PNTFSLX_FILE_NAME_ATTRIBUTE;

/*
 * $OBJECT_ID attribute (type 0x40).
 * Always resident, 16-64 bytes.
 */
typedef struct _NTFSLX_OBJECT_ID_ATTRIBUTE
{
    UCHAR ObjectId[16];
    UCHAR BirthVolumeId[16];
    UCHAR BirthObjectId[16];
    UCHAR DomainId[16];
} NTFSLX_OBJECT_ID_ATTRIBUTE, *PNTFSLX_OBJECT_ID_ATTRIBUTE;

/*
 * Attribute definition entry from $AttrDef.
 */
typedef struct _NTFSLX_ATTR_DEF
{
    WCHAR     Name[0x40];
    ULONG     Type;
    ULONG     DisplayRule;
    ULONG     CollationRule;
    ULONG     Flags;
    ULONGLONG MinSize;
    ULONGLONG MaxSize;
} NTFSLX_ATTR_DEF, *PNTFSLX_ATTR_DEF;

C_ASSERT(sizeof(NTFSLX_ATTR_DEF) == 160);

/*
 * Index header - common to INDEX_ROOT and INDEX_ALLOCATION blocks.
 * Offsets are relative to the start of this header.
 */
typedef struct _NTFSLX_INDEX_HEADER
{
    ULONG EntriesOffset;
    ULONG IndexLength;
    ULONG AllocatedSize;
    UCHAR Flags;
    UCHAR Reserved[3];
} NTFSLX_INDEX_HEADER, *PNTFSLX_INDEX_HEADER;

/*
 * $INDEX_ROOT attribute (type 0x90).
 * Always resident.
 */
typedef struct _NTFSLX_INDEX_ROOT
{
    ULONG Type;                    /* indexed attribute type */
    ULONG CollationRule;
    ULONG IndexBlockSize;
    UCHAR ClustersPerIndexBlock;
    UCHAR Reserved[3];
    NTFSLX_INDEX_HEADER Index;
} NTFSLX_INDEX_ROOT, *PNTFSLX_INDEX_ROOT;

/*
 * $INDEX_ALLOCATION block header (magic "INDX").
 * Always non-resident.
 */
typedef struct _NTFSLX_INDEX_BLOCK
{
    ULONG     Magic;               /* NTFSLX_RECORD_MAGIC_INDX */
    USHORT    UsaOffset;
    USHORT    UsaCount;
    ULONGLONG Lsn;
    ULONGLONG IndexBlockVcn;
    NTFSLX_INDEX_HEADER Index;
} NTFSLX_INDEX_BLOCK, *PNTFSLX_INDEX_BLOCK;

C_ASSERT(sizeof(NTFSLX_INDEX_BLOCK) == 40);

/*
 * Index entry header (common to all index entry types).
 */
typedef struct _NTFSLX_INDEX_ENTRY_HEADER
{
    union
    {
        struct
        {
            ULONGLONG IndexedFile;      /* MFT reference for dir entries */
        } Dir;
        struct
        {
            USHORT DataOffset;
            USHORT DataLength;
            ULONG  ReservedV;
        } Vi;                           /* view index entries */
    } Data;
    USHORT Length;                       /* total entry size */
    USHORT KeyLength;
    USHORT Flags;                       /* NTFSLX_INDEX_ENTRY_* */
    USHORT Reserved;
} NTFSLX_INDEX_ENTRY_HEADER, *PNTFSLX_INDEX_ENTRY_HEADER;

C_ASSERT(sizeof(NTFSLX_INDEX_ENTRY_HEADER) == 16);

/*
 * $REPARSE_POINT attribute (type 0xC0).
 */
typedef struct _NTFSLX_REPARSE_POINT
{
    ULONG  ReparseTag;
    USHORT ReparseDataLength;
    USHORT Reserved;
    /* UCHAR ReparseData[] follows */
} NTFSLX_REPARSE_POINT, *PNTFSLX_REPARSE_POINT;

/*
 * Reparse index key ($R in $Extend/$Reparse).
 */
typedef struct _NTFSLX_REPARSE_INDEX_KEY
{
    ULONG     ReparseTag;
    ULONGLONG FileId;
} NTFSLX_REPARSE_INDEX_KEY, *PNTFSLX_REPARSE_INDEX_KEY;

/*
 * $EA_INFORMATION attribute (type 0xD0).
 * Always resident.
 */
typedef struct _NTFSLX_EA_INFORMATION
{
    USHORT EaLength;
    USHORT NeedEaCount;
    ULONG  EaQueryLength;
} NTFSLX_EA_INFORMATION, *PNTFSLX_EA_INFORMATION;

/*
 * $EA attribute entry (type 0xE0).
 * Variable-length, value follows name.
 */
typedef struct _NTFSLX_EA_ENTRY
{
    ULONG  NextEntryOffset;
    UCHAR  Flags;
    UCHAR  EaNameLength;
    USHORT EaValueLength;
    /* CHAR EaName[] follows, then UCHAR EaValue[] */
} NTFSLX_EA_ENTRY, *PNTFSLX_EA_ENTRY;

/*
 * Self-relative security descriptor header.
 * Stored in $SECURITY_DESCRIPTOR or $SDS.
 */
typedef struct _NTFSLX_SECURITY_DESCRIPTOR_RELATIVE
{
    UCHAR  Revision;
    UCHAR  Alignment;
    USHORT Control;
    ULONG  Owner;          /* offset to owner SID */
    ULONG  Group;          /* offset to group SID */
    ULONG  Sacl;           /* offset to system ACL */
    ULONG  Dacl;           /* offset to discretionary ACL */
} NTFSLX_SECURITY_DESCRIPTOR_RELATIVE, *PNTFSLX_SECURITY_DESCRIPTOR_RELATIVE;

C_ASSERT(sizeof(NTFSLX_SECURITY_DESCRIPTOR_RELATIVE) == 20);

/*
 * Security Identifier (SID) - variable length.
 */
typedef struct _NTFSLX_SID
{
    UCHAR Revision;
    UCHAR SubAuthorityCount;
    UCHAR IdentifierAuthority[6];
    /* ULONG SubAuthority[] follows */
} NTFSLX_SID, *PNTFSLX_SID;

/*
 * Access Control List (ACL) header.
 */
typedef struct _NTFSLX_ACL
{
    UCHAR  Revision;
    UCHAR  Alignment1;
    USHORT Size;
    USHORT AceCount;
    USHORT Alignment2;
} NTFSLX_ACL, *PNTFSLX_ACL;

C_ASSERT(sizeof(NTFSLX_ACL) == 8);

/*
 * Access Control Entry (ACE).
 */
typedef struct _NTFSLX_ACE
{
    UCHAR     Type;
    UCHAR     Flags;
    USHORT    Size;
    ULONG     Mask;
    NTFSLX_SID Sid;
} NTFSLX_ACE, *PNTFSLX_ACE;

/*
 * $SII index key ($Secure).
 */
typedef struct _NTFSLX_SII_INDEX_KEY
{
    ULONG SecurityId;
} NTFSLX_SII_INDEX_KEY, *PNTFSLX_SII_INDEX_KEY;

/*
 * $SDH index key ($Secure).
 */
typedef struct _NTFSLX_SDH_INDEX_KEY
{
    ULONG Hash;
    ULONG SecurityId;
} NTFSLX_SDH_INDEX_KEY, *PNTFSLX_SDH_INDEX_KEY;

/*
 * Quota control entry ($Quota/$Q).
 */
typedef struct _NTFSLX_QUOTA_CONTROL_ENTRY
{
    ULONG     Version;
    ULONG     Flags;
    ULONGLONG BytesUsed;
    ULONGLONG ChangeTime;
    ULONGLONG Threshold;
    ULONGLONG Limit;
    ULONGLONG ExceededTime;
    NTFSLX_SID Sid;
} NTFSLX_QUOTA_CONTROL_ENTRY, *PNTFSLX_QUOTA_CONTROL_ENTRY;

/*
 * ============================================================================
 * LogFile (journal) structures
 * ============================================================================
 */

/*
 * Restart page header (magic "RSTR").
 */
typedef struct _NTFSLX_RESTART_PAGE_HEADER
{
    ULONG  Magic;                  /* NTFSLX_RECORD_MAGIC_RSTR */
    USHORT UsaOffset;
    USHORT UsaCount;
    ULONGLONG ChkdskLsn;
    ULONG  SystemPageSize;
    ULONG  LogPageSize;
    USHORT RestartAreaOffset;
    USHORT MinorVersion;
    USHORT MajorVersion;
} NTFSLX_RESTART_PAGE_HEADER, *PNTFSLX_RESTART_PAGE_HEADER;

/*
 * Restart area (follows RESTART_PAGE_HEADER at RestartAreaOffset).
 */
typedef struct _NTFSLX_RESTART_AREA
{
    ULONGLONG CurrentLsn;
    USHORT LogClients;
    USHORT ClientFreeList;
    USHORT ClientInUseList;
    USHORT Flags;
    ULONG  SeqNumberBits;
    USHORT RestartAreaLength;
    USHORT ClientArrayOffset;
    ULONGLONG FileSize;
    ULONG  LastLsnDataLength;
    USHORT LogRecordHeaderLength;
    USHORT LogPageDataOffset;
    ULONG  RestartLogOpenCount;
    ULONG  Reserved;
} NTFSLX_RESTART_AREA, *PNTFSLX_RESTART_AREA;

/*
 * Log client record (follows RESTART_AREA at ClientArrayOffset).
 */
typedef struct _NTFSLX_LOG_CLIENT_RECORD
{
    ULONGLONG OldestLsn;
    ULONGLONG ClientRestartLsn;
    USHORT PrevClient;
    USHORT NextClient;
    USHORT SeqNumber;
    UCHAR  Reserved[6];
    ULONG  ClientNameLength;
    WCHAR  ClientName[64];
} NTFSLX_LOG_CLIENT_RECORD, *PNTFSLX_LOG_CLIENT_RECORD;

#include <poppack.h>

/*
 * ============================================================================
 * Inline helpers
 * ============================================================================
 */

static __inline BOOLEAN
NtfslxIsCollationRuleSupported(_In_ ULONG CollationRule)
{
    return (CollationRule <= 0x02) ||
           (CollationRule >= 0x10 && CollationRule <= 0x13);
}

#endif /* _NTFSLX_LAYOUT_H_ */
