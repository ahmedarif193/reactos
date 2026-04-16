/*
 * PROJECT:     ReactOS NTFS Linux-Port Skeleton
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Cache Manager integration scaffolding for staged ntfslx
 *
 * Integration note:
 *   This file is intentionally self-contained until the stream/FCB layer exists.
 *   Wire the following prototypes into ntfslx.h when the caller side is ready:
 *     - NtfslxInitializeCacheManagerCallbacks
 *     - NtfslxInitializeCacheMap
 *     - NtfslxTeardownCacheMap
 *   Call NtfslxInitializeCacheMap only after a valid file object, section object
 *   pointer, and PCC_FILE_SIZES are available. Store the returned cache context
 *   with the stream/file state so teardown can hand it back here later.
 */

#include "ntfslx.h"

#define NDEBUG
#include <debug.h>

#define NTFSLX_CACHE_CONTEXT_SIGNATURE 0x4E586343UL /* 'CcXN' */

typedef struct _NTFSLX_CACHE_CONTEXT
{
    ULONG Signature;
    PFILE_OBJECT FileObject;
    CC_FILE_SIZES FileSizes;
    ERESOURCE Resource;
    BOOLEAN ResourceInitialized;
    BOOLEAN TopLevelIrpSet;
    BOOLEAN TeardownInProgress;
    BOOLEAN ReadOnly;
} NTFSLX_CACHE_CONTEXT, *PNTFSLX_CACHE_CONTEXT;

static
VOID
NtfslxNormalizeFileSizes(
    _Inout_ PCC_FILE_SIZES FileSizes)
{
    if (FileSizes->AllocationSize.QuadPart < 0)
    {
        FileSizes->AllocationSize.QuadPart = 0;
    }

    if (FileSizes->FileSize.QuadPart < 0)
    {
        FileSizes->FileSize.QuadPart = 0;
    }

    if (FileSizes->ValidDataLength.QuadPart < 0)
    {
        FileSizes->ValidDataLength.QuadPart = 0;
    }

    if (FileSizes->AllocationSize.QuadPart < FileSizes->FileSize.QuadPart)
    {
        FileSizes->AllocationSize.QuadPart = FileSizes->FileSize.QuadPart;
    }

    if (FileSizes->FileSize.QuadPart < FileSizes->ValidDataLength.QuadPart)
    {
        FileSizes->FileSize.QuadPart = FileSizes->ValidDataLength.QuadPart;
    }

    if (FileSizes->AllocationSize.QuadPart < FileSizes->ValidDataLength.QuadPart)
    {
        FileSizes->AllocationSize.QuadPart = FileSizes->ValidDataLength.QuadPart;
    }
}

static
PNTFSLX_CACHE_CONTEXT
NtfslxAllocateCacheContext(
    _In_ PFILE_OBJECT FileObject,
    _In_ BOOLEAN ReadOnly)
{
    PNTFSLX_CACHE_CONTEXT CacheContext;
    NTSTATUS Status;

    CacheContext = ExAllocatePoolWithTag(NonPagedPool,
                                         sizeof(*CacheContext),
                                         NTFSLX_TAG);
    if (CacheContext == NULL)
    {
        return NULL;
    }

    RtlZeroMemory(CacheContext, sizeof(*CacheContext));
    CacheContext->Signature = NTFSLX_CACHE_CONTEXT_SIGNATURE;
    CacheContext->FileObject = FileObject;
    CacheContext->ReadOnly = ReadOnly;

    Status = ExInitializeResourceLite(&CacheContext->Resource);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(CacheContext, NTFSLX_TAG);
        return NULL;
    }

    CacheContext->ResourceInitialized = TRUE;
    return CacheContext;
}

static
VOID
NtfslxFreeCacheContext(
    _Inout_opt_ PNTFSLX_CACHE_CONTEXT CacheContext)
{
    if (CacheContext == NULL)
    {
        return;
    }

    if (CacheContext->ResourceInitialized)
    {
        ExDeleteResourceLite(&CacheContext->Resource);
        CacheContext->ResourceInitialized = FALSE;
    }

    CacheContext->Signature = 0;
    ExFreePoolWithTag(CacheContext, NTFSLX_TAG);
}

static
BOOLEAN
NtfslxAcquireCacheContext(
    _Inout_ PNTFSLX_CACHE_CONTEXT CacheContext,
    _In_ BOOLEAN Wait,
    _In_ BOOLEAN Exclusive)
{
    if (CacheContext == NULL ||
        CacheContext->Signature != NTFSLX_CACHE_CONTEXT_SIGNATURE ||
        CacheContext->TeardownInProgress)
    {
        return FALSE;
    }

    if (CacheContext->ResourceInitialized)
    {
        if (Exclusive)
        {
            if (!ExAcquireResourceExclusiveLite(&CacheContext->Resource, Wait))
            {
                return FALSE;
            }
        }
        else
        {
            if (!ExAcquireResourceSharedLite(&CacheContext->Resource, Wait))
            {
                return FALSE;
            }
        }
    }

    if (IoGetTopLevelIrp() == NULL)
    {
        IoSetTopLevelIrp((PIRP)FSRTL_CACHE_TOP_LEVEL_IRP);
        CacheContext->TopLevelIrpSet = TRUE;
    }
    else
    {
        CacheContext->TopLevelIrpSet = FALSE;
    }

    return TRUE;
}

static
VOID
NtfslxReleaseCacheContext(
    _Inout_opt_ PNTFSLX_CACHE_CONTEXT CacheContext)
{
    if (CacheContext == NULL ||
        CacheContext->Signature != NTFSLX_CACHE_CONTEXT_SIGNATURE)
    {
        return;
    }

    if (CacheContext->TopLevelIrpSet &&
        IoGetTopLevelIrp() == (PIRP)FSRTL_CACHE_TOP_LEVEL_IRP)
    {
        IoSetTopLevelIrp(NULL);
    }

    CacheContext->TopLevelIrpSet = FALSE;

    if (CacheContext->ResourceInitialized)
    {
        ExReleaseResourceLite(&CacheContext->Resource);
    }
}

static
BOOLEAN
NTAPI
NtfslxAcquireForLazyWrite(
    _In_ PVOID Context,
    _In_ BOOLEAN Wait)
{
    PNTFSLX_CACHE_CONTEXT CacheContext;

    CacheContext = (PNTFSLX_CACHE_CONTEXT)Context;
    return NtfslxAcquireCacheContext(CacheContext, Wait, TRUE);
}

static
VOID
NTAPI
NtfslxReleaseFromLazyWrite(
    _In_ PVOID Context)
{
    NtfslxReleaseCacheContext((PNTFSLX_CACHE_CONTEXT)Context);
}

static
BOOLEAN
NTAPI
NtfslxAcquireForReadAhead(
    _In_ PVOID Context,
    _In_ BOOLEAN Wait)
{
    PNTFSLX_CACHE_CONTEXT CacheContext;

    CacheContext = (PNTFSLX_CACHE_CONTEXT)Context;
    return NtfslxAcquireCacheContext(CacheContext, Wait, FALSE);
}

static
VOID
NTAPI
NtfslxReleaseFromReadAhead(
    _In_ PVOID Context)
{
    NtfslxReleaseCacheContext((PNTFSLX_CACHE_CONTEXT)Context);
}

static CACHE_MANAGER_CALLBACKS NtfslxCacheCallbacks =
{
    NtfslxAcquireForLazyWrite,
    NtfslxReleaseFromLazyWrite,
    NtfslxAcquireForReadAhead,
    NtfslxReleaseFromReadAhead
};

VOID
NTAPI
NtfslxInitializeCacheManagerCallbacks(
    _Out_ PCACHE_MANAGER_CALLBACKS Callbacks)
{
    if (Callbacks == NULL)
    {
        return;
    }

    *Callbacks = NtfslxCacheCallbacks;
}

NTSTATUS
NTAPI
NtfslxInitializeCacheMap(
    _In_ PFILE_OBJECT FileObject,
    _In_ PCC_FILE_SIZES FileSizes,
    _In_ BOOLEAN ReadOnly,
    _Outptr_ PNTFSLX_CACHE_CONTEXT *CacheContext)
{
    PNTFSLX_CACHE_CONTEXT Context;
    CC_FILE_SIZES LocalSizes;

    if (CacheContext != NULL)
    {
        *CacheContext = NULL;
    }

    if (FileObject == NULL ||
        FileSizes == NULL ||
        FileObject->SectionObjectPointer == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (FileObject->PrivateCacheMap != NULL || CcIsFileCached(FileObject))
    {
        return STATUS_SUCCESS;
    }

    Context = NtfslxAllocateCacheContext(FileObject, ReadOnly);
    if (Context == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    LocalSizes = *FileSizes;
    NtfslxNormalizeFileSizes(&LocalSizes);
    Context->FileSizes = LocalSizes;

    CcInitializeCacheMap(FileObject,
                         &Context->FileSizes,
                         FALSE,
                         &NtfslxCacheCallbacks,
                         Context);

    if (CacheContext != NULL)
    {
        *CacheContext = Context;
    }

    return STATUS_SUCCESS;
}

VOID
NTAPI
NtfslxTeardownCacheMap(
    _In_opt_ PFILE_OBJECT FileObject,
    _In_opt_ PLARGE_INTEGER TruncateSize,
    _Inout_opt_ PNTFSLX_CACHE_CONTEXT CacheContext)
{
    if (CacheContext != NULL)
    {
        CacheContext->TeardownInProgress = TRUE;
    }

    if (FileObject != NULL &&
        FileObject->SectionObjectPointer != NULL &&
        CcIsFileCached(FileObject))
    {
        CcUninitializeCacheMap(FileObject, TruncateSize, NULL);
    }

    if (CacheContext != NULL)
    {
        NtfslxFreeCacheContext(CacheContext);
    }
}

/*
 * This module is intentionally conservative: it sets up the cache-manager
 * contract and keeps the synchronization primitive local until the real FCB /
 * stream-object layer exists. Callers can replace the stub behavior with per-
 * stream locks later without changing the cache manager wiring shape.
 */
