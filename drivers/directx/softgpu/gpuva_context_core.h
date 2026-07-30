/*
 * PROJECT:     ReactOS WDDM Null/Software GPU Miniport
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     By-value GPUVA root state shared by contexts and submissions
 */

#ifndef _SOFTGPU_GPUVA_CONTEXT_CORE_H_
#define _SOFTGPU_GPUVA_CONTEXT_CORE_H_

#include <ntddk.h>

/*
 * Callers serialize mutations and snapshots with the adapter fence lock.
 * Keeping the root in one value object prevents a 32-bit build from observing
 * a torn physical address and makes queued submissions independent of later
 * context switches.
 */
typedef struct _SOFTGPU_GPUVA_ROOT
{
    ULONGLONG PhysicalAddress;
    ULONG EntryCount;
} SOFTGPU_GPUVA_ROOT, *PSOFTGPU_GPUVA_ROOT;

FORCEINLINE VOID
SoftGpuGpuVaRootProgram(
    _Out_ PSOFTGPU_GPUVA_ROOT Root,
    _In_ ULONGLONG PhysicalAddress,
    _In_ ULONG EntryCount)
{
    Root->PhysicalAddress = PhysicalAddress;
    Root->EntryCount = EntryCount;
}

FORCEINLINE VOID
SoftGpuGpuVaRootClear(
    _Out_ PSOFTGPU_GPUVA_ROOT Root)
{
    SoftGpuGpuVaRootProgram(Root, 0, 0);
}

FORCEINLINE VOID
SoftGpuGpuVaRootSnapshot(
    _In_ CONST SOFTGPU_GPUVA_ROOT *Source,
    _Out_ PSOFTGPU_GPUVA_ROOT Snapshot)
{
    Snapshot->PhysicalAddress = Source->PhysicalAddress;
    Snapshot->EntryCount = Source->EntryCount;
}

/*
 * Capture every piece of per-process GPUVA identity while the caller still
 * holds the adapter lock.  A queued submission must not dereference the process
 * object again after teardown is allowed to begin.
 */
FORCEINLINE VOID
SoftGpuGpuVaSubmissionSnapshot(
    _In_ CONST SOFTGPU_GPUVA_ROOT *Source,
    _In_ HANDLE ProcessHandle,
    _Out_ PSOFTGPU_GPUVA_ROOT RootSnapshot,
    _Out_ PHANDLE ProcessHandleSnapshot)
{
    SoftGpuGpuVaRootSnapshot(Source, RootSnapshot);
    *ProcessHandleSnapshot = ProcessHandle;
}

#endif /* _SOFTGPU_GPUVA_CONTEXT_CORE_H_ */
