/*
 * PROJECT:     ReactOS Shell
 * LICENSE:     LGPL-2.1-or-later
 * PURPOSE:     Known-folder COM access to the shell's folder table
 */
#include "precomp.h"

extern "C" HRESULT SHELL_GetKnownFolderInfo(UINT, KNOWNFOLDERID *, KF_CATEGORY *);
extern "C" UINT SHELL_GetKnownFolderCount(void);

static HRESULT FindKnownFolder(REFKNOWNFOLDERID id, int *csidl, KF_CATEGORY *category)
{
    KNOWNFOLDERID candidate;
    for (UINT i = 0; i < SHELL_GetKnownFolderCount(); ++i)
    {
        if (SUCCEEDED(SHELL_GetKnownFolderInfo(i, &candidate, category)) && IsEqualGUID(candidate, id))
        {
            *csidl = i;
            return S_OK;
        }
    }
    return E_INVALIDARG;
}

class CKnownFolder : public CComObjectRootEx<CComMultiThreadModelNoCS>, public IKnownFolder
{
public:
    KNOWNFOLDERID m_id;
    KF_CATEGORY m_category;
    BEGIN_COM_MAP(CKnownFolder)
        COM_INTERFACE_ENTRY_IID(IID_IKnownFolder, IKnownFolder)
    END_COM_MAP()

    STDMETHOD(GetId)(KNOWNFOLDERID *id) override
    { if (!id) return E_POINTER; *id = m_id; return S_OK; }
    STDMETHOD(GetCategory)(KF_CATEGORY *category) override
    { if (!category) return E_POINTER; *category = m_category; return S_OK; }
    STDMETHOD(GetShellItem)(DWORD flags, REFIID iid, void **out) override
    {
        PIDLIST_ABSOLUTE pidl;
        if (!out) return E_POINTER;
        *out = NULL;
        HRESULT hr = GetIDList(flags, &pidl);
        if (FAILED(hr)) return hr;
        hr = SHCreateItemFromIDList(pidl, iid, out);
        CoTaskMemFree(pidl);
        return hr;
    }
    STDMETHOD(GetPath)(DWORD flags, LPWSTR *path) override
    { return SHGetKnownFolderPath(m_id, flags, NULL, path); }
    STDMETHOD(SetPath)(DWORD flags, LPCWSTR path) override
    { return E_NOTIMPL; }
    STDMETHOD(GetIDList)(DWORD flags, PIDLIST_ABSOLUTE *pidl) override
    { return SHGetKnownFolderIDList(m_id, flags, NULL, pidl); }
    STDMETHOD(GetFolderType)(FOLDERTYPEID *type) override
    { if (!type) return E_POINTER; *type = GUID_NULL; return E_NOTIMPL; }
    STDMETHOD(GetRedirectionCapabilities)(KF_REDIRECTION_CAPABILITIES *caps) override
    {
        if (!caps) return E_POINTER;
        *caps = KF_REDIRECTION_CAPABILITIES_DENY_ALL;
        return S_OK;
    }
    STDMETHOD(GetFolderDefinition)(KNOWNFOLDER_DEFINITION *definition) override
    {
        if (!definition) return E_POINTER;
        ZeroMemory(definition, sizeof(*definition));
        return E_NOTIMPL;
    }
};

class CKnownFolderManager : public CComObjectRootEx<CComMultiThreadModelNoCS>, public IKnownFolderManager
{
public:
    BEGIN_COM_MAP(CKnownFolderManager)
        COM_INTERFACE_ENTRY_IID(IID_IKnownFolderManager, IKnownFolderManager)
    END_COM_MAP()

    STDMETHOD(FolderIdFromCsidl)(int csidl, KNOWNFOLDERID *id) override
    {
        KF_CATEGORY category;
        if (!id) return E_POINTER;
        *id = GUID_NULL;
        return SHELL_GetKnownFolderInfo(csidl, id, &category);
    }
    STDMETHOD(FolderIdToCsidl)(REFKNOWNFOLDERID id, int *csidl) override
    {
        KF_CATEGORY category;
        if (!csidl) return E_POINTER;
        *csidl = -1;
        return FindKnownFolder(id, csidl, &category);
    }
    STDMETHOD(GetFolderIds)(KNOWNFOLDERID **ids, UINT *count) override
    {
        KNOWNFOLDERID id;
        KF_CATEGORY category;
        if (!ids || !count) return E_POINTER;
        *count = 0;
        *ids = static_cast<KNOWNFOLDERID *>(CoTaskMemAlloc(SHELL_GetKnownFolderCount() * sizeof(**ids)));
        if (!*ids) return E_OUTOFMEMORY;
        for (UINT i = 0; i < SHELL_GetKnownFolderCount(); ++i)
        {
            if (FAILED(SHELL_GetKnownFolderInfo(i, &id, &category))) continue;
            UINT j;
            for (j = 0; j < *count; ++j)
                if (IsEqualGUID((*ids)[j], id)) break;
            if (j == *count) (*ids)[(*count)++] = id;
        }
        return S_OK;
    }
    STDMETHOD(GetFolder)(REFKNOWNFOLDERID id, IKnownFolder **out) override
    {
        int csidl;
        KF_CATEGORY category;
        if (!out) return E_POINTER;
        *out = NULL;
        HRESULT hr = FindKnownFolder(id, &csidl, &category);
        if (FAILED(hr)) return hr;
        CComObject<CKnownFolder> *folder;
        hr = CComObject<CKnownFolder>::CreateInstance(&folder);
        if (FAILED(hr)) return hr;
        folder->m_id = id;
        folder->m_category = category;
        return folder->QueryInterface(IID_PPV_ARG(IKnownFolder, out));
    }
    STDMETHOD(GetFolderByName)(LPCWSTR name, IKnownFolder **out) override;
    STDMETHOD(RegisterFolder)(REFKNOWNFOLDERID id, const KNOWNFOLDER_DEFINITION *definition) override
    { return E_NOTIMPL; }
    STDMETHOD(UnregisterFolder)(REFKNOWNFOLDERID id) override
    { return E_NOTIMPL; }
    STDMETHOD(FindFolderFromPath)(LPCWSTR path, FFFP_MODE mode, IKnownFolder **out) override
    {
        KNOWNFOLDERID id, best = GUID_NULL;
        KF_CATEGORY category;
        SIZE_T bestLength = 0;
        if (!out) return E_POINTER;
        *out = NULL;
        if (!path || (mode != FFFP_EXACTMATCH && mode != FFFP_NEARESTPARENTMATCH)) return E_INVALIDARG;
        for (UINT i = 0; i < SHELL_GetKnownFolderCount(); ++i)
        {
            PWSTR candidate;
            if (FAILED(SHELL_GetKnownFolderInfo(i, &id, &category)) ||
                FAILED(SHGetKnownFolderPath(id, KF_FLAG_DONT_VERIFY, NULL, &candidate))) continue;
            SIZE_T length = wcslen(candidate);
            BOOL match = !wcsicmp(path, candidate);
            if (!match && mode == FFFP_NEARESTPARENTMATCH && length && wcslen(path) > length)
                match = !wcsnicmp(path, candidate, length) &&
                        (candidate[length - 1] == L'\\' || path[length] == L'\\');
            if (match && length > bestLength) { best = id; bestLength = length; }
            CoTaskMemFree(candidate);
        }
        return bestLength ? GetFolder(best, out) : HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
    }
    STDMETHOD(FindFolderFromIDList)(PCIDLIST_ABSOLUTE pidl, IKnownFolder **out) override
    {
        WCHAR path[MAX_PATH];
        if (!out) return E_POINTER;
        *out = NULL;
        if (!pidl || !SHGetPathFromIDListW(pidl, path)) return E_INVALIDARG;
        return FindFolderFromPath(path, FFFP_EXACTMATCH, out);
    }
    STDMETHOD(Redirect)(REFKNOWNFOLDERID id, HWND hwnd, KF_REDIRECT_FLAGS flags, LPCWSTR path,
                       UINT count, const KNOWNFOLDERID *exclude, LPWSTR *error) override
    { if (error) *error = NULL; return E_NOTIMPL; }
};

STDMETHODIMP CKnownFolderManager::GetFolderByName(LPCWSTR name, IKnownFolder **out)
{
    static const struct { const WCHAR *name; const KNOWNFOLDERID *id; } folders[] =
    {
        { L"Desktop", &FOLDERID_Desktop },
        { L"InternetFolder", &FOLDERID_InternetFolder },
        { L"Programs", &FOLDERID_Programs },
        { L"ControlPanelFolder", &FOLDERID_ControlPanelFolder },
        { L"PrintersFolder", &FOLDERID_PrintersFolder },
        { L"Documents", &FOLDERID_Documents },
        { L"Favorites", &FOLDERID_Favorites },
        { L"Startup", &FOLDERID_Startup },
        { L"Recent", &FOLDERID_Recent },
        { L"SendTo", &FOLDERID_SendTo },
        { L"RecycleBinFolder", &FOLDERID_RecycleBinFolder },
        { L"StartMenu", &FOLDERID_StartMenu },
        { L"Music", &FOLDERID_Music },
        { L"Videos", &FOLDERID_Videos },
        { L"ComputerFolder", &FOLDERID_ComputerFolder },
        { L"NetworkFolder", &FOLDERID_NetworkFolder },
        { L"NetHood", &FOLDERID_NetHood },
        { L"Fonts", &FOLDERID_Fonts },
        { L"Templates", &FOLDERID_Templates },
        { L"CommonStartMenu", &FOLDERID_CommonStartMenu },
        { L"CommonPrograms", &FOLDERID_CommonPrograms },
        { L"CommonStartup", &FOLDERID_CommonStartup },
        { L"PublicDesktop", &FOLDERID_PublicDesktop },
        { L"RoamingAppData", &FOLDERID_RoamingAppData },
        { L"PrintHood", &FOLDERID_PrintHood },
        { L"LocalAppData", &FOLDERID_LocalAppData },
        { L"InternetCache", &FOLDERID_InternetCache },
        { L"Cookies", &FOLDERID_Cookies },
        { L"History", &FOLDERID_History },
        { L"ProgramData", &FOLDERID_ProgramData },
        { L"Windows", &FOLDERID_Windows },
        { L"System", &FOLDERID_System },
        { L"ProgramFiles", &FOLDERID_ProgramFiles },
        { L"Pictures", &FOLDERID_Pictures },
        { L"Profile", &FOLDERID_Profile },
        { L"SystemX86", &FOLDERID_SystemX86 },
        { L"ProgramFilesX86", &FOLDERID_ProgramFilesX86 },
        { L"ProgramFilesCommon", &FOLDERID_ProgramFilesCommon },
        { L"ProgramFilesCommonX86", &FOLDERID_ProgramFilesCommonX86 },
        { L"CommonTemplates", &FOLDERID_CommonTemplates },
        { L"PublicDocuments", &FOLDERID_PublicDocuments },
        { L"CommonAdminTools", &FOLDERID_CommonAdminTools },
        { L"AdminTools", &FOLDERID_AdminTools },
        { L"ConnectionsFolder", &FOLDERID_ConnectionsFolder },
        { L"PublicMusic", &FOLDERID_PublicMusic },
        { L"PublicPictures", &FOLDERID_PublicPictures },
        { L"PublicVideos", &FOLDERID_PublicVideos },
        { L"ResourceDir", &FOLDERID_ResourceDir },
        { L"LocalizedResourcesDir", &FOLDERID_LocalizedResourcesDir },
        { L"CommonOEMLinks", &FOLDERID_CommonOEMLinks },
        { L"CDBurning", &FOLDERID_CDBurning },
        { L"AddNewPrograms", &FOLDERID_AddNewPrograms },
        { L"AppUpdates", &FOLDERID_AppUpdates },
        { L"ChangeRemovePrograms", &FOLDERID_ChangeRemovePrograms },
        { L"ConflictFolder", &FOLDERID_ConflictFolder },
        { L"Contacts", &FOLDERID_Contacts },
        { L"DeviceMetadataStore", &FOLDERID_DeviceMetadataStore },
        { L"DocumentsLibrary", &FOLDERID_DocumentsLibrary },
        { L"Downloads", &FOLDERID_Downloads },
        { L"Games", &FOLDERID_Games },
        { L"GameTasks", &FOLDERID_GameTasks },
        { L"HomeGroup", &FOLDERID_HomeGroup },
        { L"ImplicitAppShortcuts", &FOLDERID_ImplicitAppShortcuts },
        { L"Libraries", &FOLDERID_Libraries },
        { L"Links", &FOLDERID_Links },
        { L"LocalAppDataLow", &FOLDERID_LocalAppDataLow },
        { L"MusicLibrary", &FOLDERID_MusicLibrary },
        { L"OriginalImages", &FOLDERID_OriginalImages },
        { L"PhotoAlbums", &FOLDERID_PhotoAlbums },
        { L"PicturesLibrary", &FOLDERID_PicturesLibrary },
        { L"Playlists", &FOLDERID_Playlists },
        { L"ProgramFilesX64", &FOLDERID_ProgramFilesX64 },
        { L"ProgramFilesCommonX64", &FOLDERID_ProgramFilesCommonX64 },
        { L"Public", &FOLDERID_Public },
        { L"PublicDownloads", &FOLDERID_PublicDownloads },
        { L"PublicGameTasks", &FOLDERID_PublicGameTasks },
        { L"PublicLibraries", &FOLDERID_PublicLibraries },
        { L"PublicRingtones", &FOLDERID_PublicRingtones },
        { L"QuickLaunch", &FOLDERID_QuickLaunch },
        { L"RecordedTVLibrary", &FOLDERID_RecordedTVLibrary },
        { L"Ringtones", &FOLDERID_Ringtones },
        { L"SampleMusic", &FOLDERID_SampleMusic },
        { L"SamplePictures", &FOLDERID_SamplePictures },
        { L"SamplePlaylists", &FOLDERID_SamplePlaylists },
        { L"SampleVideos", &FOLDERID_SampleVideos },
        { L"SavedGames", &FOLDERID_SavedGames },
        { L"SavedSearches", &FOLDERID_SavedSearches },
        { L"SEARCH_CSC", &FOLDERID_SEARCH_CSC },
        { L"SEARCH_MAPI", &FOLDERID_SEARCH_MAPI },
        { L"SearchHome", &FOLDERID_SearchHome },
        { L"SidebarDefaultParts", &FOLDERID_SidebarDefaultParts },
        { L"SidebarParts", &FOLDERID_SidebarParts },
        { L"SyncManagerFolder", &FOLDERID_SyncManagerFolder },
        { L"SyncResultsFolder", &FOLDERID_SyncResultsFolder },
        { L"SyncSetupFolder", &FOLDERID_SyncSetupFolder },
        { L"UserPinned", &FOLDERID_UserPinned },
        { L"UserProfiles", &FOLDERID_UserProfiles },
        { L"UserProgramFiles", &FOLDERID_UserProgramFiles },
        { L"UserProgramFilesCommon", &FOLDERID_UserProgramFilesCommon },
        { L"UsersFiles", &FOLDERID_UsersFiles },
        { L"UsersLibraries", &FOLDERID_UsersLibraries },
        { L"VideosLibrary", &FOLDERID_VideosLibrary },
    };
    if (!out) return E_POINTER;
    *out = NULL;
    if (!name) return E_INVALIDARG;
    for (UINT i = 0; i < _countof(folders); ++i)
        if (!wcsicmp(name, folders[i].name)) return GetFolder(*folders[i].id, out);
    return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
}

extern "C" HRESULT WINAPI KnownFolderManager_Constructor(IUnknown *outer, REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (outer) return CLASS_E_NOAGGREGATION;
    return ShellObjectCreator<CKnownFolderManager>(iid, out);
}
