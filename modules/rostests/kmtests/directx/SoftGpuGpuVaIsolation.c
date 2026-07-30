/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     SoftGPU per-context GPUVA root submission isolation
 */

#include <kmt_test.h>
#include "gpuva_context_core.h"

static VOID
CheckRoot(
    _In_ CONST SOFTGPU_GPUVA_ROOT *Root,
    _In_ ULONGLONG PhysicalAddress,
    _In_ ULONG EntryCount)
{
    ok_eq_ulonglong(Root->PhysicalAddress, PhysicalAddress);
    ok_eq_ulong(Root->EntryCount, EntryCount);
}

START_TEST(SoftGpuGpuVaIsolation)
{
    SOFTGPU_GPUVA_ROOT ContextA;
    SOFTGPU_GPUVA_ROOT ContextB;
    SOFTGPU_GPUVA_ROOT SubmitA;
    SOFTGPU_GPUVA_ROOT SubmitB;
    HANDLE SubmitProcessA;
    HANDLE SubmitProcessB;

    RtlZeroMemory(&ContextA, sizeof(ContextA));
    RtlZeroMemory(&ContextB, sizeof(ContextB));
    RtlZeroMemory(&SubmitA, sizeof(SubmitA));
    RtlZeroMemory(&SubmitB, sizeof(SubmitB));

    SoftGpuGpuVaRootProgram(&ContextA, 0x1111222233334000ULL, 512);
    SoftGpuGpuVaSubmissionSnapshot(
        &ContextA,
        (HANDLE)(ULONG_PTR)0x11110000,
        &SubmitA,
        &SubmitProcessA);

    SoftGpuGpuVaRootProgram(&ContextB, 0xAAAABBBBCCCCD000ULL, 256);
    SoftGpuGpuVaSubmissionSnapshot(
        &ContextB,
        (HANDLE)(ULONG_PTR)0x22220000,
        &SubmitB,
        &SubmitProcessB);

    /* Programming B must not retarget context A or A's queued work. */
    SoftGpuGpuVaRootProgram(&ContextB, 0x5555666677778000ULL, 128);
    CheckRoot(&ContextA, 0x1111222233334000ULL, 512);
    CheckRoot(&SubmitA, 0x1111222233334000ULL, 512);
    CheckRoot(&SubmitB, 0xAAAABBBBCCCCD000ULL, 256);
    ok_eq_pointer(SubmitProcessA, (HANDLE)(ULONG_PTR)0x11110000);
    ok_eq_pointer(SubmitProcessB, (HANDLE)(ULONG_PTR)0x22220000);

    /* Reprogramming or tearing down A cannot mutate either saved packet. */
    SoftGpuGpuVaRootProgram(&ContextA, 0x9999AAAABBBBC000ULL, 64);
    SoftGpuGpuVaRootClear(&ContextA);
    CheckRoot(&ContextA, 0, 0);
    CheckRoot(&ContextB, 0x5555666677778000ULL, 128);
    CheckRoot(&SubmitA, 0x1111222233334000ULL, 512);
    CheckRoot(&SubmitB, 0xAAAABBBBCCCCD000ULL, 256);
    ok_eq_pointer(SubmitProcessA, (HANDLE)(ULONG_PTR)0x11110000);
    ok_eq_pointer(SubmitProcessB, (HANDLE)(ULONG_PTR)0x22220000);
}

/* EOF */
