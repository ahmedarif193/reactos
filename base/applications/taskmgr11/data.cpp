/*
 * PROJECT:     ReactOS Task Manager
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Data engine: system/process sampling, services, users,
 *              startup items, app history, process actions
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#include "app.h"

namespace Data {

SysSnapshot g;

/* ------------------------------------------------------------------ */
/*  Dynamic API resolution (keep working on older kernels)             */
/* ------------------------------------------------------------------ */

typedef BOOL (WINAPI *PFN_QueryFullProcessImageNameW)(HANDLE, DWORD, LPWSTR, PDWORD);
typedef BOOL (WINAPI *PFN_IsHungAppWindow)(HWND);
typedef BOOL (WINAPI *PFN_IsWow64Process)(HANDLE, PBOOL);

static PFN_QueryFullProcessImageNameW pQueryFullProcessImageNameW;
static PFN_IsHungAppWindow pIsHungAppWindow;
static PFN_IsWow64Process pIsWow64Process;

/* ProcessPowerThrottlingState (Win10 EcoQoS); harmless failure elsewhere */
#define TM_ProcessPowerThrottlingState ((PROCESSINFOCLASS)77)
typedef struct _TM_POWER_THROTTLING_STATE
{
    ULONG Version;
    ULONG ControlMask;
    ULONG StateMask;
} TM_POWER_THROTTLING_STATE;
#define TM_POWER_THROTTLING_VERSION        1
#define TM_POWER_THROTTLING_EXECUTION_SPEED 0x1

/* thread state constants for suspend detection */
#define TM_THREADSTATE_WAITING 5
#define TM_WAITREASON_SUSPENDED 5

/* ------------------------------------------------------------------ */
/*  Internal state                                                     */
/* ------------------------------------------------------------------ */

static PUCHAR s_procBuf;
static ULONG  s_procBufSize;

struct PrevProc
{
    ULONG    pid;
    LONGLONG createTime;
    LONGLONG cpu100ns;
    ULONGLONG ioBytes;
};
static Vec<PrevProc> s_prev;

struct WndEntry
{
    ULONG pid;
    HWND  hwnd;
    WCHAR title[128];
};
static Vec<WndEntry> s_wnds;

static Vec<ProcExtra*> s_extras;

struct EffMark { ULONG pid; LONGLONG create; };
static Vec<EffMark> s_effSet;

static Vec<SvcRow> s_services;
struct SvcHostEntry { ULONG pid; WCHAR names[512]; WCHAR group[64]; };
static Vec<SvcHostEntry> s_svchostMap;
struct SvcGroupCache { WCHAR name[96]; WCHAR group[64]; };
static Vec<SvcGroupCache> s_svcGroups;

static Vec<UserRow> s_users;
static Vec<StartupRow> s_startup;
static Vec<AppHistRow> s_appHist;

static LONGLONG s_lastQpc;
static double   s_qpcFreq;
static LONGLONG s_prevIdle, s_prevKernel, s_prevUser;
static LONGLONG s_prevCpuIdle[64], s_prevCpuKernel[64], s_prevCpuUser[64];
static ULONGLONG s_prevIoRead, s_prevIoWrite;
static BOOL s_first = TRUE;

/* selected physical disk; disk 0 is the common ReactOS system disk */
static HANDLE s_diskHandle = INVALID_HANDLE_VALUE;
static DISK_PERFORMANCE s_prevDiskPerf;
static BOOL s_havePrevDiskPerf;
static BOOL s_diskCountersEnabled;

/* network deltas */
static DWORD s_netIfIndex = (DWORD)-1;
static DWORD s_prevNetIn, s_prevNetOut;
static int   s_netInfoAge = 999;
static BOOL  s_wsaStarted;

static WCHAR s_winDir[MAX_PATH];

/* names that belong to the "Windows processes" section */
static const WCHAR* s_windowsProcs[] =
{
    L"System", L"System Idle Process", L"Registry", L"Memory Compression",
    L"smss.exe", L"csrss.exe", L"wininit.exe", L"winlogon.exe",
    L"services.exe", L"lsass.exe", L"lsm.exe", L"svchost.exe",
    L"spoolsv.exe", L"dwm.exe", L"conhost.exe", L"fontdrvhost.exe",
    L"userinit.exe", L"logonui.exe", L"dllhost.exe", L"wudfhost.exe",
    L"audiodg.exe", L"ctfmon.exe", L"sihost.exe", L"taskhostw.exe",
    L"mutant.exe"
};

static const WCHAR* s_criticalProcs[] =
{
    L"System", L"smss.exe", L"csrss.exe", L"wininit.exe", L"winlogon.exe",
    L"services.exe", L"lsass.exe", L"lsm.exe"
};

static BOOL NameInList(const WCHAR* name, const WCHAR** list, int n)
{
    for (int i = 0; i < n; i++)
        if (lstrcmpiW(name, list[i]) == 0) return TRUE;
    return FALSE;
}

/* ------------------------------------------------------------------ */
/*  Hardware detail: logical processor info + SMBIOS (arch-neutral)    */
/* ------------------------------------------------------------------ */

#ifndef PF_VIRT_FIRMWARE_ENABLED
#define PF_VIRT_FIRMWARE_ENABLED 21
#endif

static void DetectCpuTopology(void)
{
    HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    BOOL haveTopology = FALSE;

    /* Prefer the variable-size API so processor groups and all cache records
       are represented. Fall back for older ReactOS installations. */
    typedef BOOL (WINAPI *PFN_GLPIEX)(
        LOGICAL_PROCESSOR_RELATIONSHIP,
        PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX,
        PDWORD);
    PFN_GLPIEX pGlpiEx = (PFN_GLPIEX)GetProcAddress(
        kernel32, "GetLogicalProcessorInformationEx");
    if (pGlpiEx)
    {
        DWORD cb = 0;
        pGlpiEx(RelationAll, NULL, &cb);
        if (cb && cb < 1024 * 1024)
        {
            BYTE* buffer = (BYTE*)HeapAlloc(GetProcessHeap(), 0, cb);
            if (buffer && pGlpiEx(RelationAll,
                                  (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)buffer,
                                  &cb))
            {
                int cores = 0, packages = 0;
                ULONG l1 = 0, l2 = 0, l3 = 0;
                DWORD offset = 0;
                while (offset + sizeof(ULONG) * 2 <= cb)
                {
                    PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX info =
                        (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)(buffer + offset);
                    if (info->Size < sizeof(ULONG) * 2 || info->Size > cb - offset)
                        break;

                    switch (info->Relationship)
                    {
                    case RelationProcessorCore: cores++; break;
                    case RelationProcessorPackage: packages++; break;
                    case RelationCache:
                        switch (info->Cache.Level)
                        {
                        case 1: l1 += info->Cache.CacheSize / 1024; break;
                        case 2: l2 += info->Cache.CacheSize / 1024; break;
                        case 3: l3 += info->Cache.CacheSize / 1024; break;
                        }
                        break;
                    default: break;
                    }
                    offset += info->Size;
                }
                if (cores) g.cores = cores;
                if (packages) g.sockets = packages;
                if (l1) g.l1KB = l1;
                if (l2) g.l2KB = l2;
                if (l3) g.l3KB = l3;
                haveTopology = cores != 0;
            }
            if (buffer) HeapFree(GetProcessHeap(), 0, buffer);
        }
    }

    if (!haveTopology)
    {
        typedef BOOL (WINAPI *PFN_GLPI)(
            PSYSTEM_LOGICAL_PROCESSOR_INFORMATION,
            PDWORD);
        PFN_GLPI pGlpi = (PFN_GLPI)GetProcAddress(
            kernel32, "GetLogicalProcessorInformation");
        if (pGlpi)
        {
            DWORD cb = 0;
            pGlpi(NULL, &cb);
            if (cb && cb < 1024 * 1024)
            {
                SYSTEM_LOGICAL_PROCESSOR_INFORMATION* info =
                    (SYSTEM_LOGICAL_PROCESSOR_INFORMATION*)HeapAlloc(GetProcessHeap(), 0, cb);
                if (info && pGlpi(info, &cb))
                {
                    int cores = 0, packages = 0;
                    ULONG l1 = 0, l2 = 0, l3 = 0;
                    int count = cb / sizeof(*info);
                    for (int i = 0; i < count; i++)
                    {
                        switch (info[i].Relationship)
                        {
                        case RelationProcessorCore: cores++; break;
                        case RelationProcessorPackage: packages++; break;
                        case RelationCache:
                            switch (info[i].Cache.Level)
                            {
                            case 1: l1 += info[i].Cache.Size / 1024; break;
                            case 2: l2 += info[i].Cache.Size / 1024; break;
                            case 3: l3 += info[i].Cache.Size / 1024; break;
                            }
                            break;
                        default: break;
                        }
                    }
                    if (cores) g.cores = cores;
                    if (packages) g.sockets = packages;
                    if (l1) g.l1KB = l1;
                    if (l2) g.l2KB = l2;
                    if (l3) g.l3KB = l3;
                }
                if (info) HeapFree(GetProcessHeap(), 0, info);
            }
        }
    }
    if (!g.cores) g.cores = g.nCpu;
    if (!g.sockets) g.sockets = 1;

    /* virtualization state comes from the kernel's firmware checks */
    if (IsProcessorFeaturePresent(PF_VIRT_FIRMWARE_ENABLED))
        g.virtMode = 1;
}

static void DetectMemoryDevices(void)
{
    typedef UINT (WINAPI *PFN_GetSystemFirmwareTable)(DWORD, DWORD, PVOID, DWORD);
    PFN_GetSystemFirmwareTable pGet = (PFN_GetSystemFirmwareTable)
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetSystemFirmwareTable");
    if (!pGet) return;

    const DWORD RSMB = 0x52534D42;   /* 'RSMB' */
    UINT cb = pGet(RSMB, 0, NULL, 0);
    if (!cb || cb > 1024 * 1024) return;
    BYTE* buf = (BYTE*)HeapAlloc(GetProcessHeap(), 0, cb);
    if (!buf) return;
    if (pGet(RSMB, 0, buf, cb) != cb)
    {
        HeapFree(GetProcessHeap(), 0, buf);
        return;
    }

    /* RawSMBIOSData: BYTE method, major, minor, dmiRev; DWORD Length; data */
    if (cb < 8)
    {
        HeapFree(GetProcessHeap(), 0, buf);
        return;
    }
    DWORD len = *(DWORD*)(buf + 4);
    BYTE* p = buf + 8;
    BYTE* end = p + (len < cb - 8 ? len : cb - 8);

    ULONGLONG installed = 0;
    int used = 0, total = 0;
    DWORD speed = 0;
    BYTE ff = 0;

    while (p + 4 <= end)
    {
        BYTE type = p[0], slen = p[1];
        if (slen < 4) break;

        if (type == 17 && p + slen <= end)
        {
            total++;
            ULONGLONG bytes = 0;
            if (slen >= 0x0E)
            {
                WORD size = *(WORD*)(p + 0x0C);
                if (size == 0x7FFF && slen >= 0x20)
                {
                    DWORD ext = *(DWORD*)(p + 0x1C) & 0x7FFFFFFF;   /* MB */
                    bytes = (ULONGLONG)ext * 1024 * 1024;
                }
                else if (size != 0 && size != 0xFFFF)
                {
                    /* bit15: 1 = KB units, 0 = MB units */
                    if (size & 0x8000)
                        bytes = (ULONGLONG)(size & 0x7FFF) * 1024;
                    else
                        bytes = (ULONGLONG)size * 1024 * 1024;
                }
            }
            if (bytes)
            {
                used++;
                installed += bytes;
                if (!ff && slen > 0x0E) ff = p[0x0E];
                if (slen >= 0x17)
                {
                    WORD spd = *(WORD*)(p + 0x15);
                    if (spd && spd != 0xFFFF && spd > speed) speed = spd;
                }
            }
        }
        else if (type == 127)
        {
            break;
        }

        /* skip formatted area + trailing strings (double NUL) */
        BYTE* q = p + slen;
        while (q + 1 < end && (q[0] || q[1])) q++;
        p = q + 2;
    }
    HeapFree(GetProcessHeap(), 0, buf);

    g.ramInstalled = installed;
    g.ramSlotsUsed = used;
    g.ramSlotsTotal = total;
    g.ramSpeedMTs = speed;
    g.ramFormFactor = ff;
}

static BOOL GetVolumeDiskNumber(WCHAR driveLetter, DWORD* diskNumber)
{
    WCHAR path[] = L"\\\\.\\C:";
    path[4] = driveLetter;
    HANDLE volume = CreateFileW(path, 0,
                                FILE_SHARE_READ | FILE_SHARE_WRITE,
                                NULL, OPEN_EXISTING, 0, NULL);
    if (volume == INVALID_HANDLE_VALUE)
        return FALSE;

    STORAGE_DEVICE_NUMBER number;
    DWORD returned = 0;
    BOOL ok = DeviceIoControl(volume,
                              IOCTL_STORAGE_GET_DEVICE_NUMBER,
                              NULL, 0, &number, sizeof(number),
                              &returned, NULL);
    CloseHandle(volume);
    if (!ok || number.DeviceType != FILE_DEVICE_DISK)
        return FALSE;

    *diskNumber = number.DeviceNumber;
    return TRUE;
}

static void CopyDescriptorText(const BYTE* descriptor,
                               DWORD descriptorSize,
                               ULONG offset,
                               WCHAR* output,
                               int outputCount)
{
    output[0] = 0;
    if (!offset || offset >= descriptorSize)
        return;

    const char* text = (const char*)descriptor + offset;
    int length = 0;
    while (offset + length < descriptorSize && text[length])
        length++;
    while (length && (text[length - 1] == ' ' || text[length - 1] == '\t'))
        length--;
    if (!length)
        return;

    int converted = MultiByteToWideChar(CP_ACP, 0, text, length,
                                         output, outputCount - 1);
    if (converted > 0)
        output[converted] = 0;
}

static const WCHAR* StorageBusName(STORAGE_BUS_TYPE bus)
{
    switch ((int)bus)
    {
    case BusTypeScsi: return L"SCSI";
    case BusTypeAtapi: return L"ATAPI";
    case BusTypeAta: return L"ATA";
    case BusType1394: return L"IEEE 1394";
    case BusTypeSsa: return L"SSA";
    case BusTypeFibre: return L"Fibre Channel";
    case BusTypeUsb: return L"USB";
    case BusTypeRAID: return L"RAID";
    case BusTypeiScsi: return L"iSCSI";
    case BusTypeSas: return L"SAS";
    case BusTypeSata: return L"SATA";
    case BusTypeSd: return L"SD";
    case BusTypeMmc: return L"MMC";
    case BusTypeVirtual: return L"Virtual";
    case BusTypeFileBackedVirtual: return L"File-backed virtual";
    case 0x10: return L"Storage Spaces";
    case 0x11: return L"NVMe";
    case 0x12: return L"SCM";
    case 0x13: return L"UFS";
    default: return L"Unknown";
    }
}

static void AppendDiskVolume(const WCHAR* root)
{
    WCHAR name[4] = { root[0], L':', 0, 0 };
    if (g.diskVolumes[0])
        StringCchCatW(g.diskVolumes, _countof(g.diskVolumes), L" ");
    StringCchCatW(g.diskVolumes, _countof(g.diskVolumes), name);
}

static void DetectDisk(void)
{
    DWORD diskNumber = 0;
    if (s_winDir[0] && s_winDir[1] == L':')
        GetVolumeDiskNumber(s_winDir[0], &diskNumber);
    g.diskNumber = diskNumber;

    WCHAR path[64];
    StringCchPrintfW(path, _countof(path), L"\\\\.\\PhysicalDrive%lu", diskNumber);
    s_diskHandle = CreateFileW(path, 0,
                               FILE_SHARE_READ | FILE_SHARE_WRITE,
                               NULL, OPEN_EXISTING, 0, NULL);
    g.diskPresent = s_diskHandle != INVALID_HANDLE_VALUE;

    if (g.diskPresent)
    {
        DWORD returned = 0;
        GET_LENGTH_INFORMATION length;
        if (DeviceIoControl(s_diskHandle, IOCTL_DISK_GET_LENGTH_INFO,
                            NULL, 0, &length, sizeof(length),
                            &returned, NULL))
        {
            g.diskCapacity = (ULONGLONG)length.Length.QuadPart;
        }

        BYTE descriptorBuffer[1024];
        ZeroMemory(descriptorBuffer, sizeof(descriptorBuffer));
        STORAGE_PROPERTY_QUERY query;
        ZeroMemory(&query, sizeof(query));
        query.PropertyId = StorageDeviceProperty;
        query.QueryType = PropertyStandardQuery;
        if (DeviceIoControl(s_diskHandle, IOCTL_STORAGE_QUERY_PROPERTY,
                            &query, sizeof(query),
                            descriptorBuffer, sizeof(descriptorBuffer),
                            &returned, NULL) &&
            returned >= sizeof(STORAGE_DEVICE_DESCRIPTOR))
        {
            PSTORAGE_DEVICE_DESCRIPTOR descriptor =
                (PSTORAGE_DEVICE_DESCRIPTOR)descriptorBuffer;
            DWORD validSize = descriptor->Size;
            if (!validSize || validSize > returned)
                validSize = returned;

            WCHAR vendor[64], product[96];
            CopyDescriptorText(descriptorBuffer, validSize,
                               descriptor->VendorIdOffset,
                               vendor, _countof(vendor));
            CopyDescriptorText(descriptorBuffer, validSize,
                               descriptor->ProductIdOffset,
                               product, _countof(product));
            if (vendor[0])
                StringCchCopyW(g.diskModel, _countof(g.diskModel), vendor);
            if (product[0])
            {
                if (g.diskModel[0])
                    StringCchCatW(g.diskModel, _countof(g.diskModel), L" ");
                StringCchCatW(g.diskModel, _countof(g.diskModel), product);
            }
            StringCchCopyW(g.diskInterface, _countof(g.diskInterface),
                           StorageBusName(descriptor->BusType));
        }

        DEVICE_SEEK_PENALTY_DESCRIPTOR penalty;
        ZeroMemory(&penalty, sizeof(penalty));
        ZeroMemory(&query, sizeof(query));
        query.PropertyId = StorageDeviceSeekPenaltyProperty;
        query.QueryType = PropertyStandardQuery;
        if (DeviceIoControl(s_diskHandle, IOCTL_STORAGE_QUERY_PROPERTY,
                            &query, sizeof(query), &penalty, sizeof(penalty),
                            &returned, NULL))
        {
            StringCchCopyW(g.diskType, _countof(g.diskType),
                           penalty.IncursSeekPenalty ? L"HDD" : L"SSD");
        }
    }

    WCHAR drives[256];
    if (GetLogicalDriveStringsW(_countof(drives), drives))
    {
        for (WCHAR* root = drives; *root; root += lstrlenW(root) + 1)
        {
            if (GetDriveTypeW(root) != DRIVE_FIXED)
                continue;

            DWORD number;
            if (!GetVolumeDiskNumber(root[0], &number) || number != diskNumber)
                continue;

            g.diskPresent = TRUE;
            AppendDiskVolume(root);
            ULARGE_INTEGER available, total, freeBytes;
            if (GetDiskFreeSpaceExW(root, &available, &total, &freeBytes))
                g.diskFormatted += total.QuadPart;
            if (s_winDir[0] && (root[0] == s_winDir[0] ||
                                root[0] == s_winDir[0] + (L'a' - L'A') ||
                                root[0] + (L'a' - L'A') == s_winDir[0]))
                g.diskSystem = TRUE;
        }
    }

    HKEY key;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                      L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Memory Management",
                      0, KEY_QUERY_VALUE, &key) == ERROR_SUCCESS)
    {
        WCHAR pagingFiles[1024];
        DWORD type = 0, cb = sizeof(pagingFiles);
        if (RegQueryValueExW(key, L"PagingFiles", NULL, &type,
                            (LPBYTE)pagingFiles, &cb) == ERROR_SUCCESS &&
            (type == REG_MULTI_SZ || type == REG_SZ))
        {
            pagingFiles[_countof(pagingFiles) - 1] = 0;
            for (WCHAR* entry = pagingFiles; *entry; entry += lstrlenW(entry) + 1)
            {
                if (entry[1] == L':')
                {
                    DWORD number;
                    if (GetVolumeDiskNumber(entry[0], &number) && number == diskNumber)
                    {
                        g.diskPageFile = TRUE;
                        break;
                    }
                }
            }
        }
        RegCloseKey(key);
    }

    if (!g.diskCapacity)
        g.diskCapacity = g.diskFormatted;
    if (!g.diskModel[0])
        StringCchCopyW(g.diskModel, _countof(g.diskModel), L"Disk device");
    if (!g.diskType[0])
        StringCchCopyW(g.diskType, _countof(g.diskType), L"Unknown");
    if (!g.diskInterface[0])
        StringCchCopyW(g.diskInterface, _countof(g.diskInterface), L"Unknown");
}

static const WCHAR* NetworkTypeName(DWORD type)
{
    switch (type)
    {
    case IF_TYPE_IEEE80211: return L"Wi-Fi";
    case IF_TYPE_ETHERNET_CSMACD: return L"Ethernet";
    case IF_TYPE_PPP: return L"PPP";
    default: return L"Network";
    }
}

static void RefreshNetworkMetadata(const MIB_IFROW* row)
{
    StringCchCopyW(g.netType, _countof(g.netType), NetworkTypeName(row->dwType));
    g.netLinkBps = row->dwSpeed;
    g.netIpv4[0] = 0;
    g.netIpv6[0] = 0;
    g.netDns[0] = 0;
    g.netName[0] = 0;

    ULONG size = 0;
    ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
                  GAA_FLAG_SKIP_DNS_SERVER;
    DWORD error = GetAdaptersAddresses(AF_UNSPEC, flags, NULL, NULL, &size);
    if (error == ERROR_BUFFER_OVERFLOW && size && size < 1024 * 1024)
    {
        PIP_ADAPTER_ADDRESSES addresses =
            (PIP_ADAPTER_ADDRESSES)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size);
        if (addresses &&
            GetAdaptersAddresses(AF_UNSPEC, flags, NULL, addresses, &size) == NO_ERROR)
        {
            for (PIP_ADAPTER_ADDRESSES adapter = addresses;
                 adapter; adapter = adapter->Next)
            {
                if (adapter->IfIndex != row->dwIndex &&
                    adapter->Ipv6IfIndex != row->dwIndex)
                    continue;

                if (adapter->FriendlyName && adapter->FriendlyName[0])
                    StringCchCopyW(g.netName, _countof(g.netName), adapter->FriendlyName);
                if (adapter->Description && adapter->Description[0])
                    StringCchCopyW(g.netAdapter, _countof(g.netAdapter), adapter->Description);
                if (adapter->DnsSuffix && adapter->DnsSuffix[0])
                    StringCchCopyW(g.netDns, _countof(g.netDns), adapter->DnsSuffix);
                if (adapter->Length >= FIELD_OFFSET(IP_ADAPTER_ADDRESSES, ReceiveLinkSpeed) +
                                       sizeof(adapter->ReceiveLinkSpeed))
                {
                    g.netLinkBps = adapter->ReceiveLinkSpeed > adapter->TransmitLinkSpeed ?
                                   adapter->ReceiveLinkSpeed : adapter->TransmitLinkSpeed;
                }

                for (PIP_ADAPTER_UNICAST_ADDRESS address = adapter->FirstUnicastAddress;
                     address; address = address->Next)
                {
                    SOCKADDR* socketAddress = address->Address.lpSockaddr;
                    if (!socketAddress)
                        continue;
                    if (socketAddress->sa_family == AF_INET && !g.netIpv4[0])
                    {
                        SOCKADDR_IN* ipv4 = (SOCKADDR_IN*)socketAddress;
                        InetNtopW(AF_INET, &ipv4->sin_addr,
                                  g.netIpv4, _countof(g.netIpv4));
                    }
                    else if (socketAddress->sa_family == AF_INET6 && !g.netIpv6[0])
                    {
                        SOCKADDR_IN6* ipv6 = (SOCKADDR_IN6*)socketAddress;
                        WCHAR addressText[64];
                        if (InetNtopW(AF_INET6, &ipv6->sin6_addr,
                                     addressText, _countof(addressText)))
                        {
                            if (ipv6->sin6_scope_id)
                                StringCchPrintfW(g.netIpv6, _countof(g.netIpv6),
                                                 L"%s%%%lu", addressText,
                                                 ipv6->sin6_scope_id);
                            else
                                StringCchCopyW(g.netIpv6, _countof(g.netIpv6), addressText);
                        }
                    }
                }
                break;
            }
        }
        if (addresses)
            HeapFree(GetProcessHeap(), 0, addresses);
    }

    if (!g.netAdapter[0])
    {
        int descriptionLength = row->dwDescrLen;
        if (descriptionLength > MAXLEN_IFDESCR)
            descriptionLength = MAXLEN_IFDESCR;
        int converted = MultiByteToWideChar(CP_ACP, 0,
                                             (const char*)row->bDescr,
                                             descriptionLength,
                                             g.netAdapter,
                                             _countof(g.netAdapter) - 1);
        if (converted > 0)
            g.netAdapter[converted] = 0;
    }
    if (!g.netName[0])
        StringCchCopyW(g.netName, _countof(g.netName), g.netType);

    if (!g.netDns[0])
    {
        DWORD count = _countof(g.netDns);
        GetComputerNameExW(ComputerNameDnsFullyQualified, g.netDns, &count);
    }
}

static void PollDiskPerformance(double dt,
                                const SYSTEM_PERFORMANCE_INFORMATION* systemPerformance)
{
    ULONGLONG ioRead = (ULONGLONG)systemPerformance->IoReadTransferCount.QuadPart;
    ULONGLONG ioWrite = (ULONGLONG)systemPerformance->IoWriteTransferCount.QuadPart;
    if (!s_first)
    {
        g.diskReadBps = (double)(ioRead - s_prevIoRead) / dt;
        g.diskWriteBps = (double)(ioWrite - s_prevIoWrite) / dt;
    }
    s_prevIoRead = ioRead;
    s_prevIoWrite = ioWrite;

    g.diskPerfValid = FALSE;
    if (s_diskHandle == INVALID_HANDLE_VALUE)
        return;

    DISK_PERFORMANCE current;
    DWORD returned = 0;
    ZeroMemory(&current, sizeof(current));
    if (!DeviceIoControl(s_diskHandle, IOCTL_DISK_PERFORMANCE,
                         NULL, 0, &current, sizeof(current),
                         &returned, NULL))
    {
        s_havePrevDiskPerf = FALSE;
        return;
    }

    /* The first successful request holds the enable reference. Balance each
       later sampling request; the retained reference is released at exit. */
    if (s_diskCountersEnabled)
    {
        DWORD ignored;
        DeviceIoControl(s_diskHandle, IOCTL_DISK_PERFORMANCE_OFF,
                        NULL, 0, NULL, 0, &ignored, NULL);
    }
    else
    {
        s_diskCountersEnabled = TRUE;
    }

    if (s_havePrevDiskPerf)
    {
        LONGLONG queryDelta = current.QueryTime.QuadPart -
                              s_prevDiskPerf.QueryTime.QuadPart;
        LONGLONG readTimeDelta = current.ReadTime.QuadPart -
                                 s_prevDiskPerf.ReadTime.QuadPart;
        LONGLONG writeTimeDelta = current.WriteTime.QuadPart -
                                  s_prevDiskPerf.WriteTime.QuadPart;
        LONGLONG idleDelta = current.IdleTime.QuadPart -
                             s_prevDiskPerf.IdleTime.QuadPart;
        ULONG readCountDelta = current.ReadCount - s_prevDiskPerf.ReadCount;
        ULONG writeCountDelta = current.WriteCount - s_prevDiskPerf.WriteCount;

        g.diskReadBps = (double)(current.BytesRead.QuadPart -
                                 s_prevDiskPerf.BytesRead.QuadPart) / dt;
        g.diskWriteBps = (double)(current.BytesWritten.QuadPart -
                                  s_prevDiskPerf.BytesWritten.QuadPart) / dt;
        if (queryDelta > 0)
        {
            LONGLONG activeDelta;
            if (current.IdleTime.QuadPart || s_prevDiskPerf.IdleTime.QuadPart)
                activeDelta = queryDelta - idleDelta;
            else
                activeDelta = readTimeDelta + writeTimeDelta;
            if (activeDelta < 0)
                activeDelta = 0;
            if (activeDelta > queryDelta)
                activeDelta = queryDelta;
            g.diskActivePct = 100.0 * activeDelta / queryDelta;
        }
        ULONG operationCount = readCountDelta + writeCountDelta;
        if (operationCount)
            g.diskResponseMs = (readTimeDelta + writeTimeDelta) /
                               (10000.0 * operationCount);
        else
            g.diskResponseMs = 0;
        g.diskPerfValid = TRUE;
    }

    s_prevDiskPerf = current;
    s_havePrevDiskPerf = TRUE;
}

/* ------------------------------------------------------------------ */
/*  Init                                                               */
/* ------------------------------------------------------------------ */

void Init(void)
{
    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    HMODULE u32 = GetModuleHandleW(L"user32.dll");
    pQueryFullProcessImageNameW = (PFN_QueryFullProcessImageNameW)GetProcAddress(k32, "QueryFullProcessImageNameW");
    pIsWow64Process = (PFN_IsWow64Process)GetProcAddress(k32, "IsWow64Process");
    pIsHungAppWindow = (PFN_IsHungAppWindow)GetProcAddress(u32, "IsHungAppWindow");

    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    s_qpcFreq = (double)f.QuadPart;

    WSADATA wsaData;
    s_wsaStarted = WSAStartup(MAKEWORD(2, 2), &wsaData) == 0;

    GetWindowsDirectoryW(s_winDir, _countof(s_winDir));

    /* static hardware info */
    SYSTEM_BASIC_INFORMATION sbi;
    if (NT_SUCCESS(NtQuerySystemInformation(SystemBasicInformation, &sbi, sizeof(sbi), NULL)))
    {
        g.nCpu = sbi.NumberOfProcessors;
        g.pageSize = sbi.PageSize;
        g.ramBytes = (ULONGLONG)sbi.NumberOfPhysicalPages * sbi.PageSize;
    }
    if (g.nCpu < 1) g.nCpu = 1;
    if (!g.pageSize) g.pageSize = 4096;

    HKEY hk;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                      L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                      0, KEY_QUERY_VALUE, &hk) == ERROR_SUCCESS)
    {
        DWORD cb = sizeof(g.cpuName);
        RegQueryValueExW(hk, L"ProcessorNameString", NULL, NULL, (LPBYTE)g.cpuName, &cb);
        DWORD mhz = 0; cb = sizeof(mhz);
        if (RegQueryValueExW(hk, L"~MHz", NULL, NULL, (LPBYTE)&mhz, &cb) == ERROR_SUCCESS)
            g.cpuMHz = mhz;
        RegCloseKey(hk);
    }
    if (!g.cpuName[0])
        StringCchCopyW(g.cpuName, _countof(g.cpuName), L"Processor");

    DWORD cch = _countof(g.hostName);
    GetComputerNameW(g.hostName, &cch);

    DetectCpuTopology();
    DetectMemoryDevices();
    DetectDisk();

    RefreshServices();
    RefreshStartup();
    RefreshUsers();
    Tick();          /* prime deltas */
    Tick();
}

void Shutdown(void)
{
    for (int i = 0; i < s_extras.n; i++)
    {
        if (s_extras[i]->icon) DestroyIcon(s_extras[i]->icon);
        HeapFree(GetProcessHeap(), 0, s_extras[i]);
    }
    s_extras.Clear();
    if (s_procBuf) HeapFree(GetProcessHeap(), 0, s_procBuf);
    s_procBuf = NULL;
    if (s_diskHandle != INVALID_HANDLE_VALUE)
    {
        if (s_diskCountersEnabled)
        {
            DWORD ignored;
            DeviceIoControl(s_diskHandle, IOCTL_DISK_PERFORMANCE_OFF,
                            NULL, 0, NULL, 0, &ignored, NULL);
            s_diskCountersEnabled = FALSE;
        }
        CloseHandle(s_diskHandle);
        s_diskHandle = INVALID_HANDLE_VALUE;
    }
    if (s_wsaStarted)
    {
        WSACleanup();
        s_wsaStarted = FALSE;
    }
}

/* ------------------------------------------------------------------ */
/*  Window enumeration (app detection)                                 */
/* ------------------------------------------------------------------ */

static BOOL CALLBACK EnumWndProc(HWND hwnd, LPARAM lp)
{
    (void)lp;
    if (!IsWindowVisible(hwnd)) return TRUE;
    if (GetWindow(hwnd, GW_OWNER)) return TRUE;
    LONG ex = GetWindowLongW(hwnd, GWL_EXSTYLE);
    if (ex & WS_EX_TOOLWINDOW) return TRUE;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (!pid) return TRUE;

    WCHAR title[128] = L"";
    GetWindowTextW(hwnd, title, _countof(title));

    /* keep the first titled window per process; else remember untitled */
    for (int i = 0; i < s_wnds.n; i++)
    {
        if (s_wnds[i].pid == pid)
        {
            if (!s_wnds[i].title[0] && title[0])
            {
                s_wnds[i].hwnd = hwnd;
                StringCchCopyW(s_wnds[i].title, 128, title);
            }
            return TRUE;
        }
    }
    WndEntry* e = s_wnds.Add();
    if (e)
    {
        e->pid = pid;
        e->hwnd = hwnd;
        StringCchCopyW(e->title, 128, title);
    }
    return TRUE;
}

/* ------------------------------------------------------------------ */
/*  Extra (slow) process info, resolved lazily                         */
/* ------------------------------------------------------------------ */

static ProcExtra* FindExtra(ULONG pid, LONGLONG createTime)
{
    for (int i = 0; i < s_extras.n; i++)
        if (s_extras[i]->pid == pid && s_extras[i]->createTime == createTime)
            return s_extras[i];
    return NULL;
}

static void DevicePathToDos(WCHAR* path, int cch)
{
    /* \Device\HarddiskVolume1\... -> C:\... */
    if (path[0] != L'\\') return;
    WCHAR drives[512];
    if (!GetLogicalDriveStringsW(_countof(drives), drives)) return;
    for (WCHAR* d = drives; *d; d += lstrlenW(d) + 1)
    {
        WCHAR root[3] = { d[0], L':', 0 };
        WCHAR dev[MAX_PATH];
        if (QueryDosDeviceW(root, dev, _countof(dev)))
        {
            int len = lstrlenW(dev);
            if (_wcsnicmp(path, dev, len) == 0 && path[len] == L'\\')
            {
                WCHAR tmp[MAX_PATH];
                StringCchPrintfW(tmp, _countof(tmp), L"%s%s", root, path + len);
                StringCchCopyW(path, cch, tmp);
                return;
            }
        }
    }
}

static void WellKnownUser(const WCHAR* image, WCHAR* buf, int cch)
{
    if (NameInList(image, s_criticalProcs, _countof(s_criticalProcs)) ||
        lstrcmpiW(image, L"svchost.exe") == 0 ||
        lstrcmpiW(image, L"System Idle Process") == 0)
        StringCchCopyW(buf, cch, L"SYSTEM");
    else
        buf[0] = 0;
}

static void ResolveExtra(ProcRow& p)
{
    ProcExtra* x = p.x;
    x->resolved = TRUE;

    if (p.pid == 0 || p.pid == 4)
    {
        StringCchCopyW(x->user, _countof(x->user), L"SYSTEM");
        StringCchCopyW(x->desc, _countof(x->desc),
                       p.pid ? L"NT Kernel & System" : L"Percentage of time the processor is idle");
        return;
    }

    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, p.pid);
    if (!h)
        h = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, p.pid);

    if (h)
    {
        /* image path */
        WCHAR path[MAX_PATH] = L"";
        DWORD cch = _countof(path);
        BOOL got = FALSE;
        if (pQueryFullProcessImageNameW)
            got = pQueryFullProcessImageNameW(h, 0, path, &cch);
        if (!got)
        {
            if (GetProcessImageFileNameW(h, path, _countof(path)))
            {
                DevicePathToDos(path, _countof(path));
                got = path[0] == L'C' || path[1] == L':';
            }
        }
        if (got)
            StringCchCopyW(x->path, _countof(x->path), path);

        /* user name from token */
        HANDLE tok;
        if (OpenProcessToken(h, TOKEN_QUERY, &tok))
        {
            BYTE tuBuf[256];
            DWORD cb = 0;
            if (GetTokenInformation(tok, TokenUser, tuBuf, sizeof(tuBuf), &cb))
            {
                TOKEN_USER* tu = (TOKEN_USER*)tuBuf;
                WCHAR name[96] = L"", dom[96] = L"";
                DWORD cn = _countof(name), cd = _countof(dom);
                SID_NAME_USE use;
                if (LookupAccountSidW(NULL, tu->User.Sid, name, &cn, dom, &cd, &use))
                {
                    if (g_app.st.fullAcctName && dom[0])
                        StringCchPrintfW(x->user, _countof(x->user), L"%s\\%s", dom, name);
                    else
                        StringCchCopyW(x->user, _countof(x->user), name);
                }
            }
            CloseHandle(tok);
        }

        /* architecture */
        if (pIsWow64Process)
        {
            BOOL wow = FALSE;
            if (pIsWow64Process(h, &wow))
                StringCchCopyW(x->arch, _countof(x->arch), wow ? L"x86" : L"x64");
        }

        CloseHandle(h);
    }

    if (!x->user[0])
        WellKnownUser(p.image, x->user, _countof(x->user));

    if (x->path[0])
    {
        /* description from version resource */
        DWORD dummy;
        DWORD cb = GetFileVersionInfoSizeW(x->path, &dummy);
        if (cb && cb < 256 * 1024)
        {
            void* blk = HeapAlloc(GetProcessHeap(), 0, cb);
            if (blk && GetFileVersionInfoW(x->path, 0, cb, blk))
            {
                struct LANGCP { WORD lang, cp; } *lc = NULL;
                UINT lcLen = 0;
                VerQueryValueW(blk, L"\\VarFileInfo\\Translation", (void**)&lc, &lcLen);
                WORD lang = 0x0409, cp = 0x04B0;
                if (lc && lcLen >= sizeof(LANGCP)) { lang = lc->lang; cp = lc->cp; }
                WCHAR sub[64];
                StringCchPrintfW(sub, _countof(sub),
                                 L"\\StringFileInfo\\%04x%04x\\FileDescription", lang, cp);
                WCHAR* descr = NULL;
                UINT dLen = 0;
                if (VerQueryValueW(blk, sub, (void**)&descr, &dLen) && descr && descr[0])
                    StringCchCopyW(x->desc, _countof(x->desc), descr);

                if (!x->desc[0])
                {
                    StringCchPrintfW(sub, _countof(sub),
                                     L"\\StringFileInfo\\040904B0\\FileDescription");
                    if (VerQueryValueW(blk, sub, (void**)&descr, &dLen) && descr && descr[0])
                        StringCchCopyW(x->desc, _countof(x->desc), descr);
                }
            }
            if (blk) HeapFree(GetProcessHeap(), 0, blk);
        }

        /* small icon */
        SHFILEINFOW sfi;
        ZeroMemory(&sfi, sizeof(sfi));
        if (SHGetFileInfoW(x->path, 0, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_SMALLICON))
            x->icon = sfi.hIcon;
    }

    if (!x->desc[0])
        StringCchCopyW(x->desc, _countof(x->desc), p.image);
}

/* company name of an exe (startup page) */
BOOL GetFileCompany(const WCHAR* path, WCHAR* buf, int cch)
{
    buf[0] = 0;
    DWORD dummy;
    DWORD cb = GetFileVersionInfoSizeW(path, &dummy);
    if (!cb || cb > 256 * 1024) return FALSE;
    void* blk = HeapAlloc(GetProcessHeap(), 0, cb);
    if (!blk) return FALSE;
    BOOL ok = FALSE;
    if (GetFileVersionInfoW(path, 0, cb, blk))
    {
        struct LANGCP { WORD lang, cp; } *lc = NULL;
        UINT lcLen = 0;
        VerQueryValueW(blk, L"\\VarFileInfo\\Translation", (void**)&lc, &lcLen);
        WORD lang = 0x0409, cp = 0x04B0;
        if (lc && lcLen >= 4) { lang = lc->lang; cp = lc->cp; }
        WCHAR sub[64];
        StringCchPrintfW(sub, _countof(sub), L"\\StringFileInfo\\%04x%04x\\CompanyName", lang, cp);
        WCHAR* v = NULL; UINT vLen = 0;
        if (VerQueryValueW(blk, sub, (void**)&v, &vLen) && v && v[0])
        {
            StringCchCopyW(buf, cch, v);
            ok = TRUE;
        }
    }
    HeapFree(GetProcessHeap(), 0, blk);
    return ok;
}

/* ------------------------------------------------------------------ */
/*  Main tick                                                          */
/* ------------------------------------------------------------------ */

static const WndEntry* FindWnd(ULONG pid)
{
    for (int i = 0; i < s_wnds.n; i++)
        if (s_wnds[i].pid == pid) return &s_wnds[i];
    return NULL;
}

static const PrevProc* FindPrev(ULONG pid, LONGLONG create)
{
    for (int i = 0; i < s_prev.n; i++)
        if (s_prev[i].pid == pid && s_prev[i].createTime == create) return &s_prev[i];
    return NULL;
}

static BOOL IsEffMarked(ULONG pid, LONGLONG create)
{
    for (int i = 0; i < s_effSet.n; i++)
        if (s_effSet[i].pid == pid && s_effSet[i].create == create) return TRUE;
    return FALSE;
}

const WCHAR* SvchostServices(ULONG pid)
{
    for (int i = 0; i < s_svchostMap.n; i++)
        if (s_svchostMap[i].pid == pid) return s_svchostMap[i].names;
    return NULL;
}

const WCHAR* SvchostGroup(ULONG pid)
{
    for (int i = 0; i < s_svchostMap.n; i++)
        if (s_svchostMap[i].pid == pid && s_svchostMap[i].group[0])
            return s_svchostMap[i].group;
    return NULL;
}

static void AccumHistory(const WCHAR* image, LONGLONG cpuDelta)
{
    if (cpuDelta <= 0) return;
    for (int i = 0; i < s_appHist.n; i++)
    {
        if (lstrcmpiW(s_appHist[i].image, image) == 0)
        {
            s_appHist[i].cpu100ns += cpuDelta;
            return;
        }
    }
    AppHistRow* r = s_appHist.Add();
    if (r)
    {
        StringCchCopyW(r->image, _countof(r->image), image);
        r->cpu100ns = cpuDelta;
    }
}

void Tick(void)
{
    /* ---- timing ---- */
    LARGE_INTEGER qpc;
    QueryPerformanceCounter(&qpc);
    double dt = s_lastQpc ? (qpc.QuadPart - s_lastQpc) / s_qpcFreq : 1.0;
    if (dt <= 0.001) dt = 0.001;
    s_lastQpc = qpc.QuadPart;

    /* ---- global performance counters ---- */
    SYSTEM_PERFORMANCE_INFORMATION perf;
    ZeroMemory(&perf, sizeof(perf));
    NtQuerySystemInformation(SystemPerformanceInformation, &perf, sizeof(perf), NULL);

    SYSTEM_TIMEOFDAY_INFORMATION tod;
    ZeroMemory(&tod, sizeof(tod));
    if (NT_SUCCESS(NtQuerySystemInformation(SystemTimeOfDayInformation, &tod, sizeof(tod), NULL)))
        g.upSeconds = (ULONGLONG)((tod.CurrentTime.QuadPart - tod.BootTime.QuadPart) / 10000000);

    SYSTEM_FILECACHE_INFORMATION fc;
    ZeroMemory(&fc, sizeof(fc));
    NtQuerySystemInformation(SystemFileCacheInformation, &fc, sizeof(fc), NULL);

    /* current cpu speed (guarded; older kernels fail this cleanly) */
    {
        struct TM_PPI
        {
            ULONG Number, MaxMhz, CurrentMhz, MhzLimit, MaxIdleState, CurrentIdleState;
        } ppi[64];
        if (g.nCpu <= 64 &&
            NT_SUCCESS(NtPowerInformation(ProcessorInformation, NULL, 0,
                                          ppi, sizeof(ppi[0]) * g.nCpu)))
        {
            DWORD cur = 0, maxMhz = 0;
            for (int i = 0; i < g.nCpu; i++)
            {
                if (ppi[i].CurrentMhz > cur) cur = ppi[i].CurrentMhz;
                if (ppi[i].MaxMhz > maxMhz) maxMhz = ppi[i].MaxMhz;
            }
            if (cur) g.cpuCurMHz = cur;
            if (maxMhz && !g.cpuMHz) g.cpuMHz = maxMhz;
        }
    }

    /* ---- cpu total ---- */
    {
        SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION sppi[64];
        ULONG got = 0;
        LONGLONG idle = 0, kern = 0, user = 0;
        if (NT_SUCCESS(NtQuerySystemInformation(SystemProcessorPerformanceInformation,
                                                sppi, sizeof(sppi), &got)))
        {
            int n = got / sizeof(sppi[0]);
            if (n > 64) n = 64;
            for (int i = 0; i < n; i++)
            {
                idle += sppi[i].IdleTime.QuadPart;
                kern += sppi[i].KernelTime.QuadPart;   /* includes idle */
                user += sppi[i].UserTime.QuadPart;

                LONGLONG cpuIdle = sppi[i].IdleTime.QuadPart;
                LONGLONG cpuKernel = sppi[i].KernelTime.QuadPart;
                LONGLONG cpuUser = sppi[i].UserTime.QuadPart;
                LONGLONG idleDelta = cpuIdle - s_prevCpuIdle[i];
                LONGLONG kernelDelta = cpuKernel - s_prevCpuKernel[i];
                LONGLONG userDelta = cpuUser - s_prevCpuUser[i];
                LONGLONG totalDelta = kernelDelta + userDelta;
                double busyPercent = 0;
                double kernelPercent = 0;
                if (totalDelta > 0 && s_prevCpuKernel[i])
                {
                    double busy = (double)(totalDelta - idleDelta) / totalDelta;
                    double kernelBusy = (double)(kernelDelta - idleDelta) / totalDelta;
                    if (busy < 0) busy = 0;
                    if (busy > 1) busy = 1;
                    if (kernelBusy < 0) kernelBusy = 0;
                    if (kernelBusy > 1) kernelBusy = 1;
                    busyPercent = busy * 100.0;
                    kernelPercent = kernelBusy * 100.0;
                }
                g.hCpuLogical[i].Push((float)busyPercent);
                g.hCpuLogicalKernel[i].Push((float)kernelPercent);
                s_prevCpuIdle[i] = cpuIdle;
                s_prevCpuKernel[i] = cpuKernel;
                s_prevCpuUser[i] = cpuUser;
            }
        }
        LONGLONG dIdle = idle - s_prevIdle;
        LONGLONG dKern = kern - s_prevKernel;
        LONGLONG dUser = user - s_prevUser;
        LONGLONG dTotal = dKern + dUser;
        if (dTotal > 0 && s_prevKernel)
        {
            double busy = (double)(dTotal - dIdle) / (double)dTotal;
            if (busy < 0) busy = 0;
            if (busy > 1) busy = 1;
            g.cpuTotalPct = busy * 100.0;
            double kernBusy = (double)(dKern - dIdle) / (double)dTotal;
            if (kernBusy < 0) kernBusy = 0;
            g.cpuKernelPct = kernBusy * 100.0;
        }
        s_prevIdle = idle; s_prevKernel = kern; s_prevUser = user;
    }

    /* ---- memory ---- */
    g.memTotal = g.ramBytes;
    g.memAvail = (ULONGLONG)perf.AvailablePages * g.pageSize;
    if (g.memAvail > g.memTotal) g.memAvail = g.memTotal;
    g.memInUse = g.memTotal - g.memAvail;
    g.memCommit = (ULONGLONG)perf.CommittedPages * g.pageSize;
    g.memCommitLimit = (ULONGLONG)perf.CommitLimit * g.pageSize;
    g.memCached = (ULONGLONG)fc.CurrentSize;
    g.memPagedPool = (ULONGLONG)perf.PagedPoolPages * g.pageSize;
    g.memNonPagedPool = (ULONGLONG)perf.NonPagedPoolPages * g.pageSize;

    /* IOCTL_DISK_PERFORMANCE is preferred; ReactOS installations without
       diskperf support retain the system-wide transfer-rate fallback. */
    PollDiskPerformance(dt, &perf);
    if (g.diskReadBps < 0) g.diskReadBps = 0;
    if (g.diskWriteBps < 0) g.diskWriteBps = 0;

    /* ---- network ---- */
    {
        ULONG cb = 0;
        g.netPresent = FALSE;
        g.netConnected = FALSE;
        DWORD tableError = GetIfTable(NULL, &cb, FALSE);
        PMIB_IFTABLE table = NULL;
        if (tableError == ERROR_INSUFFICIENT_BUFFER && cb && cb < 1024 * 1024)
            table = (PMIB_IFTABLE)HeapAlloc(GetProcessHeap(), 0, cb);
        if (table && GetIfTable(table, &cb, FALSE) == NO_ERROR)
        {
            MIB_IFROW* best = NULL;
            for (DWORD i = 0; i < table->dwNumEntries; i++)
            {
                MIB_IFROW* r = &table->table[i];
                if (r->dwType == MIB_IF_TYPE_LOOPBACK) continue;
                BOOL connected = r->dwOperStatus >= IF_OPER_STATUS_CONNECTED;
                BOOL bestConnected = best &&
                    best->dwOperStatus >= IF_OPER_STATUS_CONNECTED;
                if (s_netIfIndex == r->dwIndex && connected)
                {
                    best = r;
                    break;
                }
                if (!best || (connected && !bestConnected) ||
                    (connected == bestConnected &&
                     (r->dwInOctets + r->dwOutOctets) >
                     (best->dwInOctets + best->dwOutOctets)))
                    best = r;
            }
            if (best)
            {
                if (s_netIfIndex != best->dwIndex)
                {
                    s_netIfIndex = best->dwIndex;
                    s_prevNetIn = best->dwInOctets;
                    s_prevNetOut = best->dwOutOctets;
                    s_netInfoAge = 999;
                }
                DWORD dIn = best->dwInOctets - s_prevNetIn;    /* wraps ok (unsigned) */
                DWORD dOut = best->dwOutOctets - s_prevNetOut;
                g.netRecvBps = dIn / dt;
                g.netSendBps = dOut / dt;
                s_prevNetIn = best->dwInOctets;
                s_prevNetOut = best->dwOutOctets;
                g.netPresent = TRUE;
                g.netConnected = best->dwOperStatus >= IF_OPER_STATUS_CONNECTED;

                if (++s_netInfoAge > 30)
                {
                    s_netInfoAge = 0;
                    RefreshNetworkMetadata(best);
                }
            }
        }
        if (table)
            HeapFree(GetProcessHeap(), 0, table);
        if (!g.netPresent)
        {
            g.netRecvBps = 0;
            g.netSendBps = 0;
        }
    }

    /* ---- window map ---- */
    s_wnds.Clear();
    EnumWindows(EnumWndProc, 0);

    /* ---- process list ---- */
    if (!s_procBuf)
    {
        s_procBufSize = 256 * 1024;
        s_procBuf = (PUCHAR)HeapAlloc(GetProcessHeap(), 0, s_procBufSize);
    }
    NTSTATUS st;
    ULONG need = 0;
    for (;;)
    {
        st = NtQuerySystemInformation(SystemProcessInformation,
                                      s_procBuf, s_procBufSize, &need);
        if (st != STATUS_INFO_LENGTH_MISMATCH) break;
        ULONG newSize = need + 64 * 1024;
        PUCHAR nb = (PUCHAR)HeapReAlloc(GetProcessHeap(), 0, s_procBuf, newSize);
        if (!nb) break;
        s_procBuf = nb;
        s_procBufSize = newSize;
    }

    /* stash previous cumulative counters for delta computation */
    s_prev.Clear();
    for (int i = 0; i < g.procs.n; i++)
    {
        PrevProc pp;
        pp.pid = g.procs[i].pid;
        pp.createTime = g.procs[i].createTime;
        pp.cpu100ns = g.procs[i].cpu100ns;
        pp.ioBytes = g.procs[i].ioBytes;
        s_prev.Push(pp);
    }

    g.procs.Clear();
    g.procCount = 0;
    g.threadCount = 0;
    g.handleCount = 0;

    /* GC mark */
    for (int i = 0; i < s_extras.n; i++)
        s_extras[i]->inUse = FALSE;

    if (NT_SUCCESS(st))
    {
        PUCHAR ptr = s_procBuf;
        for (;;)
        {
            PSYSTEM_PROCESS_INFORMATION spi = (PSYSTEM_PROCESS_INFORMATION)ptr;
            ProcRow* p = g.procs.Add();
            if (!p) break;

            p->pid = HandleToUlong(spi->UniqueProcessId);
            p->ppid = HandleToUlong(spi->InheritedFromUniqueProcessId);
            p->sessionId = spi->SessionId;
            p->threads = spi->NumberOfThreads;
            p->handles = spi->HandleCount;
            p->basePri = spi->BasePriority;
            p->createTime = spi->CreateTime.QuadPart;
            p->cpu100ns = spi->KernelTime.QuadPart + spi->UserTime.QuadPart;
            p->ioBytes = (ULONGLONG)spi->ReadTransferCount.QuadPart +
                         (ULONGLONG)spi->WriteTransferCount.QuadPart;

            if (spi->ImageName.Buffer && spi->ImageName.Length)
            {
                /* basename only (some kernels return a path) */
                WCHAR tmp[MAX_PATH];
                UINT n = spi->ImageName.Length / sizeof(WCHAR);
                if (n >= _countof(tmp)) n = _countof(tmp) - 1;
                CopyMemory(tmp, spi->ImageName.Buffer, n * sizeof(WCHAR));
                tmp[n] = 0;
                const WCHAR* base = wcsrchr(tmp, L'\\');
                StringCchCopyW(p->image, _countof(p->image), base ? base + 1 : tmp);
            }
            else
            {
                StringCchCopyW(p->image, _countof(p->image),
                               p->pid == 0 ? L"System Idle Process" : L"System");
            }

            /* private memory: prefer private working set when the kernel fills it */
            p->memBytes = (ULONGLONG)spi->WorkingSetPrivateSize.QuadPart;
            if (!p->memBytes)
                p->memBytes = (ULONGLONG)spi->PagefileUsage;

            /* deltas */
            const PrevProc* pv = FindPrev(p->pid, p->createTime);
            LONGLONG cpuDelta;
            if (pv)
            {
                cpuDelta = p->cpu100ns - pv->cpu100ns;
                p->diskBps = (double)(LONGLONG)(p->ioBytes - pv->ioBytes) / dt;
                if (p->diskBps < 0) p->diskBps = 0;
            }
            else
            {
                cpuDelta = 0;
                p->flags |= PF_NEW;
            }
            double denom = dt * g.nCpu * 10000000.0;
            p->cpuPct = denom > 0 ? (cpuDelta * 100.0) / denom : 0;
            if (p->cpuPct < 0) p->cpuPct = 0;
            if (p->cpuPct > 100) p->cpuPct = 100;

            if (p->pid != 0)
                AccumHistory(p->image, cpuDelta);

            /* suspended? all threads in Wait/Suspended */
            if (spi->NumberOfThreads)
            {
                PSYSTEM_THREAD_INFORMATION th = (PSYSTEM_THREAD_INFORMATION)(spi + 1);
                BOOL allSusp = TRUE;
                for (ULONG t = 0; t < spi->NumberOfThreads; t++)
                {
                    if (!(th[t].ThreadState == TM_THREADSTATE_WAITING &&
                          th[t].WaitReason == TM_WAITREASON_SUSPENDED))
                    {
                        allSusp = FALSE;
                        break;
                    }
                }
                if (allSusp) p->flags |= PF_SUSPENDED;
            }

            /* windows */
            const WndEntry* we = (p->pid > 4) ? FindWnd(p->pid) : NULL;
            if (we)
            {
                p->flags |= PF_HASWINDOW;
                p->mainWnd = we->hwnd;
                StringCchCopyW(p->wndTitle, _countof(p->wndTitle),
                               we->title[0] ? we->title : p->image);
                if (pIsHungAppWindow && pIsHungAppWindow(we->hwnd))
                    p->flags |= PF_HUNG;
            }

            if (lstrcmpiW(p->image, L"svchost.exe") == 0)
                p->flags |= PF_SVCHOST;
            if (NameInList(p->image, s_criticalProcs, _countof(s_criticalProcs)))
                p->flags |= PF_CRITICAL;
            /* leaf = explicitly set by us, or exactly idle base priority */
            if (IsEffMarked(p->pid, p->createTime) || (p->basePri == 4 && p->pid > 4))
                p->flags |= PF_EFFICIENCY;

            /* category */
            if ((p->flags & PF_HASWINDOW) && p->wndTitle[0])
                p->category = CAT_APP;
            else if (NameInList(p->image, s_windowsProcs, _countof(s_windowsProcs)))
                p->category = CAT_WINDOWS;
            else
                p->category = CAT_BACKGROUND;

            /* extras cache */
            ProcExtra* x = FindExtra(p->pid, p->createTime);
            if (!x)
            {
                x = (ProcExtra*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(ProcExtra));
                if (x)
                {
                    x->pid = p->pid;
                    x->createTime = p->createTime;
                    s_extras.Push(x);
                }
            }
            if (x) x->inUse = TRUE;
            p->x = x;

            if (p->pid != 0)
            {
                g.procCount++;
                g.threadCount += p->threads;
                g.handleCount += p->handles;
            }

            if (!spi->NextEntryOffset) break;
            ptr += spi->NextEntryOffset;
        }
    }

    /* resolve a few extras per tick (icons, users, descriptions) */
    {
        int budget = 8;
        for (int i = 0; i < g.procs.n && budget; i++)
        {
            if (g.procs[i].x && !g.procs[i].x->resolved)
            {
                ResolveExtra(g.procs[i]);
                budget--;
            }
        }
    }

    /* GC extras of dead processes */
    for (int i = s_extras.n - 1; i >= 0; i--)
    {
        if (!s_extras[i]->inUse)
        {
            if (s_extras[i]->icon) DestroyIcon(s_extras[i]->icon);
            HeapFree(GetProcessHeap(), 0, s_extras[i]);
            s_extras.RemoveAt(i);
        }
    }

    /* history rings */
    g.hCpu.Push((float)g.cpuTotalPct);
    g.hCpuKernel.Push((float)g.cpuKernelPct);
    g.hMem.Push(g.memTotal ? (float)(100.0 * g.memInUse / g.memTotal) : 0.0f);
    g.hDisk.Push((float)(g.diskReadBps + g.diskWriteBps));
    if (g.diskPerfValid)
        g.hDiskActive.Push((float)g.diskActivePct);
    g.hNetRecv.Push((float)g.netRecvBps);
    g.hNetSend.Push((float)g.netSendBps);

    s_first = FALSE;
}

ProcRow* FindProc(ULONG pid)
{
    for (int i = 0; i < g.procs.n; i++)
        if (g.procs[i].pid == pid) return &g.procs[i];
    return NULL;
}

/* ------------------------------------------------------------------ */
/*  Services                                                           */
/* ------------------------------------------------------------------ */

Vec<SvcRow>& Services(void) { return s_services; }

static const WCHAR* CachedGroup(const WCHAR* name)
{
    for (int i = 0; i < s_svcGroups.n; i++)
        if (lstrcmpiW(s_svcGroups[i].name, name) == 0) return s_svcGroups[i].group;
    return NULL;
}

static void CacheGroup(SC_HANDLE scm, const WCHAR* name)
{
    if (CachedGroup(name)) return;
    SvcGroupCache* c = s_svcGroups.Add();
    if (!c) return;
    StringCchCopyW(c->name, _countof(c->name), name);

    SC_HANDLE svc = OpenServiceW(scm, name, SERVICE_QUERY_CONFIG);
    if (svc)
    {
        BYTE buf[8192];
        DWORD need = 0;
        if (QueryServiceConfigW(svc, (LPQUERY_SERVICE_CONFIGW)buf, sizeof(buf), &need))
        {
            LPQUERY_SERVICE_CONFIGW cfg = (LPQUERY_SERVICE_CONFIGW)buf;
            if (cfg->lpBinaryPathName)
            {
                /* svchost.exe -k <group> */
                const WCHAR* k = StrStrIW(cfg->lpBinaryPathName, L"-k ");
                if (k && StrStrIW(cfg->lpBinaryPathName, L"svchost"))
                {
                    k += 3;
                    while (*k == L' ') k++;
                    int o = 0;
                    while (k[o] && k[o] != L' ' && k[o] != L'"' && o < 62)
                    {
                        c->group[o] = k[o];
                        o++;
                    }
                    c->group[o] = 0;
                }
            }
        }
        CloseServiceHandle(svc);
    }
}

void RefreshServices(void)
{
    s_services.Clear();
    s_svchostMap.Clear();

    SC_HANDLE scm = OpenSCManagerW(NULL, NULL, SC_MANAGER_ENUMERATE_SERVICE);
    if (!scm) return;

    DWORD cb = 0, count = 0, resume = 0;
    EnumServicesStatusExW(scm, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_STATE_ALL,
                          NULL, 0, &cb, &count, &resume, NULL);
    if (cb)
    {
        BYTE* buf = (BYTE*)HeapAlloc(GetProcessHeap(), 0, cb + 4096);
        if (buf)
        {
            resume = 0;
            if (EnumServicesStatusExW(scm, SC_ENUM_PROCESS_INFO, SERVICE_WIN32,
                                      SERVICE_STATE_ALL, buf, cb + 4096, &cb, &count,
                                      &resume, NULL))
            {
                LPENUM_SERVICE_STATUS_PROCESSW es = (LPENUM_SERVICE_STATUS_PROCESSW)buf;
                for (DWORD i = 0; i < count; i++)
                {
                    SvcRow* r = s_services.Add();
                    if (!r) break;
                    StringCchCopyW(r->name, _countof(r->name), es[i].lpServiceName);
                    StringCchCopyW(r->disp, _countof(r->disp), es[i].lpDisplayName);
                    r->pid = es[i].ServiceStatusProcess.dwProcessId;
                    r->state = es[i].ServiceStatusProcess.dwCurrentState;

                    CacheGroup(scm, r->name);
                    const WCHAR* grp = CachedGroup(r->name);
                    if (grp)
                        StringCchCopyW(r->group, _countof(r->group), grp);

                    /* svchost service-name grouping */
                    if (r->pid && r->state == SERVICE_RUNNING)
                    {
                        SvcHostEntry* he = NULL;
                        for (int m = 0; m < s_svchostMap.n; m++)
                            if (s_svchostMap[m].pid == r->pid) { he = &s_svchostMap[m]; break; }
                        if (!he)
                        {
                            he = s_svchostMap.Add();
                            if (he)
                            {
                                he->pid = r->pid;
                                if (grp)
                                    StringCchCopyW(he->group, _countof(he->group), grp);
                            }
                        }
                        if (he)
                        {
                            if (he->names[0])
                                StringCchCatW(he->names, _countof(he->names), L", ");
                            StringCchCatW(he->names, _countof(he->names), es[i].lpDisplayName);
                        }
                    }
                }
            }
            HeapFree(GetProcessHeap(), 0, buf);
        }
    }
    CloseServiceHandle(scm);
}

BOOL SvcControl(const WCHAR* name, int op)
{
    SC_HANDLE scm = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
    if (!scm) return FALSE;
    DWORD access = (op == 1) ? SERVICE_START :
                   (op == 0) ? SERVICE_STOP :
                               (SERVICE_START | SERVICE_STOP | SERVICE_QUERY_STATUS);
    SC_HANDLE svc = OpenServiceW(scm, name, access | SERVICE_QUERY_STATUS);
    BOOL ok = FALSE;
    if (svc)
    {
        SERVICE_STATUS ss;
        if (op == 0 || op == 2)
        {
            ok = ControlService(svc, SERVICE_CONTROL_STOP, &ss);
            if (op == 2)
            {
                /* wait for stop (max ~5s) then start */
                for (int i = 0; i < 25; i++)
                {
                    if (!QueryServiceStatus(svc, &ss)) break;
                    if (ss.dwCurrentState == SERVICE_STOPPED) break;
                    Sleep(200);
                }
                ok = StartServiceW(svc, 0, NULL);
            }
        }
        else if (op == 1)
        {
            ok = StartServiceW(svc, 0, NULL);
        }
        CloseServiceHandle(svc);
    }
    CloseServiceHandle(scm);
    return ok;
}

/* ------------------------------------------------------------------ */
/*  Users                                                              */
/* ------------------------------------------------------------------ */

Vec<UserRow>& Users(void) { return s_users; }

static const WCHAR* WtsStateName(int st)
{
    switch (st)
    {
        case WTSActive: return L"Active";
        case WTSConnected: return L"Connected";
        case WTSDisconnected: return L"Disconnected";
        case WTSIdle: return L"Idle";
        case WTSListen: return L"Listen";
        default: return L"Down";
    }
}

void RefreshUsers(void)
{
    s_users.Clear();

    PWTS_SESSION_INFOW si = NULL;
    DWORD count = 0;
    BOOL wtsOk = WTSEnumerateSessionsW(WTS_CURRENT_SERVER_HANDLE, 0, 1, &si, &count);

    if (wtsOk && si)
    {
        for (DWORD i = 0; i < count; i++)
        {
            if (si[i].State == WTSListen) continue;

            /* pure service sessions are not "users" */
            if (si[i].pWinStationName &&
                lstrcmpiW(si[i].pWinStationName, L"Services") == 0)
                continue;

            WCHAR* user = NULL;
            DWORD cb = 0;
            WCHAR name[96] = L"";
            if (WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE, si[i].SessionId,
                                            WTSUserName, &user, &cb) && user)
            {
                StringCchCopyW(name, _countof(name), user);
                WTSFreeMemory(user);
            }

            UserRow* r = s_users.Add();
            if (!r) break;
            r->sessionId = si[i].SessionId;
            StringCchCopyW(r->user, _countof(r->user), name);
            StringCchCopyW(r->state, _countof(r->state), WtsStateName(si[i].State));
        }
        WTSFreeMemory(si);
    }

    /* wtsapi32 session enumeration is authoritative (implemented for the
       console session on ReactOS); resolve missing user names from the
       session's processes */
    for (int u = 0; u < s_users.n; u++)
    {
        if (s_users[u].user[0]) continue;
        for (int i = 0; i < g.procs.n; i++)
        {
            ProcRow& p = g.procs[i];
            if (p.sessionId == s_users[u].sessionId && p.x && p.x->resolved &&
                p.x->user[0] && (p.flags & PF_HASWINDOW))
            {
                StringCchCopyW(s_users[u].user, _countof(s_users[u].user),
                               p.x->user);
                break;
            }
        }
        if (!s_users[u].user[0])
        {
            DWORD cch2 = _countof(s_users[u].user);
            GetUserNameW(s_users[u].user, &cch2);
        }
    }

    /* aggregate live usage per session */
    for (int u = 0; u < s_users.n; u++)
    {
        s_users[u].cpuPct = 0;
        s_users[u].memBytes = 0;
        s_users[u].nProc = 0;
        for (int i = 0; i < g.procs.n; i++)
        {
            if (g.procs[i].pid == 0) continue;
            if (g.procs[i].sessionId == s_users[u].sessionId)
            {
                s_users[u].cpuPct += g.procs[i].cpuPct;
                s_users[u].memBytes += g.procs[i].memBytes;
                s_users[u].nProc++;
            }
        }
    }
}

BOOL UserDisconnect(DWORD session)
{
    return WTSDisconnectSession(WTS_CURRENT_SERVER_HANDLE, session, FALSE);
}

BOOL UserLogoff(DWORD session)
{
    /* not in this SDK's wtsapi32.h; resolve at runtime */
    typedef BOOL (WINAPI *PFN_WTSLogoffSession)(HANDLE, DWORD, BOOL);
    HMODULE wts = GetModuleHandleW(L"wtsapi32.dll");
    if (!wts) wts = LoadLibraryW(L"wtsapi32.dll");
    PFN_WTSLogoffSession pLogoff = wts ?
        (PFN_WTSLogoffSession)GetProcAddress(wts, "WTSLogoffSession") : NULL;
    if (pLogoff)
        return pLogoff(WTS_CURRENT_SERVER_HANDLE, session, FALSE);
    return FALSE;
}

/* ------------------------------------------------------------------ */
/*  Startup items                                                      */
/* ------------------------------------------------------------------ */

Vec<StartupRow>& StartupItems(void) { return s_startup; }

static const WCHAR* RUN_KEY = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const WCHAR* APPROVED_KEY = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run";
static const WCHAR* APPROVED_FOLDER_KEY = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\StartupFolder";

static BOOL ReadApproved(HKEY root, const WCHAR* key, const WCHAR* value, BOOL* enabled)
{
    HKEY hk;
    BOOL found = FALSE;
    if (RegOpenKeyExW(root, key, 0, KEY_QUERY_VALUE, &hk) == ERROR_SUCCESS)
    {
        BYTE data[32];
        DWORD cb = sizeof(data), type = 0;
        if (RegQueryValueExW(hk, value, NULL, &type, data, &cb) == ERROR_SUCCESS &&
            type == REG_BINARY && cb >= 1)
        {
            /* byte0: 0x02 = enabled, 0x03 (bit0) = disabled */
            *enabled = !(data[0] & 1);
            found = TRUE;
        }
        RegCloseKey(hk);
    }
    return found;
}

static void ExtractExePath(const WCHAR* cmd, WCHAR* out, int cch)
{
    out[0] = 0;
    WCHAR expanded[512];
    ExpandEnvironmentStringsW(cmd, expanded, _countof(expanded));
    const WCHAR* p = expanded;
    while (*p == L' ') p++;
    if (*p == L'"')
    {
        p++;
        int o = 0;
        while (*p && *p != L'"' && o < cch - 1) out[o++] = *p++;
        out[o] = 0;
    }
    else
    {
        /* up to first space that yields an existing file, else whole string */
        StringCchCopyW(out, cch, p);
        WCHAR* sp = out;
        while ((sp = wcschr(sp, L' ')) != NULL)
        {
            *sp = 0;
            if (GetFileAttributesW(out) != INVALID_FILE_ATTRIBUTES) return;
            *sp = L' ';
            sp++;
        }
    }
}

static void AddRunEntries(HKEY root, int source)
{
    HKEY hk;
    if (RegOpenKeyExW(root, RUN_KEY, 0, KEY_QUERY_VALUE, &hk) != ERROR_SUCCESS)
        return;
    for (DWORD i = 0;; i++)
    {
        WCHAR name[128];
        DWORD cn = _countof(name);
        WCHAR data[512];
        DWORD cb = sizeof(data), type;
        LONG rc = RegEnumValueW(hk, i, name, &cn, NULL, &type, (LPBYTE)data, &cb);
        if (rc != ERROR_SUCCESS) break;
        if (type != REG_SZ && type != REG_EXPAND_SZ) continue;

        StartupRow* r = s_startup.Add();
        if (!r) break;
        StringCchCopyW(r->name, _countof(r->name), name);
        StringCchCopyW(r->valueName, _countof(r->valueName), name);
        StringCchCopyW(r->command, _countof(r->command), data);
        r->source = source;
        r->enabled = TRUE;
        ReadApproved(root, APPROVED_KEY, name, &r->enabled);

        WCHAR exe[MAX_PATH];
        ExtractExePath(data, exe, _countof(exe));
        if (exe[0])
            GetFileCompany(exe, r->publisher, _countof(r->publisher));
    }
    RegCloseKey(hk);
}

static void AddFolderEntries(int csidl, int source)
{
    WCHAR dir[MAX_PATH];
    if (FAILED(SHGetFolderPathW(NULL, csidl, NULL, SHGFP_TYPE_CURRENT, dir)))
        return;
    WCHAR pattern[MAX_PATH];
    StringCchPrintfW(pattern, _countof(pattern), L"%s\\*", dir);

    WIN32_FIND_DATAW fd;
    HANDLE hf = FindFirstFileW(pattern, &fd);
    if (hf == INVALID_HANDLE_VALUE) return;
    do
    {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (lstrcmpiW(fd.cFileName, L"desktop.ini") == 0) continue;

        StartupRow* r = s_startup.Add();
        if (!r) break;
        StringCchCopyW(r->name, _countof(r->name), fd.cFileName);
        PathRemoveExtensionW(r->name);
        StringCchCopyW(r->valueName, _countof(r->valueName), fd.cFileName);
        StringCchPrintfW(r->command, _countof(r->command), L"%s\\%s", dir, fd.cFileName);
        r->source = source;
        r->enabled = TRUE;
        HKEY root = (source == SS_FOLDER_USER) ? HKEY_CURRENT_USER : HKEY_LOCAL_MACHINE;
        ReadApproved(root, APPROVED_FOLDER_KEY, fd.cFileName, &r->enabled);

        /* resolve .lnk target for publisher */
        WCHAR target[MAX_PATH] = L"";
        if (StrStrIW(fd.cFileName, L".lnk"))
        {
            IShellLinkW* sl = NULL;
            if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER,
                                           IID_IShellLinkW, (void**)&sl)))
            {
                IPersistFile* pf = NULL;
                if (SUCCEEDED(sl->QueryInterface(IID_IPersistFile, (void**)&pf)))
                {
                    if (SUCCEEDED(pf->Load(r->command, STGM_READ)))
                        sl->GetPath(target, _countof(target), NULL, SLGP_UNCPRIORITY);
                    pf->Release();
                }
                sl->Release();
            }
        }
        else
        {
            StringCchCopyW(target, _countof(target), r->command);
        }
        if (target[0])
            GetFileCompany(target, r->publisher, _countof(r->publisher));
    } while (FindNextFileW(hf, &fd));
    FindClose(hf);
}

void RefreshStartup(void)
{
    s_startup.Clear();
    AddRunEntries(HKEY_CURRENT_USER, SS_HKCU_RUN);
    AddRunEntries(HKEY_LOCAL_MACHINE, SS_HKLM_RUN);
    AddFolderEntries(CSIDL_STARTUP, SS_FOLDER_USER);
    AddFolderEntries(CSIDL_COMMON_STARTUP, SS_FOLDER_COMMON);
}

BOOL SetStartupEnabled(const StartupRow& it, BOOL enable)
{
    HKEY root = (it.source == SS_HKCU_RUN || it.source == SS_FOLDER_USER)
                ? HKEY_CURRENT_USER : HKEY_LOCAL_MACHINE;
    const WCHAR* key = (it.source == SS_HKCU_RUN || it.source == SS_HKLM_RUN)
                       ? APPROVED_KEY : APPROVED_FOLDER_KEY;
    HKEY hk;
    if (RegCreateKeyExW(root, key, 0, NULL, 0, KEY_SET_VALUE, NULL, &hk, NULL) != ERROR_SUCCESS)
        return FALSE;
    BYTE data[12];
    ZeroMemory(data, sizeof(data));
    data[0] = enable ? 0x02 : 0x03;
    if (!enable)
    {
        /* store disable timestamp like Windows does */
        FILETIME ft;
        GetSystemTimeAsFileTime(&ft);
        CopyMemory(&data[4], &ft, sizeof(ft));
    }
    LONG rc = RegSetValueExW(hk, it.valueName, 0, REG_BINARY, data, sizeof(data));
    RegCloseKey(hk);
    return rc == ERROR_SUCCESS;
}

/* ------------------------------------------------------------------ */
/*  App history                                                        */
/* ------------------------------------------------------------------ */

Vec<AppHistRow>& AppHistory(void) { return s_appHist; }
void ClearAppHistory(void) { s_appHist.Clear(); }

/* ------------------------------------------------------------------ */
/*  Actions                                                            */
/* ------------------------------------------------------------------ */

static BOOL KillWorker(ULONG pid, BOOL tree, int depth)
{
    if (tree && depth < 16)
    {
        /* terminate children first; depth cap guards ppid cycles from pid reuse */
        for (int i = 0; i < g.procs.n; i++)
        {
            ProcRow& c = g.procs[i];
            if (c.ppid == pid && c.pid != pid && c.pid > 4)
                KillWorker(c.pid, TRUE, depth + 1);
        }
    }

    HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
    if (!h) return FALSE;
    BOOL ok = TerminateProcess(h, 1);
    CloseHandle(h);
    return ok;
}

BOOL KillProcess(ULONG pid, BOOL tree)
{
    return KillWorker(pid, tree, 0);
}

BOOL SetEfficiency(ULONG pid, BOOL on)
{
    HANDLE h = OpenProcess(PROCESS_SET_INFORMATION | PROCESS_QUERY_LIMITED_INFORMATION,
                           FALSE, pid);
    if (!h)
        h = OpenProcess(PROCESS_SET_INFORMATION, FALSE, pid);
    if (!h) return FALSE;

    /* EcoQoS if this kernel knows it (NT10 parity); ignore failure */
    TM_POWER_THROTTLING_STATE pts;
    pts.Version = TM_POWER_THROTTLING_VERSION;
    pts.ControlMask = TM_POWER_THROTTLING_EXECUTION_SPEED;
    pts.StateMask = on ? TM_POWER_THROTTLING_EXECUTION_SPEED : 0;
    NtSetInformationProcess(h, TM_ProcessPowerThrottlingState, &pts, sizeof(pts));

    BOOL ok = SetPriorityClass(h, on ? IDLE_PRIORITY_CLASS : NORMAL_PRIORITY_CLASS);
    CloseHandle(h);

    if (ok)
    {
        ProcRow* p = FindProc(pid);
        LONGLONG create = p ? p->createTime : 0;
        if (on)
        {
            EffMark m = { pid, create };
            if (!IsEffMarked(pid, create)) s_effSet.Push(m);
        }
        else
        {
            for (int i = s_effSet.n - 1; i >= 0; i--)
                if (s_effSet[i].pid == pid) s_effSet.RemoveAt(i);
        }
        if (p)
        {
            if (on) p->flags |= PF_EFFICIENCY;
            else    p->flags &= ~PF_EFFICIENCY;
        }
    }
    return ok;
}

BOOL IsEfficiency(const ProcRow& p)
{
    return (p.flags & PF_EFFICIENCY) != 0;
}

DWORD GetPriClass(ULONG pid)
{
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) h = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);
    DWORD cls = 0;
    if (h)
    {
        cls = GetPriorityClass(h);
        CloseHandle(h);
    }
    if (!cls)
    {
        /* infer from base priority */
        ProcRow* p = FindProc(pid);
        if (p)
        {
            switch (p->basePri)
            {
                case 4:  cls = IDLE_PRIORITY_CLASS; break;
                case 6:  cls = BELOW_NORMAL_PRIORITY_CLASS; break;
                case 10: cls = ABOVE_NORMAL_PRIORITY_CLASS; break;
                case 13: cls = HIGH_PRIORITY_CLASS; break;
                case 24: cls = REALTIME_PRIORITY_CLASS; break;
                default: cls = NORMAL_PRIORITY_CLASS; break;
            }
        }
    }
    return cls;
}

BOOL SetPriClass(ULONG pid, DWORD cls)
{
    HANDLE h = OpenProcess(PROCESS_SET_INFORMATION, FALSE, pid);
    if (!h) return FALSE;
    BOOL ok = SetPriorityClass(h, cls);
    CloseHandle(h);
    return ok;
}

BOOL GetAffinity(ULONG pid, DWORD_PTR* mask, DWORD_PTR* sysMask)
{
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) h = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (!h) return FALSE;
    BOOL ok = GetProcessAffinityMask(h, mask, sysMask);
    CloseHandle(h);
    return ok;
}

BOOL SetAffinity(ULONG pid, DWORD_PTR mask)
{
    HANDLE h = OpenProcess(PROCESS_SET_INFORMATION, FALSE, pid);
    if (!h) return FALSE;
    BOOL ok = SetProcessAffinityMask(h, mask);
    CloseHandle(h);
    return ok;
}

BOOL OpenFileLocation(const ProcRow& p)
{
    if (!p.x || !p.x->path[0]) return FALSE;

    WCHAR dir[MAX_PATH];
    StringCchCopyW(dir, _countof(dir), p.x->path);
    PathRemoveFileSpecW(dir);

    /* try select-in-folder via explorer, fallback: open the folder */
    WCHAR params[MAX_PATH + 16];
    StringCchPrintfW(params, _countof(params), L"/select,\"%s\"", p.x->path);
    HINSTANCE hi = ShellExecuteW(NULL, NULL, L"explorer.exe", params, NULL, SW_SHOWNORMAL);
    if ((UINT_PTR)hi > 32) return TRUE;

    return (UINT_PTR)ShellExecuteW(NULL, NULL, dir, NULL, NULL, SW_SHOWNORMAL) > 32;
}

void SearchOnline(const WCHAR* term)
{
    WCHAR url[512];
    StringCchPrintfW(url, _countof(url), L"https://www.bing.com/search?q=%s", term);
    /* crude escaping: spaces only */
    for (WCHAR* c = url; *c; c++)
        if (*c == L' ') *c = L'+';
    ShellExecuteW(NULL, L"open", url, NULL, NULL, SW_SHOWNORMAL);
}

BOOL ShowFileProperties(const ProcRow& p)
{
    if (!p.x || !p.x->path[0]) return FALSE;
    SHELLEXECUTEINFOW sei;
    ZeroMemory(&sei, sizeof(sei));
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_INVOKEIDLIST;
    sei.hwnd = g_app.hFrame;
    sei.lpVerb = L"properties";
    sei.lpFile = p.x->path;
    sei.nShow = SW_SHOW;
    return ShellExecuteExW(&sei);
}

BOOL RunTask(const WCHAR* cmd, BOOL admin)
{
    WCHAR expanded[1024];
    ExpandEnvironmentStringsW(cmd, expanded, _countof(expanded));

    /* split first token (quoted or not) + args */
    WCHAR file[MAX_PATH];
    const WCHAR* args = NULL;
    const WCHAR* p = expanded;
    while (*p == L' ') p++;
    if (*p == L'"')
    {
        p++;
        int o = 0;
        while (*p && *p != L'"' && o < MAX_PATH - 1) file[o++] = *p++;
        file[o] = 0;
        if (*p == L'"') p++;
    }
    else
    {
        int o = 0;
        while (*p && *p != L' ' && o < MAX_PATH - 1) file[o++] = *p++;
        file[o] = 0;
    }
    while (*p == L' ') p++;
    if (*p) args = p;

    HINSTANCE hi = ShellExecuteW(NULL, admin ? L"runas" : NULL,
                                 file, args, NULL, SW_SHOWNORMAL);
    if ((UINT_PTR)hi > 32) return TRUE;

    /* last resort: raw CreateProcess on the whole line */
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    WCHAR line[1024];
    StringCchCopyW(line, _countof(line), expanded);
    if (CreateProcessW(NULL, line, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
    {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return TRUE;
    }
    return FALSE;
}

} /* namespace Data */
