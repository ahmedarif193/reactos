/*
 * PROJECT:     ReactOS display stack API tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Classify the active display path before WDM/WDDM/DWM runs
 */

#include "precomp.h"

static BOOL CALLBACK
CountMonitorProc(HMONITOR Monitor, HDC Dc, LPRECT Rect, LPARAM Context)
{
    ULONG *Count = (ULONG *)Context;

    ok(Monitor != NULL, "EnumDisplayMonitors returned a NULL monitor\n");
    ok(Rect != NULL, "EnumDisplayMonitors returned a NULL rect\n");
    if (Rect)
    {
        ok(Rect->right > Rect->left,
           "Monitor width is not positive: left=%ld right=%ld\n",
           Rect->left, Rect->right);
        ok(Rect->bottom > Rect->top,
           "Monitor height is not positive: top=%ld bottom=%ld\n",
           Rect->top, Rect->bottom);
    }

    if (Dc)
        trace("Monitor callback HDC=%p\n", Dc);

    ++(*Count);
    return TRUE;
}

static void
CloseD3dkmtAdapter(D3DKMT_HANDLE Adapter)
{
    PFN_D3DKMTCloseAdapter CloseAdapter;
    D3DKMT_CLOSEADAPTER CloseData;
    NTSTATUS Status;

    if (!Adapter)
        return;

    CloseAdapter = (PFN_D3DKMTCloseAdapter)
        LoadDisplayProc(L"gdi32.dll", "D3DKMTCloseAdapter");
    ok(CloseAdapter != NULL, "D3DKMTCloseAdapter is not exported\n");
    if (!CloseAdapter)
        return;

    ZeroMemory(&CloseData, sizeof(CloseData));
    CloseData.hAdapter = Adapter;
    Status = CloseAdapter(&CloseData);
    ok(NT_SUCCESS(Status),
       "D3DKMTCloseAdapter(%p) failed with 0x%lx\n",
       (PVOID)(ULONG_PTR)Adapter, Status);
}

static void
TestDisplayDeviceAndGdi(void)
{
    DISPLAY_DEVICEW Device;
    DEVMODEW Mode;
    HDC Dc;
    INT HorzRes, VertRes, BitsPerPixel, Planes;
    ULONG MonitorCount = 0;
    BOOL Ret;

    ZeroMemory(&Device, sizeof(Device));
    Device.cb = sizeof(Device);
    Ret = EnumDisplayDevicesW(NULL, 0, &Device, 0);
    ok(Ret, "EnumDisplayDevicesW(NULL, 0) failed, error %lu\n", GetLastError());
    if (!Ret)
        return;

    trace("DISPLAY0 name='%S' string='%S' flags=0x%08lx id='%S' key='%S'\n",
          Device.DeviceName,
          Device.DeviceString,
          Device.StateFlags,
          Device.DeviceID,
          Device.DeviceKey);

    ok(Device.DeviceName[0] != UNICODE_NULL, "Display device name is empty\n");
    ok(Device.DeviceString[0] != UNICODE_NULL, "Display device string is empty\n");
    ok((Device.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) != 0,
       "Primary display is not attached to the desktop, flags 0x%08lx\n",
       Device.StateFlags);

    ZeroMemory(&Mode, sizeof(Mode));
    Mode.dmSize = sizeof(Mode);
    Ret = EnumDisplaySettingsExW(Device.DeviceName, ENUM_CURRENT_SETTINGS, &Mode, 0);
    ok(Ret, "EnumDisplaySettingsExW(%S) failed, error %lu\n",
       Device.DeviceName, GetLastError());
    if (Ret)
    {
        trace("Mode %lux%lu %lu bpp freq=%lu flags=0x%08lx\n",
              Mode.dmPelsWidth,
              Mode.dmPelsHeight,
              Mode.dmBitsPerPel,
              Mode.dmDisplayFrequency,
              Mode.dmDisplayFlags);
        ok(Mode.dmPelsWidth > 0, "Display mode width is zero\n");
        ok(Mode.dmPelsHeight > 0, "Display mode height is zero\n");
        ok(Mode.dmBitsPerPel >= 8, "Display bpp %lu is below 8\n",
           Mode.dmBitsPerPel);
    }

    Dc = CreateDCW(Device.DeviceName, NULL, NULL, NULL);
    ok(Dc != NULL, "CreateDCW(%S) failed, error %lu\n",
       Device.DeviceName, GetLastError());
    if (!Dc)
        return;

    HorzRes = GetDeviceCaps(Dc, HORZRES);
    VertRes = GetDeviceCaps(Dc, VERTRES);
    BitsPerPixel = GetDeviceCaps(Dc, BITSPIXEL);
    Planes = GetDeviceCaps(Dc, PLANES);

    trace("GDI caps HORZRES=%d VERTRES=%d BITSPIXEL=%d PLANES=%d TECHNOLOGY=%d\n",
          HorzRes, VertRes, BitsPerPixel, Planes, GetDeviceCaps(Dc, TECHNOLOGY));
    ok(HorzRes > 0, "HORZRES is %d\n", HorzRes);
    ok(VertRes > 0, "VERTRES is %d\n", VertRes);
    ok(BitsPerPixel > 0, "BITSPIXEL is %d\n", BitsPerPixel);
    ok(Planes > 0, "PLANES is %d\n", Planes);
    if (Ret)
    {
        ok((ULONG)HorzRes == Mode.dmPelsWidth,
           "GDI width %d does not match mode width %lu\n",
           HorzRes, Mode.dmPelsWidth);
        ok((ULONG)VertRes == Mode.dmPelsHeight,
           "GDI height %d does not match mode height %lu\n",
           VertRes, Mode.dmPelsHeight);
    }

    Ret = EnumDisplayMonitors(NULL, NULL, CountMonitorProc, (LPARAM)&MonitorCount);
    ok(Ret, "EnumDisplayMonitors failed, error %lu\n", GetLastError());
    ok(MonitorCount >= 1, "Expected at least one monitor, got %lu\n",
       MonitorCount);
    ok(GetSystemMetrics(SM_CMONITORS) >= 1,
       "SM_CMONITORS returned %d\n", GetSystemMetrics(SM_CMONITORS));
    trace("Monitor count callback=%lu system=%d remote=%d\n",
          MonitorCount,
          GetSystemMetrics(SM_CMONITORS),
          GetSystemMetrics(SM_REMOTESESSION));

    DeleteDC(Dc);
}

static void
TestD3dkmtIdentity(void)
{
    DISPLAY_DEVICEW Device;
    D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME OpenData;
    D3DKMT_ENUMADAPTERS EnumData;
    PFN_D3DKMTOpenAdapterFromGdiDisplayName OpenAdapter;
    PFN_D3DKMTEnumAdapters EnumAdapters;
    NTSTATUS Status;
    ULONG Index;

    OpenAdapter = (PFN_D3DKMTOpenAdapterFromGdiDisplayName)
        LoadDisplayProc(L"gdi32.dll", "D3DKMTOpenAdapterFromGdiDisplayName");
    EnumAdapters = (PFN_D3DKMTEnumAdapters)
        LoadDisplayProc(L"gdi32.dll", "D3DKMTEnumAdapters");

    ok(OpenAdapter != NULL,
       "D3DKMTOpenAdapterFromGdiDisplayName is required for WDDM identity\n");
    ok(EnumAdapters != NULL,
       "D3DKMTEnumAdapters is required for Win8+ WDDM identity\n");
    if (!OpenAdapter || !EnumAdapters)
        return;

    ZeroMemory(&Device, sizeof(Device));
    Device.cb = sizeof(Device);
    ok(EnumDisplayDevicesW(NULL, 0, &Device, 0),
       "EnumDisplayDevicesW(NULL, 0) failed before D3DKMT open\n");
    if (Device.DeviceName[0] == UNICODE_NULL)
        return;

    ZeroMemory(&OpenData, sizeof(OpenData));
    lstrcpynW(OpenData.DeviceName, Device.DeviceName, ARRAYSIZE(OpenData.DeviceName));
    Status = OpenAdapter(&OpenData);
    ok(NT_SUCCESS(Status),
       "D3DKMTOpenAdapterFromGdiDisplayName(%S) failed with 0x%lx\n",
       Device.DeviceName, Status);
    if (NT_SUCCESS(Status))
    {
        trace("D3DKMT adapter handle=%p luid=%08lx:%08lx source=%lu\n",
              (PVOID)(ULONG_PTR)OpenData.hAdapter,
              OpenData.AdapterLuid.HighPart,
              OpenData.AdapterLuid.LowPart,
              OpenData.VidPnSourceId);
        ok(OpenData.hAdapter != 0, "D3DKMT adapter handle is zero\n");
        CloseD3dkmtAdapter(OpenData.hAdapter);
    }

    ZeroMemory(&EnumData, sizeof(EnumData));
    Status = EnumAdapters(&EnumData);
    ok(NT_SUCCESS(Status), "D3DKMTEnumAdapters failed with 0x%lx\n", Status);
    if (!NT_SUCCESS(Status))
        return;

    trace("D3DKMTEnumAdapters NumAdapters=%lu\n", EnumData.NumAdapters);
    ok(EnumData.NumAdapters >= 1, "D3DKMTEnumAdapters returned zero adapters\n");
    ok(EnumData.NumAdapters <= ARRAYSIZE(EnumData.Adapters),
       "D3DKMTEnumAdapters returned too many adapters: %lu > %Iu\n",
       EnumData.NumAdapters, ARRAYSIZE(EnumData.Adapters));

    for (Index = 0;
         Index < EnumData.NumAdapters && Index < ARRAYSIZE(EnumData.Adapters);
         ++Index)
    {
        trace("Adapter[%lu] handle=%p luid=%08lx:%08lx source=%lu\n",
              Index,
              (PVOID)(ULONG_PTR)EnumData.Adapters[Index].hAdapter,
              EnumData.Adapters[Index].AdapterLuid.HighPart,
              EnumData.Adapters[Index].AdapterLuid.LowPart,
              EnumData.Adapters[Index].NumOfSources);
        ok(EnumData.Adapters[Index].hAdapter != 0,
           "Adapter[%lu] handle is zero\n", Index);
        ok(EnumData.Adapters[Index].NumOfSources >= 1,
           "Adapter[%lu] has no sources\n", Index);
        CloseD3dkmtAdapter(EnumData.Adapters[Index].hAdapter);
    }
}

static void
TestDwmCompositionState(void)
{
    PFN_DwmIsCompositionEnabled DwmIsCompositionEnabled;
    BOOL Enabled = 0x7f;
    HRESULT Result;

    DwmIsCompositionEnabled = (PFN_DwmIsCompositionEnabled)
        LoadDisplayProc(L"dwmapi.dll", "DwmIsCompositionEnabled");
    ok(DwmIsCompositionEnabled != NULL,
       "DwmIsCompositionEnabled is required for the Aero composition path\n");
    if (!DwmIsCompositionEnabled)
        return;

    Result = DwmIsCompositionEnabled(&Enabled);
    ok(SUCCEEDED(Result),
       "DwmIsCompositionEnabled failed with 0x%08lx\n", Result);
    ok(Enabled == FALSE || Enabled == TRUE,
       "DwmIsCompositionEnabled returned non-BOOL value %d\n", Enabled);
    trace("DWM composition enabled=%d\n", Enabled);
}

START_TEST(DisplayStackClassifier)
{
    OSVERSIONINFOW Version;

    ZeroMemory(&Version, sizeof(Version));
    Version.dwOSVersionInfoSize = sizeof(Version);
    ok(GetVersionExW(&Version), "GetVersionExW failed, error %lu\n",
       GetLastError());
    trace("OS version %lu.%lu build %lu platform %lu\n",
          Version.dwMajorVersion,
          Version.dwMinorVersion,
          Version.dwBuildNumber,
          Version.dwPlatformId);

    TestDisplayDeviceAndGdi();
    TestD3dkmtIdentity();
    TestDwmCompositionState();
}
