/*
 * ReactOS Vulkan loader.
 *
 * This is intentionally a thin direct-ICD loader. It discovers one ICD from
 * HKLM\SOFTWARE\Khronos\Vulkan\Drivers, loads it, and passes Vulkan entrypoint
 * resolution through vk_icdGetInstanceProcAddr.
 */

#define WIN32_NO_STATUS
#define VK_NO_PROTOTYPES
#define VK_USE_PLATFORM_WIN32_KHR

#include <windows.h>
#include <winreg.h>
#include <wchar.h>
#include <string.h>
#include <stdlib.h>

#include <vulkan/vulkan.h>
#include <vulkan/vk_icd.h>

/* NT10 / WDDM3.0 — for the lightweight D3DKMT hardware probe below. */
#ifndef DXGKDDI_INTERFACE_VERSION
#define DXGKDDI_INTERFACE_VERSION 0xF003
#endif
#include <d3dkmthk.h>
#include <reactos/rpi5vc4_umd.h>

#ifndef NT_SUCCESS
#define NT_SUCCESS(x) ((NTSTATUS)(x) >= 0)
#endif

ULONG __cdecl DbgPrint(_In_z_ _Printf_format_string_ PCSTR Format, ...);

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

#define VULKAN_DRIVERS_KEY L"SOFTWARE\\Khronos\\Vulkan\\Drivers"

static volatile LONG LoaderState;
static HMODULE IcdModule;
static PFN_vk_icdGetInstanceProcAddr IcdGetInstanceProcAddr;

/*
 * Last successfully created instance. Per the ICD interface,
 * vk_icdGetInstanceProcAddr(NULL, ...) resolves ONLY global functions
 * (vkCreateInstance / vkEnumerateInstance*); instance-level entrypoints
 * (physical device queries, surfaces, device creation) need a valid
 * instance. This thin direct-ICD loader assumes one live instance.
 */
static VkInstance CachedInstance;

static PFN_vkVoidFunction VKAPI_CALL vk_loader_get_local_proc(const char *Name);

static BOOL
IsAbsolutePathW(const WCHAR *Path)
{
    if (!Path || !Path[0])
        return FALSE;

    if (((Path[0] >= L'A' && Path[0] <= L'Z') ||
         (Path[0] >= L'a' && Path[0] <= L'z')) &&
        Path[1] == L':' &&
        (Path[2] == L'\\' || Path[2] == L'/'))
        return TRUE;

    return (Path[0] == L'\\' && Path[1] == L'\\') || Path[0] == L'/';
}

static BOOL
EndsWithJsonW(const WCHAR *Path)
{
    SIZE_T Length;

    if (!Path)
        return FALSE;

    Length = wcslen(Path);
    if (Length < 5)
        return FALSE;

    Path += Length - 5;
    return (Path[0] == L'.' &&
            (Path[1] == L'j' || Path[1] == L'J') &&
            (Path[2] == L's' || Path[2] == L'S') &&
            (Path[3] == L'o' || Path[3] == L'O') &&
            (Path[4] == L'n' || Path[4] == L'N'));
}

static void
NormalizeExpandedStringW(const WCHAR *Input, DWORD Type, WCHAR *Output, DWORD OutputCount)
{
    DWORD Written;

    if (!OutputCount)
        return;

    Output[0] = UNICODE_NULL;
    if (!Input || !Input[0])
        return;

    if (Type == REG_EXPAND_SZ)
    {
        Written = ExpandEnvironmentStringsW(Input, Output, OutputCount);
        if (Written > 0 && Written <= OutputCount)
            return;
    }

    wcsncpy(Output, Input, OutputCount - 1);
    Output[OutputCount - 1] = UNICODE_NULL;
}

static BOOL
ReadRegistryDriverValue(const WCHAR *Value, WCHAR *Path, DWORD PathCount)
{
    HKEY Key;
    DWORD Type, Size;
    WCHAR Buffer[MAX_PATH];
    LONG Status;

    Status = RegOpenKeyExW(HKEY_LOCAL_MACHINE, VULKAN_DRIVERS_KEY, 0, KEY_READ, &Key);
    if (Status != ERROR_SUCCESS)
        return FALSE;

    Size = sizeof(Buffer);
    Status = RegQueryValueExW(Key, Value, NULL, &Type, (BYTE *)Buffer, &Size);
    if (Status == ERROR_SUCCESS && (Type == REG_SZ || Type == REG_EXPAND_SZ) && Buffer[0])
    {
        Buffer[ARRAY_SIZE(Buffer) - 1] = UNICODE_NULL;
        NormalizeExpandedStringW(Buffer, Type, Path, PathCount);
        RegCloseKey(Key);
        return Path[0] != UNICODE_NULL;
    }

    RegCloseKey(Key);
    return FALSE;
}

static BOOL
ReadRegistryManifestPath(WCHAR *Path, DWORD PathCount)
{
    HKEY Key;
    DWORD Index = 0;
    LONG Status;

    Status = RegOpenKeyExW(HKEY_LOCAL_MACHINE, VULKAN_DRIVERS_KEY, 0, KEY_READ, &Key);
    if (Status != ERROR_SUCCESS)
        return FALSE;

    for (;;)
    {
        WCHAR Name[MAX_PATH];
        BYTE Data[MAX_PATH * sizeof(WCHAR)];
        DWORD NameCount = ARRAY_SIZE(Name);
        DWORD DataSize = sizeof(Data);
        DWORD Type = 0;

        Status = RegEnumValueW(Key, Index++, Name, &NameCount, NULL, &Type, Data, &DataSize);
        if (Status != ERROR_SUCCESS)
            break;

        if (!wcscmp(Name, L"DriverName"))
            continue;

        if (Type == REG_DWORD)
        {
            DWORD Enabled = 0;

            if (DataSize >= sizeof(DWORD))
                memcpy(&Enabled, Data, sizeof(DWORD));

            if (Enabled == 0 && Name[0])
            {
                NormalizeExpandedStringW(Name, REG_EXPAND_SZ, Path, PathCount);
                RegCloseKey(Key);
                return Path[0] != UNICODE_NULL;
            }
        }
        else if ((Type == REG_SZ || Type == REG_EXPAND_SZ) && DataSize >= sizeof(WCHAR))
        {
            WCHAR *Value = (WCHAR *)Data;

            Value[(DataSize / sizeof(WCHAR)) - 1] = UNICODE_NULL;
            if (Value[0])
            {
                NormalizeExpandedStringW(Value, Type, Path, PathCount);
                RegCloseKey(Key);
                return Path[0] != UNICODE_NULL;
            }
        }
    }

    RegCloseKey(Key);
    return FALSE;
}

static BOOL
CopyManifestDirectory(const WCHAR *ManifestPath, WCHAR *Directory, DWORD DirectoryCount)
{
    const WCHAR *LastSlash = NULL;
    const WCHAR *Cursor;
    SIZE_T Count;

    if (!ManifestPath || !DirectoryCount)
        return FALSE;

    for (Cursor = ManifestPath; *Cursor; ++Cursor)
    {
        if (*Cursor == L'\\' || *Cursor == L'/')
            LastSlash = Cursor;
    }

    if (!LastSlash)
        return FALSE;

    Count = LastSlash - ManifestPath + 1;
    if (Count >= DirectoryCount)
        return FALSE;

    memcpy(Directory, ManifestPath, Count * sizeof(WCHAR));
    Directory[Count] = UNICODE_NULL;
    return TRUE;
}

static BOOL
AppendRelativePathW(WCHAR *Output, DWORD OutputCount, const WCHAR *Directory, const WCHAR *Relative)
{
    SIZE_T DirectoryLength, RelativeLength;

    if (!OutputCount)
        return FALSE;

    Output[0] = UNICODE_NULL;
    if (!Directory || !Relative)
        return FALSE;

    DirectoryLength = wcslen(Directory);
    RelativeLength = wcslen(Relative);
    if (DirectoryLength + RelativeLength >= OutputCount)
        return FALSE;

    memcpy(Output, Directory, DirectoryLength * sizeof(WCHAR));
    memcpy(Output + DirectoryLength, Relative, (RelativeLength + 1) * sizeof(WCHAR));
    return TRUE;
}

static const char *
SkipJsonWhitespace(const char *Cursor)
{
    while (*Cursor == ' ' || *Cursor == '\t' || *Cursor == '\r' || *Cursor == '\n')
        ++Cursor;
    return Cursor;
}

static BOOL
ReadJsonStringValue(const char *Cursor, char *Value, SIZE_T ValueCount)
{
    SIZE_T Out = 0;

    Cursor = SkipJsonWhitespace(Cursor);
    if (*Cursor != ':')
        return FALSE;

    Cursor = SkipJsonWhitespace(Cursor + 1);
    if (*Cursor != '"')
        return FALSE;

    ++Cursor;
    while (*Cursor && *Cursor != '"')
    {
        char Ch = *Cursor++;

        if (Ch == '\\' && *Cursor)
        {
            Ch = *Cursor++;
            if (Ch == 'n')
                Ch = '\n';
            else if (Ch == 'r')
                Ch = '\r';
            else if (Ch == 't')
                Ch = '\t';
        }

        if (Out + 1 >= ValueCount)
            return FALSE;

        Value[Out++] = Ch;
    }

    if (*Cursor != '"')
        return FALSE;

    Value[Out] = '\0';
    return TRUE;
}

static BOOL
FindJsonLibraryPath(const char *Json, char *LibraryPath, SIZE_T LibraryPathCount)
{
    const char *Cursor = Json;

    while ((Cursor = strstr(Cursor, "\"library_path\"")) != NULL)
    {
        Cursor += sizeof("\"library_path\"") - 1;
        if (ReadJsonStringValue(Cursor, LibraryPath, LibraryPathCount))
            return TRUE;
    }

    return FALSE;
}

static BOOL
ReadManifestLibraryPath(const WCHAR *ManifestPath, WCHAR *LibraryPath, DWORD LibraryPathCount)
{
    HANDLE File;
    DWORD FileSize, BytesRead;
    char *Json;
    char LibraryA[MAX_PATH];
    WCHAR LibraryW[MAX_PATH];
    WCHAR Directory[MAX_PATH];
    BOOL Success = FALSE;

    File = CreateFileW(ManifestPath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (File == INVALID_HANDLE_VALUE)
        return FALSE;

    FileSize = GetFileSize(File, NULL);
    if (FileSize == INVALID_FILE_SIZE || FileSize == 0 || FileSize > 1024 * 1024)
    {
        CloseHandle(File);
        return FALSE;
    }

    Json = HeapAlloc(GetProcessHeap(), 0, FileSize + 1);
    if (!Json)
    {
        CloseHandle(File);
        return FALSE;
    }

    if (ReadFile(File, Json, FileSize, &BytesRead, NULL) && BytesRead == FileSize)
    {
        Json[FileSize] = '\0';

        if (FindJsonLibraryPath(Json, LibraryA, ARRAY_SIZE(LibraryA)) &&
            MultiByteToWideChar(CP_UTF8, 0, LibraryA, -1, LibraryW, ARRAY_SIZE(LibraryW)) > 0)
        {
            if (IsAbsolutePathW(LibraryW))
            {
                wcsncpy(LibraryPath, LibraryW, LibraryPathCount - 1);
                LibraryPath[LibraryPathCount - 1] = UNICODE_NULL;
                Success = TRUE;
            }
            else if (CopyManifestDirectory(ManifestPath, Directory, ARRAY_SIZE(Directory)))
            {
                Success = AppendRelativePathW(LibraryPath, LibraryPathCount, Directory, LibraryW);
            }
            else
            {
                wcsncpy(LibraryPath, LibraryW, LibraryPathCount - 1);
                LibraryPath[LibraryPathCount - 1] = UNICODE_NULL;
                Success = TRUE;
            }
        }
    }

    HeapFree(GetProcessHeap(), 0, Json);
    CloseHandle(File);
    return Success;
}

static HMODULE
LoadIcdFromPath(const WCHAR *Path)
{
    WCHAR LibraryPath[MAX_PATH];

    if (!Path || !Path[0])
        return NULL;

    if (EndsWithJsonW(Path) && ReadManifestLibraryPath(Path, LibraryPath, ARRAY_SIZE(LibraryPath)))
        return LoadLibraryW(LibraryPath);

    return LoadLibraryW(Path);
}

/*
 * Hardware-first ICD selection without instantiating an ICD during load.
 *
 * Real Vulkan loaders never create an instance while loading; doing so here
 * (to see whether v3dv exposes a device) spun up v3dv's whole device-init —
 * WSI, dxgi, dcomp, and the shim's global adapter state — during
 * InitializeLoader, which then double-initialised against the app's real
 * instance and crashed.  Instead, ask dxgkrnl directly whether the V3D adapter
 * is present via the same escape the v3dv winsys uses: open the display
 * adapter and issue the rpi5vc4 QUERY_INFO private escape.  A non-V3D adapter
 * (e.g. softgpu under QEMU) rejects it, so we fall back to the software ICD.
 * No Vulkan object, no re-entrancy, no shim state touched.
 */
static BOOL
PrimaryIcdHasV3dHardware(void)
{
    D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME OpenAdapter;
    D3DKMT_CLOSEADAPTER CloseAdapter;
    RPI5VC4_ESCAPE_INFO Info;
    BOOL HasHardware = FALSE;

    /* VC4KMT_FAKE: the v3dv winsys runs on a CPU-only stand-in device, so
     * treat the primary ICD as present (QEMU repro of UMD-side issues). */
    if (GetEnvironmentVariableW(L"VC4KMT_FAKE", NULL, 0) != 0)
        return TRUE;

    memset(&OpenAdapter, 0, sizeof(OpenAdapter));
    wcscpy(OpenAdapter.DeviceName, L"\\\\.\\DISPLAY1");
    if (!NT_SUCCESS(D3DKMTOpenAdapterFromGdiDisplayName(&OpenAdapter)))
        return FALSE;

    {
        D3DKMT_CREATEDEVICE CreateDevice;

        memset(&CreateDevice, 0, sizeof(CreateDevice));
        CreateDevice.hAdapter = OpenAdapter.hAdapter;
        if (NT_SUCCESS(D3DKMTCreateDevice(&CreateDevice)))
        {
            D3DKMT_ESCAPE Escape;
            D3DKMT_DESTROYDEVICE DestroyDevice;

            memset(&Info, 0, sizeof(Info));
            Info.Magic = RPI5VC4_ESCAPE_MAGIC;
            Info.Op = RPI5VC4_ESCAPE_OP_QUERY_INFO;

            memset(&Escape, 0, sizeof(Escape));
            Escape.hAdapter = OpenAdapter.hAdapter;
            Escape.hDevice = CreateDevice.hDevice;
            Escape.Type = D3DKMT_ESCAPE_DRIVERPRIVATE;
            Escape.pPrivateDriverData = &Info;
            Escape.PrivateDriverDataSize = sizeof(Info);

            if (NT_SUCCESS(D3DKMTEscape(&Escape)) &&
                Info.Magic == RPI5VC4_ESCAPE_MAGIC && Info.V3dReady)
            {
                HasHardware = TRUE;
            }

            memset(&DestroyDevice, 0, sizeof(DestroyDevice));
            DestroyDevice.hDevice = CreateDevice.hDevice;
            D3DKMTDestroyDevice(&DestroyDevice);
        }
    }

    memset(&CloseAdapter, 0, sizeof(CloseAdapter));
    CloseAdapter.hAdapter = OpenAdapter.hAdapter;
    D3DKMTCloseAdapter(&CloseAdapter);
    return HasHardware;
}

/*
 * Vendor-neutral ICD probe, matching how the standard (Windows) Vulkan loader
 * decides an ICD is usable: create a throwaway instance through the ICD's OWN
 * vk_icdGetInstanceProcAddr (never the loader's wrappers, so there is no
 * re-entrancy into InitializeLoader), enumerate its physical devices, and
 * classify them by VkPhysicalDeviceProperties::deviceType. Works for any
 * vendor -- v3dv on RPi5 silicon, any discrete/integrated GPU ICD, or a CPU
 * software device (lavapipe, SwiftShader) -- with no driver-specific escapes.
 */
static void
IcdProbeDevices(PFN_vk_icdGetInstanceProcAddr IcdGetProc,
                uint32_t *DeviceCount,
                BOOL *HasHardwareDevice)
{
    PFN_vkCreateInstance CreateInstance;
    PFN_vkEnumeratePhysicalDevices EnumeratePhysicalDevices;
    PFN_vkGetPhysicalDeviceProperties GetPhysicalDeviceProperties;
    PFN_vkDestroyInstance DestroyInstance;
    VkInstanceCreateInfo CreateInfo;
    VkInstance Instance = VK_NULL_HANDLE;
    VkPhysicalDevice Devices[8];
    uint32_t Count = ARRAY_SIZE(Devices);
    uint32_t Index;

    *DeviceCount = 0;
    *HasHardwareDevice = FALSE;

    if (!IcdGetProc)
        return;

    CreateInstance = (PFN_vkCreateInstance)IcdGetProc(NULL, "vkCreateInstance");
    if (!CreateInstance)
        return;

    /* pApplicationInfo left NULL -> apiVersion 1.0, which every ICD accepts
     * without loader/ICD interface negotiation. */
    memset(&CreateInfo, 0, sizeof(CreateInfo));
    CreateInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;

    if (CreateInstance(&CreateInfo, NULL, &Instance) != VK_SUCCESS ||
        Instance == VK_NULL_HANDLE)
    {
        return;
    }

    EnumeratePhysicalDevices = (PFN_vkEnumeratePhysicalDevices)
        IcdGetProc(Instance, "vkEnumeratePhysicalDevices");
    GetPhysicalDeviceProperties = (PFN_vkGetPhysicalDeviceProperties)
        IcdGetProc(Instance, "vkGetPhysicalDeviceProperties");

    /* VK_SUCCESS or VK_INCOMPLETE (positive) both mean devices were written. */
    if (EnumeratePhysicalDevices &&
        EnumeratePhysicalDevices(Instance, &Count, Devices) >= VK_SUCCESS)
    {
        *DeviceCount = Count;

        if (GetPhysicalDeviceProperties)
        {
            for (Index = 0; Index < Count; Index++)
            {
                VkPhysicalDeviceProperties Properties;

                GetPhysicalDeviceProperties(Devices[Index], &Properties);
                if (Properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ||
                    Properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ||
                    Properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU)
                {
                    *HasHardwareDevice = TRUE;
                    break;
                }
            }
        }
    }

    DestroyInstance = (PFN_vkDestroyInstance)IcdGetProc(Instance, "vkDestroyInstance");
    if (DestroyInstance)
        DestroyInstance(Instance, NULL);
}

/* Negotiate the loader/ICD interface version.  Without this an ICD must
 * assume a pre-v5 loader and reject any vkCreateInstance apiVersion above
 * 1.0 with VK_ERROR_INCOMPATIBLE_DRIVER (that is exactly what SwiftShader
 * does), which breaks Vulkan 1.1+ consumers such as zink and DXVK. */
static void
NegotiateIcdInterfaceVersion(HMODULE Module)
{
    typedef VkResult (VKAPI_PTR *PFN_vk_icdNegotiateLoaderICDInterfaceVersion)(uint32_t *pVersion);
    PFN_vk_icdNegotiateLoaderICDInterfaceVersion Negotiate =
        (PFN_vk_icdNegotiateLoaderICDInterfaceVersion)
            GetProcAddress(Module, "vk_icdNegotiateLoaderICDInterfaceVersion");

    if (Negotiate)
    {
        uint32_t Version = 5;
        if (Negotiate(&Version) != VK_SUCCESS)
            DbgPrint("VULKAN-1: ICD interface negotiation failed\n");
    }
}

static PFN_vk_icdGetInstanceProcAddr
GetIcdEntrypoint(HMODULE Module)
{
    PFN_vk_icdGetInstanceProcAddr Proc =
        (PFN_vk_icdGetInstanceProcAddr)GetProcAddress(Module, "vk_icdGetInstanceProcAddr");

    if (!Proc)
        Proc = (PFN_vk_icdGetInstanceProcAddr)GetProcAddress(Module, "vkGetInstanceProcAddr");

    return Proc;
}

static void
InitializeLoader(void)
{
    HKEY Key;
    HMODULE SoftwareModule = NULL;
    PFN_vk_icdGetInstanceProcAddr SoftwareProc = NULL;

    /* Windows-loader (Win11) semantics, vendor-neutral: walk EVERY ICD value
     * registered under the Vulkan Drivers key and probe each one with a
     * minimal instance.  The first ICD exposing a hardware (non-CPU) physical
     * device is selected on the spot -- any vendor.  An ICD exposing only CPU
     * devices (lavapipe, SwiftShader) is retained as the software fallback and
     * used when no hardware ICD exists on this machine.  Value names carry no
     * meaning; DriverName/DriverNameFallback remain as conventional entries. */
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, VULKAN_DRIVERS_KEY, 0, KEY_READ, &Key) == ERROR_SUCCESS)
    {
        DWORD Index;

        for (Index = 0; IcdModule == NULL; Index++)
        {
            WCHAR Name[128], Buffer[MAX_PATH], Path[MAX_PATH];
            DWORD NameLen = ARRAY_SIZE(Name);
            DWORD Type = REG_NONE;
            DWORD Size = sizeof(Buffer) - sizeof(UNICODE_NULL);
            HMODULE Module;
            PFN_vk_icdGetInstanceProcAddr Proc;
            uint32_t DeviceCount = 0;
            BOOL HasHardwareDevice = FALSE;

            if (RegEnumValueW(Key, Index, Name, &NameLen, NULL, &Type,
                              (BYTE *)Buffer, &Size) != ERROR_SUCCESS)
            {
                break;
            }

            if ((Type != REG_SZ && Type != REG_EXPAND_SZ) || Size < sizeof(WCHAR))
                continue;

            Buffer[Size / sizeof(WCHAR)] = UNICODE_NULL;
            if (!Buffer[0])
                continue;

            Path[0] = UNICODE_NULL;
            NormalizeExpandedStringW(Buffer, Type, Path, ARRAY_SIZE(Path));
            if (!Path[0])
                continue;

            Module = LoadIcdFromPath(Path);
            if (!Module)
            {
                DbgPrint("VULKAN-1: ICD '%ws' failed to load (error %lu)\n",
                         Path, GetLastError());
                continue;
            }

            Proc = GetIcdEntrypoint(Module);
            if (!Proc)
            {
                DbgPrint("VULKAN-1: ICD '%ws' exports no vk_icdGetInstanceProcAddr\n", Path);
                FreeLibrary(Module);
                continue;
            }

            NegotiateIcdInterfaceVersion(Module);
            IcdProbeDevices(Proc, &DeviceCount, &HasHardwareDevice);
            DbgPrint("VULKAN-1: ICD '%ws' presents %u device(s)%s\n",
                     Path, DeviceCount, HasHardwareDevice ? ", hardware" : "");

            if (HasHardwareDevice)
            {
                /* Any vendor's hardware device wins immediately. */
                IcdModule = Module;
                IcdGetInstanceProcAddr = Proc;
                DbgPrint("VULKAN-1: hardware ICD selected: '%ws'\n", Path);
            }
            else if (DeviceCount != 0 && SoftwareModule == NULL)
            {
                SoftwareModule = Module;
                SoftwareProc = Proc;
            }
            else
            {
                FreeLibrary(Module);
            }
        }

        RegCloseKey(Key);
    }

    if (IcdModule == NULL && SoftwareModule != NULL)
    {
        IcdModule = SoftwareModule;
        IcdGetInstanceProcAddr = SoftwareProc;
        SoftwareModule = NULL;
        DbgPrint("VULKAN-1: software (CPU) ICD selected\n");
    }

    if (SoftwareModule != NULL)
        FreeLibrary(SoftwareModule);

    if (IcdModule == NULL)
    {
        /* Nothing usable under the Drivers key: fall back to the Win11-style
         * per-adapter manifest registration. */
        WCHAR Path[MAX_PATH];
        HMODULE Module;

        Path[0] = UNICODE_NULL;
        ReadRegistryManifestPath(Path, ARRAY_SIZE(Path));
        Module = LoadIcdFromPath(Path);
        if (!Module)
        {
            DbgPrint("VULKAN-1: no usable ICD (last path '%ws', error %lu)\n",
                     Path, GetLastError());
            return;
        }

        IcdGetInstanceProcAddr = GetIcdEntrypoint(Module);
        if (!IcdGetInstanceProcAddr)
        {
            DbgPrint("VULKAN-1: ICD '%ws' exports no vk_icdGetInstanceProcAddr\n", Path);
            FreeLibrary(Module);
            return;
        }

        NegotiateIcdInterfaceVersion(Module);
        IcdModule = Module;
    }
}

static BOOL
EnsureLoaderInitialized(void)
{
    LONG State;

    State = InterlockedCompareExchange(&LoaderState, 1, 0);
    if (State == 0)
    {
        InitializeLoader();
        InterlockedExchange(&LoaderState, 2);
    }
    else
    {
        while (LoaderState == 1)
            Sleep(0);
    }

    return IcdModule != NULL && IcdGetInstanceProcAddr != NULL;
}

static PFN_vkVoidFunction
GetIcdProcAddr(VkInstance Instance, const char *Name)
{
    PFN_vkVoidFunction Proc;

    if (!Name || !EnsureLoaderInitialized())
        return NULL;

    /* Instance-level resolution against the live instance: NULL here only
     * resolves globals on a conformant ICD. */
    if (Instance == NULL)
        Instance = CachedInstance;

    Proc = IcdGetInstanceProcAddr(Instance, Name);
    if (!Proc)
        Proc = (PFN_vkVoidFunction)GetProcAddress(IcdModule, Name);

    return Proc;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vkGetInstanceProcAddr(VkInstance instance, const char *pName)
{
    PFN_vkVoidFunction Proc;

    Proc = vk_loader_get_local_proc(pName);
    if (Proc)
        return Proc;

    return GetIcdProcAddr(instance, pName);
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vk_icdGetInstanceProcAddr(VkInstance instance, const char *pName)
{
    return vkGetInstanceProcAddr(instance, pName);
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vkGetDeviceProcAddr(VkDevice device, const char *pName)
{
    PFN_vkGetDeviceProcAddr Proc;

    if (!pName || !EnsureLoaderInitialized())
        return NULL;

    Proc = (PFN_vkGetDeviceProcAddr)GetIcdProcAddr(NULL, "vkGetDeviceProcAddr");
    if (Proc)
        return Proc(device, pName);

    return GetIcdProcAddr(NULL, pName);
}

VKAPI_ATTR VkResult VKAPI_CALL
vkCreateInstance(const VkInstanceCreateInfo *pCreateInfo,
                 const VkAllocationCallbacks *pAllocator,
                 VkInstance *pInstance)
{
    PFN_vkCreateInstance Proc = (PFN_vkCreateInstance)GetIcdProcAddr(NULL, "vkCreateInstance");
    VkResult Result;

    if (!Proc)
    {
        DbgPrint("VULKAN-1: vkCreateInstance unavailable (no ICD loaded)\n");
        return VK_ERROR_INCOMPATIBLE_DRIVER;
    }

    Result = Proc(pCreateInfo, pAllocator, pInstance);
    if (Result != VK_SUCCESS)
        DbgPrint("VULKAN-1: ICD vkCreateInstance returned %d\n", (int)Result);

    if (Result == VK_SUCCESS && pInstance != NULL)
        CachedInstance = *pInstance;

    return Result;
}

VKAPI_ATTR void VKAPI_CALL
vkDestroyInstance(VkInstance instance, const VkAllocationCallbacks *pAllocator)
{
    PFN_vkDestroyInstance Proc = (PFN_vkDestroyInstance)GetIcdProcAddr(instance, "vkDestroyInstance");

    if (Proc)
        Proc(instance, pAllocator);

    if (instance != NULL && instance == CachedInstance)
        CachedInstance = NULL;
}

VKAPI_ATTR VkResult VKAPI_CALL
vkEnumerateInstanceExtensionProperties(const char *pLayerName,
                                       uint32_t *pPropertyCount,
                                       VkExtensionProperties *pProperties)
{
    PFN_vkEnumerateInstanceExtensionProperties Proc =
        (PFN_vkEnumerateInstanceExtensionProperties)GetIcdProcAddr(NULL, "vkEnumerateInstanceExtensionProperties");

    if (!Proc)
        return VK_ERROR_INCOMPATIBLE_DRIVER;

    return Proc(pLayerName, pPropertyCount, pProperties);
}

VKAPI_ATTR VkResult VKAPI_CALL
vkEnumerateInstanceLayerProperties(uint32_t *pPropertyCount,
                                   VkLayerProperties *pProperties)
{
    /* Instance layers are a loader-level concept, never provided by an ICD —
     * forwarding to the ICD returns NULL and wrongly fails the call, which
     * makes consumers (zink) reject the driver.  This thin loader exposes no
     * layers: report an empty list. */
    (void)pProperties;
    if (pPropertyCount)
        *pPropertyCount = 0;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
vkEnumerateInstanceVersion(uint32_t *pApiVersion)
{
    PFN_vkEnumerateInstanceVersion Proc =
        (PFN_vkEnumerateInstanceVersion)GetIcdProcAddr(NULL, "vkEnumerateInstanceVersion");

    if (Proc)
        return Proc(pApiVersion);

    if (!EnsureLoaderInitialized())
        return VK_ERROR_INCOMPATIBLE_DRIVER;

    if (pApiVersion)
        *pApiVersion = VK_API_VERSION_1_0;

    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
vkEnumeratePhysicalDevices(VkInstance instance,
                           uint32_t *pPhysicalDeviceCount,
                           VkPhysicalDevice *pPhysicalDevices)
{
    PFN_vkEnumeratePhysicalDevices Proc =
        (PFN_vkEnumeratePhysicalDevices)GetIcdProcAddr(instance, "vkEnumeratePhysicalDevices");
    VkResult Result;

    if (!Proc)
    {
        DbgPrint("VULKAN-1: EnumeratePhysicalDevices: no proc (inst=%p cached=%p)\n", instance, CachedInstance);
        return VK_ERROR_INCOMPATIBLE_DRIVER;
    }

    Result = Proc(instance, pPhysicalDeviceCount, pPhysicalDevices);
    DbgPrint("VULKAN-1: EnumeratePhysicalDevices inst=%p res=%d count=%u\n",
             instance, (int)Result,
             pPhysicalDeviceCount ? *pPhysicalDeviceCount : 0);
    return Result;
}

VKAPI_ATTR void VKAPI_CALL
vkGetPhysicalDeviceFeatures(VkPhysicalDevice physicalDevice,
                            VkPhysicalDeviceFeatures *pFeatures)
{
    PFN_vkGetPhysicalDeviceFeatures Proc =
        (PFN_vkGetPhysicalDeviceFeatures)GetIcdProcAddr(NULL, "vkGetPhysicalDeviceFeatures");

    if (Proc)
        Proc(physicalDevice, pFeatures);
}

VKAPI_ATTR void VKAPI_CALL
vkGetPhysicalDeviceFormatProperties(VkPhysicalDevice physicalDevice,
                                    VkFormat format,
                                    VkFormatProperties *pFormatProperties)
{
    PFN_vkGetPhysicalDeviceFormatProperties Proc =
        (PFN_vkGetPhysicalDeviceFormatProperties)GetIcdProcAddr(NULL, "vkGetPhysicalDeviceFormatProperties");

    if (Proc)
        Proc(physicalDevice, format, pFormatProperties);
}

VKAPI_ATTR VkResult VKAPI_CALL
vkGetPhysicalDeviceImageFormatProperties(VkPhysicalDevice physicalDevice,
                                         VkFormat format,
                                         VkImageType type,
                                         VkImageTiling tiling,
                                         VkImageUsageFlags usage,
                                         VkImageCreateFlags flags,
                                         VkImageFormatProperties *pImageFormatProperties)
{
    PFN_vkGetPhysicalDeviceImageFormatProperties Proc =
        (PFN_vkGetPhysicalDeviceImageFormatProperties)GetIcdProcAddr(NULL, "vkGetPhysicalDeviceImageFormatProperties");

    if (!Proc)
        return VK_ERROR_INCOMPATIBLE_DRIVER;

    return Proc(physicalDevice, format, type, tiling, usage, flags, pImageFormatProperties);
}

VKAPI_ATTR void VKAPI_CALL
vkGetPhysicalDeviceProperties(VkPhysicalDevice physicalDevice,
                              VkPhysicalDeviceProperties *pProperties)
{
    PFN_vkGetPhysicalDeviceProperties Proc =
        (PFN_vkGetPhysicalDeviceProperties)GetIcdProcAddr(NULL, "vkGetPhysicalDeviceProperties");

    if (Proc)
        Proc(physicalDevice, pProperties);
}

VKAPI_ATTR void VKAPI_CALL
vkGetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice physicalDevice,
                                         uint32_t *pQueueFamilyPropertyCount,
                                         VkQueueFamilyProperties *pQueueFamilyProperties)
{
    PFN_vkGetPhysicalDeviceQueueFamilyProperties Proc =
        (PFN_vkGetPhysicalDeviceQueueFamilyProperties)GetIcdProcAddr(NULL, "vkGetPhysicalDeviceQueueFamilyProperties");

    if (Proc)
        Proc(physicalDevice, pQueueFamilyPropertyCount, pQueueFamilyProperties);
}

VKAPI_ATTR void VKAPI_CALL
vkGetPhysicalDeviceMemoryProperties(VkPhysicalDevice physicalDevice,
                                    VkPhysicalDeviceMemoryProperties *pMemoryProperties)
{
    PFN_vkGetPhysicalDeviceMemoryProperties Proc =
        (PFN_vkGetPhysicalDeviceMemoryProperties)GetIcdProcAddr(NULL, "vkGetPhysicalDeviceMemoryProperties");

    if (Proc)
        Proc(physicalDevice, pMemoryProperties);
}

VKAPI_ATTR VkResult VKAPI_CALL
vkCreateDevice(VkPhysicalDevice physicalDevice,
               const VkDeviceCreateInfo *pCreateInfo,
               const VkAllocationCallbacks *pAllocator,
               VkDevice *pDevice)
{
    PFN_vkCreateDevice Proc = (PFN_vkCreateDevice)GetIcdProcAddr(NULL, "vkCreateDevice");

    if (!Proc)
        return VK_ERROR_INCOMPATIBLE_DRIVER;

    return Proc(physicalDevice, pCreateInfo, pAllocator, pDevice);
}

VKAPI_ATTR void VKAPI_CALL
vkDestroyDevice(VkDevice device, const VkAllocationCallbacks *pAllocator)
{
    PFN_vkDestroyDevice Proc;

    Proc = (PFN_vkDestroyDevice)vkGetDeviceProcAddr(device, "vkDestroyDevice");
    if (Proc)
        Proc(device, pAllocator);
}

VKAPI_ATTR VkResult VKAPI_CALL
vkEnumerateDeviceExtensionProperties(VkPhysicalDevice physicalDevice,
                                     const char *pLayerName,
                                     uint32_t *pPropertyCount,
                                     VkExtensionProperties *pProperties)
{
    PFN_vkEnumerateDeviceExtensionProperties Proc =
        (PFN_vkEnumerateDeviceExtensionProperties)GetIcdProcAddr(NULL, "vkEnumerateDeviceExtensionProperties");

    if (!Proc)
        return VK_ERROR_INCOMPATIBLE_DRIVER;

    return Proc(physicalDevice, pLayerName, pPropertyCount, pProperties);
}

VKAPI_ATTR VkResult VKAPI_CALL
vkEnumerateDeviceLayerProperties(VkPhysicalDevice physicalDevice,
                                 uint32_t *pPropertyCount,
                                 VkLayerProperties *pProperties)
{
    PFN_vkEnumerateDeviceLayerProperties Proc =
        (PFN_vkEnumerateDeviceLayerProperties)GetIcdProcAddr(NULL, "vkEnumerateDeviceLayerProperties");

    if (!Proc)
        return VK_ERROR_INCOMPATIBLE_DRIVER;

    return Proc(physicalDevice, pPropertyCount, pProperties);
}

VKAPI_ATTR void VKAPI_CALL
vkGetDeviceQueue(VkDevice device,
                 uint32_t queueFamilyIndex,
                 uint32_t queueIndex,
                 VkQueue *pQueue)
{
    PFN_vkGetDeviceQueue Proc;

    Proc = (PFN_vkGetDeviceQueue)vkGetDeviceProcAddr(device, "vkGetDeviceQueue");
    if (Proc)
        Proc(device, queueFamilyIndex, queueIndex, pQueue);
}

VKAPI_ATTR VkResult VKAPI_CALL
vkQueueWaitIdle(VkQueue queue)
{
    PFN_vkQueueWaitIdle Proc;

    Proc = (PFN_vkQueueWaitIdle)GetIcdProcAddr(NULL, "vkQueueWaitIdle");
    if (!Proc)
        return VK_ERROR_INCOMPATIBLE_DRIVER;

    return Proc(queue);
}

VKAPI_ATTR VkResult VKAPI_CALL
vkDeviceWaitIdle(VkDevice device)
{
    PFN_vkDeviceWaitIdle Proc;

    Proc = (PFN_vkDeviceWaitIdle)vkGetDeviceProcAddr(device, "vkDeviceWaitIdle");
    if (!Proc)
        return VK_ERROR_INCOMPATIBLE_DRIVER;

    return Proc(device);
}

VKAPI_ATTR VkResult VKAPI_CALL
vkCreateWin32SurfaceKHR(VkInstance instance,
                        const VkWin32SurfaceCreateInfoKHR *pCreateInfo,
                        const VkAllocationCallbacks *pAllocator,
                        VkSurfaceKHR *pSurface)
{
    PFN_vkCreateWin32SurfaceKHR Proc =
        (PFN_vkCreateWin32SurfaceKHR)GetIcdProcAddr(instance, "vkCreateWin32SurfaceKHR");

    if (!Proc)
        return VK_ERROR_EXTENSION_NOT_PRESENT;

    return Proc(instance, pCreateInfo, pAllocator, pSurface);
}

VKAPI_ATTR VkBool32 VKAPI_CALL
vkGetPhysicalDeviceWin32PresentationSupportKHR(VkPhysicalDevice physicalDevice,
                                               uint32_t queueFamilyIndex)
{
    PFN_vkGetPhysicalDeviceWin32PresentationSupportKHR Proc =
        (PFN_vkGetPhysicalDeviceWin32PresentationSupportKHR)GetIcdProcAddr(NULL, "vkGetPhysicalDeviceWin32PresentationSupportKHR");

    if (!Proc)
        return VK_FALSE;

    return Proc(physicalDevice, queueFamilyIndex);
}

VKAPI_ATTR void VKAPI_CALL
vkDestroySurfaceKHR(VkInstance instance,
                    VkSurfaceKHR surface,
                    const VkAllocationCallbacks *pAllocator)
{
    PFN_vkDestroySurfaceKHR Proc =
        (PFN_vkDestroySurfaceKHR)GetIcdProcAddr(instance, "vkDestroySurfaceKHR");

    if (Proc)
        Proc(instance, surface, pAllocator);
}

VKAPI_ATTR VkResult VKAPI_CALL
vkGetPhysicalDeviceSurfaceSupportKHR(VkPhysicalDevice physicalDevice,
                                     uint32_t queueFamilyIndex,
                                     VkSurfaceKHR surface,
                                     VkBool32 *pSupported)
{
    PFN_vkGetPhysicalDeviceSurfaceSupportKHR Proc =
        (PFN_vkGetPhysicalDeviceSurfaceSupportKHR)GetIcdProcAddr(NULL, "vkGetPhysicalDeviceSurfaceSupportKHR");

    if (!Proc)
        return VK_ERROR_EXTENSION_NOT_PRESENT;

    return Proc(physicalDevice, queueFamilyIndex, surface, pSupported);
}

VKAPI_ATTR VkResult VKAPI_CALL
vkGetPhysicalDeviceSurfaceCapabilitiesKHR(VkPhysicalDevice physicalDevice,
                                          VkSurfaceKHR surface,
                                          VkSurfaceCapabilitiesKHR *pSurfaceCapabilities)
{
    PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR Proc =
        (PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR)GetIcdProcAddr(NULL, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");

    if (!Proc)
        return VK_ERROR_EXTENSION_NOT_PRESENT;

    return Proc(physicalDevice, surface, pSurfaceCapabilities);
}

VKAPI_ATTR VkResult VKAPI_CALL
vkGetPhysicalDeviceSurfaceFormatsKHR(VkPhysicalDevice physicalDevice,
                                     VkSurfaceKHR surface,
                                     uint32_t *pSurfaceFormatCount,
                                     VkSurfaceFormatKHR *pSurfaceFormats)
{
    PFN_vkGetPhysicalDeviceSurfaceFormatsKHR Proc =
        (PFN_vkGetPhysicalDeviceSurfaceFormatsKHR)GetIcdProcAddr(NULL, "vkGetPhysicalDeviceSurfaceFormatsKHR");

    if (!Proc)
        return VK_ERROR_EXTENSION_NOT_PRESENT;

    return Proc(physicalDevice, surface, pSurfaceFormatCount, pSurfaceFormats);
}

VKAPI_ATTR VkResult VKAPI_CALL
vkGetPhysicalDeviceSurfacePresentModesKHR(VkPhysicalDevice physicalDevice,
                                          VkSurfaceKHR surface,
                                          uint32_t *pPresentModeCount,
                                          VkPresentModeKHR *pPresentModes)
{
    PFN_vkGetPhysicalDeviceSurfacePresentModesKHR Proc =
        (PFN_vkGetPhysicalDeviceSurfacePresentModesKHR)GetIcdProcAddr(NULL, "vkGetPhysicalDeviceSurfacePresentModesKHR");

    if (!Proc)
        return VK_ERROR_EXTENSION_NOT_PRESENT;

    return Proc(physicalDevice, surface, pPresentModeCount, pPresentModes);
}

static PFN_vkVoidFunction VKAPI_CALL
vk_loader_get_local_proc(const char *Name)
{
    if (!Name)
        return NULL;

#define VK_LOCAL_PROC(name) \
    if (!strcmp(Name, #name)) return (PFN_vkVoidFunction)name

    VK_LOCAL_PROC(vkCreateDevice);
    VK_LOCAL_PROC(vkCreateInstance);
    VK_LOCAL_PROC(vkCreateWin32SurfaceKHR);
    VK_LOCAL_PROC(vkDestroyDevice);
    VK_LOCAL_PROC(vkDestroyInstance);
    VK_LOCAL_PROC(vkDestroySurfaceKHR);
    VK_LOCAL_PROC(vkDeviceWaitIdle);
    VK_LOCAL_PROC(vkEnumerateDeviceExtensionProperties);
    VK_LOCAL_PROC(vkEnumerateDeviceLayerProperties);
    VK_LOCAL_PROC(vkEnumerateInstanceExtensionProperties);
    VK_LOCAL_PROC(vkEnumerateInstanceLayerProperties);
    VK_LOCAL_PROC(vkEnumerateInstanceVersion);
    VK_LOCAL_PROC(vkEnumeratePhysicalDevices);
    VK_LOCAL_PROC(vkGetDeviceProcAddr);
    VK_LOCAL_PROC(vkGetDeviceQueue);
    VK_LOCAL_PROC(vkGetInstanceProcAddr);
    VK_LOCAL_PROC(vkGetPhysicalDeviceFeatures);
    VK_LOCAL_PROC(vkGetPhysicalDeviceFormatProperties);
    VK_LOCAL_PROC(vkGetPhysicalDeviceImageFormatProperties);
    VK_LOCAL_PROC(vkGetPhysicalDeviceMemoryProperties);
    VK_LOCAL_PROC(vkGetPhysicalDeviceProperties);
    VK_LOCAL_PROC(vkGetPhysicalDeviceQueueFamilyProperties);
    VK_LOCAL_PROC(vkGetPhysicalDeviceSurfaceCapabilitiesKHR);
    VK_LOCAL_PROC(vkGetPhysicalDeviceSurfaceFormatsKHR);
    VK_LOCAL_PROC(vkGetPhysicalDeviceSurfacePresentModesKHR);
    VK_LOCAL_PROC(vkGetPhysicalDeviceSurfaceSupportKHR);
    VK_LOCAL_PROC(vkGetPhysicalDeviceWin32PresentationSupportKHR);
    VK_LOCAL_PROC(vkQueueWaitIdle);
    VK_LOCAL_PROC(vk_icdGetInstanceProcAddr);

#undef VK_LOCAL_PROC

    return NULL;
}

BOOL WINAPI
DllMain(HINSTANCE Instance, DWORD Reason, LPVOID Reserved)
{
    UNREFERENCED_PARAMETER(Instance);
    UNREFERENCED_PARAMETER(Reserved);

    /* No FreeLibrary on process detach: calling the loader under the loader
     * lock is illegal, and the process is going away anyway. */
    if (Reason == DLL_PROCESS_ATTACH)
    {
        WCHAR Exe[260];

        Exe[0] = UNICODE_NULL;
        GetModuleFileNameW(NULL, Exe, 260);
        DbgPrint("VULKAN-1: loaded into %ws\n", Exe);
        DisableThreadLibraryCalls(Instance);
    }

    return TRUE;
}
