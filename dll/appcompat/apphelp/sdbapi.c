/*
 * PROJECT:     ReactOS Application compatibility module
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Sdb low level glue layer
 * COPYRIGHT:   Copyright 2011 André Hentschel
 *              Copyright 2013 Mislav Blaževic
 *              Copyright 2015-2019 Mark Jansen (mark.jansen@reactos.org)
 */

#include "ntndk.h"
#include "strsafe.h"
#include "apphelp.h"
#include "sdbstringtable.h"


static const GUID GUID_DATABASE_MSI = {0xd8ff6d16,0x6a3a,0x468a, {0x8b,0x44,0x01,0x71,0x4d,0xdc,0x49,0xea}};
static const GUID GUID_DATABASE_SHIM = {0x11111111,0x1111,0x1111, {0x11,0x11,0x11,0x11,0x11,0x11,0x11,0x11}};
static const GUID GUID_DATABASE_DRIVERS = {0xf9ab2228,0x3312,0x4a73, {0xb6,0xf9,0x93,0x6d,0x70,0xe1,0x12,0xef}};

static HANDLE SdbpHeap(void);

#if SDBAPI_DEBUG_ALLOC

/* dbgheap.c */
void SdbpInsertAllocation(PVOID address, SIZE_T size, int line, const char* file);
void SdbpUpdateAllocation(PVOID address, PVOID newaddress, SIZE_T size, int line, const char* file);
void SdbpRemoveAllocation(PVOID address, int line, const char* file);
void SdbpDebugHeapInit(HANDLE privateHeapPtr);
void SdbpDebugHeapDeinit(void);

#endif

static HANDLE g_Heap;
void SdbpHeapInit(void)
{
    g_Heap = RtlCreateHeap(HEAP_GROWABLE, NULL, 0, 0x10000, NULL, NULL);
#if SDBAPI_DEBUG_ALLOC
    SdbpDebugHeapInit(g_Heap);
#endif
}

void SdbpHeapDeinit(void)
{
#if SDBAPI_DEBUG_ALLOC
    SdbpDebugHeapDeinit();
#endif
    RtlDestroyHeap(g_Heap);
}

static HANDLE SdbpHeap(void)
{
    return g_Heap;
}

LPVOID SdbpAlloc(SIZE_T size
#if SDBAPI_DEBUG_ALLOC
    , int line, const char* file
#endif
    )
{
    LPVOID mem = RtlAllocateHeap(SdbpHeap(), HEAP_ZERO_MEMORY, size);
#if SDBAPI_DEBUG_ALLOC
    SdbpInsertAllocation(mem, size, line, file);
#endif
    return mem;
}

LPVOID SdbpReAlloc(LPVOID mem, SIZE_T size, SIZE_T oldSize
#if SDBAPI_DEBUG_ALLOC
    , int line, const char* file
#endif
    )
{
    LPVOID newmem = RtlReAllocateHeap(SdbpHeap(), HEAP_ZERO_MEMORY, mem, size);
#if SDBAPI_DEBUG_ALLOC
    SdbpUpdateAllocation(mem, newmem, size, line, file);
#endif
    return newmem;
}

void SdbpFree(LPVOID mem
#if SDBAPI_DEBUG_ALLOC
    , int line, const char* file
#endif
    )
{
#if SDBAPI_DEBUG_ALLOC
    SdbpRemoveAllocation(mem, line, file);
#endif
    RtlFreeHeap(SdbpHeap(), 0, mem);
}

PDB WINAPI SdbpCreate(LPCWSTR path, PATH_TYPE type, BOOL write)
{
    NTSTATUS Status;
    IO_STATUS_BLOCK io;
    OBJECT_ATTRIBUTES attr;
    UNICODE_STRING str;
    PDB pdb;

    if (type == DOS_PATH)
    {
        if (!RtlDosPathNameToNtPathName_U(path, &str, NULL, NULL))
            return NULL;
    }
    else
    {
        RtlInitUnicodeString(&str, path);
    }

    /* SdbAlloc zeroes the memory. */
    pdb = (PDB)SdbAlloc(sizeof(DB));
    if (!pdb)
    {
        SHIM_ERR("Failed to allocate memory for shim database\n");
        return NULL;
    }

    InitializeObjectAttributes(&attr, &str, OBJ_CASE_INSENSITIVE, NULL, NULL);

    Status = NtCreateFile(&pdb->file, (write ? FILE_GENERIC_WRITE : FILE_GENERIC_READ )| SYNCHRONIZE,
                          &attr, &io, NULL, FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ,
                          write ? FILE_SUPERSEDE : FILE_OPEN, FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT, NULL, 0);

    pdb->for_write = write;

    if (type == DOS_PATH)
        RtlFreeUnicodeString(&str);

    if (!NT_SUCCESS(Status))
    {
        SdbCloseDatabase(pdb);
        SHIM_ERR("Failed to create shim database file: %lx\n", Status);
        return NULL;
    }

    return pdb;
}

void WINAPI SdbpFlush(PDB pdb)
{
    IO_STATUS_BLOCK io;
    NTSTATUS Status;

    ASSERT(pdb->for_write);
    Status = NtWriteFile(pdb->file, NULL, NULL, NULL, &io,
        pdb->data, pdb->write_iter, NULL, NULL);
    if (!NT_SUCCESS(Status))
        SHIM_WARN("failed with 0x%lx\n", Status);
}

DWORD SdbpStrlen(PCWSTR string)
{
    return (DWORD)wcslen(string);
}

DWORD SdbpStrsize(PCWSTR string)
{
    return (SdbpStrlen(string) + 1) * sizeof(WCHAR);
}

PWSTR SdbpStrDup(LPCWSTR string)
{
    PWSTR ret = SdbAlloc(SdbpStrsize(string));
    wcscpy(ret, string);
    return ret;
}


BOOL WINAPI SdbpOpenMemMappedFile(LPCWSTR path, PMEMMAPPED mapping)
{
    NTSTATUS Status;
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    FILE_STANDARD_INFORMATION FileStandard;
    UNICODE_STRING FileName;

    RtlZeroMemory(mapping, sizeof(*mapping));

    RtlInitUnicodeString(&FileName, path);

    InitializeObjectAttributes(&ObjectAttributes, &FileName, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtOpenFile(&mapping->file, GENERIC_READ | SYNCHRONIZE, &ObjectAttributes, &IoStatusBlock, FILE_SHARE_READ, FILE_SYNCHRONOUS_IO_NONALERT);

    if (Status == STATUS_OBJECT_NAME_INVALID || Status == STATUS_OBJECT_PATH_SYNTAX_BAD)
    {
        if (!RtlDosPathNameToNtPathName_U(path, &FileName, NULL, NULL))
        {
            SHIM_ERR("Failed to convert %S to Nt path: 0x%lx\n", path, Status);
            return FALSE;
        }
        InitializeObjectAttributes(&ObjectAttributes, &FileName, OBJ_CASE_INSENSITIVE, NULL, NULL);
        Status = NtOpenFile(&mapping->file, GENERIC_READ | SYNCHRONIZE, &ObjectAttributes, &IoStatusBlock, FILE_SHARE_READ, FILE_SYNCHRONOUS_IO_NONALERT);
        RtlFreeUnicodeString(&FileName);
    }

    if (!NT_SUCCESS(Status))
    {
        SHIM_ERR("Failed to open file %S: 0x%lx\n", path, Status);
        return FALSE;
    }

    Status = NtCreateSection(&mapping->section, STANDARD_RIGHTS_REQUIRED | SECTION_QUERY | SECTION_MAP_READ, 0, 0, PAGE_READONLY, SEC_COMMIT, mapping->file);
    if (!NT_SUCCESS(Status))
    {
        /* Special case */
        if (Status == STATUS_MAPPED_FILE_SIZE_ZERO)
        {
            NtClose(mapping->file);
            mapping->file = mapping->section = NULL;
            return TRUE;
        }
        SHIM_ERR("Failed to create mapping for file: 0x%lx\n", Status);
        goto err_out;
    }

    Status = NtQueryInformationFile(mapping->file, &IoStatusBlock, &FileStandard, sizeof(FileStandard), FileStandardInformation);
    if (!NT_SUCCESS(Status))
    {
        SHIM_ERR("Failed to read file info for file: 0x%lx\n", Status);
        goto err_out;
    }

    mapping->mapped_size = mapping->size = FileStandard.EndOfFile.LowPart;
    Status = NtMapViewOfSection(mapping->section, NtCurrentProcess(), (PVOID*)&mapping->view, 0, 0, 0, &mapping->mapped_size, ViewUnmap, 0, PAGE_READONLY);
    if (!NT_SUCCESS(Status))
    {
        SHIM_ERR("Failed to map view of file: 0x%lx\n", Status);
        goto err_out;
    }

    return TRUE;

err_out:
    if (!mapping->view)
    {
        if (mapping->section)
            NtClose(mapping->section);
        NtClose(mapping->file);
    }
    return FALSE;
}

void WINAPI SdbpCloseMemMappedFile(PMEMMAPPED mapping)
{
    /* Prevent a VAD warning */
    if (mapping->view)
        NtUnmapViewOfSection(NtCurrentProcess(), mapping->view);
    NtClose(mapping->section);
    NtClose(mapping->file);
    RtlZeroMemory(mapping, sizeof(*mapping));
}

BOOL WINAPI SdbpCheckTagType(TAG tag, WORD type)
{
    if ((tag & TAG_TYPE_MASK) != type)
        return FALSE;
    return TRUE;
}

BOOL WINAPI SdbpCheckTagIDType(PDB pdb, TAGID tagid, WORD type)
{
    TAG tag = SdbGetTagFromTagID(pdb, tagid);
    if (tag == TAG_NULL)
        return FALSE;
    return SdbpCheckTagType(tag, type);
}

PDB SdbpOpenDatabase(LPCWSTR path, PATH_TYPE type)
{
    IO_STATUS_BLOCK io;
    FILE_STANDARD_INFORMATION fsi;
    PDB pdb;
    NTSTATUS Status;
    BYTE header[12];

    pdb = SdbpCreate(path, type, FALSE);
    if (!pdb)
        return NULL;

    Status = NtQueryInformationFile(pdb->file, &io, &fsi, sizeof(FILE_STANDARD_INFORMATION), FileStandardInformation);
    if (!NT_SUCCESS(Status))
    {
        SdbCloseDatabase(pdb);
        SHIM_ERR("Failed to get shim database size: 0x%lx\n", Status);
        return NULL;
    }

    pdb->size = fsi.EndOfFile.u.LowPart;
    pdb->data = SdbAlloc(pdb->size);
    Status = NtReadFile(pdb->file, NULL, NULL, NULL, &io, pdb->data, pdb->size, NULL, NULL);

    if (!NT_SUCCESS(Status))
    {
        SdbCloseDatabase(pdb);
        SHIM_ERR("Failed to open shim database file: 0x%lx\n", Status);
        return NULL;
    }

    if (!SdbpReadData(pdb, &header, 0, 12))
    {
        SdbCloseDatabase(pdb);
        SHIM_ERR("Failed to read shim database header\n");
        return NULL;
    }

    if (memcmp(&header[8], "sdbf", 4) != 0)
    {
        SdbCloseDatabase(pdb);
        SHIM_ERR("Shim database header is invalid\n");
        return NULL;
    }

    pdb->major = *(DWORD*)&header[0];
    pdb->minor = *(DWORD*)&header[4];

    return pdb;
}


/**
 * Opens specified shim database file.
 *
 * @param [in]  path    Path to the shim database.
 * @param [in]  type    Type of path. Either DOS_PATH or NT_PATH.
 *
 * @return  Success: Handle to the shim database, NULL otherwise.
 */
PDB WINAPI SdbOpenDatabase(LPCWSTR path, PATH_TYPE type)
{
    PDB pdb;
    TAGID root, name;

    pdb = SdbpOpenDatabase(path, type);
    if (!pdb)
        return NULL;

    if (pdb->major != 2 && pdb->major != 3)
    {
        SdbCloseDatabase(pdb);
        SHIM_ERR("Invalid shim database version\n");
        return NULL;
    }

    pdb->stringtable = SdbFindFirstTag(pdb, TAGID_ROOT, TAG_STRINGTABLE);
    if (!SdbGetDatabaseID(pdb, &pdb->database_id))
    {
        SHIM_INFO("Failed to get the database id\n");
    }

    root = SdbFindFirstTag(pdb, TAGID_ROOT, TAG_DATABASE);
    if (root != TAGID_NULL)
    {
        name = SdbFindFirstTag(pdb, root, TAG_NAME);
        if (name != TAGID_NULL)
        {
            pdb->database_name = SdbGetStringTagPtr(pdb, name);
        }
    }
    if (!pdb->database_name)
    {
        SHIM_INFO("Failed to get the database name\n");
    }

    return pdb;
}

/**
 * Closes specified database and frees its memory.
 *
 * @param [in]  pdb  Handle to the shim database.
 */
void WINAPI SdbCloseDatabase(PDB pdb)
{
    if (!pdb)
        return;

    if (pdb->file)
        NtClose(pdb->file);
    if (pdb->string_buffer)
        SdbCloseDatabase(pdb->string_buffer);
    if (pdb->string_lookup)
        SdbpTableDestroy(&pdb->string_lookup);
    SdbFree(pdb->data);
    SdbFree(pdb);
}

/**
 * Parses a string to retrieve a GUID.
 *
 * @param [in]  GuidString  The string to parse.
 * @param [out] Guid        The resulting GUID.
 *
 * @return  TRUE if it succeeds, FALSE if it fails.
 */
BOOL WINAPI SdbGUIDFromString(PCWSTR GuidString, GUID *Guid)
{
    UNICODE_STRING GuidString_u;
    RtlInitUnicodeString(&GuidString_u, GuidString);
    return NT_SUCCESS(RtlGUIDFromString(&GuidString_u, Guid));
}

/**
 * Converts a GUID to a string.
 *
 * @param [in]  Guid        The GUID to convert.
 * @param [out] GuidString  The resulting string representation of Guid.
 * @param [in]  Length      The length of GuidString.
 *
 * @return  TRUE if it succeeds, FALSE if it fails.
 */
BOOL WINAPI SdbGUIDToString(CONST GUID *Guid, PWSTR GuidString, SIZE_T Length)
{
    UNICODE_STRING GuidString_u;
    if (NT_SUCCESS(RtlStringFromGUID(Guid, &GuidString_u)))
    {
        HRESULT hr = StringCchCopyNW(GuidString, Length, GuidString_u.Buffer, GuidString_u.Length / sizeof(WCHAR));
        RtlFreeUnicodeString(&GuidString_u);
        return SUCCEEDED(hr);
    }
    return FALSE;
}

/**
 * Checks if the specified GUID is a NULL GUID
 *
 * @param [in]  Guid    The GUID to check.
 *
 * @return  TRUE if it is a NULL GUID.
 */
BOOL WINAPI SdbIsNullGUID(CONST GUID *Guid)
{
    static GUID NullGuid = { 0 };
    return !Guid || IsEqualGUID(&NullGuid, Guid);
}

/**
 * Get the GUID from one of the standard databases.
 *
 * @param [in]  Flags   The ID to retrieve the guid from. (See SDB_DATABASE_MAIN_[xxx])
 * @param [out] Guid    The resulting GUID.
 *
 * @return  TRUE if a known database ID.
 */
BOOL WINAPI SdbGetStandardDatabaseGUID(DWORD Flags, GUID* Guid)
{
    const GUID* copy_from = NULL;
    switch(Flags & HID_DATABASE_TYPE_MASK)
    {
    case SDB_DATABASE_MAIN_MSI:
        copy_from = &GUID_DATABASE_MSI;
        break;
    case SDB_DATABASE_MAIN_SHIM:
        copy_from = &GUID_DATABASE_SHIM;
        break;
    case SDB_DATABASE_MAIN_DRIVERS:
        copy_from = &GUID_DATABASE_DRIVERS;
        break;
    default:
        SHIM_ERR("Cannot obtain database guid for databases other than main\n");
        return FALSE;
    }
    if (Guid)
    {
        memcpy(Guid, copy_from, sizeof(GUID));
    }
    return TRUE;
}

/**
 * Read the database version from the specified database.
 *
 * @param [in]  database    The database.
 * @param [out] VersionHi   The first part of the version number.
 * @param [out] VersionLo   The second part of the version number.
 *
 * @return  TRUE if it succeeds or fails, FALSE if ???
 */
BOOL WINAPI SdbGetDatabaseVersion(LPCWSTR database, PDWORD VersionHi, PDWORD VersionLo)
{
    PDB pdb;

    pdb = SdbpOpenDatabase(database, DOS_PATH);
    if (pdb)
    {
        *VersionHi = pdb->major;
        *VersionLo = pdb->minor;
        SdbCloseDatabase(pdb);
    }

    return TRUE;
}

/**
 * @name SdbGetDatabaseInformation
 * Get information about the database
 *
 * @param pdb           The database
 * @param information   The returned information
 * @return TRUE on success
 */
BOOL WINAPI SdbGetDatabaseInformation(PDB pdb, PDB_INFORMATION information)
{
    RtlZeroMemory(information, sizeof(*information));

    if (pdb)
    {
        information->dwFlags = 0;
        information->dwMajor = pdb->major;
        information->dwMinor = pdb->minor;
        information->Description = pdb->database_name;
        if (!SdbIsNullGUID(&pdb->database_id))
        {
            information->dwFlags |= DB_INFO_FLAGS_VALID_GUID;
            information->Id = pdb->database_id;
        }
        return TRUE;
    }

    return FALSE;
}

/**
 * @unimplemented
 * @name SdbGetDatabaseInformationByName
 * Get information about the database
 *
 * @param lpwszFileName The database file
 * @param ppAttrInfo    The returned information, allocated by this function
 * @return TRUE on success
 */
BOOL WINAPI
SdbGetDatabaseInformationByName(_In_ LPCWSTR lpwszFileName, _Outptr_ PDB_INFORMATION *ppAttrInfo)
{
    SHIM_ERR("Unimplemented\n");
    *ppAttrInfo = NULL;
    return FALSE;
}

/**
 * @unimplemented
 * @name SdbFreeDatabaseInformation
 * Free up resources allocated in SdbGetDatabaseInformationByName
 *
 * @param information   The information retrieved from SdbGetDatabaseInformationByName
 */
VOID WINAPI SdbFreeDatabaseInformation(_In_opt_ PDB_INFORMATION information)
{
    SHIM_ERR("Unimplemented\n");
}

/**
 * Find the first named child tag.
 *
 * @param [in]  pdb         The database.
 * @param [in]  root        The tag to start at
 * @param [in]  find        The tag type to find
 * @param [in]  nametag     The child of 'find' that contains the name
 * @param [in]  find_name   The name to find
 *
 * @return  The found tag, or TAGID_NULL on failure
 */
TAGID WINAPI SdbFindFirstNamedTag(PDB pdb, TAGID root, TAGID find, TAGID nametag, LPCWSTR find_name)
{
    TAGID iter;

    iter = SdbFindFirstTag(pdb, root, find);

    while (iter != TAGID_NULL)
    {
        TAGID tmp = SdbFindFirstTag(pdb, iter, nametag);
        if (tmp != TAGID_NULL)
        {
            LPCWSTR name = SdbGetStringTagPtr(pdb, tmp);
            if (name && !_wcsicmp(name, find_name))
                return iter;
        }
        iter = SdbFindNextTag(pdb, root, iter);
    }
    return TAGID_NULL;
}


/**
 * Find a named layer in a multi-db.
 *
 * @param [in]  hsdb        The multi-database.
 * @param [in]  layerName   The named tag to find.
 *
 * @return  The layer, or TAGREF_NULL on failure
 */
TAGREF WINAPI SdbGetLayerTagRef(HSDB hsdb, LPCWSTR layerName)
{
    PDB pdb = hsdb->pdb;

    TAGID database = SdbFindFirstTag(pdb, TAGID_ROOT, TAG_DATABASE);
    if (database != TAGID_NULL)
    {
        TAGID layer = SdbFindFirstNamedTag(pdb, database, TAG_LAYER, TAG_NAME, layerName);
        if (layer != TAGID_NULL)
        {
            TAGREF tr;
            if (SdbTagIDToTagRef(hsdb, pdb, layer, &tr))
            {
                return tr;
            }
        }
    }
    return TAGREF_NULL;
}


#ifndef REG_SZ
#define REG_SZ 1
#define REG_DWORD 4
#define REG_QWORD 11
#endif


/**
 * Retrieve a Data entry
 *
 * @param [in]  pdb                     The database.
 * @param [in]  tiExe                   The tagID to start at
 * @param [in,opt]  lpszDataName        The name of the Data entry to find, or NULL to return all.
 * @param [out,opt]  lpdwDataType       Any of REG_SZ, REG_QWORD, REG_DWORD, ...
 * @param [out]  lpBuffer               The output buffer
 * @param [in,out,opt]  lpcbBufferSize  The size of lpBuffer in bytes
 * @param [out,opt]  ptiData            The tagID of the data
 *
 * @return  ERROR_SUCCESS
 */
DWORD WINAPI SdbQueryDataExTagID(PDB pdb, TAGID tiExe, LPCWSTR lpszDataName, LPDWORD lpdwDataType, LPVOID lpBuffer, LPDWORD lpcbBufferSize, TAGID *ptiData)
{
    TAGID tiData, tiValueType, tiValue;
    DWORD dwDataType, dwSizeRequired, dwInputSize;
    LPCWSTR lpStringData = NULL;
    /* Not supported yet */
    if (!lpszDataName)
        return ERROR_INVALID_PARAMETER;

    tiData = SdbFindFirstNamedTag(pdb, tiExe, TAG_DATA, TAG_NAME, lpszDataName);
    if (tiData == TAGID_NULL)
    {
        SHIM_INFO("No data tag found\n");
        return ERROR_NOT_FOUND;
    }

    if (ptiData)
        *ptiData = tiData;

    tiValueType = SdbFindFirstTag(pdb, tiData, TAG_DATA_VALUETYPE);
    if (tiValueType == TAGID_NULL)
    {
        SHIM_WARN("Data tag (0x%x) without valuetype\n", tiData);
        return ERROR_INTERNAL_DB_CORRUPTION;
    }

    dwDataType = SdbReadDWORDTag(pdb, tiValueType, 0);
    switch (dwDataType)
    {
    case REG_SZ:
        tiValue = SdbFindFirstTag(pdb, tiData, TAG_DATA_STRING);
        break;
    case REG_DWORD:
        tiValue = SdbFindFirstTag(pdb, tiData, TAG_DATA_DWORD);
        break;
    case REG_QWORD:
        tiValue = SdbFindFirstTag(pdb, tiData, TAG_DATA_QWORD);
        break;
    default:
        /* Not supported (yet) */
        SHIM_WARN("Unsupported dwDataType=0x%x\n", dwDataType);
        return ERROR_INVALID_PARAMETER;
    }

    if (lpdwDataType)
        *lpdwDataType = dwDataType;

    if (tiValue == TAGID_NULL)
    {
        SHIM_WARN("Data tag (0x%x) without data\n", tiData);
        return ERROR_INTERNAL_DB_CORRUPTION;
    }

    if (dwDataType != REG_SZ)
    {
        dwSizeRequired = SdbGetTagDataSize(pdb, tiValue);
    }
    else
    {
        lpStringData = SdbpGetString(pdb, tiValue, &dwSizeRequired);
        if (lpStringData == NULL)
        {
            return ERROR_INTERNAL_DB_CORRUPTION;
        }
    }
    if (!lpcbBufferSize)
        return ERROR_INSUFFICIENT_BUFFER;

    dwInputSize = *lpcbBufferSize;
    *lpcbBufferSize = dwSizeRequired;

    if (dwInputSize < dwSizeRequired || lpBuffer == NULL)
    {
        SHIM_WARN("dwInputSize %u not sufficient to hold %u bytes\n", dwInputSize, dwSizeRequired);
        return ERROR_INSUFFICIENT_BUFFER;
    }

    if (dwDataType != REG_SZ)
    {
        SdbpReadData(pdb, lpBuffer, tiValue + sizeof(TAG), dwSizeRequired);
    }
    else
    {
        StringCbCopyNW(lpBuffer, dwInputSize, lpStringData, dwSizeRequired);
    }

    return ERROR_SUCCESS;
}


/**
 * Converts the specified string to an index key.
 *
 * @param [in]  str The string which will be converted.
 *
 * @return  The resulting index key
 *
 * @todo: Fix this for unicode strings.
 */
LONGLONG WINAPI SdbMakeIndexKeyFromString(LPCWSTR str)
{
    LONGLONG result = 0;
    int shift = 56;

    while (*str && shift >= 0)
    {
        WCHAR c = toupper(*(str++));

        if (c & 0xff)
        {
            result |= (((LONGLONG)(c & 0xff)) << shift);
            shift -= 8;
        }

        if (shift < 0)
            break;

        c >>= 8;

        if (c & 0xff)
        {
            result |= (((LONGLONG)(c & 0xff)) << shift);
            shift -= 8;
        }
    }

    return result;
}


static const PCWSTR g_TagNullNames[] =
{
    L"InvalidTag", L"INCLUDE", L"GENERAL", L"MATCH_LOGIC_NOT", L"APPLY_ALL_SHIMS", L"USE_SERVICE_PACK_FILES",
    L"MITIGATION_OS", L"TRACE_PCA", L"INCLUDEEXCLUDEDLL", L"RAC_EVENT_OFF", L"TELEMETRY_OFF", L"SHIM_ENGINE_OFF",
    L"LAYER_PROPAGATION_OFF", L"FORCE_CACHE", L"MONITORING_OFF", L"QUIRK_OFF", L"ELEVATED_PROP_OFF",
    L"UPGRADE_ACTION_BLOCK_WEBSETUP", L"UPGRADE_ACTION_PROCEED_TO_MEDIASETUP", L"HWCOMPAT_DEVICE", L"HWEXCLUDE_DEVICE",
    L"WUCOMPAT_DEVICE", L"APPEND_COMMANDLINE", L"COMPARE_CASE", L"InvalidTag", L"InvalidTag", L"MATCHED_OBJECT",
    L"USE_INVENTORY"
};

static const PCWSTR g_TagWordNames[] =
{
    L"InvalidTag", L"MATCH_MODE", L"QUIRK_COMPONENT_CODE_ID", L"QUIRK_CODE_ID"
};

static const PCWSTR g_TagWordNamesIndex[] =
{
    L"InvalidTag", L"TAG", L"INDEX_TAG", L"INDEX_KEY"
};

static const PCWSTR g_TagDwordNames[] =
{
    L"InvalidTag", L"SIZE", L"OFFSET", L"CHECKSUM", L"SHIM_TAGID", L"PATCH_TAGID", L"MODULE_TYPE", L"VERDATEHI",
    L"VERDATELO", L"VERFILEOS", L"VERFILETYPE", L"PE_CHECKSUM", L"PREVOSMAJORVER", L"PREVOSMINORVER",
    L"PREVOSPLATFORMID", L"PREVOSBUILDNO", L"PROBLEMSEVERITY", L"LANGID", L"VER_LANGUAGE", L"OS_KIND", L"ENGINE",
    L"HTMLHELPID", L"INDEX_FLAGS", L"FLAGS", L"DATA_VALUETYPE", L"DATA_DWORD", L"LAYER_TAGID", L"MSI_TRANSFORM_TAGID",
    L"LINKER_VERSION", L"LINK_DATE", L"UPTO_LINK_DATE", L"InvalidTag", L"FLAG_TAGID", L"RUNTIME_PLATFORM",
    L"InvalidTag", L"GUEST_TARGET_PLATFORM", L"APP_NAME_RC_ID", L"VENDOR_NAME_RC_ID", L"SUMMARY_MSG_RC_ID",
    L"InvalidTag", L"DESCRIPTION_RC_ID", L"PARAMETER1_RC_ID", L"HWCOMPAT_HWID_COUNT", L"TITLE_MSG_RC_ID_BACKUP",
    L"SUMMARY_MSG_RC_ID_BACKUP", L"InvalidTag", L"InvalidTag", L"InvalidTag", L"CONTEXT_TAGID", L"EXE_WRAPPER",
    L"EXE_TYPE", L"FROM_LINK_DATE", L"REVISION_EQ", L"REVISION_LE", L"REVISION_GE", L"DATE_EQ", L"DATE_LE", L"DATE_GE",
    L"CPU_MODEL_EQ", L"CPU_MODEL_LE", L"CPU_MODEL_GE", L"CPU_FAMILY_EQ", L"CPU_FAMILY_LE", L"CPU_FAMILY_GE",
    L"CREATOR_REVISION_EQ", L"CREATOR_REVISION_LE", L"CREATOR_REVISION_GE", L"SIZE_OF_IMAGE", L"SHIM_CLASS",
    L"PACKAGEID_ARCHITECTURE", L"REINSTALL_UPGRADE_TYPE", L"BLOCK_UPGRADE_TYPE", L"ROUTING_MODE", L"OS_VERSION_VALUE",
    L"CRC_CHECKSUM", L"URL_ID", L"QUIRK_TAGID", L"InvalidTag", L"MIGRATION_DATA_TYPE", L"UPGRADE_DATA",
    L"MIGRATION_DATA_TAGID", L"REG_VALUE_TYPE", L"REG_VALUE_DATA_DWORD", L"TEXT_ENCODING", L"UX_BLOCKTYPE_OVERRIDE",
    L"EDITION", L"FW_LINK_ID", L"KB_ARTICLE_ID", L"InvalidTag", L"TITLE_MSG_RC_ID", L"LINK_TEXT_RC_ID",
    L"LINK_TEXT_RC_ID_BACKUP", L"InvalidTag", L"InvalidTag", L"InvalidTag", L"InvalidTag", L"REQUESTED_ATTRIBUTES",
    L"BACKUP_LABEL"
};

static const PCWSTR g_TagDwordNamesIndex[] =
{
    L"InvalidTag", L"TAGID"
};

static const PCWSTR g_TagQwordNames[] =
{
    L"InvalidTag", L"TIME", L"BIN_FILE_VERSION", L"BIN_PRODUCT_VERSION", L"MODTIME", L"FLAG_MASK_KERNEL",
    L"UPTO_BIN_PRODUCT_VERSION", L"DATA_QWORD", L"FLAG_MASK_USER", L"FLAGS_NTVDM1", L"FLAGS_NTVDM2", L"FLAGS_NTVDM3",
    L"FLAG_MASK_SHELL", L"UPTO_BIN_FILE_VERSION", L"FLAG_MASK_FUSION", L"FLAG_PROCESSPARAM", L"FLAG_LUA",
    L"FLAG_INSTALL", L"FROM_BIN_PRODUCT_VERSION", L"FROM_BIN_FILE_VERSION", L"PACKAGEID_VERSION",
    L"FROM_PACKAGEID_VERSION", L"UPTO_PACKAGEID_VERSION", L"OSMAXVERSIONTESTED", L"FROM_OSMAXVERSIONTESTED",
    L"UPTO_OSMAXVERSIONTESTED", L"FLAG_MASK_WINRT", L"REG_VALUE_DATA_QWORD", L"QUIRK_ENABLED_VERSION_LT", L"SOURCE_OS",
    L"SOURCE_OS_LTE", L"SOURCE_OS_GTE", L"FILESIZE"
};

static const PCWSTR g_TagStringRefNames[] =
{
    L"InvalidTag", L"NAME", L"DESCRIPTION", L"MODULE", L"API", L"VENDOR", L"APP_NAME", L"InvalidTag", L"COMMAND_LINE",
    L"COMPANY_NAME", L"DLLFILE", L"WILDCARD_NAME", L"InvalidTag", L"InvalidTag", L"InvalidTag", L"InvalidTag",
    L"PRODUCT_NAME", L"PRODUCT_VERSION", L"FILE_DESCRIPTION", L"FILE_VERSION", L"ORIGINAL_FILENAME", L"INTERNAL_NAME",
    L"LEGAL_COPYRIGHT", L"16BIT_DESCRIPTION", L"APPHELP_DETAILS", L"LINK_URL", L"LINK_TEXT", L"APPHELP_TITLE",
    L"APPHELP_CONTACT", L"SXS_MANIFEST", L"DATA_STRING", L"MSI_TRANSFORM_FILE", L"16BIT_MODULE_NAME",
    L"LAYER_DISPLAYNAME", L"COMPILER_VERSION", L"ACTION_TYPE", L"EXPORT_NAME", L"VENDOR_ID", L"DEVICE_ID",
    L"SUB_VENDOR_ID", L"SUB_SYSTEM_ID", L"PACKAGEID_NAME", L"PACKAGEID_PUBLISHER", L"PACKAGEID_LANGUAGE", L"URL",
    L"MANUFACTURER", L"MODEL", L"DATE", L"REG_VALUE_NAME", L"REG_VALUE_DATA_SZ", L"MIGRATION_DATA_TEXT",
    L"APP_STORE_PRODUCT_ID", L"MORE_INFO_URL", L"DEST_OS_VALUE_DEF", L"DEST_OS_GTE", L"DEST_OS_LT", L"DEST_OS",
    L"PACKAGE_STRONGNAME", L"FALLBACK_XML", L"LINK_TEXT_OVERRIDE", L"MATCH_LOGIC_NOT_IF_SDB_CAPABILITY_EXISTS",
    L"ESCAPE_CHARACTER", L"InvalidTag", L"InvalidTag", L"InvalidTag", L"InvalidTag", L"PUBLISHER", L"MATCHING_LABEL",
    L"UPTO_PRODUCT_VERSION", L"UPTO_FILE_VERSION", L"FROM_PRODUCT_VERSION", L"FROM_FILE_VERSION", L"LANGUAGE"
};

static const PCWSTR g_TagListNames[] =
{
    L"InvalidTag", L"DATABASE", L"LIBRARY", L"INEXCLUDE", L"SHIM", L"PATCH", L"APP", L"EXE", L"MATCHING_FILE",
    L"SHIM_REF", L"PATCH_REF", L"LAYER", L"FILE", L"APPHELP", L"LINK", L"DATA", L"MSI_TRANSFORM", L"MSI_TRANSFORM_REF",
    L"MSI_PACKAGE", L"FLAG", L"MSI_CUSTOM_ACTION", L"FLAG_REF", L"ACTION", L"LOOKUP", L"CONTEXT", L"CONTEXT_REF",
    L"KDEVICE", L"InvalidTag", L"KDRIVER", L"InvalidTag", L"MATCHING_DEVICE", L"ACPI", L"BIOS", L"CPU", L"OEM",
    L"KFLAG", L"KFLAG_REF", L"KSHIM", L"KSHIM_REF", L"REINSTALL_UPGRADE", L"KDATA", L"BLOCK_UPGRADE", L"InvalidTag",
    L"QUIRK", L"QUIRK_REF", L"BIOS_BLOCK", L"MATCHING_INFO_BLOCK", L"DEVICE_BLOCK", L"MIGRATION_DATA",
    L"MIGRATION_DATA_REF", L"MATCHING_REG", L"MATCHING_TEXT", L"MACHINE_BLOCK", L"OS_UPGRADE", L"PACKAGE", L"PICK_ONE",
    L"MATCH_PLUGIN", L"MIGRATION_SHIM", L"UPGRADE_DRIVER_BLOCK", L"InvalidTag", L"MIGRATION_SHIM_REF", L"CONTAINS_FILE",
    L"CONTAINS_HWID", L"DRIVER_PACKAGE_BLOCK", L"DEST_OS_VALUES", L"XAP", L"HWCOMPAT_SOURCES", L"HWCOMPAT_SOURCE_INFO",
    L"C_STRUCT", L"PROCESS_MODULE", L"C_STRUCT_REF", L"MATCHING_WILDCARD_FILE", L"MATCHING_WILDCARD_REG",
    L"MATCHING_DIR", L"MATCHING_SDB_CAPABILITY", L"MATCHING_COMMAND_LINE", L"InvalidTag", L"InvalidTag", L"InvalidTag",
    L"InvalidTag", L"InvalidTag", L"InvalidTag", L"InvalidTag", L"InvalidTag", L"BACKUP_FILE", L"BACKUP_APPLICATION",
    L"BACKUP_PACKAGE", L"RESTORE_FILE", L"RESTORE_APPLICATION", L"RESTORE_PACKAGE", L"BACKUP_INCLUDE_FILE",
    L"MATCHING_BACKUP_FILE", L"MATCHING_WILDCARD_BACKUP_FILE", L"RESTORE_ACTION", L"MATCHING_BACKUP_LABEL",
    L"MATCHING_RESTORE_ACTION", L"MATCHING_APPLICATION_ATTRIBUTES"
};

static const PCWSTR g_TagListNamesIndex[] =
{
    L"InvalidTag", L"STRINGTABLE", L"INDEXES", L"INDEX"
};

static const PCWSTR g_TagStringNamesIndex[] =
{
    L"InvalidTag", L"STRINGTABLE_ITEM"
};

static const PCWSTR g_TagBinaryNames[] =
{
    L"InvalidTag", L"InvalidTag", L"PATCH_BITS", L"FILE_BITS", L"EXE_ID", L"DATA_BITS", L"MSI_PACKAGE_ID",
    L"DATABASE_ID", L"CONTEXT_PLATFORM_ID", L"CONTEXT_BRANCH_ID", L"XAP_ID", L"C_STRUCT_BIN_DATA", L"C_STRUCT_VERSION",
    L"InvalidTag", L"InvalidTag", L"InvalidTag", L"FIX_ID", L"APP_ID", L"REG_VALUE_DATA_BINARY", L"TEXT", L"BACKUP_ID"
};

static const PCWSTR g_TagBinaryNamesIndex[] =
{
    L"InvalidTag", L"INDEX_BITS"
};

static const struct
{
    TAG Base;
    ULONG Count;
    const PCWSTR *Names;
} g_TagNames[] =
{
    { TAG_TYPE_NULL, sizeof(g_TagNullNames) / sizeof(g_TagNullNames[0]), g_TagNullNames },
    { TAG_TYPE_WORD, sizeof(g_TagWordNames) / sizeof(g_TagWordNames[0]), g_TagWordNames },
    { TAG_TYPE_WORD | 0x800, sizeof(g_TagWordNamesIndex) / sizeof(g_TagWordNamesIndex[0]), g_TagWordNamesIndex },
    { TAG_TYPE_DWORD, sizeof(g_TagDwordNames) / sizeof(g_TagDwordNames[0]), g_TagDwordNames },
    { TAG_TYPE_DWORD | 0x800, sizeof(g_TagDwordNamesIndex) / sizeof(g_TagDwordNamesIndex[0]), g_TagDwordNamesIndex },
    { TAG_TYPE_QWORD, sizeof(g_TagQwordNames) / sizeof(g_TagQwordNames[0]), g_TagQwordNames },
    { TAG_TYPE_STRINGREF, sizeof(g_TagStringRefNames) / sizeof(g_TagStringRefNames[0]), g_TagStringRefNames },
    { TAG_TYPE_LIST, sizeof(g_TagListNames) / sizeof(g_TagListNames[0]), g_TagListNames },
    { TAG_TYPE_LIST | 0x800, sizeof(g_TagListNamesIndex) / sizeof(g_TagListNamesIndex[0]), g_TagListNamesIndex },
    { TAG_TYPE_STRING | 0x800, sizeof(g_TagStringNamesIndex) / sizeof(g_TagStringNamesIndex[0]), g_TagStringNamesIndex },
    { TAG_TYPE_BINARY, sizeof(g_TagBinaryNames) / sizeof(g_TagBinaryNames[0]), g_TagBinaryNames },
    { TAG_TYPE_BINARY | 0x800, sizeof(g_TagBinaryNamesIndex) / sizeof(g_TagBinaryNamesIndex[0]), g_TagBinaryNamesIndex },
};

/**
 * Converts specified tag into a string.
 *
 * @param [in]  tag The tag which will be converted to a string.
 *
 * @return  Success: Pointer to the string matching specified tag, or L"InvalidTag" on failure.
 *
 */
LPCWSTR WINAPI SdbTagToString(TAG tag)
{
    ULONG i;

    if (tag == TAG_NULL)
        return L"NULL";

    for (i = 0; i < ARRAYSIZE(g_TagNames); i++)
    {
        if ((tag & (TAG_TYPE_MASK | 0x800)) == g_TagNames[i].Base && (tag & 0x7FF) < g_TagNames[i].Count)
            return g_TagNames[i].Names[tag & 0x7FF];
    }
    return L"InvalidTag";
}
