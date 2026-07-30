/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     WDDM scheduler and SoftGPU terminal fault policy tests
 */

#include <kmt_test.h>
#include "vidsch_policy_core.h"
#include "fault_policy_core.h"

START_TEST(DxgkGpuFaultPolicy)
{
    PVOID Context = (PVOID)(ULONG_PTR)0x1111;
    PVOID Device = (PVOID)(ULONG_PTR)0x2222;
    SOFTGPU_FAULT_RETIREMENT Retirement;

    /* Node and engine are distinct WDDM coordinates. */
    ok_bool_true(
        VidSchPolicyNodeEngineSupported(0, 0, 2),
        "node 0 engine 0");
    ok_bool_true(
        VidSchPolicyNodeEngineSupported(1, 0, 2),
        "node 1 engine 0");
    ok_bool_false(
        VidSchPolicyNodeEngineSupported(2, 0, 2),
        "node out of range");
    ok_bool_false(
        VidSchPolicyNodeEngineSupported(0, 1, 2),
        "nonzero engine must not alias node 1");

    ok_eq_pointer(
        VidSchPolicyOwnerCookie(Context, Device),
        Context);
    ok_eq_pointer(
        VidSchPolicyOwnerCookie(NULL, Device),
        Device);
    ok_eq_pointer(
        VidSchPolicyOwnerCookie(NULL, NULL),
        NULL);

    ok_bool_true(
        VidSchPolicyCompletionMustFail(4, 4),
        "terminal page-fault state rejects completion");
    ok_bool_false(
        VidSchPolicyCompletionMustFail(0, 4),
        "active device accepts completion");

    ok_bool_true(
        SoftGpuDmaCommandAddressClassAllowed(
            FALSE,
            SoftGpuDmaAddressPhysicalSurface),
        "patched physical submission can execute a surface command");
    ok_bool_true(
        SoftGpuDmaCommandAddressClassAllowed(
            FALSE,
            SoftGpuDmaAddressKernelPaging),
        "KMD physical submission can execute a paging command");
    ok_bool_true(
        SoftGpuDmaCommandAddressClassAllowed(
            TRUE,
            SoftGpuDmaAddressGpuVaChecked),
        "virtual submission can execute an explicitly GPUVA-checked command");
    ok_bool_false(
        SoftGpuDmaCommandAddressClassAllowed(
            TRUE,
            SoftGpuDmaAddressPhysicalSurface),
        "virtual submission cannot reinterpret a GPUVA as a slab address");
    ok_bool_false(
        SoftGpuDmaCommandAddressClassAllowed(
            TRUE,
            SoftGpuDmaAddressKernelPaging),
        "process command buffer cannot supply a kernel paging address");
    ok_bool_false(
        SoftGpuDmaCommandAddressClassAllowed(
            TRUE,
            (SOFTGPU_DMA_ADDRESS_CLASS)99),
        "unknown command address class");

    SoftGpuFaultPolicyRetire(1300, 7, 12, 10, &Retirement);
    ok_eq_ulong(Retirement.NextHead, 8);
    ok_eq_ulong(Retirement.CompletedFence, 12);
    ok_bool_false(
        Retirement.NotifyPageFault,
        "WDDM 1.x consumes a fault as a no-op completion");

    /* Signed fence comparison must preserve wrap-around ordering. */
    SoftGpuFaultPolicyRetire(
        1999,
        MAXULONG,
        1,
        MAXULONG - 1,
        &Retirement);
    ok_eq_ulong(Retirement.NextHead, 0);
    ok_eq_ulong(Retirement.CompletedFence, 1);
    ok_bool_false(
        Retirement.NotifyPageFault,
        "pre-2.0 target keeps forward progress");

    SoftGpuFaultPolicyRetire(2000, 9, 15, 12, &Retirement);
    ok_eq_ulong(Retirement.NextHead, 10);
    ok_eq_ulong(Retirement.CompletedFence, 12);
    ok_bool_true(
        Retirement.NotifyPageFault,
        "WDDM 2.0 reports the fault without signaling its fence");

    SoftGpuFaultPolicyRetire(3000, 3, 25, 20, &Retirement);
    ok_eq_ulong(Retirement.NextHead, 4);
    ok_eq_ulong(Retirement.CompletedFence, 20);
    ok_bool_true(
        Retirement.NotifyPageFault,
        "WDDM 3.0 keeps the WDDM 2.0 fault contract");
}
