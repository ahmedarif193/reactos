/*
 * PROJECT:     ReactOS D3DKMT API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Embedded user-pointer capture and failure rollback tests
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 */

#include "precomp.h"

#ifndef STATUS_ACCESS_VIOLATION
#define STATUS_ACCESS_VIOLATION ((NTSTATUS)0xC0000005L)
#endif

#define CAPTURE_CALL(Status, Faulted, Expression) \
do { \
    (Status) = STATUS_UNSUCCESSFUL; \
    (Faulted) = FALSE; \
    _SEH2_TRY { (Status) = (Expression); } \
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) { (Status) = _SEH2_GetExceptionCode(); (Faulted) = TRUE; } \
    _SEH2_END; \
} while (0)

static SIZE_T
GetPageSize(void)
{
    SYSTEM_INFO SystemInfo;

    GetSystemInfo(&SystemInfo);
    return SystemInfo.dwPageSize;
}

static void
Test_EnumAdapters2EmbeddedOutput(void)
{
    PFND3DKMT_ENUMADAPTERS2 pfn;
    D3DKMT_ENUMADAPTERS2 Data;
    D3DKMT_ENUMADAPTERS2 Before;
    PVOID GuardPage;
    PVOID ReadOnlyPage;
    PVOID ExpectedBuffer;
    SIZE_T BufferSize;
    ULONG Capacity;
    DWORD OldProtect;
    BOOL Faulted;
    NTSTATUS Status;

    pfn = (PFND3DKMT_ENUMADAPTERS2)LoadD3DKMTProc("D3DKMTEnumAdapters2");
    if (pfn == NULL)
    {
        skip("D3DKMTEnumAdapters2 not exported\n");
        return;
    }

    memset(&Data, 0, sizeof(Data));
    CAPTURE_CALL(Status, Faulted, pfn(&Data));
    if (Faulted || (!NT_SUCCESS(Status) && Status != STATUS_BUFFER_TOO_SMALL) || Data.NumAdapters > 256)
    {
        skip("EnumAdapters2 count query cannot establish a safe test capacity (0x%08lX%s, count %lu)\n", (long)Status, Faulted ? " faulted" : "", Data.NumAdapters);
        return;
    }
    Capacity = Data.NumAdapters != 0 ? Data.NumAdapters : 1;
    BufferSize = (SIZE_T)Capacity * sizeof(D3DKMT_ADAPTERINFO);
    ExpectedBuffer = LocalAlloc(LMEM_FIXED, BufferSize);
    if (ExpectedBuffer == NULL)
    {
        skip("LocalAlloc failed for the EnumAdapters2 output canary\n");
        return;
    }

    GuardPage = VirtualAlloc(NULL, BufferSize, MEM_COMMIT | MEM_RESERVE, PAGE_NOACCESS);
    if (GuardPage == NULL)
    {
        skip("VirtualAlloc failed for EnumAdapters2 guard page\n");
        LocalFree(ExpectedBuffer);
        return;
    }

    memset(&Data, 0, sizeof(Data));
    Data.NumAdapters = Capacity;
    Data.pAdapters = GuardPage;
    Before = Data;
    CAPTURE_CALL(Status, Faulted, pfn(&Data));
    ok(Status == STATUS_ACCESS_VIOLATION, "EnumAdapters2 with an inaccessible output array returned 0x%08lX%s, expected STATUS_ACCESS_VIOLATION\n", (long)Status, Faulted ? " (faulted)" : "");
    ok(Data.NumAdapters == Before.NumAdapters && Data.pAdapters == Before.pAdapters, "EnumAdapters2 changed its descriptor after rejecting an inaccessible output array\n");
    VirtualFree(GuardPage, 0, MEM_RELEASE);

    ReadOnlyPage = VirtualAlloc(NULL, BufferSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (ReadOnlyPage == NULL)
    {
        skip("VirtualAlloc failed for EnumAdapters2 read-only page\n");
        LocalFree(ExpectedBuffer);
        return;
    }

    memset(ReadOnlyPage, 0xA5, BufferSize);
    memcpy(ExpectedBuffer, ReadOnlyPage, BufferSize);
    if (!VirtualProtect(ReadOnlyPage, BufferSize, PAGE_READONLY, &OldProtect))
    {
        skip("VirtualProtect failed for EnumAdapters2 read-only page\n");
        VirtualFree(ReadOnlyPage, 0, MEM_RELEASE);
        LocalFree(ExpectedBuffer);
        return;
    }

    memset(&Data, 0, sizeof(Data));
    Data.NumAdapters = Capacity;
    Data.pAdapters = (D3DKMT_ADAPTERINFO *)ReadOnlyPage;
    Before = Data;
    CAPTURE_CALL(Status, Faulted, pfn(&Data));
    ok(Status == STATUS_ACCESS_VIOLATION, "EnumAdapters2 with a read-only output array returned 0x%08lX%s, expected STATUS_ACCESS_VIOLATION\n", (long)Status, Faulted ? " (faulted)" : "");
    ok(Data.NumAdapters == Before.NumAdapters && Data.pAdapters == Before.pAdapters, "EnumAdapters2 changed its descriptor after rejecting a read-only output array\n");
    if (VirtualProtect(ReadOnlyPage, BufferSize, PAGE_READWRITE, &OldProtect))
        ok(memcmp(ReadOnlyPage, ExpectedBuffer, BufferSize) == 0, "EnumAdapters2 modified a read-only output canary before reporting failure\n");
    else
        skip("VirtualProtect could not restore the EnumAdapters2 canary page\n");
    VirtualFree(ReadOnlyPage, 0, MEM_RELEASE);
    LocalFree(ExpectedBuffer);
}

static void
Test_CreateContextPrivateCapture(void)
{
    PFN_D3DKMTCreateContext pfn;
    D3DKMT_CREATECONTEXT Data;
    D3DKMT_CREATECONTEXT Before;
    D3DKMT_HANDLE hAdapter;
    D3DKMT_HANDLE hDevice;
    PVOID GuardPage;
    SIZE_T PageSize;
    BOOL Faulted;
    NTSTATUS Status;

    pfn = (PFN_D3DKMTCreateContext)LoadD3DKMTProc("D3DKMTCreateContext");
    if (pfn == NULL)
    {
        skip("D3DKMTCreateContext not exported\n");
        return;
    }

    hAdapter = OpenAdapterFromDisplay1();
    if (hAdapter == 0)
    {
        skip("Cannot open an adapter for CreateContext capture tests\n");
        return;
    }
    hDevice = CreateTestDevice(hAdapter);
    if (hDevice == 0)
    {
        skip("Cannot create a device for CreateContext capture tests\n");
        CloseAdapter(hAdapter);
        return;
    }

    PageSize = GetPageSize();
    GuardPage = VirtualAlloc(NULL, PageSize, MEM_COMMIT | MEM_RESERVE, PAGE_NOACCESS);
    if (GuardPage == NULL)
    {
        skip("VirtualAlloc failed for CreateContext guard page\n");
        DestroyTestDevice(hDevice);
        CloseAdapter(hAdapter);
        return;
    }

    memset(&Data, 0, sizeof(Data));
    Data.hDevice = hDevice;
    Data.NodeOrdinal = 0;
    Data.EngineAffinity = 1;
    Data.pPrivateDriverData = GuardPage;
    Data.PrivateDriverDataSize = 1;
    Data.hContext = (D3DKMT_HANDLE)0xCA710001;
    Data.pCommandBuffer = (PVOID)(ULONG_PTR)0xCA710002;
    Data.CommandBufferSize = 0xCA710003;
    Data.pAllocationList = (D3DDDI_ALLOCATIONLIST *)(ULONG_PTR)0xCA710004;
    Data.AllocationListSize = 0xCA710005;
    Data.pPatchLocationList = (D3DDDI_PATCHLOCATIONLIST *)(ULONG_PTR)0xCA710006;
    Data.PatchLocationListSize = 0xCA710007;
    Data.CommandBuffer = (D3DGPU_VIRTUAL_ADDRESS)0xCA710008CA710009ULL;
    Before = Data;

    CAPTURE_CALL(Status, Faulted, pfn(&Data));
    ok(Status == STATUS_ACCESS_VIOLATION, "CreateContext with inaccessible private data returned 0x%08lX%s, expected STATUS_ACCESS_VIOLATION\n", (long)Status, Faulted ? " (faulted)" : "");
    ok(memcmp(&Data, &Before, sizeof(Data)) == 0, "CreateContext changed caller-visible outputs after private-data capture failed\n");

    VirtualFree(GuardPage, 0, MEM_RELEASE);
    DestroyTestDevice(hDevice);
    CloseAdapter(hAdapter);
}

static void
Test_EscapePrivateCapture(void)
{
    PFN_D3DKMTEscape pfn;
    D3DKMT_ESCAPE Data;
    D3DKMT_ESCAPE Before;
    D3DKMT_HANDLE hAdapter;
    UCHAR Expected[32];
    PVOID GuardPage;
    PVOID ReadOnlyPage;
    SIZE_T PageSize;
    DWORD OldProtect;
    BOOL Faulted;
    NTSTATUS Status;

    pfn = (PFN_D3DKMTEscape)LoadD3DKMTProc("D3DKMTEscape");
    if (pfn == NULL)
    {
        skip("D3DKMTEscape not exported\n");
        return;
    }

    hAdapter = OpenAdapterFromDisplay1();
    if (hAdapter == 0)
    {
        skip("Cannot open an adapter for Escape capture tests\n");
        return;
    }

    PageSize = GetPageSize();
    GuardPage = VirtualAlloc(NULL, PageSize, MEM_COMMIT | MEM_RESERVE, PAGE_NOACCESS);
    if (GuardPage == NULL)
    {
        skip("VirtualAlloc failed for Escape guard page\n");
        CloseAdapter(hAdapter);
        return;
    }

    memset(&Data, 0, sizeof(Data));
    Data.hAdapter = hAdapter;
    Data.Type = D3DKMT_ESCAPE_DRIVERPRIVATE;
    Data.pPrivateDriverData = GuardPage;
    Data.PrivateDriverDataSize = 1;
    Before = Data;
    CAPTURE_CALL(Status, Faulted, pfn(&Data));
    ok(Status == STATUS_ACCESS_VIOLATION, "Escape with inaccessible private data returned 0x%08lX%s, expected STATUS_ACCESS_VIOLATION\n", (long)Status, Faulted ? " (faulted)" : "");
    ok(memcmp(&Data, &Before, sizeof(Data)) == 0, "Escape changed its descriptor after private-data capture failed\n");
    VirtualFree(GuardPage, 0, MEM_RELEASE);

    ReadOnlyPage = VirtualAlloc(NULL, PageSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (ReadOnlyPage == NULL)
    {
        skip("VirtualAlloc failed for Escape read-only page\n");
        CloseAdapter(hAdapter);
        return;
    }

    memset(ReadOnlyPage, 0x5A, sizeof(Expected));
    memcpy(Expected, ReadOnlyPage, sizeof(Expected));
    if (!VirtualProtect(ReadOnlyPage, PageSize, PAGE_READONLY, &OldProtect))
    {
        skip("VirtualProtect failed for Escape read-only page\n");
        VirtualFree(ReadOnlyPage, 0, MEM_RELEASE);
        CloseAdapter(hAdapter);
        return;
    }

    memset(&Data, 0, sizeof(Data));
    Data.hAdapter = hAdapter;
    Data.Type = D3DKMT_ESCAPE_DRIVERPRIVATE;
    Data.pPrivateDriverData = ReadOnlyPage;
    Data.PrivateDriverDataSize = sizeof(Expected);
    Before = Data;
    CAPTURE_CALL(Status, Faulted, pfn(&Data));
    ok(Status == STATUS_ACCESS_VIOLATION, "Escape with read-only private data returned 0x%08lX%s, expected STATUS_ACCESS_VIOLATION\n", (long)Status, Faulted ? " (faulted)" : "");
    ok(memcmp(&Data, &Before, sizeof(Data)) == 0, "Escape changed its descriptor after rejecting read-only private data\n");
    if (VirtualProtect(ReadOnlyPage, PageSize, PAGE_READWRITE, &OldProtect))
        ok(memcmp(ReadOnlyPage, Expected, sizeof(Expected)) == 0, "Escape modified a read-only private-data canary before reporting failure\n");
    else
        skip("VirtualProtect could not restore the Escape canary page\n");
    VirtualFree(ReadOnlyPage, 0, MEM_RELEASE);
    CloseAdapter(hAdapter);
}

static NTSTATUS
QueryExclusiveOwnership(PFN_D3DKMTCheckVidPnExclusiveOwnership pfn, D3DKMT_HANDLE hAdapter, D3DDDI_VIDEO_PRESENT_SOURCE_ID SourceId)
{
    D3DKMT_CHECKVIDPNEXCLUSIVEOWNERSHIP Data;

    memset(&Data, 0, sizeof(Data));
    Data.hAdapter = hAdapter;
    Data.VidPnSourceId = SourceId;
    return pfn(&Data);
}

static void
Test_SetVidPnSourceOwnerCaptureAndRollback(void)
{
    PFN_D3DKMTOpenAdapterFromGdiDisplayName pfnOpen;
    PFN_D3DKMTSetVidPnSourceOwner pfnOwner;
    PFN_D3DKMTCheckVidPnExclusiveOwnership pfnCheck;
    D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME OpenData;
    D3DKMT_SETVIDPNSOURCEOWNER OwnerData;
    D3DKMT_SETVIDPNSOURCEOWNER Before;
    D3DKMT_VIDPNSOURCEOWNER_TYPE OwnerTypes[2];
    D3DKMT_VIDPNSOURCEOWNER_TYPE OwnerTypesBefore[2];
    D3DDDI_VIDEO_PRESENT_SOURCE_ID SourceIds[2];
    D3DDDI_VIDEO_PRESENT_SOURCE_ID SourceIdsBefore[2];
    D3DKMT_HANDLE hDevice;
    PVOID GuardPage;
    SIZE_T PageSize;
    NTSTATUS BaselineStatus;
    NTSTATUS CheckStatus;
    NTSTATUS Status;
    BOOL Faulted;

    pfnOpen = (PFN_D3DKMTOpenAdapterFromGdiDisplayName)LoadD3DKMTProc("D3DKMTOpenAdapterFromGdiDisplayName");
    pfnOwner = (PFN_D3DKMTSetVidPnSourceOwner)LoadD3DKMTProc("D3DKMTSetVidPnSourceOwner");
    pfnCheck = (PFN_D3DKMTCheckVidPnExclusiveOwnership)LoadD3DKMTProc("D3DKMTCheckVidPnExclusiveOwnership");
    if (pfnOpen == NULL || pfnOwner == NULL || pfnCheck == NULL)
    {
        skip("VidPn owner capture exports are unavailable\n");
        return;
    }

    memset(&OpenData, 0, sizeof(OpenData));
    wcscpy(OpenData.DeviceName, L"\\\\.\\DISPLAY1");
    Status = pfnOpen(&OpenData);
    if (!NT_SUCCESS(Status) || OpenData.hAdapter == 0)
    {
        skip("Cannot open DISPLAY1 for VidPn owner capture tests (0x%08lX)\n", (long)Status);
        return;
    }

    hDevice = CreateTestDevice(OpenData.hAdapter);
    if (hDevice == 0)
    {
        skip("Cannot create a device for VidPn owner capture tests\n");
        CloseAdapter(OpenData.hAdapter);
        return;
    }

    memset(&OwnerData, 0, sizeof(OwnerData));
    OwnerData.hDevice = hDevice;
    CAPTURE_CALL(Status, Faulted, pfnOwner(&OwnerData));
    if (Faulted || !NT_SUCCESS(Status))
    {
        skip("Cannot establish an unowned VidPn baseline (0x%08lX%s)\n", (long)Status, Faulted ? " faulted" : "");
        DestroyTestDevice(hDevice);
        CloseAdapter(OpenData.hAdapter);
        return;
    }

    BaselineStatus = QueryExclusiveOwnership(pfnCheck, OpenData.hAdapter, OpenData.VidPnSourceId);

    OwnerTypes[0] = D3DKMT_VIDPNSOURCEOWNER_EXCLUSIVE;
    SourceIds[0] = (D3DDDI_VIDEO_PRESENT_SOURCE_ID)-1;
    memset(&OwnerData, 0, sizeof(OwnerData));
    OwnerData.hDevice = hDevice;
    OwnerData.pType = OwnerTypes;
    OwnerData.pVidPnSourceId = SourceIds;
    OwnerData.VidPnSourceCount = 1;
    CAPTURE_CALL(Status, Faulted, pfnOwner(&OwnerData));
    ok(!Faulted && Status == STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE, "SetVidPnSourceOwner with an invalid source returned 0x%08lX%s, expected STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE\n", (long)Status, Faulted ? " (faulted)" : "");
    CheckStatus = QueryExclusiveOwnership(pfnCheck, OpenData.hAdapter, OpenData.VidPnSourceId);
    ok(CheckStatus == BaselineStatus, "Invalid-source rejection changed VidPn ownership: baseline 0x%08lX, after 0x%08lX\n", (long)BaselineStatus, (long)CheckStatus);

    OwnerTypes[0] = D3DKMT_VIDPNSOURCEOWNER_EXCLUSIVEGDI;
    SourceIds[0] = OpenData.VidPnSourceId;
    CAPTURE_CALL(Status, Faulted, pfnOwner(&OwnerData));
    ok(!Faulted && Status == STATUS_INVALID_PARAMETER, "SetVidPnSourceOwner accepted EXCLUSIVEGDI on a non-legacy device: 0x%08lX%s\n", (long)Status, Faulted ? " (faulted)" : "");
    CheckStatus = QueryExclusiveOwnership(pfnCheck, OpenData.hAdapter, OpenData.VidPnSourceId);
    ok(CheckStatus == BaselineStatus, "Non-legacy EXCLUSIVEGDI rejection changed VidPn ownership: baseline 0x%08lX, after 0x%08lX\n", (long)BaselineStatus, (long)CheckStatus);

    PageSize = GetPageSize();
    GuardPage = VirtualAlloc(NULL, PageSize, MEM_COMMIT | MEM_RESERVE, PAGE_NOACCESS);
    if (GuardPage == NULL)
    {
        skip("VirtualAlloc failed for VidPn owner guard page\n");
        DestroyTestDevice(hDevice);
        CloseAdapter(OpenData.hAdapter);
        return;
    }

    OwnerTypes[0] = D3DKMT_VIDPNSOURCEOWNER_EXCLUSIVE;
    SourceIds[0] = OpenData.VidPnSourceId;
    memset(&OwnerData, 0, sizeof(OwnerData));
    OwnerData.hDevice = hDevice;
    OwnerData.pType = (D3DKMT_VIDPNSOURCEOWNER_TYPE *)GuardPage;
    OwnerData.pVidPnSourceId = SourceIds;
    OwnerData.VidPnSourceCount = 1;
    Before = OwnerData;
    CAPTURE_CALL(Status, Faulted, pfnOwner(&OwnerData));
    ok(Status == STATUS_ACCESS_VIOLATION, "SetVidPnSourceOwner with an inaccessible type array returned 0x%08lX%s, expected STATUS_ACCESS_VIOLATION\n", (long)Status, Faulted ? " (faulted)" : "");
    ok(memcmp(&OwnerData, &Before, sizeof(OwnerData)) == 0, "SetVidPnSourceOwner changed its descriptor after type-array capture failed\n");
    CheckStatus = QueryExclusiveOwnership(pfnCheck, OpenData.hAdapter, OpenData.VidPnSourceId);
    ok(CheckStatus == BaselineStatus, "Type-array capture failure changed VidPn ownership: baseline 0x%08lX, after 0x%08lX\n", (long)BaselineStatus, (long)CheckStatus);

    memset(&OwnerData, 0, sizeof(OwnerData));
    OwnerData.hDevice = hDevice;
    OwnerData.pType = OwnerTypes;
    OwnerData.pVidPnSourceId = (D3DDDI_VIDEO_PRESENT_SOURCE_ID *)GuardPage;
    OwnerData.VidPnSourceCount = 1;
    Before = OwnerData;
    CAPTURE_CALL(Status, Faulted, pfnOwner(&OwnerData));
    ok(Status == STATUS_ACCESS_VIOLATION, "SetVidPnSourceOwner with an inaccessible source array returned 0x%08lX%s, expected STATUS_ACCESS_VIOLATION\n", (long)Status, Faulted ? " (faulted)" : "");
    ok(memcmp(&OwnerData, &Before, sizeof(OwnerData)) == 0, "SetVidPnSourceOwner changed its descriptor after source-array capture failed\n");
    CheckStatus = QueryExclusiveOwnership(pfnCheck, OpenData.hAdapter, OpenData.VidPnSourceId);
    ok(CheckStatus == BaselineStatus, "Source-array capture failure changed VidPn ownership: baseline 0x%08lX, after 0x%08lX\n", (long)BaselineStatus, (long)CheckStatus);

    if (BaselineStatus == STATUS_SUCCESS || BaselineStatus == STATUS_GRAPHICS_PRESENT_UNOCCLUDED)
    {
        OwnerTypes[0] = D3DKMT_VIDPNSOURCEOWNER_EXCLUSIVE;
        OwnerTypes[1] = D3DKMT_VIDPNSOURCEOWNER_EXCLUSIVE;
        SourceIds[0] = OpenData.VidPnSourceId;
        SourceIds[1] = (D3DDDI_VIDEO_PRESENT_SOURCE_ID)-1;
        memcpy(OwnerTypesBefore, OwnerTypes, sizeof(OwnerTypes));
        memcpy(SourceIdsBefore, SourceIds, sizeof(SourceIds));
        memset(&OwnerData, 0, sizeof(OwnerData));
        OwnerData.hDevice = hDevice;
        OwnerData.pType = OwnerTypes;
        OwnerData.pVidPnSourceId = SourceIds;
        OwnerData.VidPnSourceCount = ARRAYSIZE(SourceIds);
        Before = OwnerData;
        CAPTURE_CALL(Status, Faulted, pfnOwner(&OwnerData));
        ok(!Faulted && Status == STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE, "SetVidPnSourceOwner partial-invalid request returned 0x%08lX%s, expected STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE\n", (long)Status, Faulted ? " (faulted)" : "");
        ok(memcmp(&OwnerData, &Before, sizeof(OwnerData)) == 0 && memcmp(OwnerTypes, OwnerTypesBefore, sizeof(OwnerTypes)) == 0 && memcmp(SourceIds, SourceIdsBefore, sizeof(SourceIds)) == 0, "SetVidPnSourceOwner changed caller input after rejecting a partial-invalid request\n");
        CheckStatus = QueryExclusiveOwnership(pfnCheck, OpenData.hAdapter, OpenData.VidPnSourceId);
        ok(CheckStatus == BaselineStatus, "Partial-invalid VidPn request committed its valid prefix: baseline 0x%08lX, after 0x%08lX\n", (long)BaselineStatus, (long)CheckStatus);
    }
    else
    {
        skip("VidPn source %u reports exclusive ownership (0x%08lX); partial-commit rollback cannot be isolated\n", OpenData.VidPnSourceId, (long)BaselineStatus);
    }

    VirtualFree(GuardPage, 0, MEM_RELEASE);
    memset(&OwnerData, 0, sizeof(OwnerData));
    OwnerData.hDevice = hDevice;
    CAPTURE_CALL(Status, Faulted, pfnOwner(&OwnerData));
    ok(!Faulted && NT_SUCCESS(Status), "Final VidPn ownership release failed with 0x%08lX%s\n", (long)Status, Faulted ? " (faulted)" : "");
    DestroyTestDevice(hDevice);
    CloseAdapter(OpenData.hAdapter);
}

START_TEST(capture)
{
    Test_EnumAdapters2EmbeddedOutput();
    Test_CreateContextPrivateCapture();
    Test_EscapePrivateCapture();
    Test_SetVidPnSourceOwnerCaptureAndRollback();
}
