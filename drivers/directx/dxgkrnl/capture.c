/*
 * PROJECT:     ReactOS WDDM DirectX Graphics Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Safe capture and publication of embedded D3DKMT user buffers
 */

#include "dxgkrnl_private.h"

NTSTATUS
NTAPI
DxgkpCopyFromUserBuffer(
    _Out_writes_bytes_(BufferSize) PVOID KernelBuffer,
    _In_reads_bytes_(BufferSize) CONST VOID *SourceBuffer,
    _In_ SIZE_T BufferSize,
    _In_ KPROCESSOR_MODE AccessMode)
{
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();

    if (BufferSize == 0)
        return STATUS_SUCCESS;
    if (KernelBuffer == NULL || SourceBuffer == NULL)
        return STATUS_INVALID_PARAMETER;

    _SEH2_TRY
    {
        if (AccessMode != KernelMode)
            ProbeForRead(SourceBuffer, BufferSize, 1);
        RtlCopyMemory(KernelBuffer, SourceBuffer, BufferSize);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    return Status;
}

NTSTATUS
NTAPI
DxgkpCopyToUserBuffer(
    _Out_writes_bytes_(BufferSize) PVOID DestinationBuffer,
    _In_reads_bytes_(BufferSize) CONST VOID *KernelBuffer,
    _In_ SIZE_T BufferSize,
    _In_ KPROCESSOR_MODE AccessMode)
{
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();

    if (BufferSize == 0)
        return STATUS_SUCCESS;
    if (DestinationBuffer == NULL || KernelBuffer == NULL)
        return STATUS_INVALID_PARAMETER;

    _SEH2_TRY
    {
        if (AccessMode != KernelMode)
            ProbeForWrite(DestinationBuffer, BufferSize, 1);
        RtlCopyMemory(DestinationBuffer, KernelBuffer, BufferSize);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    return Status;
}

NTSTATUS
NTAPI
DxgkpProbeOutputBuffer(
    _Out_writes_bytes_(BufferSize) PVOID DestinationBuffer,
    _In_ SIZE_T BufferSize,
    _In_ KPROCESSOR_MODE AccessMode)
{
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();

    if (BufferSize == 0)
        return STATUS_SUCCESS;
    if (DestinationBuffer == NULL)
        return STATUS_INVALID_PARAMETER;

    _SEH2_TRY
    {
        if (AccessMode != KernelMode)
            ProbeForWrite(DestinationBuffer, BufferSize, 1);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    return Status;
}

NTSTATUS
NTAPI
DxgkpCaptureUserBuffer(
    _In_reads_bytes_opt_(BufferSize) CONST VOID *SourceBuffer,
    _In_ SIZE_T BufferSize,
    _In_ KPROCESSOR_MODE AccessMode,
    _In_ ULONG PoolTag,
    _Outptr_result_bytebuffer_maybenull_(BufferSize) PVOID *CapturedBuffer)
{
    PVOID Buffer;
    NTSTATUS Status;

    PAGED_CODE();

    if (CapturedBuffer == NULL)
        return STATUS_INVALID_PARAMETER;
    *CapturedBuffer = NULL;
    if (BufferSize == 0)
        return STATUS_SUCCESS;
    if (SourceBuffer == NULL)
        return STATUS_INVALID_PARAMETER;

    Buffer = ExAllocatePoolWithTag(NonPagedPool, BufferSize, PoolTag);
    if (Buffer == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    Status = DxgkpCopyFromUserBuffer(Buffer, SourceBuffer, BufferSize, AccessMode);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Buffer, PoolTag);
        return Status;
    }

    *CapturedBuffer = Buffer;
    return STATUS_SUCCESS;
}
