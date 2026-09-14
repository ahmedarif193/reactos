/*
 * PROJECT:     ReactOS Control Panel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Control Panel item catalog: shell namespace enumeration,
 *              canonical names, categories and task links
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#include "control11.h"

CPITEM g_Items[CP_MAX_ITEMS];
int g_cItems;
CPTASK g_Tasks[CP_MAX_TASKS];
int g_cTasks;
IShellFolder2 *g_pControlsFolder;

static const PROPERTYKEY PKEY_ControlPanel_Category = { { 0x305CA226, 0xD286, 0x468E, { 0xB8, 0x48, 0x2B, 0x2E, 0x8E, 0x69, 0x7B, 0x74 } }, 2 };
static const GUID CLSID_NetworkAndSharingCenter = { 0x8E908FC9, 0xBECC, 0x40F6, { 0x91, 0x5B, 0xF4, 0xCA, 0x0E, 0x70, 0xD0, 0x3D } };

CPCATEGORY g_Categories[9] =
{
    { 0, IDI_CAT_ALL,        L"All Control Panel Items",       L"" },
    { 1, IDI_CAT_APPEARANCE, L"Appearance and Personalization", L"Change the appearance of desktop items, apply a theme or screen saver to your computer, or customize the Start menu and taskbar." },
    { 2, IDI_CAT_HARDWARE,   L"Hardware and Sound",            L"Add or remove printers and other hardware, change system sounds, play CDs automatically, conserve power, update device drivers, and more." },
    { 3, IDI_CAT_NETWORK,    L"Network and Internet",          L"Check network status and change settings, set preferences for sharing files and computers, configure Internet display and connection, and more." },
    { 5, IDI_CAT_SYSTEM,     L"System and Security",           L"View and change system and security status, back up and restore file and system settings, update your computer, view RAM and processor speed, check firewall, and more." },
    { 6, IDI_CAT_CLOCK,      L"Clock, Language, and Region",   L"Change the date, time, and time zone for your computer, the language to use, and the way numbers, currencies, dates, and times are displayed." },
    { 7, IDI_CAT_ACCESS,     L"Ease of Access",                L"Adjust your computer settings for vision, hearing, and mobility, and use speech recognition to control your computer with voice commands." },
    { 8, IDI_CAT_PROGRAMS,   L"Programs",                      L"Uninstall programs or Windows features, uninstall gadgets, get new programs from the network or online, and more." },
    { 9, IDI_CAT_USERS,      L"User Accounts and Family Safety", L"Change user account settings and passwords, and set up parental controls." },
};

typedef struct _CPBUILTIN
{
    LPCWSTR pszCanonical;
    LPCWSTR pszName;
    LPCWSTR pszGuid;
    LPCWSTR pszCategories;
    int hub;
    LPCWSTR pszInfoTip;
} CPBUILTIN;

static const CPBUILTIN s_Builtin[] =
{
    { L"Microsoft.ActionCenter",              L"Action Center",                   L"{BB64F8A7-BEE7-4E1A-AB8D-7D8273F7FDB6}", L"5",    HUB_NONE, L"Review recent messages and resolve problems with your computer." },
    { L"Microsoft.AdministrativeTools",       L"Administrative Tools",            L"{D20EA4E1-3957-11D2-A40B-0C5020524153}", L"5",    HUB_NONE, L"Configure administrative settings for your computer." },
    { L"Microsoft.AutoPlay",                  L"AutoPlay",                        L"{9C60DE1E-E5FC-40F4-A487-460851A8D915}", L"2",    HUB_NONE, L"Change default settings for CDs, DVDs, and devices so that you can automatically play music, view pictures, install software, and play games." },
    { L"Microsoft.BackupAndRestore",          L"Backup and Restore",              L"{B98A2BEA-7D42-4558-8BD1-832F41BAC6FD}", L"5",    HUB_NONE, L"Back up your files and system, or restore them from a previous backup." },
    { L"Microsoft.BiometricDevices",          L"Biometric Devices",               L"{0142E4D0-FB7A-11DC-BA4A-000FFE7AB428}", L"2",    HUB_NONE, L"Manage biometric devices." },
    { L"Microsoft.BitLockerDriveEncryption",  L"BitLocker Drive Encryption",      L"{D9EF8727-CAC2-4E60-809E-86F80A666C91}", L"5",    HUB_NONE, L"Protect your computer by encrypting data on your disk." },
    { L"Microsoft.ColorManagement",           L"Color Management",                L"{B2C761C6-29BC-4F19-9251-E6195265BAF1}", L"1",    HUB_NONE, L"Change advanced color management settings for displays, scanners, and printers." },
    { L"Microsoft.CredentialManager",         L"Credential Manager",              L"{1206F5F1-0569-412C-8FEC-3204630DFB70}", L"9",    HUB_NONE, L"Manage your Windows credentials." },
    { L"Microsoft.DateAndTime",               L"Date and Time",                   L"{E2E7934B-DCE5-43C4-9576-7FE4F75E7480}", L"6",    HUB_NONE, L"Set the date, time, and time zone for your computer." },
    { L"Microsoft.DefaultPrograms",           L"Default Programs",                L"{17CD9488-1228-4B2F-88CE-4298E93E0966}", L"8",    HUB_NONE, L"Choose the programs that Windows uses by default." },
    { L"Microsoft.DeviceManager",             L"Device Manager",                  L"{74246BFC-4C96-11D0-ABEF-0020AF6B0B7A}", L"2,5",  HUB_NONE, L"View and update your hardware's settings and driver software." },
    { L"Microsoft.DevicesAndPrinters",        L"Devices and Printers",            L"{A8A91A66-3A7D-4424-8D24-04E180695C7A}", L"2",    HUB_NONE, L"View and manage devices, printers, and print jobs." },
    { L"Microsoft.Display",                   L"Display",                         L"{C555438B-3C23-4769-A71F-B6D3D9B6053A}", L"1,2",  HUB_NONE, L"Make text and other items larger or smaller, and adjust screen resolution." },
    { L"Microsoft.EaseOfAccessCenter",        L"Ease of Access Center",           L"{D555645E-D4F8-4C29-A827-D93C859C4F2A}", L"7,1",  HUB_NONE, L"Make your computer easier to use." },
    { L"Microsoft.FolderOptions",             L"Folder Options",                  L"{6DFD7C5C-2451-11D3-A299-00C04F8EF6AF}", L"1",    HUB_NONE, L"Customize the display of files and folders." },
    { L"Microsoft.Fonts",                     L"Fonts",                           L"{D20EA4E1-3957-11D2-A40B-0C5020524152}", L"1",    HUB_NONE, L"Add, change, and manage fonts on your computer." },
    { L"Microsoft.GettingStarted",            L"Getting Started",                 L"{CB1B7F8C-C50A-4176-B604-9E24DEE8D4D1}", L"0",    HUB_NONE, L"Learn about the features of ReactOS." },
    { L"Microsoft.HomeGroup",                 L"HomeGroup",                       L"{67CA7650-96E6-4FDD-BB43-A8E774F73A57}", L"3",    HUB_NONE, L"Share libraries and printers with other computers on your home network." },
    { L"Microsoft.IndexingOptions",           L"Indexing Options",                L"{87D66A43-7B11-4A28-9811-C86EE395ACF7}", L"5",    HUB_NONE, L"Change how Windows indexes to search faster." },
    { L"Microsoft.InternetOptions",           L"Internet Options",                L"{A3DD4F92-658A-410F-84FD-6FBBBEF2FFFE}", L"3",    HUB_NONE, L"Configure your Internet display and connection settings." },
    { L"Microsoft.Keyboard",                  L"Keyboard",                        L"{725BE8F7-668E-4C7B-8F90-46BDB0936430}", L"2",    HUB_NONE, L"Customize your keyboard settings, such as the cursor blink rate and the character repeat rate." },
    { L"Microsoft.LocationAndOtherSensors",   L"Location and Other Sensors",      L"{E9950154-C418-419E-A90A-20C5287AE24B}", L"2",    HUB_NONE, L"Enable location and other sensors." },
    { L"Microsoft.Mouse",                     L"Mouse",                           L"{6C8EEC18-8D75-41B2-A177-8831D59D2D50}", L"2",    HUB_NONE, L"Customize your mouse settings, such as the button configuration, double-click speed, mouse pointers, and motion speed." },
    { L"Microsoft.NotificationAreaIcons",     L"Notification Area Icons",         L"{05D7B0F4-2121-4EFF-BF6B-ED3F69B894D9}", L"1",    HUB_NONE, L"Customize which icons and notifications appear on the taskbar." },
    { L"Microsoft.ParentalControls",          L"Parental Controls",               L"{96AE8D84-A250-4520-95A5-A47A7E3C548B}", L"9",    HUB_NONE, L"Set up parental controls for any user." },
    { L"Microsoft.PenAndTouch",               L"Pen and Touch",                   L"{F82DF8F7-8B9F-442E-A48C-818EA735FF9B}", L"2",    HUB_NONE, L"Adjust settings for pen and touch input." },
    { L"Microsoft.PerformanceInformationAndTools", L"Performance Information and Tools", L"{78F3955E-3B90-4184-BD14-5397C15F1EFC}", L"5", HUB_NONE, L"Rate and improve your computer's performance." },
    { L"Microsoft.Personalization",           L"Personalization",                 L"{ED834ED6-4B5A-4BFE-8F11-A626DCB6A921}", L"1",    HUB_PERSONALIZATION, L"Change the visuals and sounds on your computer." },
    { L"Microsoft.PhoneAndModem",             L"Phone and Modem",                 L"{40419485-C444-4567-851A-2DD7BFA1684D}", L"2",    HUB_NONE, L"Configure your telephone dialing rules and modem settings." },
    { L"Microsoft.PowerOptions",              L"Power Options",                   L"{025A5937-A6BE-4686-A844-36FE4BEC8B6D}", L"2,5",  HUB_POWER, L"Manage battery and change power-saving settings for your computer." },
    { L"Microsoft.Printers",                  L"Printers",                        L"{2227A280-3AEA-1069-A2DE-08002B30309D}", L"2",    HUB_NONE, L"Shows installed printers and fax printers and helps you add new ones." },
    { L"Microsoft.ProgramsAndFeatures",       L"Programs and Features",           L"{7B81BE6A-CE2B-4676-A29E-EB907A5126C5}", L"8",    HUB_NONE, L"Uninstall or change programs on your computer." },
    { L"Microsoft.Recovery",                  L"Recovery",                        L"{9FE63AFD-59CF-4419-9775-ABCC3849F861}", L"5",    HUB_NONE, L"Restore your computer to an earlier point in time." },
    { L"Microsoft.RegionAndLanguage",         L"Region and Language",             L"{62D8ED13-C9D0-4CE8-A914-47DD628FB1B0}", L"6",    HUB_NONE, L"Customize the language, number format, time, and date settings for your computer." },
    { L"Microsoft.RemoteAppAndDesktopConnections", L"RemoteApp and Desktop Connections", L"{241D7C96-F8BF-4F85-B01F-E2B043341A4B}", L"3", HUB_NONE, L"Access programs and desktops on your workplace network." },
    { L"Microsoft.Sound",                     L"Sound",                           L"{F2DDFC82-8F12-4CDD-B7DC-D4FE1425AA4D}", L"2",    HUB_NONE, L"Configure your audio devices or change the sound scheme for your computer." },
    { L"Microsoft.SpeechRecognition",         L"Speech Recognition",              L"{58E3C745-D971-4081-9034-86E34B30836A}", L"7",    HUB_NONE, L"Set up your computer for speech recognition." },
    { L"Microsoft.SyncCenter",                L"Sync Center",                     L"{9C73F5E5-7AE7-4E32-A8E8-8D23B85255BF}", L"3",    HUB_NONE, L"Sync files between your computer and network folders." },
    { L"Microsoft.System",                    L"System",                          L"{BB06C0E4-D293-4F75-8A90-CB05B6477EEE}", L"5",    HUB_SYSTEM, L"See information about your computer, and change settings for hardware, performance, and remote connections." },
    { L"Microsoft.TabletPCSettings",          L"Tablet PC Settings",              L"{80F3F1D5-FECA-45F3-BC32-752C152E456E}", L"2",    HUB_NONE, L"Configure your tablet pen and screen settings." },
    { L"Microsoft.TaskbarAndStartMenu",       L"Taskbar and Start Menu",          L"{0DF44EAA-FF21-4412-828E-260A8728E7F1}", L"1",    HUB_NONE, L"Customize the Start menu and the taskbar, such as the types of items to be displayed and how they should appear." },
    { L"Microsoft.Troubleshooting",           L"Troubleshooting",                 L"{C58C4893-3BE0-4B45-ABB5-A63E4B8C8651}", L"5",    HUB_NONE, L"Troubleshoot and fix common computer problems." },
    { L"Microsoft.UserAccounts",              L"User Accounts",                   L"{60632754-C523-4B62-B45C-4172DA012619}", L"9",    HUB_NONE, L"Change user account settings and passwords." },
    { L"Microsoft.WindowsDefender",           L"Windows Defender",                L"{D8559EB9-20C0-410E-BEDA-7ED416AECC2A}", L"5",    HUB_NONE, L"Scan your computer for malware." },
    { L"Microsoft.WindowsFirewall",           L"Windows Firewall",                L"{4026492F-2F69-46B8-B9BF-5654FC07E423}", L"5",    HUB_NONE, L"Set firewall security options to protect your computer from hackers and malicious software." },
    { L"Microsoft.MobilityCenter",            L"Windows Mobility Center",         L"{5EA4F148-308C-46D7-98A9-49041B1DD468}", L"2",    HUB_NONE, L"Adjust commonly used mobility settings." },
    { L"Microsoft.WindowsUpdate",             L"Windows Update",                  L"{36EEF7DB-88AD-4E81-AD49-0E313F0C35F8}", L"5",    HUB_NONE, L"Deliver software updates and drivers, and set automatic updating options." },
    { L"Microsoft.AddHardware",               L"Add Hardware",                    L"{7A979262-40CE-46FF-AEEE-7884AC3B6136}", L"2",    HUB_NONE, L"Installs and troubleshoots hardware." },
    { L"Microsoft.NetworkConnections",        L"Network Connections",             L"{7007ACC7-3202-11D1-AAD2-00805FC1270E}", L"3",    HUB_NONE, L"Connects to other computers, networks, and the Internet." },
    { L"Microsoft.ScannersAndCameras",        L"Scanners and Cameras",            L"{E211B736-43FD-11D1-9EFB-0000F8757FCD}", L"2",    HUB_NONE, L"Add, remove, and configure scanners and cameras." },
    { L"Microsoft.ScheduledTasks",            L"Scheduled Tasks",                 L"{D6277990-4C6A-11CF-8D87-00AA0060F5BF}", L"5",    HUB_NONE, L"Schedule computer tasks to run automatically." },
};

int S(int px);

static const CPBUILTIN *FindBuiltinByGuid(REFGUID guid)
{
    for (UINT i = 0; i < _countof(s_Builtin); i++)
    {
        GUID g;
        if (SUCCEEDED(CLSIDFromString(s_Builtin[i].pszGuid, &g)) && IsEqualGUID(g, guid))
            return &s_Builtin[i];
    }
    return NULL;
}

static const CPBUILTIN *FindBuiltinByCanonical(LPCWSTR psz)
{
    for (UINT i = 0; i < _countof(s_Builtin); i++)
        if (!lstrcmpiW(s_Builtin[i].pszCanonical, psz))
            return &s_Builtin[i];
    return NULL;
}

static void ParseCategories(LPCWSTR psz, int *pCats, int *pcCats)
{
    *pcCats = 0;
    while (psz && *psz && *pcCats < CP_MAX_CATS)
    {
        while (*psz == L' ' || *psz == L',') psz++;
        if (!*psz) break;
        int neg = 0;
        if (*psz == L'-') { neg = 1; psz++; }
        int v = 0;
        while (*psz >= L'0' && *psz <= L'9') { v = v * 10 + (*psz - L'0'); psz++; }
        if (!neg)
        {
            if (v == 4 || v == 10) v = (v == 4) ? 2 : 5;
            if (v == 11) v = 0;
            pCats[(*pcCats)++] = v;
        }
        while (*psz && *psz != L',') psz++;
    }
}

int Catalog_CategoryIndex(int id)
{
    for (int i = 0; i < (int)_countof(g_Categories); i++)
        if (g_Categories[i].id == id) return i;
    return -1;
}

BOOL Catalog_ItemInCategory(const CPITEM *pItem, int cat)
{
    if (cat == 0) return TRUE;
    for (int i = 0; i < pItem->cCategories; i++)
        if (pItem->categories[i] == cat) return TRUE;
    return FALSE;
}

BOOL Catalog_TaskInCategory(const CPTASK *pTask, int cat)
{
    for (int i = 0; i < pTask->cCategories; i++)
        if (pTask->categories[i] == cat) return TRUE;
    return FALSE;
}

CPITEM *Catalog_FindByCanonical(LPCWSTR pszCanonical)
{
    if (!pszCanonical || !*pszCanonical) return NULL;
    for (int i = 0; i < g_cItems; i++)
        if (!lstrcmpiW(g_Items[i].szCanonical, pszCanonical)) return &g_Items[i];
    return NULL;
}

CPITEM *Catalog_FindByGuid(REFGUID guid)
{
    for (int i = 0; i < g_cItems; i++)
        if (g_Items[i].bHasGuid && IsEqualGUID(g_Items[i].guid, guid)) return &g_Items[i];
    return NULL;
}

CPTASK *Catalog_FindTask(REFGUID id)
{
    for (int i = 0; i < g_cTasks; i++)
        if (IsEqualGUID(g_Tasks[i].id, id)) return &g_Tasks[i];
    return NULL;
}

static BOOL ReadDetailString(IShellFolder2 *psf, PCUITEMID_CHILD pidl, const PROPERTYKEY *pkey, LPWSTR pszOut, int cch)
{
    VARIANT v;
    VariantInit(&v);
    pszOut[0] = 0;
    if (FAILED(psf->GetDetailsEx(pidl, (const SHCOLUMNID *)pkey, &v)))
        return FALSE;
    BOOL ok = FALSE;
    if (v.vt == VT_BSTR && v.bstrVal)
    {
        StringCchCopyW(pszOut, cch, v.bstrVal);
        ok = TRUE;
    }
    else if (v.vt == VT_UI4 || v.vt == VT_I4)
    {
        StringCchPrintfW(pszOut, cch, L"%d", (int)v.lVal);
        ok = TRUE;
    }
    VariantClear(&v);
    return ok;
}

static void ExtractItemIcons(IShellFolder2 *psf, CPITEM *pItem)
{
    IExtractIconW *pxi = NULL;
    PCUITEMID_CHILD apidl[1] = { pItem->pidl };
    if (FAILED(psf->GetUIObjectOf(NULL, 1, apidl, IID_IExtractIconW, NULL, (void **)&pxi)) || !pxi)
        return;
    WCHAR szPath[MAX_PATH] = L"";
    int idx = 0;
    UINT flags = 0;
    if (SUCCEEDED(pxi->GetIconLocation(0, szPath, _countof(szPath), &idx, &flags)))
    {
        HICON hL = NULL, hS = NULL;
        if (SUCCEEDED(pxi->Extract(szPath, idx, &hL, &hS, MAKELONG(S(48), S(16)))) && hL)
        {
            pItem->hIcon48 = hL;
            pItem->hIcon16 = hS;
        }
        else
        {
            hL = hS = NULL;
            WCHAR szExp[MAX_PATH];
            ExpandEnvironmentStringsW(szPath, szExp, _countof(szExp));
            PrivateExtractIconsW(szExp, idx, S(48), S(48), &hL, NULL, 1, 0);
            PrivateExtractIconsW(szExp, idx, S(16), S(16), &hS, NULL, 1, 0);
            pItem->hIcon48 = hL;
            pItem->hIcon16 = hS;
        }
        hL = NULL;
        if (FAILED(pxi->Extract(szPath, idx, &hL, NULL, MAKELONG(S(32), 0))) || !hL)
        {
            WCHAR szExp[MAX_PATH];
            ExpandEnvironmentStringsW(szPath, szExp, _countof(szExp));
            PrivateExtractIconsW(szExp, idx, S(32), S(32), &hL, NULL, 1, 0);
        }
        pItem->hIcon32 = hL;
    }
    pxi->Release();
}

static void ReadInfoTip(IShellFolder2 *psf, CPITEM *pItem)
{
    IQueryInfo *pqi = NULL;
    PCUITEMID_CHILD apidl[1] = { pItem->pidl };
    if (SUCCEEDED(psf->GetUIObjectOf(NULL, 1, apidl, IID_IQueryInfo, NULL, (void **)&pqi)) && pqi)
    {
        LPWSTR pszTip = NULL;
        if (SUCCEEDED(pqi->GetInfoTip(0, &pszTip)) && pszTip)
        {
            StringCchCopyW(pItem->szInfoTip, _countof(pItem->szInfoTip), pszTip);
            CoTaskMemFree(pszTip);
        }
        pqi->Release();
    }
    if (!pItem->szInfoTip[0])
    {
        SHELLDETAILS sd = { 0 };
        if (SUCCEEDED(psf->GetDetailsOf(pItem->pidl, 1, &sd)))
        {
            StrRetToBufW(&sd.str, pItem->pidl, pItem->szInfoTip, _countof(pItem->szInfoTip));
        }
    }
}

static BOOL PolicyHidesItem(const CPITEM *pItem)
{
    static const LPCWSTR s_Roots[] = { L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer" };
    for (int r = 0; r < 2; r++)
    {
        HKEY hRoot = r ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;
        DWORD dw = 0, cb = sizeof(dw);
        if (SHGetValueW(hRoot, s_Roots[0], L"DisallowCpl", NULL, &dw, &cb) == ERROR_SUCCESS && dw)
        {
            WCHAR szKey[MAX_PATH];
            StringCchPrintfW(szKey, _countof(szKey), L"%s\\DisallowCpl", s_Roots[0]);
            HKEY hk;
            if (RegOpenKeyExW(hRoot, szKey, 0, KEY_READ, &hk) == ERROR_SUCCESS)
            {
                for (DWORD i = 0; ; i++)
                {
                    WCHAR szName[64], szVal[256];
                    DWORD cchName = _countof(szName), cbVal = sizeof(szVal), type;
                    if (RegEnumValueW(hk, i, szName, &cchName, NULL, &type, (LPBYTE)szVal, &cbVal) != ERROR_SUCCESS) break;
                    if (type == REG_SZ && (!lstrcmpiW(szVal, pItem->szCanonical) || !lstrcmpiW(szVal, pItem->szName)))
                    { RegCloseKey(hk); return TRUE; }
                }
                RegCloseKey(hk);
            }
        }
        dw = 0; cb = sizeof(dw);
        if (SHGetValueW(hRoot, s_Roots[0], L"RestrictCpl", NULL, &dw, &cb) == ERROR_SUCCESS && dw)
        {
            WCHAR szKey[MAX_PATH];
            StringCchPrintfW(szKey, _countof(szKey), L"%s\\RestrictCpl", s_Roots[0]);
            HKEY hk;
            BOOL found = FALSE;
            if (RegOpenKeyExW(hRoot, szKey, 0, KEY_READ, &hk) == ERROR_SUCCESS)
            {
                for (DWORD i = 0; ; i++)
                {
                    WCHAR szName[64], szVal[256];
                    DWORD cchName = _countof(szName), cbVal = sizeof(szVal), type;
                    if (RegEnumValueW(hk, i, szName, &cchName, NULL, &type, (LPBYTE)szVal, &cbVal) != ERROR_SUCCESS) break;
                    if (type == REG_SZ && (!lstrcmpiW(szVal, pItem->szCanonical) || !lstrcmpiW(szVal, pItem->szName)))
                    { found = TRUE; break; }
                }
                RegCloseKey(hk);
            }
            if (!found) return TRUE;
        }
    }
    return FALSE;
}

static void AddShellItems(void)
{
    IShellFolder *pDesktop = NULL;
    PIDLIST_ABSOLUTE pidlCP = NULL;
    if (FAILED(SHGetDesktopFolder(&pDesktop)))
        return;
    if (FAILED(SHGetSpecialFolderLocation(NULL, CSIDL_CONTROLS, &pidlCP)))
    {
        pDesktop->Release();
        return;
    }
    pDesktop->BindToObject(pidlCP, NULL, IID_IShellFolder2, (void **)&g_pControlsFolder);
    pDesktop->Release();
    ILFree(pidlCP);
    if (!g_pControlsFolder)
        return;

    IEnumIDList *pEnum = NULL;
    if (FAILED(g_pControlsFolder->EnumObjects(NULL, SHCONTF_FOLDERS | SHCONTF_NONFOLDERS, &pEnum)) || !pEnum)
        return;

    PITEMID_CHILD pidl;
    while (g_cItems < CP_MAX_ITEMS && pEnum->Next(1, &pidl, NULL) == S_OK)
    {
        CPITEM *pItem = &g_Items[g_cItems];
        ZeroMemory(pItem, sizeof(*pItem));
        pItem->pidl = pidl;
        pItem->bEnabled = TRUE;

        STRRET sr;
        if (SUCCEEDED(g_pControlsFolder->GetDisplayNameOf(pidl, SHGDN_NORMAL | SHGDN_INFOLDER, &sr)))
            StrRetToBufW(&sr, pidl, pItem->szName, _countof(pItem->szName));

        WCHAR szParse[MAX_PATH] = L"";
        if (SUCCEEDED(g_pControlsFolder->GetDisplayNameOf(pidl, SHGDN_FORPARSING | SHGDN_INFOLDER, &sr)))
            StrRetToBufW(&sr, pidl, szParse, _countof(szParse));
        if (szParse[0] == L':' && szParse[1] == L':')
            pItem->bHasGuid = SUCCEEDED(CLSIDFromString(szParse + 2, &pItem->guid));

        ReadDetailString(g_pControlsFolder, pidl, &PKEY_ApplicationName, pItem->szCanonical, _countof(pItem->szCanonical));

        /* Do not revive the retired Control11-only Network and Sharing Center
           from a registry entry left behind by an older installation. */
        if (!lstrcmpiW(pItem->szCanonical, L"Microsoft.NetworkAndSharingCenter") ||
            (pItem->bHasGuid && IsEqualGUID(pItem->guid, CLSID_NetworkAndSharingCenter)))
        {
            ILFree(pidl);
            continue;
        }

        WCHAR szCats[64] = L"";
        ReadDetailString(g_pControlsFolder, pidl, &PKEY_ControlPanel_Category, szCats, _countof(szCats));

        const CPBUILTIN *pb = NULL;
        if (pItem->bHasGuid) pb = FindBuiltinByGuid(pItem->guid);
        if (!pb && pItem->szCanonical[0]) pb = FindBuiltinByCanonical(pItem->szCanonical);
        if (pb)
        {
            if (!pItem->szCanonical[0]) StringCchCopyW(pItem->szCanonical, _countof(pItem->szCanonical), pb->pszCanonical);
            if (!szCats[0]) StringCchCopyW(szCats, _countof(szCats), pb->pszCategories);
            if (!pItem->bHasGuid) pItem->bHasGuid = SUCCEEDED(CLSIDFromString(pb->pszGuid, &pItem->guid));
            pItem->hub = pb->hub;
        }
        ParseCategories(szCats, pItem->categories, &pItem->cCategories);

        ReadInfoTip(g_pControlsFolder, pItem);
        if (!pItem->szInfoTip[0] && pb)
            StringCchCopyW(pItem->szInfoTip, _countof(pItem->szInfoTip), pb->pszInfoTip);

        if (PolicyHidesItem(pItem))
        {
            ILFree(pidl);
            continue;
        }

        ExtractItemIcons(g_pControlsFolder, pItem);
        if (!pItem->hIcon48) pItem->hIcon48 = LoadFluent(IDI_ITEM_GENERIC, S(48));
        if (!pItem->hIcon32) pItem->hIcon32 = LoadFluent(IDI_ITEM_GENERIC, S(32));
        if (!pItem->hIcon16) pItem->hIcon16 = LoadFluent(IDI_ITEM_GENERIC, S(16));
        g_cItems++;
    }
    pEnum->Release();
}

static void AddPlaceholders(void)
{
    for (UINT i = 0; i < _countof(s_Builtin) && g_cItems < CP_MAX_ITEMS; i++)
    {
        GUID g;
        if (FAILED(CLSIDFromString(s_Builtin[i].pszGuid, &g))) continue;
        if (Catalog_FindByGuid(g) || Catalog_FindByCanonical(s_Builtin[i].pszCanonical)) continue;
        CPITEM *pItem = &g_Items[g_cItems++];
        ZeroMemory(pItem, sizeof(*pItem));
        StringCchCopyW(pItem->szCanonical, _countof(pItem->szCanonical), s_Builtin[i].pszCanonical);
        StringCchCopyW(pItem->szName, _countof(pItem->szName), s_Builtin[i].pszName);
        StringCchCopyW(pItem->szInfoTip, _countof(pItem->szInfoTip), s_Builtin[i].pszInfoTip);
        pItem->guid = g;
        pItem->bHasGuid = TRUE;
        pItem->hub = s_Builtin[i].hub;
        pItem->bEnabled = (s_Builtin[i].hub != HUB_NONE);
        ParseCategories(s_Builtin[i].pszCategories, pItem->categories, &pItem->cCategories);
        pItem->hIcon48 = LoadFluent(IDI_ITEM_GENERIC, S(48));
        pItem->hIcon32 = LoadFluent(IDI_ITEM_GENERIC, S(32));
        pItem->hIcon16 = LoadFluent(IDI_ITEM_GENERIC, S(16));
    }
}

static int CompareItems(const void *a, const void *b)
{
    return StrCmpLogicalW(((const CPITEM *)a)->szName, ((const CPITEM *)b)->szName);
}

/* Minimal XML scanning for the cpltasks schema */

static LPCWSTR XmlFind(LPCWSTR p, LPCWSTR pEnd, LPCWSTR tag)
{
    size_t n = wcslen(tag);
    while (p && p + n <= pEnd)
    {
        LPCWSTR q = wcsstr(p, tag);
        if (!q || q + n > pEnd) return NULL;
        return q;
    }
    return NULL;
}

static LPCWSTR XmlElementEnd(LPCWSTR pStart, LPCWSTR pEnd, LPCWSTR closeTag)
{
    LPCWSTR gt = wcschr(pStart, L'>');
    if (!gt || gt >= pEnd) return pEnd;
    if (gt > pStart && gt[-1] == L'/') return gt + 1;
    LPCWSTR c = XmlFind(gt, pEnd, closeTag);
    if (!c) return pEnd;
    return c + wcslen(closeTag);
}

static BOOL XmlAttr(LPCWSTR pTag, LPCWSTR name, LPWSTR pszOut, int cch)
{
    pszOut[0] = 0;
    LPCWSTR gt = wcschr(pTag, L'>');
    if (!gt) return FALSE;
    size_t n = wcslen(name);
    for (LPCWSTR p = pTag; p < gt; p++)
    {
        if (!wcsncmp(p, name, n) && (p == pTag || p[-1] == L' ') && p[n] == L'=' && p[n + 1] == L'"')
        {
            LPCWSTR v = p + n + 2;
            LPCWSTR e = wcschr(v, L'"');
            if (!e || e > gt) return FALSE;
            int len = (int)(e - v);
            if (len >= cch) len = cch - 1;
            wcsncpy(pszOut, v, len);
            pszOut[len] = 0;
            return TRUE;
        }
    }
    return FALSE;
}

static BOOL XmlText(LPCWSTR pStart, LPCWSTR pEnd, LPCWSTR openTag, LPCWSTR closeTag, LPWSTR pszOut, int cch)
{
    pszOut[0] = 0;
    LPCWSTR o = XmlFind(pStart, pEnd, openTag);
    if (!o) return FALSE;
    LPCWSTR gt = wcschr(o, L'>');
    if (!gt || gt >= pEnd) return FALSE;
    LPCWSTR c = XmlFind(gt, pEnd, closeTag);
    if (!c) return FALSE;
    int len = (int)(c - gt - 1);
    if (len >= cch) len = cch - 1;
    wcsncpy(pszOut, gt + 1, len);
    pszOut[len] = 0;
    StrTrimW(pszOut, L" \t\r\n");
    if (pszOut[0] == L'@')
    {
        WCHAR szTmp[512];
        if (SUCCEEDED(SHLoadIndirectString(pszOut, szTmp, _countof(szTmp), NULL)))
            StringCchCopyW(pszOut, cch, szTmp);
    }
    return TRUE;
}

static LPWSTR LoadXmlText(HMODULE hMod, UINT nId, LPCWSTR pszFile)
{
    const BYTE *pData = NULL;
    DWORD cb = 0;
    HANDLE hFile = INVALID_HANDLE_VALUE;
    BYTE *pFileBuf = NULL;
    if (pszFile)
    {
        hFile = CreateFileW(pszFile, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        if (hFile == INVALID_HANDLE_VALUE) return NULL;
        cb = GetFileSize(hFile, NULL);
        if (cb == INVALID_FILE_SIZE || cb > 4 * 1024 * 1024) { CloseHandle(hFile); return NULL; }
        pFileBuf = (BYTE *)LocalAlloc(LMEM_FIXED, cb + 2);
        DWORD rd = 0;
        if (!pFileBuf || !ReadFile(hFile, pFileBuf, cb, &rd, NULL)) { CloseHandle(hFile); LocalFree(pFileBuf); return NULL; }
        CloseHandle(hFile);
        pData = pFileBuf;
    }
    else
    {
        HRSRC hRes = FindResourceW(hMod, MAKEINTRESOURCEW(nId), L"XML");
        if (!hRes) return NULL;
        HGLOBAL hGlob = LoadResource(hMod, hRes);
        if (!hGlob) return NULL;
        pData = (const BYTE *)LockResource(hGlob);
        cb = SizeofResource(hMod, hRes);
    }
    LPWSTR pszOut = NULL;
    if (cb >= 2 && pData[0] == 0xFF && pData[1] == 0xFE)
    {
        pszOut = (LPWSTR)LocalAlloc(LMEM_FIXED, cb);
        if (pszOut) { memcpy(pszOut, pData + 2, cb - 2); pszOut[(cb - 2) / 2] = 0; }
    }
    else
    {
        int cch = MultiByteToWideChar(CP_UTF8, 0, (LPCSTR)pData, cb, NULL, 0);
        pszOut = (LPWSTR)LocalAlloc(LMEM_FIXED, (cch + 1) * sizeof(WCHAR));
        if (pszOut) { MultiByteToWideChar(CP_UTF8, 0, (LPCSTR)pData, cb, pszOut, cch); pszOut[cch] = 0; }
    }
    LocalFree(pFileBuf);
    return pszOut;
}

static void ParseApplicationTasks(LPCWSTR pApp, LPCWSTR pAppEnd, REFGUID appGuid)
{
    LPCWSTR p = pApp;
    for (;;)
    {
        LPCWSTR t = XmlFind(p, pAppEnd, L"<sh:task id=");
        if (!t) break;
        LPCWSTR tEnd = XmlElementEnd(t, pAppEnd, L"</sh:task>");
        if (g_cTasks < CP_MAX_TASKS)
        {
            CPTASK *pTask = &g_Tasks[g_cTasks];
            ZeroMemory(pTask, sizeof(*pTask));
            WCHAR szId[64];
            XmlAttr(t, L"id", szId, _countof(szId));
            if (SUCCEEDED(CLSIDFromString(szId, &pTask->id)))
            {
                pTask->app = appGuid;
                XmlText(t, tEnd, L"<sh:name", L"</sh:name>", pTask->szName, _countof(pTask->szName));
                LPCWSTR k = t;
                for (;;)
                {
                    WCHAR szKw[256];
                    LPCWSTR ko = XmlFind(k, tEnd, L"<sh:keywords");
                    if (!ko) break;
                    LPCWSTR kEnd = XmlElementEnd(ko, tEnd, L"</sh:keywords>");
                    if (XmlText(ko, kEnd, L"<sh:keywords", L"</sh:keywords>", szKw, _countof(szKw)))
                    {
                        StringCchCatW(pTask->szKeywords, _countof(pTask->szKeywords), szKw);
                        StringCchCatW(pTask->szKeywords, _countof(pTask->szKeywords), L";");
                    }
                    k = kEnd;
                }
                LPCWSTR cp = XmlFind(t, tEnd, L"<sh2:controlpanel");
                if (cp)
                {
                    XmlAttr(cp, L"name", pTask->szCanonical, _countof(pTask->szCanonical));
                    XmlAttr(cp, L"page", pTask->szPage, _countof(pTask->szPage));
                }
                XmlText(t, tEnd, L"<sh:command", L"</sh:command>", pTask->szCommand, _countof(pTask->szCommand));
                if (pTask->szName[0])
                    g_cTasks++;
            }
        }
        p = tEnd;
    }
    p = pApp;
    for (;;)
    {
        LPCWSTR c = XmlFind(p, pAppEnd, L"<category id=");
        if (!c) break;
        LPCWSTR cEnd = XmlElementEnd(c, pAppEnd, L"</category>");
        WCHAR szCat[16];
        XmlAttr(c, L"id", szCat, _countof(szCat));
        int cat = _wtoi(szCat);
        LPCWSTR r = c;
        for (;;)
        {
            LPCWSTR ref = XmlFind(r, cEnd, L"<sh:task idref=");
            if (!ref) break;
            WCHAR szRef[64];
            GUID g;
            XmlAttr(ref, L"idref", szRef, _countof(szRef));
            if (SUCCEEDED(CLSIDFromString(szRef, &g)))
            {
                CPTASK *pTask = Catalog_FindTask(g);
                if (pTask && pTask->cCategories < CP_MAX_CATS && !Catalog_TaskInCategory(pTask, cat))
                    pTask->categories[pTask->cCategories++] = cat;
            }
            r = XmlElementEnd(ref, cEnd, L"</sh:task>");
        }
        p = cEnd;
    }
}

static void ParseApplicationsXml(LPCWSTR pszXml)
{
    if (!pszXml) return;
    LPCWSTR pEnd = pszXml + wcslen(pszXml);
    LPCWSTR p = pszXml;
    for (;;)
    {
        LPCWSTR a = XmlFind(p, pEnd, L"<application id=");
        if (!a) break;
        LPCWSTR aEnd = XmlElementEnd(a, pEnd, L"</application>");
        WCHAR szId[64];
        GUID g;
        XmlAttr(a, L"id", szId, _countof(szId));
        if (SUCCEEDED(CLSIDFromString(szId, &g)))
            ParseApplicationTasks(a, aEnd, g);
        p = aEnd;
    }
}

static void ParseCategoriesXml(LPCWSTR pszXml)
{
    if (!pszXml) return;
    LPCWSTR pEnd = pszXml + wcslen(pszXml);
    LPCWSTR p = pszXml;
    for (;;)
    {
        LPCWSTR c = XmlFind(p, pEnd, L"<category id=");
        if (!c) break;
        LPCWSTR cEnd = XmlElementEnd(c, pEnd, L"</category>");
        WCHAR szCat[16];
        XmlAttr(c, L"id", szCat, _countof(szCat));
        int idx = Catalog_CategoryIndex(_wtoi(szCat));
        LPCWSTR r = c;
        for (;;)
        {
            LPCWSTR ref = XmlFind(r, cEnd, L"<sh:task idref=");
            if (!ref) break;
            WCHAR szRef[64];
            GUID g;
            XmlAttr(ref, L"idref", szRef, _countof(szRef));
            if (idx >= 0 && SUCCEEDED(CLSIDFromString(szRef, &g)) && g_Categories[idx].cHomeTasks < 8)
                g_Categories[idx].homeTasks[g_Categories[idx].cHomeTasks++] = g;
            r = XmlElementEnd(ref, cEnd, L"</sh:task>");
        }
        p = cEnd;
    }
}

static BOOL CommandExists(LPCWSTR pszCommand)
{
    WCHAR szExp[600], szExe[MAX_PATH];
    ExpandEnvironmentStringsW(pszCommand, szExp, _countof(szExp));
    LPCWSTR p = szExp;
    while (*p == L' ') p++;
    int i = 0;
    if (*p == L'"')
    {
        p++;
        while (*p && *p != L'"' && i < MAX_PATH - 1) szExe[i++] = *p++;
    }
    else
    {
        while (*p && *p != L' ' && i < MAX_PATH - 1) szExe[i++] = *p++;
    }
    szExe[i] = 0;
    if (!szExe[0]) return FALSE;
    if (!wcsncmp(szExe, L"shell:", 6)) return TRUE;
    if (PathFileExistsW(szExe)) return TRUE;
    WCHAR szFound[MAX_PATH];
    StringCchCopyW(szFound, _countof(szFound), szExe);
    return PathFindOnPathW(szFound, NULL);
}

static void LoadExternalTaskFiles(void)
{
    for (int i = 0; i < g_cItems; i++)
    {
        if (!g_Items[i].bHasGuid || !g_Items[i].pidl) continue;
        WCHAR szKey[128], szUrl[MAX_PATH], szGuid[64];
        StringFromGUID2(g_Items[i].guid, szGuid, _countof(szGuid));
        StringCchPrintfW(szKey, _countof(szKey), L"CLSID\\%s", szGuid);
        DWORD cb = sizeof(szUrl);
        if (SHGetValueW(HKEY_CLASSES_ROOT, szKey, L"System.Software.TasksFileUrl", NULL, szUrl, &cb) != ERROR_SUCCESS) continue;
        if (!lstrcmpiW(szUrl, L"Internal") || !szUrl[0]) continue;
        WCHAR szPath[MAX_PATH];
        ExpandEnvironmentStringsW(szUrl, szPath, _countof(szPath));
        LPWSTR pszXml = LoadXmlText(NULL, 0, szPath);
        if (pszXml)
        {
            ParseApplicationsXml(pszXml);
            LocalFree(pszXml);
        }
    }
}

static void ResolveTasks(void)
{
    for (int i = 0; i < g_cTasks; i++)
    {
        CPTASK *pTask = &g_Tasks[i];
        pTask->bEnabled = FALSE;
        if (pTask->szCanonical[0])
        {
            CPITEM *pItem = Catalog_FindByCanonical(pTask->szCanonical);
            pTask->bEnabled = pItem && pItem->bEnabled;
        }
        else if (pTask->szCommand[0])
        {
            pTask->bEnabled = CommandExists(pTask->szCommand);
        }
    }
}

void Catalog_Load(void)
{
    g_cItems = 0;
    g_cTasks = 0;
    AddShellItems();
    AddPlaceholders();
    qsort(g_Items, g_cItems, sizeof(g_Items[0]), CompareItems);

    HMODULE hMod = GetModuleHandleW(NULL);
    LPWSTR pszApps = LoadXmlText(hMod, IDR_CPTASKS_APPS, NULL);
    ParseApplicationsXml(pszApps);
    LocalFree(pszApps);
    LoadExternalTaskFiles();
    LPWSTR pszCats = LoadXmlText(hMod, IDR_CPTASKS_CATEGORIES, NULL);
    ParseCategoriesXml(pszCats);
    LocalFree(pszCats);
    ResolveTasks();
}

BOOL Catalog_MatchText(LPCWSTR pszHaystack, LPCWSTR pszQuery)
{
    if (!pszHaystack || !pszQuery || !*pszQuery) return FALSE;
    return StrStrIW(pszHaystack, pszQuery) != NULL;
}

BOOL Catalog_RunCommand(HWND hwnd, LPCWSTR pszCommand)
{
    WCHAR szExp[600], szExe[MAX_PATH];
    ExpandEnvironmentStringsW(pszCommand, szExp, _countof(szExp));
    LPWSTR p = szExp;
    while (*p == L' ') p++;
    int i = 0;
    if (*p == L'"')
    {
        p++;
        while (*p && *p != L'"' && i < MAX_PATH - 1) szExe[i++] = *p++;
        if (*p == L'"') p++;
    }
    else
    {
        while (*p && *p != L' ' && i < MAX_PATH - 1) szExe[i++] = *p++;
    }
    szExe[i] = 0;
    while (*p == L' ') p++;
    SHELLEXECUTEINFOW sei = { sizeof(sei) };
    sei.hwnd = hwnd;
    sei.fMask = SEE_MASK_FLAG_DDEWAIT | SEE_MASK_FLAG_NO_UI;
    sei.lpFile = szExe;
    sei.lpParameters = *p ? p : NULL;
    sei.nShow = SW_SHOWNORMAL;
    return ShellExecuteExW(&sei);
}

void Catalog_LaunchCpl(HWND hwnd, LPCWSTR pszCplAndArgs)
{
    WCHAR szParams[MAX_PATH + 64];
    StringCchPrintfW(szParams, _countof(szParams), L"shell32.dll,Control_RunDLL %s", pszCplAndArgs);
    ShellExecuteW(hwnd, NULL, L"rundll32.exe", szParams, NULL, SW_SHOWNORMAL);
}

static BOOL OpenByCanonical(HWND hwnd, LPCWSTR pszCanonical, LPCWSTR pszPage)
{
    IOpenControlPanel *pOCP = NULL;
    HRESULT hr = CoCreateInstance(CLSID_OpenControlPanel, NULL, CLSCTX_INPROC_SERVER, IID_IOpenControlPanel, (void **)&pOCP);
    if (FAILED(hr) || !pOCP) return FALSE;
    hr = pOCP->Open(pszCanonical, (pszPage && *pszPage) ? pszPage : NULL, NULL);
    pOCP->Release();
    return SUCCEEDED(hr);
}

static void OpenByPidl(HWND hwnd, CPITEM *pItem)
{
    if (!g_pControlsFolder || !pItem->pidl) return;
    IContextMenu *pcm = NULL;
    PCUITEMID_CHILD apidl[1] = { pItem->pidl };
    if (FAILED(g_pControlsFolder->GetUIObjectOf(hwnd, 1, apidl, IID_IContextMenu, NULL, (void **)&pcm)) || !pcm)
        return;
    HMENU hMenu = CreatePopupMenu();
    if (SUCCEEDED(pcm->QueryContextMenu(hMenu, 0, 1, 0x7fff, CMF_DEFAULTONLY)))
    {
        UINT idDef = GetMenuDefaultItem(hMenu, FALSE, 0);
        if (idDef == (UINT)-1) idDef = 1;
        CMINVOKECOMMANDINFO ici = { sizeof(ici) };
        ici.hwnd = hwnd;
        ici.lpVerb = MAKEINTRESOURCEA(idDef - 1);
        ici.nShow = SW_SHOWNORMAL;
        pcm->InvokeCommand(&ici);
    }
    DestroyMenu(hMenu);
    pcm->Release();
}

void Catalog_OpenItem(HWND hwnd, CPITEM *pItem, LPCWSTR pszPage)
{
    if (!pItem || !pItem->bEnabled) return;
    if (pItem->hub != HUB_NONE)
    {
        Frame_OpenHub(pItem->hub, pszPage);
        return;
    }
    if (pItem->szCanonical[0] && OpenByCanonical(hwnd, pItem->szCanonical, pszPage))
        return;
    OpenByPidl(hwnd, pItem);
}

void Catalog_RunTask(HWND hwnd, CPTASK *pTask)
{
    if (!pTask || !pTask->bEnabled) return;
    if (pTask->szCanonical[0])
    {
        CPITEM *pItem = Catalog_FindByCanonical(pTask->szCanonical);
        if (pItem)
            Catalog_OpenItem(hwnd, pItem, pTask->szPage);
        return;
    }
    if (pTask->szCommand[0])
        Catalog_RunCommand(hwnd, pTask->szCommand);
}
