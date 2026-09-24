/*
 * PROJECT:     ReactOS Vulkan loader
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Driver discovery, global entry points and dispatch setup
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "vulkan_private.h"
#include <stdlib.h>
#include <string.h>
#include <strsafe.h>

#define VK_LOADER_ICD_INTERFACE_VERSION 5
#define VK_LOADER_MIN_ICD_INTERFACE_VERSION 3
#define VK_MANIFEST_MAX_SIZE 0x10000

static CRITICAL_SECTION VkLoaderLock;
static BOOL VkIcdSearched;
static struct vk_icd VkIcd;
static struct vk_icd *VkActiveIcd;

static void vk_set_dispatch(void *Handle, struct vk_table *Table)
{
    ((VK_LOADER_DATA *)Handle)->loaderData = Table;
}

static struct vk_table *vk_alloc_table(ULONG Count)
{
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                     FIELD_OFFSET(struct vk_table, Functions[Count]));
}

static int vk_export_compare(const void *Name, const void *Export)
{
    return strcmp(Name, ((const struct vk_export *)Export)->Name);
}

static PFN_vkVoidFunction vk_find_export(const char *Name)
{
    const struct vk_export *Export;

    Export = bsearch(Name, vk_exports, vk_export_count, sizeof(*vk_exports), vk_export_compare);
    return Export ? Export->Function : NULL;
}

static BOOL vk_manifest_library(const WCHAR *ManifestPath, WCHAR *Library, DWORD LibraryChars)
{
    HANDLE File;
    char *Text, *Key, *Value, *Start = NULL, *Out;
    DWORD Size, Read;
    WCHAR Path[MAX_PATH];
    const WCHAR *Slash, *Other;
    BOOL Result = FALSE;

    File = CreateFileW(ManifestPath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (File == INVALID_HANDLE_VALUE)
        return FALSE;

    Size = GetFileSize(File, NULL);
    Text = (Size != INVALID_FILE_SIZE && Size < VK_MANIFEST_MAX_SIZE) ?
           HeapAlloc(GetProcessHeap(), 0, Size + 1) : NULL;
    if (!Text || !ReadFile(File, Text, Size, &Read, NULL))
    {
        if (Text)
            HeapFree(GetProcessHeap(), 0, Text);
        CloseHandle(File);
        return FALSE;
    }
    CloseHandle(File);
    Text[Read] = 0;

    Key = strstr(Text, "\"library_path\"");
    Value = Key ? strchr(Key + sizeof("\"library_path\"") - 1, ':') : NULL;
    Value = Value ? strchr(Value, '"') : NULL;
    if (Value)
    {
        Start = Out = Value + 1;
        for (Value = Start; *Value && *Value != '"'; Value++)
        {
            if (*Value == '\\' && Value[1])
                Value++;
            *Out++ = *Value;
        }
        if (*Value == '"')
            *Out = 0;
        else
            Start = NULL;
    }

    if (Start && MultiByteToWideChar(CP_UTF8, 0, Start, -1, Path, ARRAYSIZE(Path)))
    {
        if (!wcspbrk(Path, L"\\/") || Path[0] == L'\\' || Path[0] == L'/' || (Path[0] && Path[1] == L':'))
        {
            Result = SUCCEEDED(StringCchCopyW(Library, LibraryChars, Path));
        }
        else
        {
            Slash = wcsrchr(ManifestPath, L'\\');
            Other = wcsrchr(ManifestPath, L'/');
            if (!Slash || (Other && Other > Slash))
                Slash = Other;
            if (Slash && (DWORD)(Slash - ManifestPath + 1) < LibraryChars)
            {
                memcpy(Library, ManifestPath, (Slash - ManifestPath + 1) * sizeof(WCHAR));
                Library[Slash - ManifestPath + 1] = 0;
                Result = SUCCEEDED(StringCchCatW(Library, LibraryChars, Path));
            }
        }
    }

    HeapFree(GetProcessHeap(), 0, Text);
    return Result;
}

static BOOL vk_load_icd(const WCHAR *ManifestPath)
{
    WCHAR Library[MAX_PATH];
    PFN_vkNegotiateLoaderICDInterfaceVersion Negotiate;
    PFN_vkGetInstanceProcAddr GetInstanceProcAddr;
    uint32_t Version = VK_LOADER_ICD_INTERFACE_VERSION;
    HMODULE Module;

    if (!vk_manifest_library(ManifestPath, Library, ARRAYSIZE(Library)))
        return FALSE;

    Module = LoadLibraryExW(Library, NULL, 0);
    if (!Module)
        return FALSE;

    Negotiate = (PFN_vkNegotiateLoaderICDInterfaceVersion)GetProcAddress(Module, "vk_icdNegotiateLoaderICDInterfaceVersion");
    GetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)GetProcAddress(Module, "vk_icdGetInstanceProcAddr");
    if (!Negotiate || !GetInstanceProcAddr ||
        Negotiate(&Version) != VK_SUCCESS || Version < VK_LOADER_MIN_ICD_INTERFACE_VERSION)
    {
        FreeLibrary(Module);
        return FALSE;
    }

    VkIcd.Module = Module;
    VkIcd.GetInstanceProcAddr = GetInstanceProcAddr;
    VkActiveIcd = &VkIcd;
    return TRUE;
}

static void vk_load_icd_list(WCHAR *List)
{
    WCHAR *Path = List, *Next;

    while (Path && *Path && !VkActiveIcd)
    {
        Next = wcschr(Path, L';');
        if (Next)
            *Next++ = 0;
        if (*Path)
            vk_load_icd(Path);
        Path = Next;
    }
}

static struct vk_icd *vk_get_icd(void)
{
    static const WCHAR *const EnvironmentNames[] = { L"VK_DRIVER_FILES", L"VK_ICD_FILENAMES" };
    WCHAR Buffer[1024];
    DWORD Length, Index, Type, Data, DataSize, NameChars;
    LONG Status;
    HKEY Key;
    ULONG i;
    BOOL Override = FALSE;

    EnterCriticalSection(&VkLoaderLock);
    if (!VkIcdSearched)
    {
        VkIcdSearched = TRUE;

        for (i = 0; i < ARRAYSIZE(EnvironmentNames) && !Override; i++)
        {
            Length = GetEnvironmentVariableW(EnvironmentNames[i], Buffer, ARRAYSIZE(Buffer));
            if (Length && Length < ARRAYSIZE(Buffer))
            {
                Override = TRUE;
                vk_load_icd_list(Buffer);
            }
        }

        if (!Override &&
            RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Khronos\\Vulkan\\Drivers", 0, KEY_READ, &Key) == ERROR_SUCCESS)
        {
            for (Index = 0; !VkActiveIcd; Index++)
            {
                NameChars = ARRAYSIZE(Buffer);
                DataSize = sizeof(Data);
                Status = RegEnumValueW(Key, Index, Buffer, &NameChars, NULL, &Type, (BYTE *)&Data, &DataSize);
                if (Status == ERROR_MORE_DATA)
                    continue;
                if (Status != ERROR_SUCCESS)
                    break;
                if (Type == REG_DWORD && DataSize == sizeof(Data) && Data == 0)
                    vk_load_icd(Buffer);
            }
            RegCloseKey(Key);
        }
    }
    LeaveCriticalSection(&VkLoaderLock);

    return VkActiveIcd;
}

VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceVersion(uint32_t *pApiVersion)
{
    *pApiVersion = VK_API_VERSION_1_3;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceLayerProperties(uint32_t *pPropertyCount, VkLayerProperties *pProperties)
{
    *pPropertyCount = 0;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceExtensionProperties(const char *pLayerName, uint32_t *pPropertyCount,
                                                                      VkExtensionProperties *pProperties)
{
    struct vk_icd *Icd;
    PFN_vkEnumerateInstanceExtensionProperties Enumerate = NULL;

    if (pLayerName)
        return VK_ERROR_LAYER_NOT_PRESENT;

    Icd = vk_get_icd();
    if (Icd)
        Enumerate = (PFN_vkEnumerateInstanceExtensionProperties)Icd->GetInstanceProcAddr(NULL, "vkEnumerateInstanceExtensionProperties");
    if (!Enumerate)
    {
        *pPropertyCount = 0;
        return VK_SUCCESS;
    }
    return Enumerate(NULL, pPropertyCount, pProperties);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateInstance(const VkInstanceCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator,
                                                VkInstance *pInstance)
{
    struct vk_icd *Icd;
    struct vk_table *Table;
    PFN_vkCreateInstance Create;
    PFN_vkDestroyInstance Destroy;
    VkResult Result;
    ULONG i;

    if (pCreateInfo->enabledLayerCount)
        return VK_ERROR_LAYER_NOT_PRESENT;

    Icd = vk_get_icd();
    Create = Icd ? (PFN_vkCreateInstance)Icd->GetInstanceProcAddr(NULL, "vkCreateInstance") : NULL;
    if (!Create)
        return VK_ERROR_INCOMPATIBLE_DRIVER;

    Result = Create(pCreateInfo, pAllocator, pInstance);
    if (Result != VK_SUCCESS)
        return Result;

    Table = vk_alloc_table(VKI_COUNT);
    if (!Table)
    {
        Destroy = (PFN_vkDestroyInstance)Icd->GetInstanceProcAddr(*pInstance, "vkDestroyInstance");
        if (Destroy)
            Destroy(*pInstance, pAllocator);
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }

    Table->Icd = Icd;
    for (i = 0; i < VKI_COUNT; i++)
        Table->Functions[i] = Icd->GetInstanceProcAddr(*pInstance, vk_instance_function_names[i]);
    vk_set_dispatch(*pInstance, Table);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyInstance(VkInstance instance, const VkAllocationCallbacks *pAllocator)
{
    struct vk_table *Table;

    if (!instance)
        return;
    Table = vk_instance_table(instance);
    ((PFN_vkDestroyInstance)Table->Functions[VKI_vkDestroyInstance])(instance, pAllocator);
    HeapFree(GetProcessHeap(), 0, Table);
}

VKAPI_ATTR VkResult VKAPI_CALL vkEnumeratePhysicalDevices(VkInstance instance, uint32_t *pPhysicalDeviceCount,
                                                          VkPhysicalDevice *pPhysicalDevices)
{
    struct vk_table *Table = vk_instance_table(instance);
    VkResult Result;
    uint32_t i;

    Result = ((PFN_vkEnumeratePhysicalDevices)Table->Functions[VKI_vkEnumeratePhysicalDevices])(instance, pPhysicalDeviceCount, pPhysicalDevices);
    if ((Result == VK_SUCCESS || Result == VK_INCOMPLETE) && pPhysicalDevices)
    {
        for (i = 0; i < *pPhysicalDeviceCount; i++)
            vk_set_dispatch(pPhysicalDevices[i], Table);
    }
    return Result;
}

VKAPI_ATTR VkResult VKAPI_CALL vkEnumeratePhysicalDeviceGroups(VkInstance instance, uint32_t *pPhysicalDeviceGroupCount,
                                                               VkPhysicalDeviceGroupProperties *pPhysicalDeviceGroupProperties)
{
    struct vk_table *Table = vk_instance_table(instance);
    PFN_vkEnumeratePhysicalDeviceGroups Enumerate;
    VkResult Result;
    uint32_t i, j;

    Enumerate = (PFN_vkEnumeratePhysicalDeviceGroups)Table->Functions[VKI_vkEnumeratePhysicalDeviceGroups];
    if (!Enumerate)
        return VK_ERROR_INITIALIZATION_FAILED;

    Result = Enumerate(instance, pPhysicalDeviceGroupCount, pPhysicalDeviceGroupProperties);
    if ((Result == VK_SUCCESS || Result == VK_INCOMPLETE) && pPhysicalDeviceGroupProperties)
    {
        for (i = 0; i < *pPhysicalDeviceGroupCount; i++)
        {
            for (j = 0; j < pPhysicalDeviceGroupProperties[i].physicalDeviceCount; j++)
                vk_set_dispatch(pPhysicalDeviceGroupProperties[i].physicalDevices[j], Table);
        }
    }
    return Result;
}

VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateDeviceLayerProperties(VkPhysicalDevice physicalDevice, uint32_t *pPropertyCount,
                                                                VkLayerProperties *pProperties)
{
    *pPropertyCount = 0;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateDeviceExtensionProperties(VkPhysicalDevice physicalDevice, const char *pLayerName,
                                                                    uint32_t *pPropertyCount, VkExtensionProperties *pProperties)
{
    struct vk_table *Table = vk_instance_table(physicalDevice);

    if (pLayerName)
        return VK_ERROR_LAYER_NOT_PRESENT;
    return ((PFN_vkEnumerateDeviceExtensionProperties)Table->Functions[VKI_vkEnumerateDeviceExtensionProperties])(physicalDevice, NULL, pPropertyCount, pProperties);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateDevice(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo *pCreateInfo,
                                              const VkAllocationCallbacks *pAllocator, VkDevice *pDevice)
{
    struct vk_table *InstanceTable = vk_instance_table(physicalDevice);
    struct vk_table *Table;
    PFN_vkGetDeviceProcAddr GetDeviceProcAddr;
    PFN_vkDestroyDevice Destroy;
    VkResult Result;
    ULONG i;

    if (pCreateInfo->enabledLayerCount)
        return VK_ERROR_LAYER_NOT_PRESENT;

    GetDeviceProcAddr = (PFN_vkGetDeviceProcAddr)InstanceTable->Functions[VKI_vkGetDeviceProcAddr];
    if (!GetDeviceProcAddr)
        return VK_ERROR_INITIALIZATION_FAILED;

    Result = ((PFN_vkCreateDevice)InstanceTable->Functions[VKI_vkCreateDevice])(physicalDevice, pCreateInfo, pAllocator, pDevice);
    if (Result != VK_SUCCESS)
        return Result;

    Table = vk_alloc_table(VKD_COUNT);
    if (!Table)
    {
        Destroy = (PFN_vkDestroyDevice)GetDeviceProcAddr(*pDevice, "vkDestroyDevice");
        if (Destroy)
            Destroy(*pDevice, pAllocator);
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }

    Table->Icd = InstanceTable->Icd;
    Table->GetDeviceProcAddr = GetDeviceProcAddr;
    for (i = 0; i < VKD_COUNT; i++)
        Table->Functions[i] = GetDeviceProcAddr(*pDevice, vk_device_function_names[i]);
    vk_set_dispatch(*pDevice, Table);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyDevice(VkDevice device, const VkAllocationCallbacks *pAllocator)
{
    struct vk_table *Table;

    if (!device)
        return;
    Table = vk_device_table(device);
    ((PFN_vkDestroyDevice)Table->Functions[VKD_vkDestroyDevice])(device, pAllocator);
    HeapFree(GetProcessHeap(), 0, Table);
}

VKAPI_ATTR void VKAPI_CALL vkGetDeviceQueue(VkDevice device, uint32_t queueFamilyIndex, uint32_t queueIndex, VkQueue *pQueue)
{
    struct vk_table *Table = vk_device_table(device);

    ((PFN_vkGetDeviceQueue)Table->Functions[VKD_vkGetDeviceQueue])(device, queueFamilyIndex, queueIndex, pQueue);
    if (*pQueue)
        vk_set_dispatch(*pQueue, Table);
}

VKAPI_ATTR void VKAPI_CALL vkGetDeviceQueue2(VkDevice device, const VkDeviceQueueInfo2 *pQueueInfo, VkQueue *pQueue)
{
    struct vk_table *Table = vk_device_table(device);

    ((PFN_vkGetDeviceQueue2)Table->Functions[VKD_vkGetDeviceQueue2])(device, pQueueInfo, pQueue);
    if (*pQueue)
        vk_set_dispatch(*pQueue, Table);
}

VKAPI_ATTR VkResult VKAPI_CALL vkAllocateCommandBuffers(VkDevice device, const VkCommandBufferAllocateInfo *pAllocateInfo,
                                                        VkCommandBuffer *pCommandBuffers)
{
    struct vk_table *Table = vk_device_table(device);
    VkResult Result;
    uint32_t i;

    Result = ((PFN_vkAllocateCommandBuffers)Table->Functions[VKD_vkAllocateCommandBuffers])(device, pAllocateInfo, pCommandBuffers);
    if (Result == VK_SUCCESS)
    {
        for (i = 0; i < pAllocateInfo->commandBufferCount; i++)
            vk_set_dispatch(pCommandBuffers[i], Table);
    }
    return Result;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice device, const char *pName)
{
    if (!device || !pName)
        return NULL;

    if (!strcmp(pName, "vkGetDeviceProcAddr") || !strcmp(pName, "vkDestroyDevice") ||
        !strcmp(pName, "vkGetDeviceQueue") || !strcmp(pName, "vkGetDeviceQueue2") ||
        !strcmp(pName, "vkAllocateCommandBuffers"))
    {
        return vk_find_export(pName);
    }

    return vk_device_table(device)->GetDeviceProcAddr(device, pName);
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance instance, const char *pName)
{
    PFN_vkVoidFunction Function;

    if (!pName)
        return NULL;

    if (!strcmp(pName, "vkGetInstanceProcAddr") || !strcmp(pName, "vkCreateInstance") ||
        !strcmp(pName, "vkEnumerateInstanceExtensionProperties") ||
        !strcmp(pName, "vkEnumerateInstanceLayerProperties") || !strcmp(pName, "vkEnumerateInstanceVersion"))
    {
        return vk_find_export(pName);
    }

    if (!instance)
        return NULL;

    Function = vk_find_export(pName);
    if (Function)
        return Function;

    return vk_instance_table(instance)->Icd->GetInstanceProcAddr(instance, pName);
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    if (fdwReason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hinstDLL);
        InitializeCriticalSection(&VkLoaderLock);
    }
    else if (fdwReason == DLL_PROCESS_DETACH && !lpvReserved)
    {
        DeleteCriticalSection(&VkLoaderLock);
    }
    return TRUE;
}
