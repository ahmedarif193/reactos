/*
 * PROJECT:     ReactOS ntfslx driver
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Live cache-manager helper runtime for ntfslx
 *
 * Integration note:
 *   This file is intentionally self-contained. Wire the public prototypes into
 *   ntfslx.h and add this translation unit to the ntfslx target when the
 *   caller-side stream/FCB layer is ready. The helpers here do not fake cache
 *   success: they validate file size ordering, file-object state, and cache
 *   initialization before calling into Cc.
 */

#include "ntfslx.h"

#define NDEBUG
#include <debug.h>

#define NTFSLX_CACHE_RUNTIME_SIGNATURE 'rCxN'
#define NTFSLX_CACHE_RUNTIME_DEFAULT_READ_AHEAD_GRANULARITY PAGE_SIZE

typedef struct _NTFSLX_CACHE_RUNTIME_CONTEXT
{
    ULONG Signature;
    PDEVICE_OBJECT DeviceObject;
    PFILE_OBJECT FileObject;
    PSECTION_OBJECT_POINTERS SectionObjectPointers;
    CC_FILE_SIZES FileSizes;
    ERESOURCE Resource;
    BOOLEAN ResourceInitialized;
    BOOLEAN CacheInitialized;
    BOOLEAN TopLevelIrpSet;
    BOOLEAN TeardownInProgress;
    BOOLEAN ReadOnly;
    BOOLEAN OwnsFileObject;
    ULONG ReadAheadGranularity;
} NTFSLX_CACHE_RUNTIME_CONTEXT, *PNTFSLX_CACHE_RUNTIME_CONTEXT;

static
NTSTATUS
NtfslxCacheRuntimeValidateFileObjectState(
    _In_ PNTFSLX_CACHE_RUNTIME_CONTEXT Context,
    _In_ PFILE_OBJECT FileObject);

NTSTATUS
NTAPI
NtfslxCacheRuntimeDestroy(
    _Inout_opt_ PNTFSLX_CACHE_RUNTIME_CONTEXT CacheContext);

static
NTSTATUS
NtfslxCacheRuntimeValidateFileSizes(
    _In_ PCC_FILE_SIZES FileSizes)
{
    if (FileSizes == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (FileSizes->AllocationSize.QuadPart < 0 ||
        FileSizes->FileSize.QuadPart < 0 ||
        FileSizes->ValidDataLength.QuadPart < 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (FileSizes->AllocationSize.QuadPart < FileSizes->FileSize.QuadPart ||
        FileSizes->FileSize.QuadPart < FileSizes->ValidDataLength.QuadPart)
    {
        return STATUS_INVALID_PARAMETER;
    }

    return STATUS_SUCCESS;
}

static
NTSTATUS
NtfslxCacheRuntimeValidateFileObjectState(
    _In_ PNTFSLX_CACHE_RUNTIME_CONTEXT Context,
    _In_ PFILE_OBJECT FileObject)
{
    if (FileObject == NULL ||
        FileObject->SectionObjectPointer == NULL ||
        FileObject->Vpb == NULL)
    {
        return STATUS_INVALID_DEVICE_STATE;
    }

    if (Context != NULL)
    {
        if (FileObject != Context->FileObject ||
            Context->DeviceObject == NULL ||
            FileObject->DeviceObject != Context->DeviceObject ||
            FileObject->SectionObjectPointer != Context->SectionObjectPointers)
        {
            return STATUS_INVALID_DEVICE_STATE;
        }
    }

    if (FileObject->PrivateCacheMap != NULL)
    {
        if (!CcIsFileCached(FileObject))
        {
            return STATUS_INVALID_DEVICE_STATE;
        }
    }
    else if (CcIsFileCached(FileObject))
    {
        return STATUS_INVALID_DEVICE_STATE;
    }

    return STATUS_SUCCESS;
}

static
NTSTATUS
NtfslxCacheRuntimeValidateContext(
    _In_ PNTFSLX_CACHE_RUNTIME_CONTEXT Context)
{
    NTSTATUS Status;

    if (Context == NULL || Context->Signature != NTFSLX_CACHE_RUNTIME_SIGNATURE)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (Context->TeardownInProgress)
    {
        return STATUS_INVALID_DEVICE_STATE;
    }

    if (Context->FileObject == NULL || !Context->CacheInitialized)
    {
        return STATUS_INVALID_DEVICE_STATE;
    }

    Status = NtfslxCacheRuntimeValidateFileObjectState(Context,
                                                       Context->FileObject);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    if (Context->FileObject->PrivateCacheMap == NULL ||
        !CcIsFileCached(Context->FileObject))
    {
        return STATUS_INVALID_DEVICE_STATE;
    }

    ASSERT(Context->FileObject->SectionObjectPointer == Context->SectionObjectPointers);
    ASSERT(Context->FileObject->DeviceObject == Context->DeviceObject);
    ASSERT(Context->FileObject->PrivateCacheMap != NULL);
    ASSERT(CcIsFileCached(Context->FileObject));

    return STATUS_SUCCESS;
}

static
NTSTATUS
NtfslxCacheRuntimeValidateContextForSetup(
    _In_ PNTFSLX_CACHE_RUNTIME_CONTEXT Context)
{
    NTSTATUS Status;

    if (Context == NULL || Context->Signature != NTFSLX_CACHE_RUNTIME_SIGNATURE)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (Context->TeardownInProgress || !Context->ResourceInitialized)
    {
        return STATUS_INVALID_DEVICE_STATE;
    }

    if (Context->FileObject == NULL || Context->SectionObjectPointers == NULL)
    {
        return STATUS_INVALID_DEVICE_STATE;
    }

    Status = NtfslxCacheRuntimeValidateFileObjectState(Context,
                                                       Context->FileObject);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    if (Context->FileObject->PrivateCacheMap != NULL ||
        CcIsFileCached(Context->FileObject))
    {
        return STATUS_ALREADY_INITIALIZED;
    }

    ASSERT(Context->FileObject->SectionObjectPointer == Context->SectionObjectPointers);
    ASSERT(Context->FileObject->DeviceObject == Context->DeviceObject);

    return STATUS_SUCCESS;
}

static
PNTFSLX_CACHE_RUNTIME_CONTEXT
NtfslxCacheRuntimeAllocateContext(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PFILE_OBJECT FileObject,
    _In_ PSECTION_OBJECT_POINTERS SectionObjectPointers,
    _In_ PCC_FILE_SIZES FileSizes,
    _In_ BOOLEAN ReadOnly,
    _In_ BOOLEAN OwnsFileObject)
{
    PNTFSLX_CACHE_RUNTIME_CONTEXT Context;

    Context = ExAllocatePoolWithTag(NonPagedPool,
                                    sizeof(*Context),
                                    NTFSLX_TAG);
    if (Context == NULL)
    {
        return NULL;
    }

    RtlZeroMemory(Context, sizeof(*Context));
    Context->Signature = NTFSLX_CACHE_RUNTIME_SIGNATURE;
    Context->DeviceObject = DeviceObject;
    Context->FileObject = FileObject;
    Context->SectionObjectPointers = SectionObjectPointers;
    Context->FileSizes = *FileSizes;
    Context->ReadOnly = ReadOnly;
    Context->OwnsFileObject = OwnsFileObject;

    if (!NT_SUCCESS(ExInitializeResourceLite(&Context->Resource)))
    {
        ExFreePoolWithTag(Context, NTFSLX_TAG);
        return NULL;
    }

    Context->ResourceInitialized = TRUE;
    return Context;
}

static
VOID
NtfslxCacheRuntimeFreeContext(
    _Inout_opt_ PNTFSLX_CACHE_RUNTIME_CONTEXT Context)
{
    if (Context == NULL)
    {
        return;
    }

    Context->Signature = 0;

    if (Context->ResourceInitialized)
    {
        ExDeleteResourceLite(&Context->Resource);
        Context->ResourceInitialized = FALSE;
    }

    ExFreePoolWithTag(Context, NTFSLX_TAG);
}

static
BOOLEAN
NtfslxCacheRuntimeAcquireContext(
    _Inout_ PNTFSLX_CACHE_RUNTIME_CONTEXT Context,
    _In_ BOOLEAN Wait,
    _In_ BOOLEAN Exclusive)
{
    BOOLEAN Acquired;

    if (Context == NULL ||
        Context->Signature != NTFSLX_CACHE_RUNTIME_SIGNATURE ||
        Context->TeardownInProgress ||
        !Context->ResourceInitialized)
    {
        return FALSE;
    }

    if (Exclusive)
    {
        Acquired = ExAcquireResourceExclusiveLite(&Context->Resource, Wait);
    }
    else
    {
        Acquired = ExAcquireResourceSharedLite(&Context->Resource, Wait);
    }

    if (!Acquired)
    {
        return FALSE;
    }

    ASSERT(Context->TopLevelIrpSet == FALSE);

    if (IoGetTopLevelIrp() == NULL)
    {
        IoSetTopLevelIrp((PIRP)FSRTL_CACHE_TOP_LEVEL_IRP);
        Context->TopLevelIrpSet = TRUE;
    }

    return TRUE;
}

static
VOID
NtfslxCacheRuntimeReleaseContext(
    _Inout_opt_ PNTFSLX_CACHE_RUNTIME_CONTEXT Context)
{
    if (Context == NULL || Context->Signature != NTFSLX_CACHE_RUNTIME_SIGNATURE)
    {
        return;
    }

    if (Context->TopLevelIrpSet)
    {
        ASSERT(IoGetTopLevelIrp() == (PIRP)FSRTL_CACHE_TOP_LEVEL_IRP);
        IoSetTopLevelIrp(NULL);
        Context->TopLevelIrpSet = FALSE;
    }

    if (Context->ResourceInitialized)
    {
        ExReleaseResourceLite(&Context->Resource);
    }
}

static
BOOLEAN
NTAPI
NtfslxCacheRuntimeAcquireForLazyWrite(
    _In_ PVOID Context,
    _In_ BOOLEAN Wait)
{
    return NtfslxCacheRuntimeAcquireContext((PNTFSLX_CACHE_RUNTIME_CONTEXT)Context,
                                            Wait,
                                            TRUE);
}

static
VOID
NTAPI
NtfslxCacheRuntimeReleaseFromLazyWrite(
    _In_ PVOID Context)
{
    NtfslxCacheRuntimeReleaseContext((PNTFSLX_CACHE_RUNTIME_CONTEXT)Context);
}

static
BOOLEAN
NTAPI
NtfslxCacheRuntimeAcquireForReadAhead(
    _In_ PVOID Context,
    _In_ BOOLEAN Wait)
{
    return NtfslxCacheRuntimeAcquireContext((PNTFSLX_CACHE_RUNTIME_CONTEXT)Context,
                                            Wait,
                                            FALSE);
}

static
VOID
NTAPI
NtfslxCacheRuntimeReleaseFromReadAhead(
    _In_ PVOID Context)
{
    NtfslxCacheRuntimeReleaseContext((PNTFSLX_CACHE_RUNTIME_CONTEXT)Context);
}

static const CACHE_MANAGER_CALLBACKS NtfslxCacheRuntimeCallbacks =
{
    NtfslxCacheRuntimeAcquireForLazyWrite,
    NtfslxCacheRuntimeReleaseFromLazyWrite,
    NtfslxCacheRuntimeAcquireForReadAhead,
    NtfslxCacheRuntimeReleaseFromReadAhead
};

static
NTSTATUS
NtfslxCacheRuntimeCreateStreamFileObject(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PSECTION_OBJECT_POINTERS SectionObjectPointers,
    _Outptr_ PFILE_OBJECT *StreamFileObject)
{
    PFILE_OBJECT FileObject;

    if (StreamFileObject != NULL)
    {
        *StreamFileObject = NULL;
    }

    if (DeviceObject == NULL ||
        DeviceObject->Vpb == NULL ||
        SectionObjectPointers == NULL ||
        StreamFileObject == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    FileObject = IoCreateStreamFileObject(NULL, DeviceObject);
    if (FileObject == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    FileObject->Vpb = DeviceObject->Vpb;
    FileObject->SectionObjectPointer = SectionObjectPointers;

    ASSERT(FileObject->DeviceObject == DeviceObject);
    ASSERT(FileObject->Vpb == DeviceObject->Vpb);
    ASSERT(FileObject->SectionObjectPointer == SectionObjectPointers);

    *StreamFileObject = FileObject;
    return STATUS_SUCCESS;
}

static
NTSTATUS
NtfslxCacheRuntimeInitializeCacheMapInternal(
    _In_ PNTFSLX_CACHE_RUNTIME_CONTEXT Context)
{
    NTSTATUS Status;
    PCC_FILE_SIZES CacheFileSizes;
    BOOLEAN Initialized;

    Status = NtfslxCacheRuntimeValidateContextForSetup(Context);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    CacheFileSizes = &Context->FileSizes;

    ASSERT(CacheFileSizes->AllocationSize.QuadPart >= 0);
    ASSERT(CacheFileSizes->FileSize.QuadPart >= 0);
    ASSERT(CacheFileSizes->ValidDataLength.QuadPart >= 0);
    ASSERT(CacheFileSizes->AllocationSize.QuadPart >= CacheFileSizes->FileSize.QuadPart);
    ASSERT(CacheFileSizes->FileSize.QuadPart >= CacheFileSizes->ValidDataLength.QuadPart);

    if (Context->FileObject->PrivateCacheMap != NULL ||
        CcIsFileCached(Context->FileObject))
    {
        return STATUS_ALREADY_INITIALIZED;
    }

    Initialized = FALSE;
    _SEH2_TRY
    {
        CcInitializeCacheMap(Context->FileObject,
                             CacheFileSizes,
                             FALSE,
                             (PCACHE_MANAGER_CALLBACKS)&NtfslxCacheRuntimeCallbacks,
                             Context);
        Initialized = TRUE;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
        DbgPrint("ntfslx: CcInitializeCacheMap raised 0x%08lx\n", Status);
    }
    _SEH2_END;

    if (!Initialized)
    {
        return Status;
    }

    if (Context->FileObject->PrivateCacheMap == NULL ||
        !CcIsFileCached(Context->FileObject))
    {
        if (Context->FileObject->PrivateCacheMap != NULL ||
            CcIsFileCached(Context->FileObject))
        {
            CcUninitializeCacheMap(Context->FileObject, NULL, NULL);
        }

        return STATUS_INVALID_DEVICE_STATE;
    }

    Context->CacheInitialized = TRUE;
    ASSERT(Context->FileObject->PrivateCacheMap != NULL);
    ASSERT(CcIsFileCached(Context->FileObject));
    ASSERT(Context->FileObject->SectionObjectPointer == Context->SectionObjectPointers);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NtfslxCacheRuntimeApplyReadAheadGranularity(
    _In_ PNTFSLX_CACHE_RUNTIME_CONTEXT Context,
    _In_ ULONG ReadAheadGranularity)
{
    ULONG Granularity;
    NTSTATUS Status;

    Status = NtfslxCacheRuntimeValidateContext(Context);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Granularity = ReadAheadGranularity;
    if (Granularity == 0)
    {
        Granularity = NTFSLX_CACHE_RUNTIME_DEFAULT_READ_AHEAD_GRANULARITY;
    }

    ASSERT(Granularity != 0);
    ASSERT(Context->FileObject->PrivateCacheMap != NULL);
    CcSetReadAheadGranularity(Context->FileObject, Granularity);
    Context->ReadAheadGranularity = Granularity;

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
NtfslxCacheRuntimeCreate(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PSECTION_OBJECT_POINTERS SectionObjectPointers,
    _In_ PCC_FILE_SIZES FileSizes,
    _In_ BOOLEAN ReadOnly,
    _In_ ULONG ReadAheadGranularity,
    _Outptr_ PNTFSLX_CACHE_RUNTIME_CONTEXT *CacheContext)
{
    NTSTATUS Status;
    PFILE_OBJECT StreamFileObject;
    PNTFSLX_CACHE_RUNTIME_CONTEXT Context;

    if (CacheContext != NULL)
    {
        *CacheContext = NULL;
    }

    Status = NtfslxCacheRuntimeValidateFileSizes(FileSizes);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = NtfslxCacheRuntimeCreateStreamFileObject(DeviceObject,
                                                      SectionObjectPointers,
                                                      &StreamFileObject);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Context = NtfslxCacheRuntimeAllocateContext(DeviceObject,
                                                StreamFileObject,
                                                SectionObjectPointers,
                                                FileSizes,
                                                ReadOnly,
                                                TRUE);
    if (Context == NULL)
    {
        ObDereferenceObject(StreamFileObject);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = NtfslxCacheRuntimeInitializeCacheMapInternal(Context);
    if (!NT_SUCCESS(Status))
    {
        (VOID)NtfslxCacheRuntimeDestroy(Context);
        return Status;
    }

    Status = NtfslxCacheRuntimeApplyReadAheadGranularity(Context,
                                                         ReadAheadGranularity);
    if (!NT_SUCCESS(Status))
    {
        (VOID)NtfslxCacheRuntimeDestroy(Context);
        return Status;
    }

    ASSERT(Context->Signature == NTFSLX_CACHE_RUNTIME_SIGNATURE);
    ASSERT(Context->CacheInitialized);
    ASSERT(Context->FileObject == StreamFileObject);
    ASSERT(Context->FileObject->PrivateCacheMap != NULL);

    if (CacheContext != NULL)
    {
        *CacheContext = Context;
    }

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
NtfslxCacheRuntimeAttach(
    _In_ PFILE_OBJECT FileObject,
    _In_ PCC_FILE_SIZES FileSizes,
    _In_ BOOLEAN ReadOnly,
    _In_ ULONG ReadAheadGranularity,
    _Outptr_ PNTFSLX_CACHE_RUNTIME_CONTEXT *CacheContext)
{
    NTSTATUS Status;
    PNTFSLX_CACHE_RUNTIME_CONTEXT Context;
    PDEVICE_OBJECT DeviceObject;

    if (CacheContext != NULL)
    {
        *CacheContext = NULL;
    }

    if (FileObject == NULL ||
        FileObject->DeviceObject == NULL ||
        FileObject->SectionObjectPointer == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = NtfslxCacheRuntimeValidateFileSizes(FileSizes);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    DeviceObject = FileObject->DeviceObject;

    Context = NtfslxCacheRuntimeAllocateContext(DeviceObject,
                                                FileObject,
                                                FileObject->SectionObjectPointer,
                                                FileSizes,
                                                ReadOnly,
                                                FALSE);
    if (Context == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = NtfslxCacheRuntimeInitializeCacheMapInternal(Context);
    if (!NT_SUCCESS(Status))
    {
        (VOID)NtfslxCacheRuntimeDestroy(Context);
        return Status;
    }

    Status = NtfslxCacheRuntimeApplyReadAheadGranularity(Context,
                                                         ReadAheadGranularity);
    if (!NT_SUCCESS(Status))
    {
        (VOID)NtfslxCacheRuntimeDestroy(Context);
        return Status;
    }

    ASSERT(Context->Signature == NTFSLX_CACHE_RUNTIME_SIGNATURE);
    ASSERT(Context->CacheInitialized);
    ASSERT(Context->FileObject == FileObject);
    ASSERT(Context->OwnsFileObject == FALSE);
    ASSERT(Context->FileObject->PrivateCacheMap != NULL);

    if (CacheContext != NULL)
    {
        *CacheContext = Context;
    }

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
NtfslxCacheRuntimeSetFileSizes(
    _In_ PNTFSLX_CACHE_RUNTIME_CONTEXT CacheContext,
    _In_ PCC_FILE_SIZES NewFileSizes)
{
    NTSTATUS Status;

    Status = NtfslxCacheRuntimeValidateFileSizes(NewFileSizes);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = NtfslxCacheRuntimeValidateContext(CacheContext);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    _SEH2_TRY
    {
        CcSetFileSizes(CacheContext->FileObject, NewFileSizes);
        CacheContext->FileSizes = *NewFileSizes;
        Status = STATUS_SUCCESS;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
        DbgPrint("ntfslx: CcSetFileSizes raised 0x%08lx\n", Status);
    }
    _SEH2_END;

    return Status;
}

NTSTATUS
NTAPI
NtfslxCacheRuntimeCopyRead(
    _In_ PNTFSLX_CACHE_RUNTIME_CONTEXT CacheContext,
    _In_ PLARGE_INTEGER ByteOffset,
    _In_ ULONG Length,
    _In_ BOOLEAN Wait,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _Inout_ PIO_STATUS_BLOCK IoStatus)
{
    BOOLEAN Result;
    NTSTATUS Status;

    if (IoStatus != NULL)
    {
        IoStatus->Status = STATUS_UNSUCCESSFUL;
        IoStatus->Information = 0;
    }

    if (CacheContext == NULL ||
        ByteOffset == NULL ||
        IoStatus == NULL ||
        (Length > 0 && Buffer == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (ByteOffset->QuadPart < 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = NtfslxCacheRuntimeValidateContext(CacheContext);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    if (Length == 0)
    {
        IoStatus->Status = STATUS_SUCCESS;
        IoStatus->Information = 0;
        return STATUS_SUCCESS;
    }

    Result = CcCopyRead(CacheContext->FileObject,
                        ByteOffset,
                        Length,
                        Wait,
                        Buffer,
                        IoStatus);

    if (!Result)
    {
        if (!Wait)
        {
            IoStatus->Status = STATUS_CANT_WAIT;
            IoStatus->Information = 0;
            return STATUS_CANT_WAIT;
        }

        if (!NT_SUCCESS(IoStatus->Status))
        {
            return IoStatus->Status;
        }

        return STATUS_UNSUCCESSFUL;
    }

    ASSERT(NT_SUCCESS(IoStatus->Status));
    ASSERT(CacheContext->FileObject->PrivateCacheMap != NULL);
    return IoStatus->Status;
}

NTSTATUS
NTAPI
NtfslxCacheRuntimeValidate(
    _In_ PNTFSLX_CACHE_RUNTIME_CONTEXT CacheContext)
{
    return NtfslxCacheRuntimeValidateContext(CacheContext);
}

NTSTATUS
NTAPI
NtfslxCacheRuntimeDestroy(
    _Inout_opt_ PNTFSLX_CACHE_RUNTIME_CONTEXT CacheContext)
{
    NTSTATUS Status;

    if (CacheContext == NULL)
    {
        return STATUS_SUCCESS;
    }

    if (CacheContext->Signature != NTFSLX_CACHE_RUNTIME_SIGNATURE)
    {
        return STATUS_INVALID_PARAMETER;
    }

    CacheContext->TeardownInProgress = TRUE;

    Status = STATUS_SUCCESS;

    if (CacheContext->FileObject != NULL)
    {
        if (CacheContext->CacheInitialized)
        {
            if (CacheContext->FileObject->PrivateCacheMap != NULL ||
                CcIsFileCached(CacheContext->FileObject))
            {
                ASSERT(CacheContext->FileObject->SectionObjectPointer ==
                       CacheContext->SectionObjectPointers);
                _SEH2_TRY
                {
                    CcUninitializeCacheMap(CacheContext->FileObject, NULL, NULL);
                }
                _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
                {
                    Status = _SEH2_GetExceptionCode();
                    DbgPrint("ntfslx: CcUninitializeCacheMap raised 0x%08lx\n",
                             Status);
                }
                _SEH2_END;
            }
            else
            {
                Status = STATUS_INVALID_DEVICE_STATE;
            }

            CacheContext->CacheInitialized = FALSE;
        }

        if (CacheContext->OwnsFileObject)
        {
            CacheContext->FileObject->SectionObjectPointer = NULL;
            CacheContext->FileObject->Vpb = NULL;
            ObDereferenceObject(CacheContext->FileObject);
        }

        CacheContext->FileObject = NULL;
    }

    ASSERT(CacheContext->TopLevelIrpSet == FALSE);
    NtfslxCacheRuntimeFreeContext(CacheContext);
    return Status;
}
