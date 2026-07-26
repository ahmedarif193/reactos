
#ifndef _NTFSATTRIBDEF_H_
#define _NTFSATTRIBDEF_H_

// Attribute end marker
#define ATTR_END           0xFFFFFFFF

// Attribute flags
#define ATTR_COMPRESSION_MASK 0x00FF
#define ATTR_COMPRESSED    0x0001
#define ATTR_ENCRYPTED     0x4000
#define ATTR_SPARSE        0x8000

// FileName flags
#define FN_READONLY        0x0001
#define FN_HIDDEN          0x0002
#define FN_SYSTEM          0x0004
#define FN_ARCHIVE         0x0020
#define FN_DEVICE          0x0040
#define FN_TEMP            0x0100
#define FN_SPARSE          0x0200
#define FN_REPARSE         0x0400
#define FN_COMPRESSED      0x0800
#define FN_OFFLINE         0x1000
#define FN_NOTINDEXED      0x2000
#define FN_ENCRYPTED       0x4000
#define FN_DIRECTORY       0x10000000
#define FN_INDEX_VIEW      0x20000000

// FileName types
#define NAME_TYPE_POSIX 0             // 0b00
#define NAME_TYPE_WIN32 1             // 0b01
#define NAME_TYPE_DOS   2             // 0b10
#define NAME_TYPE_WIN32_AND_DOS 3     // 0b11

// Volume Information Flags
#define VOL_DIRTY          0x0001
#define VOL_RESIZE_LOG     0x0002
#define VOL_UPGRADE_ON_MNT 0x0004
#define VOL_USN_DEL_INPROG 0x0010
#define VOL_REPAIR_OBJ_IDS 0x0020
#define VOL_CHECKED        0x8000

/* Attribute types
 * Note that TypeVolumeVersion and TypeSymbolicLink existed in Windows NT 4 and
 * older but was unused. They were superseded by TypeObjectId and
 * TypeReparsePoint. TypePropertySet existed in Windows 2000 beta builds for
 * Native Structured Storage (NSS) before being canceled for Beta 3.
 */
enum AttributeType
{
    TypeStandardInformation = 0x10,
    TypeAttributeList       = 0x20,
    TypeFileName            = 0x30,
    TypeObjectId            = 0x40,
    TypeVolumeVersion       = 0x40,
    TypeSecurityDescriptor  = 0x50,
    TypeVolumeName          = 0x60,
    TypeVolumeInformation   = 0x70,
    TypeData                = 0x80,
    TypeIndexRoot           = 0x90,
    TypeIndexAllocation     = 0xA0,
    TypeBitmap              = 0xB0,
    TypeReparsePoint        = 0xC0,
    TypeSymbolicLink        = 0xC0,
    TypeEAInformation       = 0xD0,
    TypeEA                  = 0xE0,
    TypePropertySet         = 0xF0,
    TypeLoggedUtilityStream = 0x100,
    TypeAttributeEndMarker  = 0xFFFFFFFF
};
typedef enum AttributeType AttributeType;

typedef struct
{
    UINT32 AttributeType;                  // Offset 0x00, Size 4
    UINT32 Length;                         // Offset 0x04, Size 4
    UINT8  IsNonResident;                  // Offset 0x08, Size 1
    UINT8  NameLength;                     // Offset 0x09, Size 1
    UINT16 NameOffset;                     // Offset 0x0A, Size 2
    UINT16 Flags;                          // Offset 0x0C, Size 2
    UINT16 AttributeID;                    // Offset 0x0E, Size 2
    union
    {
        struct
        {
            /* Value of AttributeOffset should be (2 * NameLength + 0x18).
             * Size of AttributeName should be (2 * NameLength).
             * Attribute contents has an offset of (2 * NameLength + 0x18).
             */
            UINT32 DataLength;             // Offset 0x10, Size 4
            UINT16 DataOffset;             // Offset 0x14, Size 2
            UINT8  IndexedFlag;            // Offset 0x16, Size 1
            UINT8  Padding;                // Offset 0x17, Size 1
        } Resident;
        struct
        {
            /* Value of DataRunsOffset should be (2 * NameLength + 0x40).
            * Size of AttributeName should be (2 * NameLength).
            * Data Run has an offset of (2 * NameLength + 0x40).
            */
            UINT64 FirstVCN;               // Offset 0x10, Size 8
            UINT64 LastVCN;                // Offset 0x18, Size 8
            UINT16 DataRunsOffset;         // Offset 0x20, Size 2
            UINT16 CompressionUnitSize;    // Offset 0x22, Size 2
            UINT32 Reserved;               // Offset 0x24, Size 4
            UINT64 AllocatedSize;          // Offset 0x28, Size 8
            UINT64 DataSize;               // Offset 0x30, Size 8
            UINT64 InitalizedDataSize;     // Offset 0x38, Size 8
            /*
             * Present when ATTR_COMPRESSED or ATTR_SPARSE is set. Mapping
             * pairs then begin at or after offset 0x48.
             */
            UINT64 CompressedDataSize;      // Offset 0x40, Size 8
        } NonResident;
    };
} Attribute, *PAttribute;


/* *** $ATTRDEF ENTRIES *** */

// Collation Rules
#define ATTRDEF_COLLATION_BINARY      0x00
#define ATTRDEF_COLLATION_FILENAME    0x01
#define ATTRDEF_COLLATION_WSTR        0x02
#define ATTRDEF_COLLATION_ULONG       0x10
#define ATTRDEF_COLLATION_SID         0x11
#define ATTRDEF_COLLATION_SEC_HASH    0x12
#define ATTRDEF_COLLATION_ULONG_MULTI 0x13

// Entry Flags
#define ATTRDEF_INDEXED      0x02
#define ATTRDEF_RESIDENT     0x40
#define ATTRDEF_NON_RESIDENT 0x80

typedef struct
{
    WCHAR  Label[64];
    UINT32 AttributeType;
    UINT32 DisplayRule;
    UINT32 CollationRule;
    UINT32 Flags;
    UINT64 MinimumSize;
    UINT64 MaximumSize;
} AttrDefEntry, *PAttrDefEntry;

/* *** EXTENDED ATTRIBUTE HEADERS *** */

// $STANDARD_INFORMATION (0x10)
typedef struct StandardInformationEx
{
    UINT64 CreationTime;                   // Offset 0x00, Size 8
    UINT64 LastWriteTime;                  // Offset 0x08, Size 8
    UINT64 ChangeTime;                     // Offset 0x10, Size 8
    UINT64 LastAccessTime;                 // Offset 0x18, Size 8
    UINT32 FilePermissions;                // Offset 0x20, Size 4
    UINT32 MaxVersions;                    // Offset 0x24, Size 4
    UINT32 VersionNum;                     // Offset 0x28, Size 4
    UINT32 ClassId;                        // Offset 0x2C, Size 4
    UINT32 OwnerId;                        // Offset 0x30, Size 4
    UINT32 SecurityId;                     // Offset 0x34, Size 4
    UINT64 QuotaCharged;                   // Offset 0x38, Size 8
    UINT64 UpdateSequenceNumber;           // Offset 0x40, Size 8
} *PStandardInformationEx;

/// $ATTRIBUTE_LIST (0x20)
typedef struct
{
    UINT32 Type;                           // Offset 0x00, Size 4
    UINT16 RecordLength;                   // Offset 0x04, Size 2
    UINT8  NameLength;                     // Offset 0x06, Size 1
    UINT8  NameOffset;                     // Offset 0x07, Size 1
    UINT64 FirstVCN;                       // Offset 0x08, Size 8
    UINT64 BaseFileRef;                    // Offset 0x10, Size 2
    UINT16 AttributeId;                    // Offset 0x18, Size 2
    UCHAR  Padding[6];                     // Offset 0x1A, Size 6
} AttributeListEx, *PAttributeListEx;

// $FILE_NAME (0x30)
typedef struct
{
    UINT64 ParentFileReference;            // Offset 0x00, Size 8
    UINT64 CreationTime;                   // Offset 0x08, Size 8
    UINT64 LastWriteTime;                  // Offset 0x10, Size 8
    UINT64 ChangeTime;                     // Offset 0x18, Size 8
    UINT64 LastAccessTime;                 // Offset 0x20, Size 8
    UINT64 AllocatedSize;                  // Offset 0x28, Size 8
    UINT64 DataSize;                       // Offset 0x30, Size 8
    UINT32 Flags;                          // Offset 0x38, Size 4
    union
    {
        struct
        {
            USHORT PackedEASize;
            USHORT Padding;
        } EAInfo;
        ULONG ReparseTag;
    } Extended;
    UINT8  NameLength;                     // Offset 0x40, Size 1
    UINT8  NameType;                       // Offset 0x41, Size 1
    WCHAR  Name[1];                        // Offset 0x42, Size variable
} FileNameEx, *PFileNameEx;

// $OBJECT_ID (0x40)
typedef struct ObjectIdEx
{
    GUID ObjectId;                         // Offset 0x00, Size 16
    GUID BirthVolId;                       // Offset 0x10, Size 16
    GUID BirthObjId;                       // Offset 0x20, Size 16
    GUID DomainId;                         // Offset 0x30, Size 16
} ObjectIdEx, *PObjectIdEx;

// $SECURITY_DESCRIPTOR (0x50), stored in self-relative form.
typedef struct NtfsSecurityDescriptor
{
    UINT8 Revision;                        // Offset 0x00, Size 1
    UINT8 Reserved;                        // Offset 0x01, Size 1
    UINT16 Control;                        // Offset 0x02, Size 2
    UINT32 OwnerOffset;                    // Offset 0x04, Size 4
    UINT32 GroupOffset;                    // Offset 0x08, Size 4
    UINT32 SaclOffset;                     // Offset 0x0C, Size 4
    UINT32 DaclOffset;                     // Offset 0x10, Size 4
} NtfsSecurityDescriptor, *PNtfsSecurityDescriptor;

// $VOLUME_INFORMATION (0x70)
typedef struct
{
    UINT64 Reserved1;                      // Offset 0x00, Size 8
    UINT8  MajorVersion;                   // Offset 0x08, Size 1
    UINT8  MinorVersion;                   // Offset 0x09, Size 1
    UINT16 Flags;                          // Offset 0x0A, Size 2
    UINT32 Reserved2;                      // Offset 0x0C, Size 4
} VolumeInformationEx, *PVolumeInformationEx;

// $INDEX_ROOT (0x90)
typedef struct
{
    ULONG  IndexOffset;                    // Offset 0x00, Size 4
    ULONG  TotalIndexSize;                 // Offset 0x04, Size 4
    ULONG  AllocatedSize;                  // Offset 0x08, Size 4
    UCHAR  Flags;                          // Offset 0x0C, Size 1
    UCHAR  Padding[3];                     // Offset 0x0D, Size 3
} IndexNodeHeader, *PIndexNodeHeader;

typedef struct
{
    ULONG AttributeType;                  // Offset 0x00, Size 4
    ULONG CollationRule;                  // Offset 0x04, Size 4
    ULONG BytesPerIndexRec;               // Offset 0x08, Size 4
    UCHAR ClusPerIndexRec;                // Offset 0x0C, Size 1
    UCHAR Padding[3];                     // Offset 0x0D, Size 3
    IndexNodeHeader Header;               // Offset 0x10, Size 16
} IndexRootEx, *PIndexRootEx;

// Index entry flags
#define INDEX_ENTRY_NODE 1
#define INDEX_ENTRY_END  2

typedef struct
{
    union
    {
        struct                             // Offset 0x00, Size 8
        {
            ULONGLONG IndexedFile;
        } Directory;
        struct
        {
            USHORT    DataOffset;
            USHORT    DataLength;
            ULONG     Reserved;
        } ViewIndex;
    } Data;
    UINT16     EntryLength;                   // Offset 0x08, Size 2
    UINT16     StreamLength;                  // Offset 0x0A, Size 2
    UINT8      Flags;                         // Offset 0x0C, Size 1
    UINT16     Reserved;
    UCHAR      IndexStream[1];                // Offset 0x10, Size varies
} IndexEntry, *PIndexEntry;

// $REPARSE_POINT (0xC0)
typedef struct
{
    UINT32 ReparseType;                    // Offset 0x00, Size 4
    UINT16 ReparseDataLength;              // Offset 0x04, Size 2
    UINT16 Padding;                        // Offset 0x06, Size 2
} ReparsePointEx, *PReparsePointEx;

// Third-party reparse points append a GUID to the common header.
typedef struct _ThirdPartyReparsePointEx
{
    ReparsePointEx Header;                 // Offset 0x00, Size 8
    GUID ReparseGUID;                      // Offset 0x08, Size 16
} ThirdPartyReparsePointEx, *PThirdPartyReparsePointEx;

// $EA_INFORMATION (0xD0)
typedef struct
{
    UINT16 PackedEASize;                   // Offset 0x00, Size 2
    UINT16 NumEAWithNEED_EA;               // Offset 0x02, Size 2
    UINT32 UnpackedEASize;                 // Offset 0x04, Size 4
} EAInformationEx, *PEAInformationEx;

// $EA (0xE0)
typedef struct
{
    UINT32 OffsetNextEA;                   // Offset 0x00, Size 4
    UINT8  Flags;                          // Offset 0x04, Size 1
    UINT8  NameLength;                     // Offset 0x05, Size 1
    UINT16 ValueLength;                    // Offset 0x06, Size 2
} EAEx, *PEAEx;

#endif /* _NTFSATTRIBDEF_H_ */
