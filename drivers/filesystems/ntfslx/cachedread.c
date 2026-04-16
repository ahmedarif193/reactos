/*
 * PROJECT:     ReactOS ntfslx driver
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Conservative cached-read helper layer for ntfslx file objects
 *
 * Integration note:
 *   This translation unit is intentionally self-contained. It relies on the
 *   existing cache runtime helpers in cache_runtime.c and on the file-object
 *   layout established by fileio.c / vpbguard.c, but it does not require any
 *   caller-side hacks or hidden global state.
 *
 *   External edits still needed to use this from the driver proper:
 *     - add this file to the ntfslx target in the build files
 *     - add public prototypes to ntfslx.h
 *     - wire the read path to call these helpers on the cached-read branch
 */

#include "ntfslx.h"

#define NDEBUG
#include <debug.h>

static
NTSTATUS
NtfslxCachedReadValidateBindings(
    _In_ PFILE_OBJECT FileObject,
    _In_opt_ PNTFSLX_DEVICE_EXTENSION DeviceExtension,
    _Outptr_opt_ PNTFSLX_FILE_CONTEXT *FileContext)
{
    PNTFSLX_FILE_CONTEXT Context;
    PNTFSLX_DEVICE_EXTENSION ResolvedDeviceExtension;
    PVPB Vpb;

    if (FileContext != NULL)
    {
        *FileContext = NULL;
    }

    if (FileObject == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (FileObject->SectionObjectPointer == NULL)
    {
        return STATUS_INVALID_DEVICE_STATE;
    }

    if (FileObject->DeviceObject == NULL)
    {
        return STATUS_INVALID_DEVICE_STATE;
    }

    Vpb = FileObject->Vpb;
    if (Vpb == NULL)
    {
        return STATUS_INVALID_DEVICE_STATE;
    }

    if (Vpb->Type != IO_TYPE_VPB || Vpb->Size != sizeof(VPB))
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    Context = (PNTFSLX_FILE_CONTEXT)FileObject->FsContext;
    if (Context == NULL)
    {
        return STATUS_INVALID_DEVICE_REQUEST;
    }

    if (Context->Signature != NTFSLX_FILE_CONTEXT_SIGNATURE)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    ResolvedDeviceExtension = DeviceExtension;
    if (ResolvedDeviceExtension == NULL)
    {
        ResolvedDeviceExtension = Context->DeviceExtension;
    }

    if (ResolvedDeviceExtension == NULL ||
        ResolvedDeviceExtension->Signature != NTFSLX_TAG ||
        !NtfslxIsVolumeDevice(ResolvedDeviceExtension))
    {
        return STATUS_INVALID_DEVICE_STATE;
    }

    if (Context->DeviceExtension != ResolvedDeviceExtension ||
        FileObject->DeviceObject != ResolvedDeviceExtension->DeviceObject ||
        FileObject->Vpb != ResolvedDeviceExtension->Vpb)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    if (Context->IsDirectory)
    {
        return STATUS_INVALID_DEVICE_REQUEST;
    }

    if (Context->DataAttribute == NULL)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    if (!Context->ResidentData && Context->DataRunlist == NULL)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    if (Context->AllocationSize < Context->DataSize)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    if (FileObject->PrivateCacheMap != NULL && !CcIsFileCached(FileObject))
    {
        return STATUS_INVALID_DEVICE_STATE;
    }

    if (FileContext != NULL)
    {
        *FileContext = Context;
    }

    return STATUS_SUCCESS;
}

static
NTSTATUS
NtfslxCachedReadBuildFileSizes(
    _In_ PNTFSLX_FILE_CONTEXT FileContext,
    _Out_ PCC_FILE_SIZES FileSizes,
    _Out_ PBOOLEAN ReadOnly)
{
    if (FileContext == NULL || FileSizes == NULL || ReadOnly == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (FileContext->AllocationSize > (ULONGLONG)MAXLONGLONG ||
        FileContext->DataSize > (ULONGLONG)MAXLONGLONG)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    if (FileContext->AllocationSize < FileContext->DataSize)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    RtlZeroMemory(FileSizes, sizeof(*FileSizes));
    FileSizes->AllocationSize.QuadPart = (LONGLONG)FileContext->AllocationSize;
    FileSizes->FileSize.QuadPart = (LONGLONG)FileContext->DataSize;
    FileSizes->ValidDataLength.QuadPart = (LONGLONG)FileContext->DataSize;
    *ReadOnly = (BOOLEAN)!FileContext->SupportsWrite;

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
NtfslxCachedReadValidateFileObject(
    _In_ PFILE_OBJECT FileObject,
    _In_opt_ PNTFSLX_DEVICE_EXTENSION DeviceExtension,
    _Outptr_opt_ PNTFSLX_FILE_CONTEXT *FileContext)
{
    return NtfslxCachedReadValidateBindings(FileObject,
                                            DeviceExtension,
                                            FileContext);
}

NTSTATUS
NTAPI
NtfslxCachedReadReadFileObject(
    _In_ PFILE_OBJECT FileObject,
    _In_opt_ PNTFSLX_DEVICE_EXTENSION DeviceExtension,
    _In_reads_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length,
    _In_ PLARGE_INTEGER ByteOffset,
    _In_ BOOLEAN Wait,
    _Out_opt_ PULONG BytesRead)
{
    NTSTATUS Status;
    NTSTATUS DestroyStatus;
    PNTFSLX_FILE_CONTEXT FileContext;
    PNTFSLX_DEVICE_EXTENSION ResolvedDeviceExtension;
    CC_FILE_SIZES FileSizes;
    PNTFSLX_CACHE_RUNTIME_CONTEXT CacheRuntime;
    BOOLEAN ReadOnly;
    IO_STATUS_BLOCK IoStatus;
    LARGE_INTEGER LocalOffset;
    ULONG LocalLength;

    if (BytesRead != NULL)
    {
        *BytesRead = 0;
    }

    if (Buffer == NULL || ByteOffset == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = NtfslxCachedReadValidateBindings(FileObject,
                                              DeviceExtension,
                                              &FileContext);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    ResolvedDeviceExtension = (DeviceExtension != NULL) ? DeviceExtension
                                                        : FileContext->DeviceExtension;

    Status = NtfslxCachedReadBuildFileSizes(FileContext,
                                            &FileSizes,
                                            &ReadOnly);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    LocalOffset = *ByteOffset;
    if (LocalOffset.QuadPart < 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (Length == 0)
    {
        if (BytesRead != NULL)
        {
            *BytesRead = 0;
        }

        return STATUS_SUCCESS;
    }

    if ((ULONGLONG)LocalOffset.QuadPart >= FileContext->DataSize)
    {
        return STATUS_END_OF_FILE;
    }

    LocalLength = Length;
    if ((ULONGLONG)LocalOffset.QuadPart + LocalLength > FileContext->DataSize)
    {
        LocalLength = (ULONG)(FileContext->DataSize - (ULONGLONG)LocalOffset.QuadPart);
    }

    CacheRuntime = NULL;
    Status = NtfslxCacheRuntimeCreate(ResolvedDeviceExtension->DeviceObject,
                                      FileObject->SectionObjectPointer,
                                      &FileSizes,
                                      ReadOnly,
                                      0,
                                      &CacheRuntime);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    RtlZeroMemory(&IoStatus, sizeof(IoStatus));
    Status = NtfslxCacheRuntimeCopyRead(CacheRuntime,
                                        &LocalOffset,
                                        LocalLength,
                                        Wait,
                                        Buffer,
                                        &IoStatus);

    if (NT_SUCCESS(Status) && BytesRead != NULL)
    {
        *BytesRead = (ULONG)IoStatus.Information;
    }

    DestroyStatus = NtfslxCacheRuntimeDestroy(CacheRuntime);
    ASSERT(NT_SUCCESS(DestroyStatus));

    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    return IoStatus.Status;
}
