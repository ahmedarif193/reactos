/*
 * PROJECT:     ReactOS host-native tests
 * FILE:        submodules/host-tests/mmcc/cc/host/ccntshim.h
 * PURPOSE:     NT cache manager host-test compatibility definitions
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once

#ifdef MM_HOST_TEST
#include <nvs/include/mm.h>
#endif
#include <cc/include/ccengine.h>
#include <pthread.h>
#include <setjmp.h>

#define NTAPI
#define PIN_WAIT 1
#define PIN_EXCLUSIVE 2
#define PIN_NO_READ 4
#define PIN_IF_BCB 8
#define MAP_WAIT 1
#define UNINITIALIZE_CACHE_MAPS 1
#define FO_RANDOM_ACCESS 0x800
#define STATUS_ACCESS_VIOLATION ((NTSTATUS)0xC0000005)
#define STATUS_INVALID_USER_BUFFER ((NTSTATUS)0xC00000E8)
#define KernelMode 0
#define NonPagedPool 0
#define IO_NO_INCREMENT 0
#define ASSERT CC_ASSERT
#define min(a, b) ((a) < (b) ? (a) : (b))
#define ROUND_UP(a, b) (((a) + (b) - 1) & ~((b) - 1))

typedef LONG64 LONGLONG;
typedef union _LARGE_INTEGER { LONG64 QuadPart; } LARGE_INTEGER, *PLARGE_INTEGER;
typedef struct _SECTION_OBJECT_POINTERS { PVOID SharedCacheMap; PVOID DataSectionObject; PVOID ImageSectionObject; } SECTION_OBJECT_POINTERS, *PSECTION_OBJECT_POINTERS;
typedef struct _FILE_OBJECT
{
    PSECTION_OBJECT_POINTERS SectionObjectPointer;
    PVOID PrivateCacheMap;
    ULONG Flags;
    LONG References;
    PVOID FsContext;
} FILE_OBJECT, *PFILE_OBJECT;
typedef struct _ETHREAD *PETHREAD;
#ifdef MM_HOST_TEST
typedef struct _MI_CONTROL_AREA { PMI_SEGMENT Segment; PFILE_OBJECT FileObject; } MI_CONTROL_AREA, *PMI_CONTROL_AREA;
VOID MiDereferenceControlArea(PMI_CONTROL_AREA Control);
PMI_CONTROL_AREA MiReferenceDataControlArea(PSECTION_OBJECT_POINTERS Pointers);
BOOLEAN CcPurgeCacheSection(PSECTION_OBJECT_POINTERS Pointers, PLARGE_INTEGER Offset, ULONG Length, ULONG Flags);
#define MmFlushForWrite 0
BOOLEAN MmFlushImageSection(PSECTION_OBJECT_POINTERS Pointers, ULONG Type);
BOOLEAN MmCanFileBeTruncated(PSECTION_OBJECT_POINTERS Pointers, PLARGE_INTEGER NewFileSize);
NTSTATUS MiWaitForMemory(NTSTATUS Status, PULONG Attempts);
#else
typedef struct _MI_CONTROL_AREA *PMI_CONTROL_AREA;
#endif
typedef struct _KEVENT { LONG State; PVOID Context; } KEVENT;
typedef CC_LOCK KSPIN_LOCK;
typedef CC_RESOURCE ERESOURCE, *PERESOURCE;
typedef ULONG_PTR ERESOURCE_THREAD;
typedef VOID (*PFLUSH_TO_LSN)(PVOID, LARGE_INTEGER);
typedef struct _CACHE_MANAGER_CALLBACKS
{
    BOOLEAN (*AcquireForLazyWrite)(PVOID, BOOLEAN);
    VOID (*ReleaseFromLazyWrite)(PVOID);
    BOOLEAN (*AcquireForReadAhead)(PVOID, BOOLEAN);
    VOID (*ReleaseFromReadAhead)(PVOID);
} CACHE_MANAGER_CALLBACKS;
typedef struct _PUBLIC_BCB
{
    USHORT NodeTypeCode;
    USHORT NodeByteSize;
    ULONG MappedLength;
    LARGE_INTEGER MappedFileOffset;
} PUBLIC_BCB, *PPUBLIC_BCB;
typedef struct _IO_STATUS_BLOCK { NTSTATUS Status; ULONG_PTR Information; } IO_STATUS_BLOCK, *PIO_STATUS_BLOCK;

#define InterlockedIncrement(p) (CC_ATOMIC_ADD32((p), 1) + 1)
#define CcGetFileSizePointer(FO) ((PLARGE_INTEGER)((FO)->SectionObjectPointer->SharedCacheMap) + 1)

extern ULONG CcMapDataWait, CcMapDataNoWait, CcPinReadWait, CcPinReadNoWait, CcPinMappedDataCount;
extern ULONG CcFastReadNoWait, CcFastReadWait;

typedef struct _CC_HOST_EXCEPTION
{
    jmp_buf Jump;
    struct _CC_HOST_EXCEPTION *Previous;
    volatile NTSTATUS Status;
} CC_HOST_EXCEPTION;

extern _Thread_local CC_HOST_EXCEPTION *CcHostException;

#define _SEH2_TRY do { CC_HOST_EXCEPTION Exception = { .Previous = CcHostException }; \
    CcHostException = &Exception; if (setjmp(Exception.Jump) == 0)
#define _SEH2_EXCEPT(filter) else
#define _SEH2_GetExceptionCode() (Exception.Status)
#define _SEH2_END CcHostException = Exception.Previous; } while (0)
#define ExAllocatePoolWithTag(type, bytes, tag) malloc(bytes)
#define ExFreePoolWithTag(pointer, tag) free(pointer)

VOID KeDelayExecutionThread(ULONG Mode, BOOLEAN Alertable, PLARGE_INTEGER Interval);
LONG KeSetEvent(KEVENT *Event, LONG Increment, BOOLEAN Wait);
VOID ObDereferenceObject(PVOID Object);
NTSTATUS MiPagingIo(PFILE_OBJECT File, ULONG64 Offset, ULONG Length, PVOID Buffer, BOOLEAN Write, PULONG Transferred);

BOOLEAN ExAcquireResourceExclusiveLite(PERESOURCE Resource, BOOLEAN Wait);
BOOLEAN ExAcquireResourceSharedLite(PERESOURCE Resource, BOOLEAN Wait);
NTSTATUS ExInitializeResourceLite(PERESOURCE Resource);
NTSTATUS ExDeleteResourceLite(PERESOURCE Resource);
VOID ExReleaseResourceLite(PERESOURCE Resource);
VOID ExReleaseResourceForThreadLite(PERESOURCE Resource, ERESOURCE_THREAD Thread);
VOID ExSetResourceOwnerPointer(PERESOURCE Resource, PVOID Owner);
_Noreturn VOID ExRaiseStatus(NTSTATUS Status);

BOOLEAN CcMapData(PFILE_OBJECT File, PLARGE_INTEGER Offset, ULONG Length, ULONG Flags, PVOID *Bcb, PVOID *Buffer);
BOOLEAN CcPinRead(PFILE_OBJECT File, PLARGE_INTEGER Offset, ULONG Length, ULONG Flags, PVOID *Bcb, PVOID *Buffer);
BOOLEAN CcPinMappedData(PFILE_OBJECT File, PLARGE_INTEGER Offset, ULONG Length, ULONG Flags, PVOID *Bcb);
VOID CcUnpinData(PVOID Bcb);
VOID CcSetDirtyPinnedData(PVOID Bcb, PLARGE_INTEGER Lsn);
BOOLEAN CcCopyRead(PFILE_OBJECT File, PLARGE_INTEGER Offset, ULONG Length, BOOLEAN Wait, PVOID Buffer,
                   PIO_STATUS_BLOCK IoStatus);
BOOLEAN CcCopyWrite(PFILE_OBJECT File, PLARGE_INTEGER Offset, ULONG Length, BOOLEAN Wait, PVOID Buffer);
