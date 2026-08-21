/*
 * PROJECT:         ReactOS API tests
 * LICENSE:         GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:         End-to-end ALPC reserve, section, view, and security lifetime tests
 * COPYRIGHT:       Copyright 2026 Ahmed ARIF
 */

#include "precomp.h"
#include "alpc_test_utils.h"
#include <pseh/pseh2.h>

static
VOID
AlpcTestReserveBoundaries(
    _In_ HANDLE Port)
{
    static const SIZE_T Sizes[] = {0, 1, sizeof(PORT_MESSAGE) - 1, sizeof(PORT_MESSAGE), 0xffd7, 0xffd8};
    ALPC_TEST_RESERVE_OUTPUT Output;
    UCHAR MisalignedOutput[sizeof(ULONG) + 2];
    ULONG Reserve;
    NTSTATUS Status;
    ULONG Index;

    for (Index = 0; Index < RTL_NUMBER_OF(Sizes); ++Index)
    {
        Output.ResourceId = 0x55555555;
        Output.Guard = 0xa5a5a5a5;
        Status = NtAlpcCreateResourceReserve(Port, 0, Sizes[Index], &Output.ResourceId);
        trace("ALPC_OBSERVE status Reserve.boundary size=%Iu status=%08lx output=%08lx guard=%08lx\n", Sizes[Index], Status, Output.ResourceId, Output.Guard);
        alpc_trace_scalar_mutation("Reserve.boundary", "output", 0x55555555, Output.ResourceId);
        ok_eq_hex(Output.Guard, 0xa5a5a5a5);
        ok(Status != STATUS_NOT_IMPLEMENTED, "reserve size %Iu reached a stub\n", Sizes[Index]);
        if (NT_SUCCESS(Status))
        {
            alpc_expect_status("Reserve.boundary_delete", NtAlpcDeleteResourceReserve(Port, 0, Output.ResourceId), STATUS_SUCCESS);
            alpc_observe_status("Reserve.boundary_repeat_delete", NtAlpcDeleteResourceReserve(Port, 0, Output.ResourceId));
        }
    }

    RtlFillMemory(MisalignedOutput, sizeof(MisalignedOutput), 0x55);
    Status = NtAlpcCreateResourceReserve(Port, 0, sizeof(PORT_MESSAGE), (PULONG)(MisalignedOutput + 1));
    Reserve = *(UNALIGNED ULONG *)(MisalignedOutput + 1);
    trace("ALPC_OBSERVE status Reserve.misaligned_output status=%08lx output=%08lx prefix=%02x suffix=%02x\n", Status, Reserve, MisalignedOutput[0], MisalignedOutput[sizeof(MisalignedOutput) - 1]);
    ok_eq_hex(MisalignedOutput[0], 0x55);
    ok_eq_hex(MisalignedOutput[sizeof(MisalignedOutput) - 1], 0x55);
    ok(Status != STATUS_NOT_IMPLEMENTED, "misaligned reserve output reached a stub\n");
    if (NT_SUCCESS(Status))
        alpc_expect_status("Reserve.misaligned_delete", NtAlpcDeleteResourceReserve(Port, 0, Reserve), STATUS_SUCCESS);
}

static
VOID
AlpcTestSectionBoundaries(
    _In_ HANDLE Port)
{
    static const SIZE_T Sizes[] = {0, 1, PAGE_SIZE - 1, PAGE_SIZE, 0x100000, 0x100001};
    static const ULONG Flags[] = {0, ALPC_PORTSECTIONFLG_SECURE, 1, 0xffffffff};
    ALPC_HANDLE Section;
    SIZE_T ActualSize;
    NTSTATUS Status;
    ULONG Index;

    for (Index = 0; Index < RTL_NUMBER_OF(Sizes); ++Index)
    {
        Section = (ALPC_HANDLE)(ULONG_PTR)0x5555555555555555ULL;
        ActualSize = (SIZE_T)0x5555555555555555ULL;
        Status = NtAlpcCreatePortSection(Port, 0, NULL, Sizes[Index], &Section, &ActualSize);
        trace("ALPC_OBSERVE status Section.boundary size=%Iu status=%08lx section=%p actual=%Iu\n", Sizes[Index], Status, Section, ActualSize);
        alpc_trace_scalar_mutation("Section.boundary", "section", (ALPC_HANDLE)(ULONG_PTR)0x5555555555555555ULL, Section);
        alpc_trace_scalar_mutation("Section.boundary", "actual_size", (SIZE_T)0x5555555555555555ULL, ActualSize);
        ok(Status != STATUS_NOT_IMPLEMENTED, "section size %Iu reached a stub\n", Sizes[Index]);
        if (NT_SUCCESS(Status))
            alpc_expect_status("Section.boundary_delete", NtAlpcDeletePortSection(Port, 0, Section), STATUS_SUCCESS);
    }

    for (Index = 0; Index < RTL_NUMBER_OF(Flags); ++Index)
    {
        Section = (ALPC_HANDLE)(ULONG_PTR)0x5555555555555555ULL;
        ActualSize = (SIZE_T)0x5555555555555555ULL;
        Status = NtAlpcCreatePortSection(Port, Flags[Index], NULL, PAGE_SIZE, &Section, &ActualSize);
        trace("ALPC_OBSERVE status Section.flags flags=%08lx status=%08lx section=%p actual=%Iu\n", Flags[Index], Status, Section, ActualSize);
        alpc_trace_scalar_mutation("Section.flags", "section", (ALPC_HANDLE)(ULONG_PTR)0x5555555555555555ULL, Section);
        alpc_trace_scalar_mutation("Section.flags", "actual_size", (SIZE_T)0x5555555555555555ULL, ActualSize);
        ok(Status != STATUS_NOT_IMPLEMENTED, "section flags %08lx reached a stub\n", Flags[Index]);
        if (NT_SUCCESS(Status))
            NtAlpcDeletePortSection(Port, 0, Section);
    }
}

static
VOID
AlpcTestViewFlagMatrix(
    _In_ HANDLE Port)
{
    static const ULONG Flags[] =
    {
        0,
        ALPC_VIEWFLG_AUTO_RELEASE,
        ALPC_VIEWFLG_NOT_SECURE,
        ALPC_VIEWFLG_UNMAP_EXISTING,
        ALPC_VIEWFLG_AUTO_RELEASE | ALPC_VIEWFLG_NOT_SECURE,
        ALPC_VIEWFLG_AUTO_RELEASE | ALPC_VIEWFLG_NOT_SECURE | ALPC_VIEWFLG_UNMAP_EXISTING,
        1,
        0xffffffff
    };
    ALPC_DATA_VIEW_ATTR View;
    ALPC_DATA_VIEW_ATTR Before;
    ALPC_HANDLE Section = NULL;
    SIZE_T ActualSize = 0;
    NTSTATUS Status;
    ULONG Index;

    Status = NtAlpcCreatePortSection(Port, 0, NULL, 0x8000, &Section, &ActualSize);
    ok_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    for (Index = 0; Index < RTL_NUMBER_OF(Flags); ++Index)
    {
        RtlFillMemory(&View, sizeof(View), 0x55);
        View.Flags = Flags[Index];
        View.SectionHandle = Section;
        View.ViewBase = NULL;
        View.ViewSize = PAGE_SIZE;
        Before = View;
        Status = NtAlpcCreateSectionView(Port, 0, &View);
        trace("ALPC_OBSERVE status View.flags flags=%08lx status=%08lx section=%p base=%p size=%Iu\n", Flags[Index], Status, View.SectionHandle, View.ViewBase, View.ViewSize);
        AlpcTestTraceBufferMutation("View.flags", (const UCHAR *)&Before, (const UCHAR *)&View, sizeof(View));
        ok(Status != STATUS_NOT_IMPLEMENTED, "view flags %08lx reached a stub\n", Flags[Index]);
        if (NT_SUCCESS(Status) && View.ViewBase)
        {
            alpc_expect_status("View.flags_delete", NtAlpcDeleteSectionView(Port, 0, View.ViewBase), STATUS_SUCCESS);
            alpc_observe_status("View.flags_repeat_delete", NtAlpcDeleteSectionView(Port, 0, View.ViewBase));
        }
    }

    NtAlpcDeletePortSection(Port, 0, Section);
}

static
VOID
AlpcTestSecurityQosMatrix(
    _In_ HANDLE Port)
{
    static const SECURITY_IMPERSONATION_LEVEL Levels[] =
    {
        SecurityAnonymous,
        SecurityIdentification,
        SecurityImpersonation,
        SecurityDelegation,
        (SECURITY_IMPERSONATION_LEVEL)-1,
        (SECURITY_IMPERSONATION_LEVEL)4
    };
    ALPC_SECURITY_ATTR Attribute;
    ALPC_SECURITY_ATTR Before;
    SECURITY_QUALITY_OF_SERVICE Qos;
    NTSTATUS Status;
    ULONG Index;
    ULONG Tracking;
    ULONG EffectiveOnly;

    for (Index = 0; Index < RTL_NUMBER_OF(Levels); ++Index)
    {
        for (Tracking = 0; Tracking <= 1; ++Tracking)
        {
            for (EffectiveOnly = 0; EffectiveOnly <= 1; ++EffectiveOnly)
            {
                RtlZeroMemory(&Qos, sizeof(Qos));
                Qos.Length = sizeof(Qos);
                Qos.ImpersonationLevel = Levels[Index];
                Qos.ContextTrackingMode = (SECURITY_CONTEXT_TRACKING_MODE)Tracking;
                Qos.EffectiveOnly = (BOOLEAN)EffectiveOnly;
                RtlFillMemory(&Attribute, sizeof(Attribute), 0x55);
                Attribute.Flags = 0;
                Attribute.QoS = &Qos;
                Attribute.ContextHandle = (ALPC_HANDLE)(ULONG_PTR)0x5555555555555555ULL;
                Before = Attribute;
                Status = NtAlpcCreateSecurityContext(Port, 0, &Attribute);
                trace("ALPC_OBSERVE status Security.qos level=%lu tracking=%lu effective=%lu status=%08lx context=%p\n", (ULONG)Levels[Index], Tracking, EffectiveOnly, Status, Attribute.ContextHandle);
                AlpcTestTraceBufferMutation("Security.qos", (const UCHAR *)&Before, (const UCHAR *)&Attribute, sizeof(Attribute));
                ok(Status != STATUS_NOT_IMPLEMENTED, "security QoS case reached a stub\n");
                if (NT_SUCCESS(Status))
                {
                    alpc_observe_status("Security.qos_revoke", NtAlpcRevokeSecurityContext(Port, 0, Attribute.ContextHandle));
                    alpc_expect_status("Security.qos_delete", NtAlpcDeleteSecurityContext(Port, 0, Attribute.ContextHandle), STATUS_SUCCESS);
                }
            }
        }
    }

    RtlZeroMemory(&Qos, sizeof(Qos));
    Qos.Length = sizeof(Qos) - 1;
    Qos.ImpersonationLevel = SecurityImpersonation;
    RtlZeroMemory(&Attribute, sizeof(Attribute));
    Attribute.QoS = &Qos;
    alpc_observe_status("Security.qos_short_length", NtAlpcCreateSecurityContext(Port, 0, &Attribute));
}

static
VOID
AlpcTestOutputFaultRollback(
    _In_ HANDLE Port)
{
    PVOID ReadOnlyOutput;
    DWORD OldProtect;
    ALPC_HANDLE Section = NULL;
    ALPC_TEST_RESERVE_OUTPUT ReserveOutput;
    SIZE_T ActualSize;
    NTSTATUS Status;

    if (!AlpcTestNativeObservationEnabled())
    {
        skip("set ALPC_TEST_NATIVE_OBSERVE=1 to run resource output-fault rollback probes\n");
        return;
    }

    ReadOnlyOutput = VirtualAlloc(NULL, PAGE_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    ok(ReadOnlyOutput != NULL, "VirtualAlloc failed: %lu\n", GetLastError());
    if (!ReadOnlyOutput)
        return;
    RtlFillMemory(ReadOnlyOutput, PAGE_SIZE, 0x55);
    if (!VirtualProtect(ReadOnlyOutput, PAGE_SIZE, PAGE_READONLY, &OldProtect))
    {
        ok(FALSE, "VirtualProtect failed: %lu\n", GetLastError());
        VirtualFree(ReadOnlyOutput, 0, MEM_RELEASE);
        return;
    }

    ActualSize = 0;
    Status = NtAlpcCreatePortSection(Port, 0, NULL, PAGE_SIZE, (PALPC_HANDLE)ReadOnlyOutput, &ActualSize);
    trace("ALPC_OBSERVE status Rollback.section_output_fault=%08lx actual=%Iu\n", Status, ActualSize);
    ok(Status != STATUS_NOT_IMPLEMENTED, "section output fault reached a stub\n");
    Status = NtAlpcCreatePortSection(Port, 0, NULL, PAGE_SIZE, &Section, &ActualSize);
    trace("ALPC_OBSERVE status Rollback.section_after_fault=%08lx section=%p actual=%Iu\n", Status, Section, ActualSize);
    if (NT_SUCCESS(Status))
        NtAlpcDeletePortSection(Port, 0, Section);

    Status = NtAlpcCreateResourceReserve(Port, 0, sizeof(PORT_MESSAGE), (PULONG)ReadOnlyOutput);
    trace("ALPC_OBSERVE status Rollback.reserve_output_fault=%08lx\n", Status);
    ok(Status != STATUS_NOT_IMPLEMENTED, "reserve output fault reached a stub\n");
    ReserveOutput.ResourceId = 0x55555555;
    ReserveOutput.Guard = 0xa5a5a5a5;
    Status = NtAlpcCreateResourceReserve(Port, 0, sizeof(PORT_MESSAGE), &ReserveOutput.ResourceId);
    trace("ALPC_OBSERVE status Rollback.reserve_after_fault=%08lx reserve=%08lx guard=%08lx\n", Status, ReserveOutput.ResourceId, ReserveOutput.Guard);
    ok_eq_hex(ReserveOutput.Guard, 0xa5a5a5a5);
    if (NT_SUCCESS(Status))
        NtAlpcDeleteResourceReserve(Port, 0, ReserveOutput.ResourceId);

    VirtualFree(ReadOnlyOutput, 0, MEM_RELEASE);
}

static
VOID
AlpcTestOutputFaultRollbackChild(VOID)
{
    static UNICODE_STRING PortName = RTL_CONSTANT_STRING(L"\\RPC Control\\NtdllApitestNtAlpcResourcesFaults");
    HANDLE ConnectionPort = NULL;
    HANDLE ServerPort = NULL;
    HANDLE ClientPort = NULL;
    NTSTATUS Status;

    Status = AlpcTestCreateConnectedPorts(&PortName, ALPC_PORFLG_ALLOW_IMPERSONATION | ALPC_PORFLG_ALLOW_DUP_OBJECT, &ConnectionPort, &ServerPort, &ClientPort);
    ok_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;
    AlpcTestOutputFaultRollback(ClientPort);
    AlpcTestCloseConnectedPorts(ConnectionPort, ServerPort, ClientPort);
}

START_TEST(NtAlpcResources)
{
    static UNICODE_STRING PortName = RTL_CONSTANT_STRING(L"\\RPC Control\\NtdllApitestNtAlpcResources");
    SECURITY_QUALITY_OF_SERVICE SecurityQos;
    ALPC_SECURITY_ATTR SecurityAttribute;
    ALPC_DATA_VIEW_ATTR ViewAttribute;
    LARGE_INTEGER MaximumSize;
    ALPC_HANDLE ExternalSectionId = NULL;
    ALPC_HANDLE SecurityContext = NULL;
    ALPC_HANDLE SectionId = NULL;
    ALPC_TEST_RESERVE_OUTPUT ReserveOutput;
    SIZE_T ExternalActualSize = 0;
    SIZE_T ActualSize = 0;
    NTSTATUS Status;
    NTSTATUS AccessStatus;
    HANDLE ExternalSection = NULL;
    HANDLE ConnectionPort = NULL;
    HANDLE ServerPort = NULL;
    HANDLE ClientPort = NULL;

    if (AlpcTestIsChildMode("resource-output-faults"))
    {
        AlpcTestOutputFaultRollbackChild();
        return;
    }

    Status = AlpcTestCreateConnectedPorts(&PortName, ALPC_PORFLG_ALLOW_IMPERSONATION | ALPC_PORFLG_ALLOW_DUP_OBJECT, &ConnectionPort, &ServerPort, &ClientPort);
    ok_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    ReserveOutput.ResourceId = 0x55555555;
    ReserveOutput.Guard = 0xa5a5a5a5;
    alpc_observe_status("ResourceReserve.create", NtAlpcCreateResourceReserve(ClientPort, 0, 0x100, &ReserveOutput.ResourceId));
    trace("ALPC_OBSERVE value ResourceReserve.id=%08lx guard=%08lx\n", ReserveOutput.ResourceId, ReserveOutput.Guard);
    ok_eq_hex(ReserveOutput.Guard, 0xa5a5a5a5);
    if (NT_SUCCESS(Status))
    {
        ok(ReserveOutput.ResourceId != 0, "resource reserve ID was not returned\n");
        ok((ReserveOutput.ResourceId & 0x80000000) != 0, "resource reserve ID lacks native bit 31 tag: %08lx\n", ReserveOutput.ResourceId);
        alpc_observe_status("ResourceReserve.delete", NtAlpcDeleteResourceReserve(ClientPort, 0, ReserveOutput.ResourceId));
        alpc_observe_status("ResourceReserve.double_delete", NtAlpcDeleteResourceReserve(ClientPort, 0, ReserveOutput.ResourceId));
    }

    alpc_observe_status("PortSection.create_internal", NtAlpcCreatePortSection(ClientPort, 0, NULL, 0x3000, &SectionId, &ActualSize));
    trace("ALPC_NATIVE_VALUE PortSection.internal_size=%Iu\n", ActualSize);
    if (NT_SUCCESS(Status))
    {
        ok(SectionId != NULL, "port section ID was not returned\n");
        ok(ActualSize >= 0x3000, "actual section size %Iu is too small\n", ActualSize);

        RtlZeroMemory(&ViewAttribute, sizeof(ViewAttribute));
        ViewAttribute.SectionHandle = SectionId;
        ViewAttribute.ViewSize = 0x2000;
        alpc_observe_status("SectionView.create", NtAlpcCreateSectionView(ClientPort, 0, &ViewAttribute));
        trace("ALPC_NATIVE_VALUE SectionView.base=%p size=%Iu\n", ViewAttribute.ViewBase, ViewAttribute.ViewSize);
        if (NT_SUCCESS(Status))
        {
            ok(ViewAttribute.ViewBase != NULL, "section view base was not returned\n");
            ok(ViewAttribute.ViewSize >= 0x2000, "section view size %Iu is too small\n", ViewAttribute.ViewSize);
            AccessStatus = STATUS_SUCCESS;
            if (ViewAttribute.ViewBase && ViewAttribute.ViewSize >= sizeof(ULONG))
            {
                _SEH2_TRY
                {
                    *(volatile ULONG *)ViewAttribute.ViewBase = 0x56494557;
                    ok_eq_hex(*(volatile ULONG *)ViewAttribute.ViewBase, 0x56494557);
                }
                _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
                {
                    AccessStatus = _SEH2_GetExceptionCode();
                }
                _SEH2_END;
                ok_hex(AccessStatus, STATUS_SUCCESS);
            }
            alpc_observe_status("SectionView.delete", NtAlpcDeleteSectionView(ClientPort, 0, ViewAttribute.ViewBase));
            alpc_observe_status("SectionView.double_delete", NtAlpcDeleteSectionView(ClientPort, 0, ViewAttribute.ViewBase));
        }

        alpc_observe_status("PortSection.delete_internal", NtAlpcDeletePortSection(ClientPort, 0, SectionId));
        alpc_observe_status("PortSection.double_delete_internal", NtAlpcDeletePortSection(ClientPort, 0, SectionId));
    }

    MaximumSize.QuadPart = 0x5000;
    Status = NtCreateSection(&ExternalSection, SECTION_ALL_ACCESS, NULL, &MaximumSize, PAGE_READWRITE, SEC_COMMIT, NULL);
    ok_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        alpc_observe_status("PortSection.create_external", NtAlpcCreatePortSection(ServerPort, 0, ExternalSection, 0x5000, &ExternalSectionId, &ExternalActualSize));
        trace("ALPC_NATIVE_VALUE PortSection.external_size=%Iu\n", ExternalActualSize);
        if (NT_SUCCESS(Status))
        {
            ok(ExternalSectionId != NULL, "external port section ID was not returned\n");
            alpc_observe_status("PortSection.delete_external", NtAlpcDeletePortSection(ServerPort, 0, ExternalSectionId));
        }
        NtClose(ExternalSection);
    }

    RtlZeroMemory(&SecurityQos, sizeof(SecurityQos));
    SecurityQos.Length = sizeof(SecurityQos);
    SecurityQos.ImpersonationLevel = SecurityImpersonation;
    SecurityQos.ContextTrackingMode = SECURITY_DYNAMIC_TRACKING;
    SecurityQos.EffectiveOnly = FALSE;
    RtlZeroMemory(&SecurityAttribute, sizeof(SecurityAttribute));
    SecurityAttribute.QoS = &SecurityQos;
    alpc_observe_status("SecurityContext.create", NtAlpcCreateSecurityContext(ClientPort, 0, &SecurityAttribute));
    if (NT_SUCCESS(Status))
    {
        SecurityContext = SecurityAttribute.ContextHandle;
        ok(SecurityContext != NULL, "security context handle was not returned\n");
        alpc_observe_status("SecurityContext.revoke", NtAlpcRevokeSecurityContext(ClientPort, 0, SecurityContext));
        alpc_observe_status("SecurityContext.revoke_twice", NtAlpcRevokeSecurityContext(ClientPort, 0, SecurityContext));
        alpc_observe_status("SecurityContext.delete", NtAlpcDeleteSecurityContext(ClientPort, 0, SecurityContext));
        alpc_observe_status("SecurityContext.double_delete", NtAlpcDeleteSecurityContext(ClientPort, 0, SecurityContext));
    }

    AlpcTestReserveBoundaries(ClientPort);
    AlpcTestSectionBoundaries(ClientPort);
    AlpcTestViewFlagMatrix(ClientPort);
    AlpcTestSecurityQosMatrix(ClientPort);

    AlpcTestCloseConnectedPorts(ConnectionPort, ServerPort, ClientPort);
    AlpcTestRunIsolatedCase(L"NtAlpcResources", L"resource-output-faults", ALPC_TEST_CHILD_TIMEOUT_MS);
}
