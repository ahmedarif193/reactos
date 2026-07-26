#ifndef _NTFSLIB_NEW_ENV_H_
#define _NTFSLIB_NEW_ENV_H_

/*
 * Small, platform-neutral subset of the NT type system used by ntfslib.
 *
 * ReactOS builds continue to get these definitions from ntdef.h/ntifs.h.
 * Native hosts define NTFSLIB_PORTABLE and use this header instead. Keep
 * WCHAR at 16 bits: it represents UTF-16 code units stored on NTFS, not the
 * host C library's idea of wchar_t.
 */

#include <stddef.h>
#include <stdint.h>

#if !defined(__cplusplus)
#error ntfslib_new currently requires a C++ compiler
#endif

typedef uint8_t UCHAR;
typedef uint8_t UINT8;
typedef uint16_t USHORT;
typedef uint16_t UINT16;
typedef uint32_t ULONG;
typedef uint32_t UINT32;
typedef uint64_t ULONGLONG;
typedef uint64_t UINT64;
typedef int8_t INT8;
typedef int16_t SHORT;
typedef int32_t LONG;
typedef int64_t LONGLONG;
typedef int32_t NTSTATUS;
typedef uint8_t BOOLEAN;
typedef wchar_t WCHAR;
typedef size_t SIZE_T;
typedef uintptr_t ULONG_PTR;

typedef UCHAR* PUCHAR;
typedef USHORT* PUSHORT;
typedef ULONG* PULONG;
typedef ULONGLONG* PULONGLONG;
typedef BOOLEAN* PBOOLEAN;
typedef WCHAR* PWCHAR;
typedef WCHAR* PWSTR;
typedef const WCHAR* PCWSTR;

typedef union _LARGE_INTEGER
{
    struct
    {
        ULONG LowPart;
        LONG HighPart;
    };
    LONGLONG QuadPart;
} LARGE_INTEGER, *PLARGE_INTEGER;

typedef struct _UNICODE_STRING
{
    USHORT Length;
    USHORT MaximumLength;
    PWSTR Buffer;
} UNICODE_STRING, *PUNICODE_STRING;

typedef struct _GUID
{
    UINT32 Data1;
    UINT16 Data2;
    UINT16 Data3;
    UCHAR Data4[8];
} GUID;

typedef enum _POOL_TYPE
{
    NonPagedPool = 0,
    PagedPool = 1
} POOL_TYPE;

typedef struct _RTL_BITMAP
{
    ULONG SizeOfBitMap;
    PULONG Buffer;
} RTL_BITMAP, *PRTL_BITMAP;

/*
 * This structure is retained for the ReactOS-facing compatibility API. New
 * portable consumers should use NtfsDirectoryEntry/NtfsDirectoryReadNext.
 */
typedef struct _FILE_BOTH_DIR_INFORMATION
{
    ULONG NextEntryOffset;
    ULONG FileIndex;
    LARGE_INTEGER CreationTime;
    LARGE_INTEGER LastAccessTime;
    LARGE_INTEGER LastWriteTime;
    LARGE_INTEGER ChangeTime;
    LARGE_INTEGER EndOfFile;
    LARGE_INTEGER AllocationSize;
    ULONG FileAttributes;
    ULONG FileNameLength;
    ULONG EaSize;
    UCHAR ShortNameLength;
    WCHAR ShortName[12];
    WCHAR FileName[1];
} FILE_BOTH_DIR_INFORMATION, *PFILE_BOTH_DIR_INFORMATION;

#define TRUE 1
#define FALSE 0

#define STATUS_SUCCESS ((NTSTATUS)0x00000000)
#define STATUS_REPARSE ((NTSTATUS)0x00000104)
#define STATUS_BUFFER_OVERFLOW ((NTSTATUS)0x80000005)
#define STATUS_NO_MORE_FILES ((NTSTATUS)0x80000006)
#define STATUS_NO_MORE_EAS ((NTSTATUS)0x80000012)
#define STATUS_INVALID_EA_NAME ((NTSTATUS)0x80000013)
#define STATUS_NOT_IMPLEMENTED ((NTSTATUS)0xC0000002)
#define STATUS_OBJECT_NOT_EXTERNALLY_BACKED ((NTSTATUS)0xC000046D)
#define STATUS_INVALID_PARAMETER ((NTSTATUS)0xC000000D)
#define STATUS_END_OF_FILE ((NTSTATUS)0xC0000011)
#define STATUS_BUFFER_TOO_SMALL ((NTSTATUS)0xC0000023)
#define STATUS_OBJECT_NAME_INVALID ((NTSTATUS)0xC0000033)
#define STATUS_OBJECT_NAME_COLLISION ((NTSTATUS)0xC0000035)
#define STATUS_EAS_NOT_SUPPORTED ((NTSTATUS)0xC000004F)
#define STATUS_EA_TOO_LARGE ((NTSTATUS)0xC0000050)
#define STATUS_NO_EAS_ON_FILE ((NTSTATUS)0xC0000052)
#define STATUS_EA_CORRUPT_ERROR ((NTSTATUS)0xC0000053)
#define STATUS_DISK_FULL ((NTSTATUS)0xC000007F)
#define STATUS_INSUFFICIENT_RESOURCES ((NTSTATUS)0xC000009A)
#define STATUS_MEDIA_WRITE_PROTECTED ((NTSTATUS)0xC00000A2)
#define STATUS_FILE_IS_A_DIRECTORY ((NTSTATUS)0xC00000BA)
#define STATUS_DIRECTORY_NOT_EMPTY ((NTSTATUS)0xC0000101)
#define STATUS_FILE_CORRUPT_ERROR ((NTSTATUS)0xC0000102)
#define STATUS_NOT_A_DIRECTORY ((NTSTATUS)0xC0000103)
#define STATUS_NAME_TOO_LONG ((NTSTATUS)0xC0000106)
#define STATUS_UNRECOGNIZED_VOLUME ((NTSTATUS)0xC000014F)
#define STATUS_INVALID_DEVICE_STATE ((NTSTATUS)0xC0000184)
#define STATUS_NOT_FOUND ((NTSTATUS)0xC0000225)
#define STATUS_NOT_A_REPARSE_POINT ((NTSTATUS)0xC0000275)
#define STATUS_IO_REPARSE_TAG_INVALID ((NTSTATUS)0xC0000276)
#define STATUS_IO_REPARSE_TAG_MISMATCH ((NTSTATUS)0xC0000277)
#define STATUS_IO_REPARSE_DATA_INVALID ((NTSTATUS)0xC0000278)
#define STATUS_IO_REPARSE_TAG_NOT_HANDLED ((NTSTATUS)0xC0000279)
#define STATUS_REPARSE_ATTRIBUTE_CONFLICT ((NTSTATUS)0xC00002B2)
#define STATUS_ACCESS_DENIED ((NTSTATUS)0xC0000022)
#define STATUS_FILE_TOO_LARGE ((NTSTATUS)0xC0000904)
#define STATUS_LOG_BLOCK_VERSION ((NTSTATUS)0xC01A0009)

#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#define MAXULONG UINT32_C(0xffffffff)
#define MAXUSHORT UINT16_C(0xffff)
#define MAXIMUM_VOLUME_LABEL_LENGTH (32 * sizeof(WCHAR))
#define FILE_ATTRIBUTE_DIRECTORY UINT32_C(0x00000010)
#define FILE_WRITE_TO_END_OF_FILE UINT32_C(0xffffffff)
#define DOS_STAR L'<'
#define DOS_QM L'>'
#define DOS_DOT L'"'

#define ALIGN_UP_BY(Size, Align) \
    (((Size) + ((Align) - 1)) & ~((Align) - 1))
#define FIELD_OFFSET(Type, Field) offsetof(Type, Field)
#define RTL_NUMBER_OF(Array) (sizeof(Array) / sizeof((Array)[0]))
#define UNREFERENCED_PARAMETER(Parameter) ((void)(Parameter))

#define EXTERN_C extern "C"
#define __cdecl

#define _In_
#define _In_opt_
#define _In_reads_(Count)
#define _In_reads_bytes_(Count)
#define _Inout_
#define _Inout_updates_bytes_(Count)
#define _Out_
#define _Out_opt_
#define _Out_writes_bytes_(Count)
#define _Outptr_
#define _Outptr_result_bytebuffer_(Length)
#define _Outptr_result_maybenull_

#endif /* _NTFSLIB_NEW_ENV_H_ */
