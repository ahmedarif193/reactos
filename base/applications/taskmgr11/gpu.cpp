/*
 * PROJECT:     ReactOS Task Manager
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     GPU data collection for the Performance page
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 *
 * Everything here comes from the D3DKMT interface, which is where Windows
 * gets the same numbers: the adapter list, the engines the display miniport
 * declared, how busy each of them has been, how much of each memory group is
 * in use, and whatever thermal data the driver is willing to report.
 *
 * Nothing is synthesised.  An engine appears because a node exists, a
 * temperature appears because a driver answered with one, and a driver that
 * answers nothing leaves those rows out instead of showing a zero that would
 * read as a real measurement.
 */

#include "app.h"

#include <setupapi.h>
#include <cfgmgr32.h>
#include <ntddvdeo.h>
#include <devguid.h>
#include <d3dkmthk.h>
#include <dxgi1_4.h>
#include <d3d11.h>
#include <d3d12.h>

/* ------------------------------------------------------------------ */
/*  gdi32 D3DKMT entry points                                          */
/*                                                                     */
/*  Resolved by name so Task Manager still starts, and still shows      */
/*  every other resource, on a system whose graphics stack is missing   */
/*  or broken.  A GPU section that cannot be filled is simply absent.   */
/* ------------------------------------------------------------------ */

typedef NTSTATUS (APIENTRY *PFN_D3DKMTEnumAdapters2)(D3DKMT_ENUMADAPTERS2*);
typedef NTSTATUS (APIENTRY *PFN_D3DKMTCloseAdapter)(const D3DKMT_CLOSEADAPTER*);
typedef NTSTATUS (APIENTRY *PFN_D3DKMTQueryAdapterInfo)(const D3DKMT_QUERYADAPTERINFO*);
typedef NTSTATUS (APIENTRY *PFN_D3DKMTQueryStatistics)(const D3DKMT_QUERYSTATISTICS*);
typedef NTSTATUS (APIENTRY *PFN_D3DKMTOpenAdapterFromDeviceName)(D3DKMT_OPENADAPTERFROMDEVICENAME*);

static PFN_D3DKMTEnumAdapters2             pEnumAdapters2;
static PFN_D3DKMTCloseAdapter              pCloseAdapter;
static PFN_D3DKMTQueryAdapterInfo          pQueryAdapterInfo;
static PFN_D3DKMTQueryStatistics           pQueryStatistics;
static PFN_D3DKMTOpenAdapterFromDeviceName pOpenAdapterFromDeviceName;

static BOOL      s_available;
static BOOL      s_identityResolved;
static ULONGLONG s_lastSampleQpc;
static double    s_qpcFrequency;

static BOOL LoadD3DKMT(void)
{
    HMODULE gdi32 = GetModuleHandleW(L"gdi32.dll");
    if (!gdi32)
        return FALSE;

    pEnumAdapters2 = (PFN_D3DKMTEnumAdapters2)
        GetProcAddress(gdi32, "D3DKMTEnumAdapters2");
    pCloseAdapter = (PFN_D3DKMTCloseAdapter)
        GetProcAddress(gdi32, "D3DKMTCloseAdapter");
    pQueryAdapterInfo = (PFN_D3DKMTQueryAdapterInfo)
        GetProcAddress(gdi32, "D3DKMTQueryAdapterInfo");
    pQueryStatistics = (PFN_D3DKMTQueryStatistics)
        GetProcAddress(gdi32, "D3DKMTQueryStatistics");
    pOpenAdapterFromDeviceName = (PFN_D3DKMTOpenAdapterFromDeviceName)
        GetProcAddress(gdi32, "D3DKMTOpenAdapterFromDeviceName");

    return pEnumAdapters2 && pCloseAdapter && pQueryAdapterInfo && pQueryStatistics;
}

static BOOL SameLuid(const LUID& a, const LUID& b)
{
    return a.LowPart == b.LowPart && a.HighPart == b.HighPart;
}

/* ------------------------------------------------------------------ */
/*  Engine names                                                       */
/* ------------------------------------------------------------------ */

static const WCHAR* EngineTypeName(ULONG engineType)
{
    switch (engineType)
    {
    case DXGK_ENGINE_TYPE_3D:               return L"3D";
    case DXGK_ENGINE_TYPE_VIDEO_DECODE:     return L"Video Decode";
    case DXGK_ENGINE_TYPE_VIDEO_ENCODE:     return L"Video Encode";
    case DXGK_ENGINE_TYPE_VIDEO_PROCESSING: return L"Video Processing";
    case DXGK_ENGINE_TYPE_SCENE_ASSEMBLY:   return L"Scene Assembly";
    case DXGK_ENGINE_TYPE_COPY:             return L"Copy";
    case DXGK_ENGINE_TYPE_OVERLAY:          return L"Overlay";
    case DXGK_ENGINE_TYPE_CRYPTO:           return L"Security";
    default:                                return NULL;
    }
}

/*
 * The engine label Windows shows is the type name when the node has a known
 * type, and the driver's own friendly name when it does not.  A node that
 * offers neither is still a real engine, so it is numbered rather than
 * dropped.
 */
static void EngineName(const DXGK_NODEMETADATA& meta, int ordinal,
                       WCHAR* buf, int cch)
{
    const WCHAR* typeName = EngineTypeName((ULONG)meta.EngineType);
    WCHAR friendly[DXGK_MAX_METADATA_NAME_LENGTH + 1];

    if (typeName)
    {
        StringCchCopyW(buf, cch, typeName);
        return;
    }

    CopyMemory(friendly, meta.FriendlyName, sizeof(meta.FriendlyName));
    friendly[DXGK_MAX_METADATA_NAME_LENGTH] = 0;
    if (friendly[0])
        StringCchCopyW(buf, cch, friendly);
    else
        StringCchPrintfW(buf, cch, L"Engine %d", ordinal);
}

/* ------------------------------------------------------------------ */
/*  Adapter identity (name, driver, bus location)                      */
/* ------------------------------------------------------------------ */

static void FormatBusLocation(const D3DKMT_ADAPTERADDRESS& address,
                              WCHAR* buf, int cch)
{
    StringCchPrintfW(buf, cch, L"PCI bus %u, device %u, function %u",
                     address.BusNumber, address.DeviceNumber,
                     address.FunctionNumber);
}

/*
 * The version resource of the driver binary a devnode is bound to.  This is
 * the fallback for a driver whose installation wrote no DriverVersion value:
 * the binary's own version is still that driver's version, and reporting it
 * beats reporting nothing.  The service name comes from the same devnode, so
 * the file read is the very driver behind this adapter and not a guess.
 */
static BOOL ReadServiceBinaryVersion(const WCHAR* service, WCHAR* buf, int cch)
{
    WCHAR key[256];
    WCHAR imagePath[MAX_PATH];
    WCHAR fullPath[MAX_PATH];
    WCHAR systemRoot[MAX_PATH];
    HKEY hk;
    DWORD cb;
    DWORD handle = 0;
    DWORD size;
    void* block;
    VS_FIXEDFILEINFO* fixed = NULL;
    UINT fixedLength = 0;
    BOOL resolved = FALSE;

    if (!service || !service[0])
        return FALSE;

    StringCchPrintfW(key, _countof(key),
                     L"SYSTEM\\CurrentControlSet\\Services\\%s", service);
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, key, 0, KEY_QUERY_VALUE, &hk) != ERROR_SUCCESS)
        return FALSE;
    cb = sizeof(imagePath);
    imagePath[0] = 0;
    if (RegQueryValueExW(hk, L"ImagePath", NULL, NULL, (LPBYTE)imagePath, &cb) != ERROR_SUCCESS)
    {
        RegCloseKey(hk);
        return FALSE;
    }
    RegCloseKey(hk);
    imagePath[_countof(imagePath) - 1] = 0;
    if (!imagePath[0])
        return FALSE;

    GetWindowsDirectoryW(systemRoot, _countof(systemRoot));
    if (imagePath[0] == L'\\')
    {
        /* A native path: everything after the \SystemRoot prefix is the
         * part below the Windows directory. */
        const WCHAR* tail = wcschr(imagePath + 1, L'\\');

        StringCchPrintfW(fullPath, _countof(fullPath), L"%s%s", systemRoot,
                         tail ? tail : L"");
    }
    else if (imagePath[1] == L':')
    {
        StringCchCopyW(fullPath, _countof(fullPath), imagePath);
    }
    else
    {
        /* The usual form: relative to the Windows directory. */
        StringCchPrintfW(fullPath, _countof(fullPath), L"%s\\%s", systemRoot, imagePath);
    }

    size = GetFileVersionInfoSizeW(fullPath, &handle);
    if (!size)
        return FALSE;
    block = HeapAlloc(GetProcessHeap(), 0, size);
    if (!block)
        return FALSE;
    if (GetFileVersionInfoW(fullPath, handle, size, block) &&
        VerQueryValueW(block, L"\\", (LPVOID*)&fixed, &fixedLength) &&
        fixed && fixedLength >= sizeof(*fixed))
    {
        StringCchPrintfW(buf, cch, L"%u.%u.%u.%u",
                         HIWORD(fixed->dwFileVersionMS), LOWORD(fixed->dwFileVersionMS),
                         HIWORD(fixed->dwFileVersionLS), LOWORD(fixed->dwFileVersionLS));
        resolved = TRUE;
    }
    HeapFree(GetProcessHeap(), 0, block);
    return resolved;
}

static void ResolveDriverDetailsByDescription(GpuSnapshot* gpu);

/*
 * Read the description and the installed driver's version and date from the
 * devnode that owns this LUID.  The match is made by opening each display
 * adapter interface through D3DKMT and comparing the LUID it hands back, not
 * by matching names: two adapters of the same model would otherwise swap
 * their driver versions with each other.
 */
static void ResolveAdapterIdentity(void)
{
    HDEVINFO devInfo;
    SP_DEVICE_INTERFACE_DATA ifData;
    DWORD index;

    if (!pOpenAdapterFromDeviceName)
        return;

    /*
     * The arrival interface is the one a WDDM display port registers, and it
     * is the one D3DKMTOpenAdapterFromDeviceName accepts; the older adapter
     * interface is refused there even on Windows.
     */
    devInfo = SetupDiGetClassDevsW(&GUID_DISPLAY_DEVICE_ARRIVAL, NULL, NULL,
                                   DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (devInfo == INVALID_HANDLE_VALUE)
        return;

    ZeroMemory(&ifData, sizeof(ifData));
    ifData.cbSize = sizeof(ifData);
    for (index = 0;
         SetupDiEnumDeviceInterfaces(devInfo, NULL,
                                     &GUID_DISPLAY_DEVICE_ARRIVAL,
                                     index, &ifData);
         index++)
    {
        BYTE detailBuffer[sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W) + 1024];
        SP_DEVICE_INTERFACE_DETAIL_DATA_W* detail =
            (SP_DEVICE_INTERFACE_DETAIL_DATA_W*)detailBuffer;
        SP_DEVINFO_DATA devData;
        D3DKMT_OPENADAPTERFROMDEVICENAME open;
        D3DKMT_CLOSEADAPTER close;
        GpuSnapshot* gpu = NULL;
        int i;

        ZeroMemory(detailBuffer, sizeof(detailBuffer));
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        ZeroMemory(&devData, sizeof(devData));
        devData.cbSize = sizeof(devData);
        if (!SetupDiGetDeviceInterfaceDetailW(devInfo, &ifData, detail,
                                              sizeof(detailBuffer), NULL, &devData))
        {
            continue;
        }

        ZeroMemory(&open, sizeof(open));
        open.pDeviceName = detail->DevicePath;
        if (!NT_SUCCESS(pOpenAdapterFromDeviceName(&open)))
            continue;

        for (i = 0; i < Data::g.gpuCount; i++)
        {
            if (SameLuid(Data::g.gpus[i].luid, open.AdapterLuid))
            {
                gpu = &Data::g.gpus[i];
                break;
            }
        }

        if (gpu)
        {
            HKEY driverKey;
            DWORD cb;

            WCHAR description[160];

            cb = sizeof(description);
            description[0] = 0;
            if (SetupDiGetDeviceRegistryPropertyW(devInfo, &devData, SPDRP_DEVICEDESC,
                                                  NULL, (PBYTE)description, cb, NULL) &&
                description[0])
            {
                StringCchCopyW(gpu->name, _countof(gpu->name), description);
            }

            driverKey = SetupDiOpenDevRegKey(devInfo, &devData, DICS_FLAG_GLOBAL, 0,
                                             DIREG_DRV, KEY_QUERY_VALUE);
            if (driverKey != INVALID_HANDLE_VALUE)
            {
                cb = sizeof(gpu->driverVersion);
                if (RegQueryValueExW(driverKey, L"DriverVersion", NULL, NULL,
                                     (LPBYTE)gpu->driverVersion, &cb) != ERROR_SUCCESS)
                {
                    gpu->driverVersion[0] = 0;
                }
                cb = sizeof(gpu->driverDate);
                if (RegQueryValueExW(driverKey, L"DriverDate", NULL, NULL,
                                     (LPBYTE)gpu->driverDate, &cb) != ERROR_SUCCESS)
                {
                    gpu->driverDate[0] = 0;
                }
                RegCloseKey(driverKey);
            }

            if (!gpu->driverVersion[0])
            {
                WCHAR service[128];

                cb = sizeof(service);
                service[0] = 0;
                if (SetupDiGetDeviceRegistryPropertyW(devInfo, &devData, SPDRP_SERVICE,
                                                      NULL, (PBYTE)service, cb, NULL))
                {
                    ReadServiceBinaryVersion(service, gpu->driverVersion,
                                             _countof(gpu->driverVersion));
                }
            }
        }

        ZeroMemory(&close, sizeof(close));
        close.hAdapter = open.hAdapter;
        pCloseAdapter(&close);
    }

    SetupDiDestroyDeviceInfoList(devInfo);

    /* Anything the interface enumeration could not reach still has a devnode
     * in the display class. */
    for (int i = 0; i < Data::g.gpuCount; i++)
    {
        if (!Data::g.gpus[i].driverVersion[0])
            ResolveDriverDetailsByDescription(&Data::g.gpus[i]);
    }
}

/*
 * Driver details for an adapter the interface enumeration could not reach.
 *
 * The display device class can be enumerated without device-interface
 * support, but a devnode there carries no LUID, so the only thing to match
 * on is the description -- which the adapter itself supplied through
 * KMTQAITYPE_ADAPTERREGISTRYINFO, and which is that devnode's own
 * SPDRP_DEVICEDESC.  A description shared by two present adapters cannot
 * tell them apart, so that case is left alone rather than guessed at: a
 * driver version shown against the wrong adapter is worse than none.
 */
static void ResolveDriverDetailsByDescription(GpuSnapshot* gpu)
{
    HDEVINFO devInfo;
    SP_DEVINFO_DATA devData;
    SP_DEVINFO_DATA matched;
    DWORD index;
    int matches = 0;

    if (!gpu->name[0])
        return;

    devInfo = SetupDiGetClassDevsW(&GUID_DEVCLASS_DISPLAY, NULL, NULL, DIGCF_PRESENT);
    if (devInfo == INVALID_HANDLE_VALUE)
        return;

    ZeroMemory(&matched, sizeof(matched));
    ZeroMemory(&devData, sizeof(devData));
    devData.cbSize = sizeof(devData);
    for (index = 0; SetupDiEnumDeviceInfo(devInfo, index, &devData); index++)
    {
        WCHAR description[160];

        description[0] = 0;
        if (!SetupDiGetDeviceRegistryPropertyW(devInfo, &devData, SPDRP_DEVICEDESC,
                                               NULL, (PBYTE)description,
                                               sizeof(description), NULL))
        {
            continue;
        }
        if (lstrcmpiW(description, gpu->name) != 0)
            continue;
        matches++;
        matched = devData;
    }

    if (matches == 1)
    {
        HKEY driverKey = SetupDiOpenDevRegKey(devInfo, &matched, DICS_FLAG_GLOBAL, 0,
                                              DIREG_DRV, KEY_QUERY_VALUE);
        DWORD cb;

        if (driverKey != INVALID_HANDLE_VALUE)
        {
            cb = sizeof(gpu->driverVersion);
            if (RegQueryValueExW(driverKey, L"DriverVersion", NULL, NULL,
                                 (LPBYTE)gpu->driverVersion, &cb) != ERROR_SUCCESS)
            {
                gpu->driverVersion[0] = 0;
            }
            cb = sizeof(gpu->driverDate);
            if (RegQueryValueExW(driverKey, L"DriverDate", NULL, NULL,
                                 (LPBYTE)gpu->driverDate, &cb) != ERROR_SUCCESS)
            {
                gpu->driverDate[0] = 0;
            }
            RegCloseKey(driverKey);
        }

        if (!gpu->driverVersion[0])
        {
            WCHAR service[128];

            service[0] = 0;
            if (SetupDiGetDeviceRegistryPropertyW(devInfo, &matched, SPDRP_SERVICE,
                                                  NULL, (PBYTE)service,
                                                  sizeof(service), NULL))
            {
                ReadServiceBinaryVersion(service, gpu->driverVersion,
                                         _countof(gpu->driverVersion));
            }
        }
    }

    SetupDiDestroyDeviceInfoList(devInfo);
}

/* ------------------------------------------------------------------ */
/*  DirectX level                                                      */
/*                                                                     */
/*  Probed the way the runtime itself decides it: create a device on    */
/*  this exact adapter through the newest runtime that will accept it,  */
/*  and report the feature level that device came back with.  Both      */
/*  runtimes are loaded by name, so a system with no D3D12 reports what */
/*  it does have, and one with no Direct3D at all reports nothing.      */
/* ------------------------------------------------------------------ */

typedef HRESULT (WINAPI *PFN_CreateDXGIFactory1)(REFIID, void**);
typedef HRESULT (WINAPI *PFN_D3D12CreateDeviceProc)(IUnknown*, D3D_FEATURE_LEVEL,
                                                    REFIID, void**);
typedef HRESULT (WINAPI *PFN_D3D11CreateDeviceProc)(IDXGIAdapter*, D3D_DRIVER_TYPE,
                                                    HMODULE, UINT,
                                                    const D3D_FEATURE_LEVEL*, UINT,
                                                    UINT, ID3D11Device**,
                                                    D3D_FEATURE_LEVEL*,
                                                    ID3D11DeviceContext**);

static const D3D_FEATURE_LEVEL s_featureLevels[] =
{
    D3D_FEATURE_LEVEL_12_1,
    D3D_FEATURE_LEVEL_12_0,
    D3D_FEATURE_LEVEL_11_1,
    D3D_FEATURE_LEVEL_11_0,
    D3D_FEATURE_LEVEL_10_1,
    D3D_FEATURE_LEVEL_10_0,
    D3D_FEATURE_LEVEL_9_3,
    D3D_FEATURE_LEVEL_9_2,
    D3D_FEATURE_LEVEL_9_1,
};

static void FormatDirectXVersion(int runtime, D3D_FEATURE_LEVEL level,
                                 WCHAR* buf, int cch)
{
    StringCchPrintfW(buf, cch, L"%d (FL %u_%u)", runtime,
                     ((UINT)level >> 12) & 0xF, ((UINT)level >> 8) & 0xF);
}

/*
 * The adapter this LUID names, and only that one.  Picking the first DXGI
 * adapter instead would report one adapter's capabilities under another
 * adapter's heading on any machine with more than one.
 */
static IDXGIAdapter* FindDxgiAdapter(const LUID& luid)
{
    HMODULE dxgi = LoadLibraryW(L"dxgi.dll");
    PFN_CreateDXGIFactory1 createFactory;
    IDXGIFactory4* factory4 = NULL;
    IDXGIFactory1* factory1 = NULL;
    IDXGIAdapter* adapter = NULL;

    if (!dxgi)
        return NULL;

    createFactory = (PFN_CreateDXGIFactory1)GetProcAddress(dxgi, "CreateDXGIFactory1");
    if (!createFactory)
        return NULL;

    if (SUCCEEDED(createFactory(IID_IDXGIFactory4, (void**)&factory4)))
    {
        if (FAILED(factory4->EnumAdapterByLuid(luid, IID_IDXGIAdapter, (void**)&adapter)))
            adapter = NULL;
        factory4->Release();
        return adapter;
    }

    /* Without IDXGIFactory4 the LUID has to be matched by walking the list. */
    if (FAILED(createFactory(IID_IDXGIFactory1, (void**)&factory1)))
        return NULL;

    for (UINT i = 0;; i++)
    {
        IDXGIAdapter1* candidate = NULL;
        DXGI_ADAPTER_DESC1 desc;

        if (FAILED(factory1->EnumAdapters1(i, &candidate)) || !candidate)
            break;
        ZeroMemory(&desc, sizeof(desc));
        if (SUCCEEDED(candidate->GetDesc1(&desc)) && SameLuid(desc.AdapterLuid, luid))
        {
            adapter = candidate;
            break;
        }
        candidate->Release();
    }
    factory1->Release();
    return adapter;
}

static void ProbeDirectXVersion(GpuSnapshot* gpu)
{
    IDXGIAdapter* adapter;
    HMODULE d3d12;
    HMODULE d3d11;

    StringCchCopyW(gpu->directX, _countof(gpu->directX), L"Unavailable");

    adapter = FindDxgiAdapter(gpu->luid);
    if (!adapter)
        return;

    d3d12 = LoadLibraryW(L"d3d12.dll");
    if (d3d12)
    {
        PFN_D3D12CreateDeviceProc create =
            (PFN_D3D12CreateDeviceProc)GetProcAddress(d3d12, "D3D12CreateDevice");
        ID3D12Device* device = NULL;

        if (create && SUCCEEDED(create(adapter, D3D_FEATURE_LEVEL_11_0,
                                       IID_ID3D12Device, (void**)&device)))
        {
            D3D12_FEATURE_DATA_FEATURE_LEVELS levels;

            ZeroMemory(&levels, sizeof(levels));
            levels.NumFeatureLevels = _countof(s_featureLevels);
            levels.pFeatureLevelsRequested = s_featureLevels;
            if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS,
                                                   &levels, sizeof(levels))))
            {
                levels.MaxSupportedFeatureLevel = D3D_FEATURE_LEVEL_11_0;
            }
            FormatDirectXVersion(12, levels.MaxSupportedFeatureLevel,
                                 gpu->directX, _countof(gpu->directX));
            device->Release();
            adapter->Release();
            return;
        }
    }

    d3d11 = LoadLibraryW(L"d3d11.dll");
    if (d3d11)
    {
        PFN_D3D11CreateDeviceProc create =
            (PFN_D3D11CreateDeviceProc)GetProcAddress(d3d11, "D3D11CreateDevice");
        ID3D11Device* device = NULL;
        D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_9_1;

        if (create && SUCCEEDED(create(adapter, D3D_DRIVER_TYPE_UNKNOWN, NULL, 0,
                                       s_featureLevels, _countof(s_featureLevels),
                                       D3D11_SDK_VERSION, &device, &level, NULL)))
        {
            FormatDirectXVersion(11, level, gpu->directX, _countof(gpu->directX));
            device->Release();
        }
    }

    adapter->Release();
}

/* ------------------------------------------------------------------ */
/*  Per-adapter collection                                             */
/* ------------------------------------------------------------------ */

static void CollectEngines(GpuSnapshot* gpu, D3DKMT_HANDLE hAdapter,
                           ULONG nodeCount, double dt)
{
    ULONG node;

    if (nodeCount > TM_MAX_GPU_ENGINES)
        nodeCount = TM_MAX_GPU_ENGINES;

    gpu->utilPct = 0.0;
    for (node = 0; node < nodeCount; node++)
    {
        GpuEngineSnapshot* engine = &gpu->engines[node];
        D3DKMT_QUERYADAPTERINFO info;
        D3DKMT_NODEMETADATA metadata;
        D3DKMT_QUERYSTATISTICS stats;
        ULONGLONG previous = engine->runningTime;
        ULONGLONG current;
        double util;

        ZeroMemory(&metadata, sizeof(metadata));
        metadata.NodeOrdinalAndAdapterIndex = node;
        ZeroMemory(&info, sizeof(info));
        info.hAdapter = hAdapter;
        info.Type = KMTQAITYPE_NODEMETADATA;
        info.pPrivateDriverData = &metadata;
        info.PrivateDriverDataSize = sizeof(metadata);
        if (NT_SUCCESS(pQueryAdapterInfo(&info)))
            EngineName(metadata.NodeData, (int)node, engine->name, _countof(engine->name));
        else if (!engine->name[0])
            StringCchPrintfW(engine->name, _countof(engine->name), L"Engine %u", node);
        engine->engineType = (ULONG)metadata.NodeData.EngineType;

        ZeroMemory(&stats, sizeof(stats));
        stats.Type = D3DKMT_QUERYSTATISTICS_NODE;
        stats.AdapterLuid = gpu->luid;
        stats.QueryNode.NodeId = node;
        if (!NT_SUCCESS(pQueryStatistics(&stats)))
            continue;

        current = (ULONGLONG)stats.QueryResult.NodeInformation
                      .GlobalInformation.RunningTime.QuadPart;

        /*
         * Utilization is the share of the sample interval this engine spent
         * executing.  The first sample has no interval behind it, and a
         * counter that went backwards means the adapter restarted, so both
         * report nothing rather than a spike.
         */
        util = 0.0;
        if (previous != 0 && current >= previous && dt > 0.0)
        {
            util = (double)(current - previous) / (dt * 10000000.0) * 100.0;
            if (util < 0.0) util = 0.0;
            if (util > 100.0) util = 100.0;
        }
        engine->runningTime = current;
        engine->utilPct = util;
        engine->history.Push((float)util);
        if (util > gpu->utilPct)
            gpu->utilPct = util;
    }
    gpu->engineCount = (int)nodeCount;
}

static void CollectMemory(GpuSnapshot* gpu, D3DKMT_HANDLE hAdapter, ULONG segmentCount)
{
    D3DKMT_QUERYADAPTERINFO info;
    D3DKMT_SEGMENTGROUPSIZEINFO groups;
    ULONG segment;

    ZeroMemory(&groups, sizeof(groups));
    ZeroMemory(&info, sizeof(info));
    info.hAdapter = hAdapter;
    info.Type = KMTQAITYPE_GETSEGMENTGROUPSIZE;
    info.pPrivateDriverData = &groups;
    info.PrivateDriverDataSize = sizeof(groups);
    if (NT_SUCCESS(pQueryAdapterInfo(&info)))
    {
        gpu->dedicatedTotal = groups.LocalMemory;
        gpu->sharedTotal = groups.LegacyInfo.SharedSystemMemorySize;
        /*
         * Memory that belongs to no budget group is memory the hardware kept
         * for itself, which is exactly what "hardware reserved" names.
         */
        gpu->reserved = groups.NonBudgetMemory;
    }

    gpu->dedicatedUsed = 0;
    gpu->sharedUsed = 0;
    for (segment = 0; segment < segmentCount; segment++)
    {
        D3DKMT_QUERYSTATISTICS stats;

        ZeroMemory(&stats, sizeof(stats));
        stats.Type = D3DKMT_QUERYSTATISTICS_SEGMENT;
        stats.AdapterLuid = gpu->luid;
        stats.QuerySegment.SegmentId = segment;
        if (!NT_SUCCESS(pQueryStatistics(&stats)))
            continue;

        /*
         * An aperture segment is system memory the GPU can reach, which is
         * the shared group; anything else is memory dedicated to the
         * adapter.  Committed bytes is what the segment is holding, which is
         * the number Windows shows as usage.
         */
        if (stats.QueryResult.SegmentInformation.Aperture)
            gpu->sharedUsed += stats.QueryResult.SegmentInformation.BytesCommitted;
        else
            gpu->dedicatedUsed += stats.QueryResult.SegmentInformation.BytesCommitted;
    }

    gpu->hDedicated.Push((float)gpu->dedicatedUsed);
    gpu->hShared.Push((float)gpu->sharedUsed);
}

static void CollectThermal(GpuSnapshot* gpu, D3DKMT_HANDLE hAdapter)
{
    D3DKMT_QUERYADAPTERINFO info;
    D3DKMT_ADAPTER_PERFDATA perf;

    /*
     * Whether a driver reports thermals is a property of the driver, settled
     * the first time it is asked.  Re-asking a driver that has already said
     * no would put one refused request per sample on the debug log for the
     * lifetime of the process.
     */
    if (gpu->thermalAsked && !gpu->hasTemperature)
        return;

    ZeroMemory(&perf, sizeof(perf));
    ZeroMemory(&info, sizeof(info));
    info.hAdapter = hAdapter;
    info.Type = KMTQAITYPE_ADAPTERPERFDATA;
    info.pPrivateDriverData = &perf;
    info.PrivateDriverDataSize = sizeof(perf);

    /*
     * A driver that does not implement this reports no thermals at all, and
     * the row is left out.  Reporting zero degrees instead would be a
     * measurement, and a wrong one.
     */
    gpu->thermalAsked = TRUE;
    if (!NT_SUCCESS(pQueryAdapterInfo(&info)))
    {
        gpu->hasTemperature = FALSE;
        gpu->temperatureC = 0.0;
        return;
    }

    gpu->hasTemperature = TRUE;
    gpu->temperatureC = perf.Temperature / 10.0;   /* deci-Celsius */
}

static void CollectAdapter(GpuSnapshot* gpu, D3DKMT_HANDLE hAdapter, double dt)
{
    D3DKMT_QUERYSTATISTICS stats;
    D3DKMT_QUERYADAPTERINFO info;
    D3DKMT_ADAPTERADDRESS address;
    ULONG segmentCount = 0;
    ULONG nodeCount = 0;

    ZeroMemory(&stats, sizeof(stats));
    stats.Type = D3DKMT_QUERYSTATISTICS_ADAPTER;
    stats.AdapterLuid = gpu->luid;
    if (NT_SUCCESS(pQueryStatistics(&stats)))
    {
        segmentCount = stats.QueryResult.AdapterInformation.NbSegments;
        nodeCount = stats.QueryResult.AdapterInformation.NodeCount;
    }

    CollectEngines(gpu, hAdapter, nodeCount, dt);
    gpu->hUtil.Push((float)gpu->utilPct);
    CollectMemory(gpu, hAdapter, segmentCount);
    CollectThermal(gpu, hAdapter);

    /*
     * The adapter's own description, straight from the devnode that owns it.
     * This is the same string the PnP tree would give, and asking the adapter
     * for it needs no device enumeration and cannot pair the wrong name with
     * the wrong LUID.
     */
    if (!gpu->name[0])
    {
        D3DKMT_ADAPTERREGISTRYINFO registry;

        ZeroMemory(&registry, sizeof(registry));
        ZeroMemory(&info, sizeof(info));
        info.hAdapter = hAdapter;
        info.Type = KMTQAITYPE_ADAPTERREGISTRYINFO;
        info.pPrivateDriverData = &registry;
        info.PrivateDriverDataSize = sizeof(registry);
        if (NT_SUCCESS(pQueryAdapterInfo(&info)) && registry.AdapterString[0])
            StringCchCopyW(gpu->name, _countof(gpu->name), registry.AdapterString);
    }

    if (!gpu->location[0])
    {
        ZeroMemory(&address, sizeof(address));
        ZeroMemory(&info, sizeof(info));
        info.hAdapter = hAdapter;
        info.Type = KMTQAITYPE_ADAPTERADDRESS;
        info.pPrivateDriverData = &address;
        info.PrivateDriverDataSize = sizeof(address);
        if (NT_SUCCESS(pQueryAdapterInfo(&info)))
            FormatBusLocation(address, gpu->location, _countof(gpu->location));
        else
            StringCchCopyW(gpu->location, _countof(gpu->location), L"Unavailable");
    }

    gpu->present = TRUE;
}

/* ------------------------------------------------------------------ */
/*  Public entry points                                                */
/* ------------------------------------------------------------------ */

namespace Data
{

void GpuInit(void)
{
    LARGE_INTEGER frequency;

    s_available = LoadD3DKMT();
    QueryPerformanceFrequency(&frequency);
    s_qpcFrequency = (double)frequency.QuadPart;
    s_lastSampleQpc = 0;
    s_identityResolved = FALSE;
    g.gpuCount = 0;
    ZeroMemory(g.gpus, sizeof(g.gpus));
}

void GpuShutdown(void)
{
    g.gpuCount = 0;
}

void GpuTick(void)
{
    D3DKMT_ENUMADAPTERS2 enumeration;
    D3DKMT_ADAPTERINFO adapters[TM_MAX_GPUS];
    LARGE_INTEGER now;
    double dt;
    UINT i;
    int previousCount = g.gpuCount;

    if (!s_available)
        return;

    QueryPerformanceCounter(&now);
    dt = s_lastSampleQpc ? (now.QuadPart - s_lastSampleQpc) / s_qpcFrequency : 0.0;
    s_lastSampleQpc = now.QuadPart;

    ZeroMemory(adapters, sizeof(adapters));
    ZeroMemory(&enumeration, sizeof(enumeration));
    enumeration.NumAdapters = TM_MAX_GPUS;
    enumeration.pAdapters = adapters;
    if (!NT_SUCCESS(pEnumAdapters2(&enumeration)))
    {
        g.gpuCount = 0;
        return;
    }
    if (enumeration.NumAdapters > TM_MAX_GPUS)
        enumeration.NumAdapters = TM_MAX_GPUS;

    /*
     * Adapters keep their slot across ticks so their history rings and their
     * previous running-time readings stay with the adapter they belong to.
     * A list that changed shape drops the old history rather than carrying
     * one adapter's samples over onto another.
     */
    if ((int)enumeration.NumAdapters != previousCount)
    {
        ZeroMemory(g.gpus, sizeof(g.gpus));
        s_identityResolved = FALSE;
    }
    else
    {
        for (i = 0; i < enumeration.NumAdapters; i++)
        {
            if (!SameLuid(g.gpus[i].luid, adapters[i].AdapterLuid))
            {
                ZeroMemory(g.gpus, sizeof(g.gpus));
                s_identityResolved = FALSE;
                break;
            }
        }
    }

    g.gpuCount = (int)enumeration.NumAdapters;
    for (i = 0; i < enumeration.NumAdapters; i++)
    {
        GpuSnapshot* gpu = &g.gpus[i];

        gpu->luid = adapters[i].AdapterLuid;
        gpu->index = (int)i;
        CollectAdapter(gpu, adapters[i].hAdapter, dt);
        if (!gpu->name[0])
            StringCchPrintfW(gpu->name, _countof(gpu->name), L"GPU %u", i);
        if (!gpu->directX[0])
            ProbeDirectXVersion(gpu);
    }

    /*
     * Names and driver details come from the PnP tree, which does not change
     * between ticks; resolving them once keeps a per-second sample off the
     * device enumeration path entirely.
     */
    if (!s_identityResolved)
    {
        ResolveAdapterIdentity();
        s_identityResolved = TRUE;
        for (i = 0; i < enumeration.NumAdapters; i++)
        {
            if (!g.gpus[i].name[0])
                StringCchPrintfW(g.gpus[i].name, _countof(g.gpus[i].name), L"GPU %u", i);
        }
    }

    for (i = 0; i < enumeration.NumAdapters; i++)
    {
        D3DKMT_CLOSEADAPTER close;

        ZeroMemory(&close, sizeof(close));
        close.hAdapter = adapters[i].hAdapter;
        pCloseAdapter(&close);
    }
}

} /* namespace Data */
