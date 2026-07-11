/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Vulkan loader surface tests.
 */

#include <apitest.h>

#define WIN32_NO_STATUS
#define VK_NO_PROTOTYPES
#include <windef.h>
#include <winbase.h>
#include <winreg.h>
#include <string.h>
#include <vulkan/vulkan.h>

#define VULKAN_DRIVERS_KEY L"SOFTWARE\\Khronos\\Vulkan\\Drivers"

static BOOL
read_driver_name(WCHAR *Path, DWORD PathCount)
{
    HKEY Key;
    DWORD Type, Size;
    WCHAR Buffer[MAX_PATH];
    LONG Status;

    if (Path == NULL || PathCount == 0)
        return FALSE;

    Path[0] = UNICODE_NULL;

    Status = RegOpenKeyExW(HKEY_LOCAL_MACHINE, VULKAN_DRIVERS_KEY, 0, KEY_READ,
                           &Key);
    if (Status != ERROR_SUCCESS)
        return FALSE;

    Size = sizeof(Buffer);
    Status = RegQueryValueExW(Key, L"DriverName", NULL, &Type, (BYTE *)Buffer,
                              &Size);
    RegCloseKey(Key);
    if (Status != ERROR_SUCCESS ||
        (Type != REG_SZ && Type != REG_EXPAND_SZ) ||
        Buffer[0] == UNICODE_NULL)
    {
        return FALSE;
    }

    Buffer[(sizeof(Buffer) / sizeof(Buffer[0])) - 1] = UNICODE_NULL;
    if (Type == REG_EXPAND_SZ)
    {
        DWORD Written = ExpandEnvironmentStringsW(Buffer, Path, PathCount);
        return Written > 0 && Written <= PathCount && Path[0] != UNICODE_NULL;
    }

    lstrcpynW(Path, Buffer, PathCount);
    return Path[0] != UNICODE_NULL;
}

START_TEST(loader)
{
    HMODULE Module;
    PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr;
    PFN_vkCreateInstance vkCreateInstance;
    VkApplicationInfo AppInfo;
    VkInstanceCreateInfo CreateInfo;
    WCHAR DriverName[MAX_PATH];
    BOOL HaveDriverName;
    int i;

    HaveDriverName = read_driver_name(DriverName, sizeof(DriverName) / sizeof(DriverName[0]));

    Module = LoadLibraryW(L"vulkan-1.dll");
    ok(Module != NULL, "LoadLibraryW(vulkan-1.dll) failed, error %lu\n",
       GetLastError());
    if (Module == NULL)
        return;

    vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)
        GetProcAddress(Module, "vkGetInstanceProcAddr");
    vkCreateInstance = (PFN_vkCreateInstance)
        GetProcAddress(Module, "vkCreateInstance");

    ok(vkGetInstanceProcAddr != NULL,
       "vkGetInstanceProcAddr export missing\n");
    ok(vkCreateInstance != NULL, "vkCreateInstance export missing\n");
    if (vkGetInstanceProcAddr == NULL || vkCreateInstance == NULL)
    {
        FreeLibrary(Module);
        return;
    }

    ok(vkGetInstanceProcAddr(NULL, "vkGetInstanceProcAddr") != NULL,
       "vkGetInstanceProcAddr(NULL, self) returned NULL\n");
    ok(vkGetInstanceProcAddr(NULL, "vkCreateInstance") != NULL,
       "vkGetInstanceProcAddr(NULL, vkCreateInstance) returned NULL\n");

    RtlZeroMemory(&AppInfo, sizeof(AppInfo));
    AppInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    AppInfo.pApplicationName = "ReactOS vulkan1_apitest";
    AppInfo.applicationVersion = 1;
    AppInfo.pEngineName = "none";
    AppInfo.engineVersion = 1;
    AppInfo.apiVersion = VK_API_VERSION_1_0;

    RtlZeroMemory(&CreateInfo, sizeof(CreateInfo));
    CreateInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    CreateInfo.pApplicationInfo = &AppInfo;

    if (HaveDriverName)
    {
        skip("Vulkan DriverName is configured; skipping no-ICD negative path\n");
        FreeLibrary(Module);
        return;
    }

    for (i = 0; i < 4; i++)
    {
        VkInstance Instance = VK_NULL_HANDLE;
        VkResult Result;

        Result = vkCreateInstance(&CreateInfo, NULL, &Instance);
        ok(Result == VK_ERROR_INCOMPATIBLE_DRIVER,
           "vkCreateInstance pass %d returned %d, expected VK_ERROR_INCOMPATIBLE_DRIVER\n",
           i, Result);
        ok(Instance == VK_NULL_HANDLE,
           "vkCreateInstance pass %d wrote instance %p without an ICD\n",
           i, Instance);
    }

    FreeLibrary(Module);
}

START_TEST(swiftshader_icd)
{
    WCHAR DriverName[MAX_PATH];
    HMODULE Module;
    PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr;
    PFN_vkCreateInstance vkCreateInstance;
    PFN_vkDestroyInstance vkDestroyInstance;
    PFN_vkEnumeratePhysicalDevices vkEnumeratePhysicalDevices;
    PFN_vkGetPhysicalDeviceProperties vkGetPhysicalDeviceProperties;
    PFN_vkGetPhysicalDeviceQueueFamilyProperties vkGetPhysicalDeviceQueueFamilyProperties;
    PFN_vkCreateDevice vkCreateDevice;
    PFN_vkDestroyDevice vkDestroyDevice;
    PFN_vkGetDeviceQueue vkGetDeviceQueue;
    VkApplicationInfo AppInfo;
    VkInstanceCreateInfo InstanceInfo;
    VkInstance Instance = VK_NULL_HANDLE;
    VkPhysicalDevice PhysicalDevices[8];
    VkPhysicalDeviceProperties Properties;
    VkQueueFamilyProperties QueueFamilies[16];
    VkDeviceQueueCreateInfo QueueInfo;
    VkDeviceCreateInfo DeviceInfo;
    VkDevice Device = VK_NULL_HANDLE;
    VkQueue Queue = VK_NULL_HANDLE;
    float Priority = 1.0f;
    uint32_t Count, FamilyCount, QueueFamilyIndex;
    VkResult Result;

    if (!read_driver_name(DriverName, sizeof(DriverName) / sizeof(DriverName[0])))
    {
        skip("No Vulkan DriverName configured; skipping SwiftShader ICD smoke\n");
        return;
    }

    ok(GetFileAttributesW(DriverName) != INVALID_FILE_ATTRIBUTES,
       "Configured Vulkan DriverName path is not present\n");
    if (GetFileAttributesW(DriverName) == INVALID_FILE_ATTRIBUTES)
        return;

    Module = LoadLibraryW(L"vulkan-1.dll");
    ok(Module != NULL, "LoadLibraryW(vulkan-1.dll) failed, error %lu\n",
       GetLastError());
    if (Module == NULL)
        return;

    vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)
        GetProcAddress(Module, "vkGetInstanceProcAddr");
    vkCreateInstance = (PFN_vkCreateInstance)
        GetProcAddress(Module, "vkCreateInstance");
    ok(vkGetInstanceProcAddr != NULL, "vkGetInstanceProcAddr export missing\n");
    ok(vkCreateInstance != NULL, "vkCreateInstance export missing\n");
    if (vkGetInstanceProcAddr == NULL || vkCreateInstance == NULL)
        goto done;

    RtlZeroMemory(&AppInfo, sizeof(AppInfo));
    AppInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    AppInfo.pApplicationName = "ReactOS SwiftShader smoke";
    AppInfo.applicationVersion = 1;
    AppInfo.pEngineName = "none";
    AppInfo.engineVersion = 1;
    AppInfo.apiVersion = VK_API_VERSION_1_0;

    RtlZeroMemory(&InstanceInfo, sizeof(InstanceInfo));
    InstanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    InstanceInfo.pApplicationInfo = &AppInfo;

    Result = vkCreateInstance(&InstanceInfo, NULL, &Instance);
    ok(Result == VK_SUCCESS, "vkCreateInstance returned %d\n", Result);
    ok(Instance != VK_NULL_HANDLE, "vkCreateInstance returned NULL instance\n");
    if (Result != VK_SUCCESS || Instance == VK_NULL_HANDLE)
        goto done;

    vkDestroyInstance = (PFN_vkDestroyInstance)
        vkGetInstanceProcAddr(Instance, "vkDestroyInstance");
    vkEnumeratePhysicalDevices = (PFN_vkEnumeratePhysicalDevices)
        vkGetInstanceProcAddr(Instance, "vkEnumeratePhysicalDevices");
    vkGetPhysicalDeviceProperties = (PFN_vkGetPhysicalDeviceProperties)
        vkGetInstanceProcAddr(Instance, "vkGetPhysicalDeviceProperties");
    vkGetPhysicalDeviceQueueFamilyProperties =
        (PFN_vkGetPhysicalDeviceQueueFamilyProperties)
        vkGetInstanceProcAddr(Instance, "vkGetPhysicalDeviceQueueFamilyProperties");
    vkCreateDevice = (PFN_vkCreateDevice)
        vkGetInstanceProcAddr(Instance, "vkCreateDevice");
    vkDestroyDevice = (PFN_vkDestroyDevice)
        vkGetInstanceProcAddr(Instance, "vkDestroyDevice");
    vkGetDeviceQueue = (PFN_vkGetDeviceQueue)
        vkGetInstanceProcAddr(Instance, "vkGetDeviceQueue");

    ok(vkDestroyInstance != NULL, "vkDestroyInstance resolve failed\n");
    ok(vkEnumeratePhysicalDevices != NULL, "vkEnumeratePhysicalDevices resolve failed\n");
    ok(vkGetPhysicalDeviceProperties != NULL, "vkGetPhysicalDeviceProperties resolve failed\n");
    ok(vkGetPhysicalDeviceQueueFamilyProperties != NULL,
       "vkGetPhysicalDeviceQueueFamilyProperties resolve failed\n");
    ok(vkCreateDevice != NULL, "vkCreateDevice resolve failed\n");
    ok(vkDestroyDevice != NULL, "vkDestroyDevice resolve failed\n");
    ok(vkGetDeviceQueue != NULL, "vkGetDeviceQueue resolve failed\n");
    if (vkDestroyInstance == NULL ||
        vkEnumeratePhysicalDevices == NULL ||
        vkGetPhysicalDeviceProperties == NULL ||
        vkGetPhysicalDeviceQueueFamilyProperties == NULL ||
        vkCreateDevice == NULL ||
        vkDestroyDevice == NULL ||
        vkGetDeviceQueue == NULL)
    {
        goto done;
    }

    Count = 0;
    Result = vkEnumeratePhysicalDevices(Instance, &Count, NULL);
    ok(Result == VK_SUCCESS, "vkEnumeratePhysicalDevices count returned %d\n", Result);
    ok(Count > 0, "vkEnumeratePhysicalDevices returned no devices\n");
    if (Result != VK_SUCCESS || Count == 0)
        goto done;

    if (Count > sizeof(PhysicalDevices) / sizeof(PhysicalDevices[0]))
        Count = sizeof(PhysicalDevices) / sizeof(PhysicalDevices[0]);
    Result = vkEnumeratePhysicalDevices(Instance, &Count, PhysicalDevices);
    ok(Result == VK_SUCCESS, "vkEnumeratePhysicalDevices list returned %d\n", Result);
    if (Result != VK_SUCCESS || Count == 0)
        goto done;

    RtlZeroMemory(&Properties, sizeof(Properties));
    vkGetPhysicalDeviceProperties(PhysicalDevices[0], &Properties);
    trace("VULKAN: SwiftShader device = %s API %u.%u.%u\n",
          Properties.deviceName,
          VK_VERSION_MAJOR(Properties.apiVersion),
          VK_VERSION_MINOR(Properties.apiVersion),
          VK_VERSION_PATCH(Properties.apiVersion));
    ok(strstr(Properties.deviceName, "SwiftShader") != NULL,
       "physical device name is not SwiftShader: %s\n", Properties.deviceName);

    FamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(PhysicalDevices[0], &FamilyCount, NULL);
    ok(FamilyCount > 0, "no queue families reported\n");
    if (FamilyCount == 0)
        goto done;

    if (FamilyCount > sizeof(QueueFamilies) / sizeof(QueueFamilies[0]))
        FamilyCount = sizeof(QueueFamilies) / sizeof(QueueFamilies[0]);
    RtlZeroMemory(QueueFamilies, sizeof(QueueFamilies));
    vkGetPhysicalDeviceQueueFamilyProperties(PhysicalDevices[0], &FamilyCount,
                                             QueueFamilies);

    QueueFamilyIndex = FamilyCount;
    for (Count = 0; Count < FamilyCount; Count++)
    {
        if (QueueFamilies[Count].queueCount != 0)
        {
            QueueFamilyIndex = Count;
            break;
        }
    }

    ok(QueueFamilyIndex < FamilyCount, "no queue-capable family found\n");
    if (QueueFamilyIndex >= FamilyCount)
        goto done;

    RtlZeroMemory(&QueueInfo, sizeof(QueueInfo));
    QueueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    QueueInfo.queueFamilyIndex = QueueFamilyIndex;
    QueueInfo.queueCount = 1;
    QueueInfo.pQueuePriorities = &Priority;

    RtlZeroMemory(&DeviceInfo, sizeof(DeviceInfo));
    DeviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    DeviceInfo.queueCreateInfoCount = 1;
    DeviceInfo.pQueueCreateInfos = &QueueInfo;

    Result = vkCreateDevice(PhysicalDevices[0], &DeviceInfo, NULL, &Device);
    ok(Result == VK_SUCCESS, "vkCreateDevice returned %d\n", Result);
    ok(Device != VK_NULL_HANDLE, "vkCreateDevice returned NULL device\n");
    if (Result != VK_SUCCESS || Device == VK_NULL_HANDLE)
        goto done;

    vkGetDeviceQueue(Device, QueueFamilyIndex, 0, &Queue);
    ok(Queue != VK_NULL_HANDLE, "vkGetDeviceQueue returned NULL queue\n");

done:
    OutputDebugStringA("VULKAN1-TEST: teardown: destroy device...\n");
    if (Device != VK_NULL_HANDLE && vkDestroyDevice != NULL)
        vkDestroyDevice(Device, NULL);
    OutputDebugStringA("VULKAN1-TEST: teardown: destroy instance...\n");
    if (Instance != VK_NULL_HANDLE && vkDestroyInstance != NULL)
        vkDestroyInstance(Instance, NULL);
    OutputDebugStringA("VULKAN1-TEST: teardown: free library...\n");
    FreeLibrary(Module);
    OutputDebugStringA("VULKAN1-TEST: teardown: DONE\n");
}
