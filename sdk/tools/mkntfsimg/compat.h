#ifndef _MKNTFSIMG_COMPAT_H_
#define _MKNTFSIMG_COMPAT_H_

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef int             NTSTATUS;
typedef int             BOOLEAN;
typedef void            VOID;

typedef uint8_t         UCHAR;
typedef int8_t          CHAR;
typedef uint16_t        USHORT;
typedef int16_t         SHORT;
typedef uint32_t        ULONG;
typedef int32_t         LONG;
typedef uint64_t        ULONGLONG;
typedef int64_t         LONGLONG;
typedef uint16_t        WCHAR;

typedef UCHAR*          PUCHAR;
typedef USHORT*         PUSHORT;
typedef ULONG*          PULONG;
typedef ULONGLONG*      PULONGLONG;
typedef LONGLONG*       PLONGLONG;
typedef WCHAR*          PWCHAR;
typedef const WCHAR*    PCWSTR;
typedef void*           PVOID;

#define NTAPI
#define FORCEINLINE             static inline
#ifndef __inline
#define __inline                inline
#endif

#define _In_
#define _In_opt_
#define _In_reads_(n)
#define _In_reads_bytes_(n)
#define _Out_
#define _Out_opt_
#define _Outptr_
#define _Outptr_opt_
#define _Out_writes_(n)
#define _Out_writes_bytes_(n)
#define _Inout_
#define _Inout_opt_
#define _Inout_updates_(n)
#define _Success_(x)

#define TRUE                    1
#define FALSE                   0

#define STATUS_SUCCESS                  ((NTSTATUS)0x00000000)
#define STATUS_UNSUCCESSFUL             ((NTSTATUS)0xC0000001)
#define STATUS_NOT_IMPLEMENTED          ((NTSTATUS)0xC0000002)
#define STATUS_INVALID_PARAMETER        ((NTSTATUS)0xC000000D)
#define STATUS_NO_SUCH_FILE             ((NTSTATUS)0xC000000F)
#define STATUS_INSUFFICIENT_RESOURCES   ((NTSTATUS)0xC000009A)
#define STATUS_DISK_FULL                ((NTSTATUS)0xC000007F)
#define STATUS_OBJECT_NAME_NOT_FOUND    ((NTSTATUS)0xC0000034)
#define STATUS_OBJECT_NAME_COLLISION    ((NTSTATUS)0xC0000035)
#define STATUS_OBJECT_NAME_INVALID      ((NTSTATUS)0xC0000033)
#define STATUS_BUFFER_TOO_SMALL         ((NTSTATUS)0xC0000023)
#define STATUS_BUFFER_OVERFLOW          ((NTSTATUS)0x80000005)
#define STATUS_END_OF_FILE              ((NTSTATUS)0xC0000011)
#define STATUS_FILE_CORRUPT_ERROR       ((NTSTATUS)0xC0000102)
#define STATUS_DISK_CORRUPT_ERROR       ((NTSTATUS)0xC0000032)
#define STATUS_NOT_FOUND                ((NTSTATUS)0xC0000225)
#define STATUS_INTERNAL_ERROR           ((NTSTATUS)0xC00000E5)
#define STATUS_ACCESS_DENIED            ((NTSTATUS)0xC0000022)

#define NT_SUCCESS(x)           (((NTSTATUS)(x)) >= 0)

#define NonPagedPool            0
#define PagedPool               1
#define NTFSLX_TAG              0

#define ExAllocatePoolWithTag(pool, sz, tag)    calloc(1, (size_t)(sz))
#define ExFreePoolWithTag(ptr, tag)             free((void*)(ptr))

#define RtlCopyMemory(d, s, n)  memcpy((d), (s), (size_t)(n))
#define RtlMoveMemory(d, s, n)  memmove((d), (s), (size_t)(n))
#define RtlZeroMemory(d, n)     memset((d), 0, (size_t)(n))
#define RtlEqualMemory(a, b, n) (memcmp((a), (b), (size_t)(n)) == 0)
#define RtlCompareMemory(a, b, n) (memcmp((a), (b), (size_t)(n)) == 0 ? (size_t)(n) : 0)

#define PAGED_CODE()            ((void)0)
#define ASSERT(x)               ((void)0)
#define NT_ASSERT(x)            ((void)0)
#define NT_VERIFY(x)            ((x) ? 1 : 0)
#define UNREFERENCED_PARAMETER(x) ((void)(x))

#define C_ASSERT(e)             typedef char __c_assert_##__LINE__[(e) ? 1 : -1]

extern int g_mkntfsimg_verbose;
#define DPRINT(fmt, ...)        ((void)0)
#define DPRINT1(fmt, ...) \
    do { if (g_mkntfsimg_verbose) fprintf(stderr, fmt, ##__VA_ARGS__); } while (0)
#define NTFSDBG                 DPRINT1

#ifdef _MSC_VER
#define PACK_STRUCT(decl)       __pragma(pack(push,1)) decl __pragma(pack(pop))
#else
#define PACK_STRUCT(decl)       decl __attribute__((packed))
#endif

#endif
