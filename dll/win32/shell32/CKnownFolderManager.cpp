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

struct KNOWN_FOLDER_DEF
{
    const KNOWNFOLDERID *Id;
    KF_CATEGORY Category;
    PCWSTR Name;
    const KNOWNFOLDERID *Parent;
    PCWSTR RelativePath;
    PCWSTR ParsingName;
    DWORD Attributes;
    KF_DEFINITION_FLAGS Flags;
};

static const KNOWN_FOLDER_DEF KnownFolderDefs[] =
{
    { &FOLDERID_Desktop, KF_CATEGORY_PERUSER, L"Desktop", NULL, L"Desktop", NULL, FILE_ATTRIBUTE_READONLY, (KF_DEFINITION_FLAGS)(0) },
    { &FOLDERID_InternetFolder, KF_CATEGORY_VIRTUAL, L"InternetFolder", NULL, NULL, L"::{871C5380-42A0-1069-A2EA-08002B30309D}", 0, (KF_DEFINITION_FLAGS)(0) },
    { &FOLDERID_Programs, KF_CATEGORY_PERUSER, L"Programs", &FOLDERID_StartMenu, L"Programs", NULL, FILE_ATTRIBUTE_READONLY, (KF_DEFINITION_FLAGS)(0) },
    { &FOLDERID_ControlPanelFolder, KF_CATEGORY_VIRTUAL, L"ControlPanelFolder", NULL, L"::{21EC2020-3AEA-1069-A2DD-08002B30309D}", L"::{26EE0668-A00A-44D7-9371-BEB064C98683}\\0", 0, (KF_DEFINITION_FLAGS)(0) },
    { &FOLDERID_PrintersFolder, KF_CATEGORY_VIRTUAL, L"PrintersFolder", NULL, NULL, L"::{21EC2020-3AEA-1069-A2DD-08002B30309D}\\::{2227A280-3AEA-1069-A2DE-08002B30309D}", 0, (KF_DEFINITION_FLAGS)(0) },
    { &FOLDERID_Documents, KF_CATEGORY_PERUSER, L"Personal", &FOLDERID_Profile, L"Documents", L"::{59031a47-3f72-44a7-89c5-5595fe6b30ee}\\{FDD39AD0-238F-46AF-ADB4-6C85480369C7}", FILE_ATTRIBUTE_READONLY, (KF_DEFINITION_FLAGS)(KFDF_ROAMABLE | KFDF_PRECREATE) },
    { &FOLDERID_Favorites, KF_CATEGORY_PERUSER, L"Favorites", NULL, L"Favorites", NULL, FILE_ATTRIBUTE_READONLY, (KF_DEFINITION_FLAGS)(KFDF_ROAMABLE | KFDF_PRECREATE | KFDF_PUBLISHEXPANDEDPATH) },
    { &FOLDERID_Startup, KF_CATEGORY_PERUSER, L"Startup", &FOLDERID_Programs, L"StartUp", NULL, FILE_ATTRIBUTE_READONLY, (KF_DEFINITION_FLAGS)(KFDF_PRECREATE) },
    { &FOLDERID_Recent, KF_CATEGORY_PERUSER, L"Recent", &FOLDERID_RoamingAppData, L"Microsoft\\Windows\\Recent", NULL, FILE_ATTRIBUTE_READONLY, (KF_DEFINITION_FLAGS)(KFDF_PRECREATE) },
    { &FOLDERID_SendTo, KF_CATEGORY_PERUSER, L"SendTo", &FOLDERID_RoamingAppData, L"Microsoft\\Windows\\SendTo", NULL, 0, (KF_DEFINITION_FLAGS)(KFDF_PRECREATE) },
    { &FOLDERID_RecycleBinFolder, KF_CATEGORY_VIRTUAL, L"RecycleBinFolder", NULL, NULL, L"::{645FF040-5081-101B-9F08-00AA002F954E}", 0, (KF_DEFINITION_FLAGS)(0) },
    { &FOLDERID_StartMenu, KF_CATEGORY_PERUSER, L"Start Menu", &FOLDERID_RoamingAppData, L"Microsoft\\Windows\\Start Menu", NULL, FILE_ATTRIBUTE_READONLY, (KF_DEFINITION_FLAGS)(KFDF_PRECREATE) },
    { &FOLDERID_Music, KF_CATEGORY_PERUSER, L"My Music", &FOLDERID_Profile, L"Music", L"::{59031a47-3f72-44a7-89c5-5595fe6b30ee}\\{4BD8D571-6D19-48D3-BE97-422220080E43}", FILE_ATTRIBUTE_READONLY, (KF_DEFINITION_FLAGS)(KFDF_ROAMABLE | KFDF_PRECREATE) },
    { &FOLDERID_Videos, KF_CATEGORY_PERUSER, L"My Video", &FOLDERID_Profile, L"Videos", L"::{59031a47-3f72-44a7-89c5-5595fe6b30ee}\\{18989B1D-99B5-455B-841C-AB7C74E4DDFC}", FILE_ATTRIBUTE_READONLY, (KF_DEFINITION_FLAGS)(KFDF_ROAMABLE | KFDF_PRECREATE) },
    { &FOLDERID_Desktop, KF_CATEGORY_PERUSER, L"Desktop", &FOLDERID_Profile, L"Desktop", NULL, FILE_ATTRIBUTE_READONLY, (KF_DEFINITION_FLAGS)(KFDF_ROAMABLE | KFDF_PRECREATE | KFDF_PUBLISHEXPANDEDPATH) },
    { &FOLDERID_ComputerFolder, KF_CATEGORY_VIRTUAL, L"MyComputerFolder", NULL, NULL, L"::{20D04FE0-3AEA-1069-A2D8-08002B30309D}", 0, (KF_DEFINITION_FLAGS)(0) },
    { &FOLDERID_NetworkFolder, KF_CATEGORY_VIRTUAL, L"NetworkPlacesFolder", NULL, NULL, L"::{F02C1A0D-BE21-4350-88B0-7367FC96EF3C}", 0, (KF_DEFINITION_FLAGS)(0) },
    { &FOLDERID_NetHood, KF_CATEGORY_PERUSER, L"NetHood", &FOLDERID_RoamingAppData, L"Microsoft\\Windows\\Network Shortcuts", NULL, 0, (KF_DEFINITION_FLAGS)(0) },
    { &FOLDERID_Fonts, KF_CATEGORY_FIXED, L"Fonts", &FOLDERID_Windows, NULL, NULL, 0, (KF_DEFINITION_FLAGS)(0) },
    { &FOLDERID_Templates, KF_CATEGORY_PERUSER, L"Templates", &FOLDERID_RoamingAppData, L"Microsoft\\Windows\\Templates", NULL, 0, (KF_DEFINITION_FLAGS)(0) },
    { &FOLDERID_CommonStartMenu, KF_CATEGORY_COMMON, L"Common Start Menu", &FOLDERID_ProgramData, L"Microsoft\\Windows\\Start Menu", NULL, FILE_ATTRIBUTE_READONLY, (KF_DEFINITION_FLAGS)(0) },
    { &FOLDERID_CommonPrograms, KF_CATEGORY_COMMON, L"Common Programs", &FOLDERID_CommonStartMenu, L"Programs", NULL, FILE_ATTRIBUTE_READONLY, (KF_DEFINITION_FLAGS)(0) },
    { &FOLDERID_CommonStartup, KF_CATEGORY_COMMON, L"Common Startup", &FOLDERID_CommonPrograms, L"StartUp", NULL, FILE_ATTRIBUTE_READONLY, (KF_DEFINITION_FLAGS)(KFDF_PRECREATE) },
    { &FOLDERID_PublicDesktop, KF_CATEGORY_COMMON, L"Common Desktop", &FOLDERID_Public, L"Desktop", NULL, FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN, (KF_DEFINITION_FLAGS)(KFDF_PRECREATE) },
    { &FOLDERID_RoamingAppData, KF_CATEGORY_PERUSER, L"AppData", &FOLDERID_Profile, L"AppData\\Roaming", NULL, 0, (KF_DEFINITION_FLAGS)(0) },
    { &FOLDERID_PrintHood, KF_CATEGORY_PERUSER, L"PrintHood", &FOLDERID_RoamingAppData, L"Microsoft\\Windows\\Printer Shortcuts", NULL, 0, (KF_DEFINITION_FLAGS)(0) },
    { &FOLDERID_LocalAppData, KF_CATEGORY_PERUSER, L"Local AppData", &FOLDERID_Profile, L"AppData\\Local", NULL, 0, (KF_DEFINITION_FLAGS)(KFDF_LOCAL_REDIRECT_ONLY | KFDF_PUBLISHEXPANDEDPATH) },
    { &FOLDERID_Favorites, KF_CATEGORY_PERUSER, L"Favorites", &FOLDERID_Profile, L"Favorites", NULL, FILE_ATTRIBUTE_READONLY, (KF_DEFINITION_FLAGS)(KFDF_ROAMABLE | KFDF_PRECREATE | KFDF_PUBLISHEXPANDEDPATH) },
    { &FOLDERID_InternetCache, KF_CATEGORY_PERUSER, L"Cache", &FOLDERID_LocalAppData, L"Microsoft\\Windows\\INetCache", NULL, 0, (KF_DEFINITION_FLAGS)(KFDF_LOCAL_REDIRECT_ONLY) },
    { &FOLDERID_Cookies, KF_CATEGORY_PERUSER, L"Cookies", &FOLDERID_LocalAppData, L"Microsoft\\Windows\\INetCookies", NULL, 0, (KF_DEFINITION_FLAGS)(0) },
    { &FOLDERID_History, KF_CATEGORY_PERUSER, L"History", &FOLDERID_LocalAppData, L"Microsoft\\Windows\\History", NULL, 0, (KF_DEFINITION_FLAGS)(KFDF_LOCAL_REDIRECT_ONLY) },
    { &FOLDERID_ProgramData, KF_CATEGORY_FIXED, L"Common AppData", NULL, NULL, NULL, 0, (KF_DEFINITION_FLAGS)(0) },
    { &FOLDERID_Windows, KF_CATEGORY_FIXED, L"Windows", NULL, NULL, NULL, 0, (KF_DEFINITION_FLAGS)(0) },
    { &FOLDERID_System, KF_CATEGORY_FIXED, L"System", NULL, NULL, NULL, 0, (KF_DEFINITION_FLAGS)(0) },
    { &FOLDERID_ProgramFiles, KF_CATEGORY_FIXED, L"ProgramFiles", NULL, NULL, NULL, FILE_ATTRIBUTE_READONLY, (KF_DEFINITION_FLAGS)(0) },
    { &FOLDERID_Pictures, KF_CATEGORY_PERUSER, L"My Pictures", &FOLDERID_Profile, L"Pictures", L"::{59031a47-3f72-44a7-89c5-5595fe6b30ee}\\{33E28130-4E1E-4676-835A-98395C3BC3BB}", FILE_ATTRIBUTE_READONLY, (KF_DEFINITION_FLAGS)(KFDF_ROAMABLE | KFDF_PRECREATE) },
    { &FOLDERID_Profile, KF_CATEGORY_FIXED, L"Profile", NULL, NULL, NULL, 0, (KF_DEFINITION_FLAGS)(0) },
    { &FOLDERID_SystemX86, KF_CATEGORY_FIXED, L"SystemX86", NULL, NULL, NULL, 0, (KF_DEFINITION_FLAGS)(0) },
    { &FOLDERID_ProgramFilesX86, KF_CATEGORY_FIXED, L"ProgramFilesX86", NULL, NULL, NULL, FILE_ATTRIBUTE_READONLY, (KF_DEFINITION_FLAGS)(0) },
    { &FOLDERID_ProgramFilesCommon, KF_CATEGORY_FIXED, L"ProgramFilesCommon", NULL, NULL, NULL, 0, (KF_DEFINITION_FLAGS)(0) },
    { &FOLDERID_ProgramFilesCommonX86, KF_CATEGORY_FIXED, L"ProgramFilesCommonX86", NULL, NULL, NULL, 0, (KF_DEFINITION_FLAGS)(0) },
    { &FOLDERID_CommonTemplates, KF_CATEGORY_COMMON, L"Common Templates", &FOLDERID_ProgramData, L"Microsoft\\Windows\\Templates", NULL, 0, (KF_DEFINITION_FLAGS)(0) },
    { &FOLDERID_PublicDocuments, KF_CATEGORY_COMMON, L"Common Documents", &FOLDERID_Public, L"Documents", NULL, FILE_ATTRIBUTE_READONLY, (KF_DEFINITION_FLAGS)(KFDF_PRECREATE) },
    { &FOLDERID_CommonAdminTools, KF_CATEGORY_COMMON, L"Common Administrative Tools", &FOLDERID_CommonPrograms, L"Administrative Tools", NULL, FILE_ATTRIBUTE_READONLY, (KF_DEFINITION_FLAGS)(KFDF_PRECREATE) },
    { &FOLDERID_AdminTools, KF_CATEGORY_PERUSER, L"Administrative Tools", &FOLDERID_Programs, L"Administrative Tools", NULL, FILE_ATTRIBUTE_READONLY, (KF_DEFINITION_FLAGS)(KFDF_PRECREATE) },
    { &FOLDERID_ConnectionsFolder, KF_CATEGORY_VIRTUAL, L"ConnectionsFolder", NULL, L"Administrative Tools", L"::{21EC2020-3AEA-1069-A2DD-08002B30309D}\\::{7007ACC7-3202-11D1-AAD2-00805FC1270E}", 0, (KF_DEFINITION_FLAGS)(0) },
    { &FOLDERID_PublicMusic, KF_CATEGORY_COMMON, L"CommonMusic", &FOLDERID_Public, L"Music", NULL, FILE_ATTRIBUTE_READONLY, (KF_DEFINITION_FLAGS)(KFDF_PRECREATE) },
    { &FOLDERID_PublicPictures, KF_CATEGORY_COMMON, L"CommonPictures", &FOLDERID_Public, L"Pictures", NULL, FILE_ATTRIBUTE_READONLY, (KF_DEFINITION_FLAGS)(KFDF_PRECREATE) },
    { &FOLDERID_PublicVideos, KF_CATEGORY_COMMON, L"CommonVideo", &FOLDERID_Public, L"Videos", NULL, FILE_ATTRIBUTE_READONLY, (KF_DEFINITION_FLAGS)(KFDF_PRECREATE) },
    { &FOLDERID_ResourceDir, KF_CATEGORY_FIXED, L"ResourceDir", NULL, NULL, NULL, 0, (KF_DEFINITION_FLAGS)(0) },
    { &FOLDERID_LocalizedResourcesDir, KF_CATEGORY_FIXED, L"LocalizedResourcesDir", NULL, NULL, NULL, 0, (KF_DEFINITION_FLAGS)(0) },
    { &FOLDERID_CommonOEMLinks, KF_CATEGORY_COMMON, L"OEM Links", &FOLDERID_ProgramData, L"OEM Links", NULL, 0, (KF_DEFINITION_FLAGS)(0) },
    { &FOLDERID_CDBurning, KF_CATEGORY_PERUSER, L"CD Burning", &FOLDERID_LocalAppData, L"Microsoft\\Windows\\Burn\\Burn", NULL, FILE_ATTRIBUTE_READONLY, (KF_DEFINITION_FLAGS)(KFDF_LOCAL_REDIRECT_ONLY) },
    { &FOLDERID_AddNewPrograms, KF_CATEGORY_VIRTUAL, L"AddNewProgramsFolder", NULL, NULL, L"::{21EC2020-3AEA-1069-A2DD-08002B30309D}\\::{15eae92e-f17a-4431-9f28-805e482dafd4}", 0, (KF_DEFINITION_FLAGS)(0) },
    { &FOLDERID_AppUpdates, KF_CATEGORY_VIRTUAL, L"AppUpdatesFolder", NULL, NULL, L"::{21EC2020-3AEA-1069-A2DD-08002B30309D}\\::{7b81be6a-ce2b-4676-a29e-eb907a5126c5}\\::{d450a8a1-9568-45c7-9c0e-b4f9fb4537bd}", 0, (KF_DEFINITION_FLAGS)(0) },
    { &FOLDERID_ChangeRemovePrograms, KF_CATEGORY_VIRTUAL, L"ChangeRemoveProgramsFolder", NULL, NULL, L"::{21EC2020-3AEA-1069-A2DD-08002B30309D}\\::{7b81be6a-ce2b-4676-a29e-eb907a5126c5}", 0, (KF_DEFINITION_FLAGS)(0) },
    { &FOLDERID_ConflictFolder, KF_CATEGORY_VIRTUAL, L"ConflictFolder", NULL, NULL, L"::{21EC2020-3AEA-1069-A2DD-08002B30309D}\\::{9C73F5E5-7AE7-4E32-A8E8-8D23B85255BF}\\::{E413D040-6788-4C22-957E-175D1C513A34},", 0, (KF_DEFINITION_FLAGS)(0) },
    { &FOLDERID_Contacts, KF_CATEGORY_PERUSER, L"Contacts", &FOLDERID_Profile, L"Contacts", L"::{59031a47-3f72-44a7-89c5-5595fe6b30ee}\\{56784854-C6CB-462B-8169-88E350ACB882}", FILE_ATTRIBUTE_READONLY, (KF_DEFINITION_FLAGS)(KFDF_ROAMABLE | KFDF_PRECREATE | KFDF_PUBLISHEXPANDEDPATH) },
    { &FOLDERID_DeviceMetadataStore, KF_CATEGORY_COMMON, L"Device Metadata Store", &FOLDERID_ProgramData, L"Microsoft\\Windows\\DeviceMetadataStore", NULL, 0, (KF_DEFINITION_FLAGS)(0) },
    { &FOLDERID_DocumentsLibrary, KF_CATEGORY_PERUSER, L"DocumentsLibrary", &FOLDERID_Libraries, L"Documents.library-ms", L"::{031E4825-7B94-4dc3-B131-E946B44C8DD5}\\{7b0db17d-9cd2-4a93-9733-46cc89022e7c}", 0, (KF_DEFINITION_FLAGS)(KFDF_PRECREATE | KFDF_STREAM) },
    { &FOLDERID_Downloads, KF_CATEGORY_PERUSER, L"Downloads", &FOLDERID_Profile, L"Downloads", NULL, FILE_ATTRIBUTE_READONLY, (KF_DEFINITION_FLAGS)(KFDF_ROAMABLE | KFDF_PRECREATE | KFDF_PUBLISHEXPANDEDPATH) },
    { &FOLDERID_Games, KF_CATEGORY_VIRTUAL, L"Games", NULL, NULL, L"::{ED228FDF-9EA8-4870-83b1-96b02CFE0D52}", 0, (KF_DEFINITION_FLAGS)(0) },
    { &FOLDERID_GameTasks, KF_CATEGORY_PERUSER, L"GameTasks", &FOLDERID_LocalAppData, L"Microsoft\\Windows\\GameExplorer", NULL, 0, (KF_DEFINITION_FLAGS)(KFDF_LOCAL_REDIRECT_ONLY) },
    { &FOLDERID_HomeGroup, KF_CATEGORY_VIRTUAL, L"HomeGroupFolder", NULL, NULL, L"::{B4FB3F98-C1EA-428d-A78A-D1F5659CBA93}", 0, (KF_DEFINITION_FLAGS)(0) },
    { &FOLDERID_ImplicitAppShortcuts, KF_CATEGORY_PERUSER, L"ImplicitAppShortcuts", &FOLDERID_UserPinned, L"ImplicitAppShortcuts", NULL, 0, (KF_DEFINITION_FLAGS)(KFDF_PRECREATE) },
    { &FOLDERID_Libraries, KF_CATEGORY_PERUSER, L"Libraries", &FOLDERID_RoamingAppData, L"Microsoft\\Windows\\Libraries", NULL, 0, (KF_DEFINITION_FLAGS)(KFDF_PRECREATE | KFDF_PUBLISHEXPANDEDPATH) },
    { &FOLDERID_Links, KF_CATEGORY_PERUSER, L"Links", &FOLDERID_Profile, L"Links", L"::{59031a47-3f72-44a7-89c5-5595fe6b30ee}\\{bfb9d5e0-c6a9-404c-b2b2-ae6db6af4968}", FILE_ATTRIBUTE_READONLY, (KF_DEFINITION_FLAGS)(KFDF_ROAMABLE | KFDF_PRECREATE | KFDF_PUBLISHEXPANDEDPATH) },
    { &FOLDERID_LocalAppDataLow, KF_CATEGORY_PERUSER, L"LocalAppDataLow", &FOLDERID_Profile, L"AppData\\LocalLow", NULL, FILE_ATTRIBUTE_NOT_CONTENT_INDEXED, (KF_DEFINITION_FLAGS)(KFDF_LOCAL_REDIRECT_ONLY | KFDF_PRECREATE | KFDF_PUBLISHEXPANDEDPATH) },
    { &FOLDERID_MusicLibrary, KF_CATEGORY_PERUSER, L"MusicLibrary", &FOLDERID_Libraries, L"Music.library-ms", L"::{031E4825-7B94-4dc3-B131-E946B44C8DD5}\\{2112AB0A-C86A-4ffe-A368-0DE96E47012E}", 0, (KF_DEFINITION_FLAGS)(KFDF_PRECREATE | KFDF_STREAM) },
    { &FOLDERID_OriginalImages, KF_CATEGORY_PERUSER, L"Original Images", &FOLDERID_LocalAppData, L"Microsoft\\Windows Photo Gallery\\Original Images", NULL, 0, (KF_DEFINITION_FLAGS)(0) },
    { &FOLDERID_PhotoAlbums, KF_CATEGORY_PERUSER, L"PhotoAlbums", &FOLDERID_Pictures, L"Slide Shows", NULL, FILE_ATTRIBUTE_READONLY, (KF_DEFINITION_FLAGS)(0) },
    { &FOLDERID_PicturesLibrary, KF_CATEGORY_PERUSER, L"PicturesLibrary", &FOLDERID_Libraries, L"Pictures.library-ms", L"::{031E4825-7B94-4dc3-B131-E946B44C8DD5}\\{A990AE9F-A03B-4e80-94BC-9912D7504104}", 0, (KF_DEFINITION_FLAGS)(KFDF_PRECREATE | KFDF_STREAM) },
    { &FOLDERID_Playlists, KF_CATEGORY_PERUSER, L"Playlists", &FOLDERID_Music, L"Playlists", NULL, FILE_ATTRIBUTE_READONLY, (KF_DEFINITION_FLAGS)(0) },
    { &FOLDERID_ProgramFilesX64, KF_CATEGORY_FIXED, L"ProgramFilesX64", NULL, NULL, NULL, 0, (KF_DEFINITION_FLAGS)(0) },
    { &FOLDERID_ProgramFilesCommonX64, KF_CATEGORY_FIXED, L"ProgramFilesCommonX64", NULL, NULL, NULL, 0, (KF_DEFINITION_FLAGS)(0) },
    { &FOLDERID_Public, KF_CATEGORY_FIXED, L"Public", NULL, NULL, L"::{4336a54d-038b-4685-ab02-99bb52d3fb8b}", FILE_ATTRIBUTE_READONLY, (KF_DEFINITION_FLAGS)(KFDF_PRECREATE) },
    { &FOLDERID_PublicDownloads, KF_CATEGORY_COMMON, L"CommonDownloads", &FOLDERID_Public, L"Downloads", NULL, FILE_ATTRIBUTE_READONLY, (KF_DEFINITION_FLAGS)(KFDF_PRECREATE) },
    { &FOLDERID_PublicGameTasks, KF_CATEGORY_COMMON, L"PublicGameTasks", &FOLDERID_ProgramData, L"Microsoft\\Windows\\GameExplorer", NULL, 0, (KF_DEFINITION_FLAGS)(KFDF_LOCAL_REDIRECT_ONLY) },
    { &FOLDERID_PublicLibraries, KF_CATEGORY_COMMON, L"PublicLibraries", &FOLDERID_Public, L"Libraries", NULL, FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN, (KF_DEFINITION_FLAGS)(KFDF_PRECREATE) },
    { &FOLDERID_PublicRingtones, KF_CATEGORY_COMMON, L"CommonRingtones", &FOLDERID_ProgramData, L"Microsoft\\Windows\\Ringtones", NULL, 0, (KF_DEFINITION_FLAGS)(KFDF_PRECREATE) },
    { &FOLDERID_QuickLaunch, KF_CATEGORY_PERUSER, L"Quick Launch", &FOLDERID_RoamingAppData, L"Microsoft\\Internet Explorer\\Quick Launch", NULL, 0, (KF_DEFINITION_FLAGS)(0) },
    { &FOLDERID_RecordedTVLibrary, KF_CATEGORY_COMMON, L"RecordedTVLibrary", &FOLDERID_PublicLibraries, L"RecordedTV.library-ms", NULL, 0, (KF_DEFINITION_FLAGS)(KFDF_PRECREATE | KFDF_STREAM) },
    { &FOLDERID_Ringtones, KF_CATEGORY_PERUSER, L"Ringtones", &FOLDERID_LocalAppData, L"Microsoft\\Windows\\Ringtones", NULL, 0, (KF_DEFINITION_FLAGS)(KFDF_PRECREATE) },
    { &FOLDERID_SampleMusic, KF_CATEGORY_COMMON, L"SampleMusic", &FOLDERID_PublicMusic, L"Sample Music", NULL, FILE_ATTRIBUTE_READONLY, (KF_DEFINITION_FLAGS)(KFDF_PRECREATE) },
    { &FOLDERID_SamplePictures, KF_CATEGORY_COMMON, L"SamplePictures", &FOLDERID_PublicPictures, L"Sample Pictures", NULL, FILE_ATTRIBUTE_READONLY, (KF_DEFINITION_FLAGS)(KFDF_PRECREATE) },
    { &FOLDERID_SamplePlaylists, KF_CATEGORY_COMMON, L"SamplePlaylists", &FOLDERID_PublicMusic, L"Sample Playlists", NULL, FILE_ATTRIBUTE_READONLY, (KF_DEFINITION_FLAGS)(KFDF_PRECREATE) },
    { &FOLDERID_SampleVideos, KF_CATEGORY_COMMON, L"SampleVideos", &FOLDERID_PublicVideos, L"Sample Videos", NULL, FILE_ATTRIBUTE_READONLY, (KF_DEFINITION_FLAGS)(KFDF_PRECREATE) },
    { &FOLDERID_SavedGames, KF_CATEGORY_PERUSER, L"SavedGames", &FOLDERID_Profile, L"Saved Games", L"::{59031a47-3f72-44a7-89c5-5595fe6b30ee}\\{4C5C32FF-BB9D-43b0-B5B4-2D72E54EAAA4}", FILE_ATTRIBUTE_READONLY, (KF_DEFINITION_FLAGS)(KFDF_ROAMABLE | KFDF_PRECREATE | KFDF_PUBLISHEXPANDEDPATH) },
    { &FOLDERID_SavedSearches, KF_CATEGORY_PERUSER, L"Searches", &FOLDERID_Profile, L"Searches", L"::{59031a47-3f72-44a7-89c5-5595fe6b30ee}\\{7d1d3a04-debb-4115-95cf-2f29da2920da}", FILE_ATTRIBUTE_READONLY, (KF_DEFINITION_FLAGS)(KFDF_PRECREATE | KFDF_PUBLISHEXPANDEDPATH) },
    { &FOLDERID_SEARCH_CSC, KF_CATEGORY_VIRTUAL, L"CSCFolder", NULL, NULL, L"shell:::{BD7A2E7B-21CB-41b2-A086-B309680C6B7E}\\*", 0, (KF_DEFINITION_FLAGS)(0) },
    { &FOLDERID_SEARCH_MAPI, KF_CATEGORY_VIRTUAL, L"MAPIFolder", NULL, NULL, L"shell:::{89D83576-6BD1-4C86-9454-BEB04E94C819}\\*", 0, (KF_DEFINITION_FLAGS)(0) },
    { &FOLDERID_SearchHome, KF_CATEGORY_VIRTUAL, L"SearchHomeFolder", NULL, NULL, L"::{9343812e-1c37-4a49-a12e-4b2d810d956b}", 0, (KF_DEFINITION_FLAGS)(0) },
    { &FOLDERID_SidebarDefaultParts, KF_CATEGORY_COMMON, L"Default Gadgets", &FOLDERID_ProgramFiles, L"Windows Sidebar\\Gadgets", NULL, 0, (KF_DEFINITION_FLAGS)(0) },
    { &FOLDERID_SidebarParts, KF_CATEGORY_PERUSER, L"Gadgets", &FOLDERID_LocalAppData, L"Microsoft\\Windows Sidebar\\Gadgets", NULL, 0, (KF_DEFINITION_FLAGS)(0) },
    { &FOLDERID_SyncManagerFolder, KF_CATEGORY_VIRTUAL, L"SyncCenterFolder", NULL, NULL, L"::{21EC2020-3AEA-1069-A2DD-08002B30309D}\\::{9C73F5E5-7AE7-4E32-A8E8-8D23B85255BF}", 0, (KF_DEFINITION_FLAGS)(0) },
    { &FOLDERID_SyncResultsFolder, KF_CATEGORY_VIRTUAL, L"SyncResultsFolder", NULL, NULL, L"::{21EC2020-3AEA-1069-A2DD-08002B30309D}\\::{9C73F5E5-7AE7-4E32-A8E8-8D23B85255BF}\\::{BC48B32F-5910-47F5-8570-5074A8A5636A},", 0, (KF_DEFINITION_FLAGS)(0) },
    { &FOLDERID_SyncSetupFolder, KF_CATEGORY_VIRTUAL, L"SyncSetupFolder", NULL, NULL, L"::{21EC2020-3AEA-1069-A2DD-08002B30309D}\\::{9C73F5E5-7AE7-4E32-A8E8-8D23B85255BF}\\::{F1390A9A-A3F4-4E5D-9C5F-98F3BD8D935C},", 0, (KF_DEFINITION_FLAGS)(0) },
    { &FOLDERID_UserPinned, KF_CATEGORY_PERUSER, L"User Pinned", &FOLDERID_QuickLaunch, L"User Pinned", NULL, FILE_ATTRIBUTE_HIDDEN, (KF_DEFINITION_FLAGS)(KFDF_PRECREATE) },
    { &FOLDERID_UserProfiles, KF_CATEGORY_FIXED, L"UserProfiles", NULL, NULL, NULL, FILE_ATTRIBUTE_READONLY, (KF_DEFINITION_FLAGS)(KFDF_PRECREATE) },
    { &FOLDERID_UserProgramFiles, KF_CATEGORY_PERUSER, L"UserProgramFiles", &FOLDERID_LocalAppData, L"Programs", NULL, 0, (KF_DEFINITION_FLAGS)(0) },
    { &FOLDERID_UserProgramFilesCommon, KF_CATEGORY_PERUSER, L"UserProgramFilesCommon", &FOLDERID_UserProgramFiles, L"Common", NULL, 0, (KF_DEFINITION_FLAGS)(0) },
    { &FOLDERID_UsersFiles, KF_CATEGORY_VIRTUAL, L"UsersFilesFolder", NULL, NULL, L"::{59031a47-3f72-44a7-89c5-5595fe6b30ee}", 0, (KF_DEFINITION_FLAGS)(0) },
    { &FOLDERID_UsersLibraries, KF_CATEGORY_VIRTUAL, L"UsersLibrariesFolder", NULL, NULL, L"::{031E4825-7B94-4dc3-B131-E946B44C8DD5}", 0, (KF_DEFINITION_FLAGS)(0) },
    { &FOLDERID_VideosLibrary, KF_CATEGORY_PERUSER, L"VideosLibrary", NULL, L"Videos.library-ms", L"::{031E4825-7B94-4dc3-B131-E946B44C8DD5}\\{491E922F-5643-4af4-A7EB-4E7A138D8174}", 0, (KF_DEFINITION_FLAGS)(0) },
    { &FOLDERID_AccountPictures, KF_CATEGORY_PERUSER, L"AccountPictures", &FOLDERID_RoamingAppData, L"Microsoft\\Windows\\AccountPictures", NULL, FILE_ATTRIBUTE_READONLY, (KF_DEFINITION_FLAGS)(KFDF_PRECREATE | KFDF_ROAMABLE) },
    { &FOLDERID_Screenshots, KF_CATEGORY_PERUSER, L"Screenshots", &FOLDERID_Pictures, L"Screenshots", NULL, 0, (KF_DEFINITION_FLAGS)(KFDF_PRECREATE | KFDF_ROAMABLE) },
    { &FOLDERID_AppDataDocuments, KF_CATEGORY_PERUSER, L"AppDataDocuments", &FOLDERID_LocalAppData, L"Documents", NULL, 0, (KF_DEFINITION_FLAGS)(KFDF_PRECREATE | KFDF_ROAMABLE) },
};

static const KNOWN_FOLDER_DEF *FindKnownFolderDefinition(REFKNOWNFOLDERID id)
{
    for (UINT i = 0; i < _countof(KnownFolderDefs); ++i)
    {
        if (IsEqualGUID(*KnownFolderDefs[i].Id, id))
            return &KnownFolderDefs[i];
    }
    return NULL;
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
    { return m_category == KF_CATEGORY_FIXED ? E_INVALIDARG : E_NOTIMPL; }
    STDMETHOD(GetIDList)(DWORD flags, PIDLIST_ABSOLUTE *pidl) override
    { return SHGetKnownFolderIDList(m_id, flags, NULL, pidl); }
    STDMETHOD(GetFolderType)(FOLDERTYPEID *type) override
    { if (!type) return E_POINTER; *type = GUID_NULL; return E_NOTIMPL; }
    STDMETHOD(GetRedirectionCapabilities)(KF_REDIRECTION_CAPABILITIES *caps) override
    {
        if (!caps) return E_POINTER;
        *caps = m_category == KF_CATEGORY_FIXED ? (KF_REDIRECTION_CAPABILITIES)0 : KF_REDIRECTION_CAPABILITIES_DENY_ALL;
        return S_OK;
    }
    STDMETHOD(GetFolderDefinition)(KNOWNFOLDER_DEFINITION *definition) override
    {
        const KNOWN_FOLDER_DEF *def;
        HRESULT hr;
        if (!definition) return E_INVALIDARG;
        ZeroMemory(definition, sizeof(*definition));
        def = FindKnownFolderDefinition(m_id);
        if (!def) return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
        definition->category = def->Category;
        if (def->Parent) definition->fidParent = *def->Parent;
        definition->dwAttributes = def->Attributes;
        definition->kfdFlags = def->Flags;
        hr = SHStrDupW(def->Name, &definition->pszName);
        if (SUCCEEDED(hr) && def->RelativePath)
            hr = SHStrDupW(def->RelativePath, &definition->pszRelativePath);
        if (SUCCEEDED(hr) && def->ParsingName)
            hr = SHStrDupW(def->ParsingName, &definition->pszParsingName);
        if (FAILED(hr))
        {
            CoTaskMemFree(definition->pszName);
            CoTaskMemFree(definition->pszRelativePath);
            CoTaskMemFree(definition->pszParsingName);
            ZeroMemory(definition, sizeof(*definition));
        }
        return hr;
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
