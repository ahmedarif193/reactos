/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF <arif193@gmail.com>
 */

/* Test-only read of the shared primary. Never used in a composition loop. */
typedef struct _PRIMARY_PROBE_INVALIDATE
{
    D3DKMT_HANDLE hDevice;
    D3DKMT_HANDLE hAllocation;
    ULONGLONG Offset;
    ULONGLONG Length;
} PRIMARY_PROBE_INVALIDATE;
typedef NTSTATUS (APIENTRY *PRIMARY_PROBE_INVALIDATE_FN)(const PRIMARY_PROBE_INVALIDATE *);
C_ASSERT(sizeof(PRIMARY_PROBE_INVALIDATE) == 24);

static COLORREF
ReadSharedPrimaryPixel(HDC Screen, POINT Point)
{
    D3DKMT_OPENADAPTERFROMHDC Adapter = {0};
    D3DKMT_CREATEDEVICE Device = {0};
    D3DKMT_GETSHAREDPRIMARYHANDLE Primary = {0};
    D3DKMT_QUERYRESOURCEINFO Query = {0};
    D3DKMT_OPENRESOURCE Open = {0};
    D3DDDI_OPENALLOCATIONINFO Allocation = {0};
    D3DKMT_LOCK Lock = {0};
    D3DKMT_UNLOCK Unlock = {0};
    PRIMARY_PROBE_INVALIDATE Invalidate = {0};
    PRIMARY_PROBE_INVALIDATE_FN InvalidateCache =
        (PRIMARY_PROBE_INVALIDATE_FN)GetProcAddress(GetModuleHandleW(L"gdi32.dll"),
                                                   "D3DKMTInvalidateCache");
    PVOID Runtime = NULL, ResourcePrivate = NULL, AllocationPrivate = NULL;
    SIZE_T Offset, BytesRead = 0;
    ULONG Pixel;
    LONG Width = GetDeviceCaps(Screen, HORZRES);
    LONG Height = GetDeviceCaps(Screen, VERTRES);
    COLORREF Result = CLR_INVALID;
    NTSTATUS Status = 0;
    const char *Stage = "adapter";
    BOOL Locked = FALSE;

    if (Width <= 0 || Height <= 0 || GetDeviceCaps(Screen, BITSPIXEL) != 32 ||
        Point.x < 0 || Point.y < 0 || Point.x >= Width || Point.y >= Height)
        return CLR_INVALID;
    Adapter.hDc = Screen;
    Status = D3DKMTOpenAdapterFromHdc(&Adapter);
    if (Status < 0) goto Cleanup;
    Stage = "device";
    Device.hAdapter = Adapter.hAdapter;
    Status = D3DKMTCreateDevice(&Device);
    if (Status < 0) goto Cleanup;
    Stage = "primary";
    Primary.hAdapter = Adapter.hAdapter;
    Primary.VidPnSourceId = Adapter.VidPnSourceId;
    Status = D3DKMTGetSharedPrimaryHandle(&Primary);
    if (Status < 0) goto Cleanup;
    Stage = "query";
    Query.hDevice = Device.hDevice;
    Query.hGlobalShare = Primary.hSharedPrimary;
    Status = D3DKMTQueryResourceInfo(&Query);
    if (Status < 0 || Query.NumAllocations != 1 ||
        Query.PrivateRuntimeDataSize > 65536 ||
        Query.ResourcePrivateDriverDataSize > 65536 ||
        Query.TotalPrivateDriverDataSize > 65536) goto Cleanup;
    Stage = "metadata";
    if (Query.PrivateRuntimeDataSize)
        Runtime = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Query.PrivateRuntimeDataSize);
    if (Query.ResourcePrivateDriverDataSize)
        ResourcePrivate = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Query.ResourcePrivateDriverDataSize);
    if (Query.TotalPrivateDriverDataSize)
        AllocationPrivate = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Query.TotalPrivateDriverDataSize);
    if ((Query.PrivateRuntimeDataSize && !Runtime) ||
        (Query.ResourcePrivateDriverDataSize && !ResourcePrivate) ||
        (Query.TotalPrivateDriverDataSize && !AllocationPrivate)) goto Cleanup;
    Stage = "open";
    Open.hDevice = Device.hDevice;
    Open.hGlobalShare = Primary.hSharedPrimary;
    Open.NumAllocations = 1;
    Open.pOpenAllocationInfo = &Allocation;
    Open.pPrivateRuntimeData = Runtime;
    Open.PrivateRuntimeDataSize = Query.PrivateRuntimeDataSize;
    Open.pResourcePrivateDriverData = ResourcePrivate;
    Open.ResourcePrivateDriverDataSize = Query.ResourcePrivateDriverDataSize;
    Open.pTotalPrivateDriverDataBuffer = AllocationPrivate;
    Open.TotalPrivateDriverDataBufferSize = Query.TotalPrivateDriverDataSize;
    Status = D3DKMTOpenResource(&Open);
    if (Status < 0) goto Cleanup;
    Stage = "lock";
    Lock.hDevice = Device.hDevice;
    Lock.hAllocation = Allocation.hAllocation;
    Lock.Flags.ReadOnly = 1;
    Lock.Flags.LockEntire = 1;
    Status = D3DKMTLock(&Lock);
    if (Status < 0) goto Cleanup;
    Locked = TRUE;
    Stage = "bounds";
    Offset = ((SIZE_T)Point.y * Width + Point.x) * sizeof(ULONG);
    if (!Lock.pData) goto Cleanup;
    Stage = "invalidate";
    Invalidate.hDevice = Device.hDevice;
    Invalidate.hAllocation = Allocation.hAllocation;
    Invalidate.Offset = Offset;
    Invalidate.Length = sizeof(ULONG);
    if (!InvalidateCache) goto Cleanup;
    Status = InvalidateCache(&Invalidate);
    if (Status < 0) goto Cleanup;
    /* ReadProcessMemory probes the requested four bytes. VirtualQuery on
     * the MDL's physical-memory VAD currently asserts in ARM3; do not use
     * an unchecked dereference to work around that unrelated query defect. */
    Stage = "read";
    if (!ReadProcessMemory(GetCurrentProcess(), (BYTE *)Lock.pData + Offset,
                           &Pixel, sizeof(Pixel), &BytesRead) ||
        BytesRead != sizeof(Pixel)) goto Cleanup;
    Result = RGB((Pixel >> 16) & 255, (Pixel >> 8) & 255, Pixel & 255);
Cleanup:
    if (Locked)
    {
        Unlock.hDevice = Device.hDevice;
        Unlock.NumAllocations = 1;
        Unlock.phAllocations = &Allocation.hAllocation;
        D3DKMTUnlock(&Unlock);
    }
    if (Open.hResource)
    {
        D3DKMT_DESTROYALLOCATION Destroy = {0};
        Destroy.hDevice = Device.hDevice;
        Destroy.hResource = Open.hResource;
        D3DKMTDestroyAllocation(&Destroy);
    }
    if (Device.hDevice)
    {
        D3DKMT_DESTROYDEVICE Destroy = {0};
        Destroy.hDevice = Device.hDevice;
        D3DKMTDestroyDevice(&Destroy);
    }
    if (Adapter.hAdapter)
    {
        D3DKMT_CLOSEADAPTER Close = {0};
        Close.hAdapter = Adapter.hAdapter;
        D3DKMTCloseAdapter(&Close);
    }
    if (Runtime) HeapFree(GetProcessHeap(), 0, Runtime);
    if (ResourcePrivate) HeapFree(GetProcessHeap(), 0, ResourcePrivate);
    if (AllocationPrivate) HeapFree(GetProcessHeap(), 0, AllocationPrivate);
    TestPrint("DWM_SHARED_PRIMARY x=%ld y=%ld value=%08lx stage=%s status=%08lx\n",
              Point.x, Point.y, Result, Stage, Status);
    return Result;
}
