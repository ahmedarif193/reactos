/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Portable ARM64EC UCRT export and redirection parity probe
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <windows.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

typedef BOOLEAN (WINAPI *PRTL_IS_EC_CODE)(ULONG_PTR Address);
typedef void *(__cdecl *PMEMCPY)(void *Destination, const void *Source, size_t Length);
typedef char *(__cdecl *PSTRCHR)(const char *String, int Character);
typedef char *(__cdecl *PSTRRCHR)(const char *String, int Character);
typedef char *(__cdecl *PSTRSTR)(const char *String, const char *Substring);
typedef size_t (__cdecl *PSTRLEN)(const char *String);
typedef wchar_t *(__cdecl *PWCSSCHR)(const wchar_t *String, wchar_t Character);
typedef wchar_t *(__cdecl *PWCSRCHR)(const wchar_t *String, wchar_t Character);
typedef wchar_t *(__cdecl *PWCSSTR)(const wchar_t *String, const wchar_t *Substring);

typedef struct _ARM64EC_METADATA
{
    ULONG Version;
    ULONG CodeMap;
    ULONG CodeMapCount;
    ULONG CodeRangesToEntryPoints;
    ULONG RedirectionMetadata;
    ULONG DispatchCallNoRedirect;
    ULONG DispatchRet;
    ULONG DispatchCall;
    ULONG DispatchIcall;
    ULONG DispatchIcallCfg;
    ULONG AlternateEntryPoint;
    ULONG AuxiliaryIat;
    ULONG CodeRangesToEntryPointsCount;
    ULONG RedirectionMetadataCount;
    ULONG GetX64InformationFunctionPointer;
    ULONG SetX64InformationFunctionPointer;
    ULONG ExtraRfeTable;
    ULONG ExtraRfeTableSize;
    ULONG DispatchFptr;
    ULONG AuxiliaryIatCopy;
} ARM64EC_METADATA, *PARM64EC_METADATA;

typedef struct _ARM64EC_REDIRECTION
{
    ULONG Source;
    ULONG Destination;
} ARM64EC_REDIRECTION, *PARM64EC_REDIRECTION;

static PVOID
find_raw_export(HMODULE Module, PCSTR Name, PDWORD ExportRva)
{
    PIMAGE_DOS_HEADER DosHeader = (PIMAGE_DOS_HEADER)Module;
    PIMAGE_NT_HEADERS64 NtHeader;
    PIMAGE_EXPORT_DIRECTORY ExportDirectory;
    PDWORD Names, Functions;
    PWORD Ordinals;
    DWORD DirectoryRva, DirectorySize, Index, FunctionRva;

    if (!Module || DosHeader->e_magic != IMAGE_DOS_SIGNATURE)
        return NULL;

    NtHeader = (PIMAGE_NT_HEADERS64)((PBYTE)Module + DosHeader->e_lfanew);
    if (NtHeader->Signature != IMAGE_NT_SIGNATURE || NtHeader->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
        return NULL;

    DirectoryRva = NtHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress;
    DirectorySize = NtHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].Size;
    if (!DirectoryRva || DirectoryRva >= NtHeader->OptionalHeader.SizeOfImage || DirectorySize > NtHeader->OptionalHeader.SizeOfImage - DirectoryRva)
        return NULL;

    ExportDirectory = (PIMAGE_EXPORT_DIRECTORY)((PBYTE)Module + DirectoryRva);
    Names = (PDWORD)((PBYTE)Module + ExportDirectory->AddressOfNames);
    Ordinals = (PWORD)((PBYTE)Module + ExportDirectory->AddressOfNameOrdinals);
    Functions = (PDWORD)((PBYTE)Module + ExportDirectory->AddressOfFunctions);
    for (Index = 0; Index < ExportDirectory->NumberOfNames; ++Index)
    {
        if (lstrcmpA((PCSTR)Module + Names[Index], Name))
            continue;

        if (Ordinals[Index] >= ExportDirectory->NumberOfFunctions)
            return NULL;

        FunctionRva = Functions[Ordinals[Index]];
        if (!FunctionRva || FunctionRva >= NtHeader->OptionalHeader.SizeOfImage || (FunctionRva >= DirectoryRva && FunctionRva < DirectoryRva + DirectorySize))
            return NULL;

        *ExportRva = FunctionRva;
        return (PBYTE)Module + FunctionRva;
    }

    return NULL;
}

static PARM64EC_METADATA
get_arm64ec_metadata(HMODULE Module, PIMAGE_NT_HEADERS64 *NtHeaderResult)
{
    PIMAGE_DOS_HEADER DosHeader = (PIMAGE_DOS_HEADER)Module;
    PIMAGE_NT_HEADERS64 NtHeader;
    PIMAGE_LOAD_CONFIG_DIRECTORY64 LoadConfig;
    DWORD LoadConfigRva, LoadConfigSize;

    if (!Module || DosHeader->e_magic != IMAGE_DOS_SIGNATURE)
        return NULL;

    NtHeader = (PIMAGE_NT_HEADERS64)((PBYTE)Module + DosHeader->e_lfanew);
    if (NtHeader->Signature != IMAGE_NT_SIGNATURE || NtHeader->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
        return NULL;

    *NtHeaderResult = NtHeader;
    LoadConfigRva = NtHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG].VirtualAddress;
    LoadConfigSize = NtHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG].Size;
    if (!LoadConfigRva || LoadConfigRva >= NtHeader->OptionalHeader.SizeOfImage || LoadConfigSize < offsetof(IMAGE_LOAD_CONFIG_DIRECTORY64, CHPEMetadataPointer) + sizeof(ULONGLONG))
        return NULL;

    LoadConfig = (PIMAGE_LOAD_CONFIG_DIRECTORY64)((PBYTE)Module + LoadConfigRva);
    if (LoadConfig->Size < offsetof(IMAGE_LOAD_CONFIG_DIRECTORY64, CHPEMetadataPointer) + sizeof(ULONGLONG) || !LoadConfig->CHPEMetadataPointer)
        return NULL;

    return (PARM64EC_METADATA)(ULONG_PTR)LoadConfig->CHPEMetadataPointer;
}

static ULONG
find_redirection(PARM64EC_METADATA Metadata, PIMAGE_NT_HEADERS64 NtHeader, HMODULE Module, ULONG Source)
{
    PARM64EC_REDIRECTION Redirections;
    ULONG Index;

    if (!Metadata || !Metadata->RedirectionMetadata || Metadata->RedirectionMetadata >= NtHeader->OptionalHeader.SizeOfImage || Metadata->RedirectionMetadataCount > (NtHeader->OptionalHeader.SizeOfImage - Metadata->RedirectionMetadata) / sizeof(*Redirections))
        return 0;

    Redirections = (PARM64EC_REDIRECTION)((PBYTE)Module + Metadata->RedirectionMetadata);
    for (Index = 0; Index < Metadata->RedirectionMetadataCount; ++Index)
    {
        if (Redirections[Index].Source == Source)
            return Redirections[Index].Destination;
    }

    return 0;
}

static PVOID
resolve_x64_import_jump(PVOID Function)
{
    PBYTE Code = (PBYTE)Function;
    LONG Displacement;

    if (Code[0] != 0xff || Code[1] != 0x25)
        return NULL;

    memcpy(&Displacement, Code + 2, sizeof(Displacement));
    return *(PVOID *)(Code + 6 + Displacement);
}

static VOID
report_export(HMODULE Module, PARM64EC_METADATA Metadata, PIMAGE_NT_HEADERS64 NtHeader, PRTL_IS_EC_CODE RtlIsEcCode, PCSTR Name)
{
    DWORD ExportRva = 0;
    PVOID Export, Redirection, JumpTarget;
    ULONG RedirectionRva;

    Export = find_raw_export(Module, Name, &ExportRva);
    RedirectionRva = find_redirection(Metadata, NtHeader, Module, ExportRva);
    Redirection = RedirectionRva ? (PBYTE)Module + RedirectionRva : NULL;
    JumpTarget = Export ? resolve_x64_import_jump(Export) : NULL;
    printf("EXPORT %s rva=0x%lx ec=%u redir=0x%lx redir_ec=%u jump=%p jump_ec=%u bytes=%02x%02x%02x%02x%02x%02x\n", Name, ExportRva, Export && RtlIsEcCode ? RtlIsEcCode((ULONG_PTR)Export) : 0, RedirectionRva, Redirection && RtlIsEcCode ? RtlIsEcCode((ULONG_PTR)Redirection) : 0, JumpTarget, JumpTarget && RtlIsEcCode ? RtlIsEcCode((ULONG_PTR)JumpTarget) : 0, Export ? ((PBYTE)Export)[0] : 0, Export ? ((PBYTE)Export)[1] : 0, Export ? ((PBYTE)Export)[2] : 0, Export ? ((PBYTE)Export)[3] : 0, Export ? ((PBYTE)Export)[4] : 0, Export ? ((PBYTE)Export)[5] : 0);
}

int
main(void)
{
    HMODULE UcrtBase, NtDll;
    PIMAGE_NT_HEADERS64 NtHeader = NULL;
    PARM64EC_METADATA Metadata;
    PRTL_IS_EC_CODE RtlIsEcCode;
    PMEMCPY Memcpy;
    PSTRCHR Strchr;
    PSTRRCHR Strrchr;
    PSTRSTR Strstr;
    PSTRLEN Strlen;
    PWCSSCHR Wcschr;
    PWCSRCHR Wcsrchr;
    PWCSSTR Wcsstr;
    CHAR Source[32] = "arm64ec-ucrt-parity";
    CHAR Destination[32] = {0};
    static const CHAR Loaded[] = "_LOADED";
    static const CHAR SearchText[] = "arm64ec-ucrt-parity";
    static const WCHAR WideLoaded[] = L"_LOADED";
    static const WCHAR WideSearchText[] = L"arm64ec-ucrt-parity";

    setvbuf(stdout, NULL, _IONBF, 0);
    printf("CHPE_UCRT_THUNK_BEGIN\n");
    UcrtBase = LoadLibraryW(L"ucrtbase.dll");
    NtDll = GetModuleHandleW(L"ntdll.dll");
    RtlIsEcCode = NtDll ? (PRTL_IS_EC_CODE)GetProcAddress(NtDll, "RtlIsEcCode") : NULL;
    Metadata = get_arm64ec_metadata(UcrtBase, &NtHeader);
    printf("MODULE ucrtbase=%p machine=0x%04x metadata=%p version=%lu redirections=%lu\n", UcrtBase, NtHeader ? NtHeader->FileHeader.Machine : 0, Metadata, Metadata ? Metadata->Version : 0, Metadata ? Metadata->RedirectionMetadataCount : 0);
    if (!UcrtBase || !NtHeader || !Metadata || !RtlIsEcCode)
    {
        printf("CHPE_UCRT_THUNK_FAIL setup error=%lu\n", GetLastError());
        return 1;
    }

    report_export(UcrtBase, Metadata, NtHeader, RtlIsEcCode, "memchr");
    report_export(UcrtBase, Metadata, NtHeader, RtlIsEcCode, "memcmp");
    report_export(UcrtBase, Metadata, NtHeader, RtlIsEcCode, "memcpy");
    report_export(UcrtBase, Metadata, NtHeader, RtlIsEcCode, "memmove");
    report_export(UcrtBase, Metadata, NtHeader, RtlIsEcCode, "memset");
    report_export(UcrtBase, Metadata, NtHeader, RtlIsEcCode, "strchr");
    report_export(UcrtBase, Metadata, NtHeader, RtlIsEcCode, "strrchr");
    report_export(UcrtBase, Metadata, NtHeader, RtlIsEcCode, "strstr");
    report_export(UcrtBase, Metadata, NtHeader, RtlIsEcCode, "strlen");
    report_export(UcrtBase, Metadata, NtHeader, RtlIsEcCode, "wcschr");
    report_export(UcrtBase, Metadata, NtHeader, RtlIsEcCode, "wcsrchr");
    report_export(UcrtBase, Metadata, NtHeader, RtlIsEcCode, "wcsstr");

    Memcpy = (PMEMCPY)GetProcAddress(UcrtBase, "memcpy");
    Strchr = (PSTRCHR)GetProcAddress(UcrtBase, "strchr");
    Strrchr = (PSTRRCHR)GetProcAddress(UcrtBase, "strrchr");
    Strstr = (PSTRSTR)GetProcAddress(UcrtBase, "strstr");
    Strlen = (PSTRLEN)GetProcAddress(UcrtBase, "strlen");
    Wcschr = (PWCSSCHR)GetProcAddress(UcrtBase, "wcschr");
    Wcsrchr = (PWCSRCHR)GetProcAddress(UcrtBase, "wcsrchr");
    Wcsstr = (PWCSSTR)GetProcAddress(UcrtBase, "wcsstr");
    if (!Memcpy || !Strchr || !Strrchr || !Strstr || !Strlen || !Wcschr || !Wcsrchr || !Wcsstr ||
        Memcpy(Destination, Source, sizeof(Source)) != Destination ||
        lstrcmpA(Destination, Source) ||
        Strchr(Loaded, '.') != NULL ||
        Strchr(Loaded, 'D') != Loaded + 4 ||
        Strchr(Loaded, '\0') != Loaded + 7 ||
        Strrchr(Loaded, 'D') != Loaded + 6 ||
        Strrchr(Loaded, '\0') != Loaded + 7 ||
        Strstr(SearchText, "ucrt") != SearchText + 8 ||
        Strstr(SearchText, "") != SearchText ||
        Strstr(SearchText, "missing") != NULL ||
        Wcschr(WideLoaded, L'.') != NULL ||
        Wcschr(WideLoaded, L'D') != WideLoaded + 4 ||
        Wcschr(WideLoaded, L'\0') != WideLoaded + 7 ||
        Wcsrchr(WideLoaded, L'D') != WideLoaded + 6 ||
        Wcsrchr(WideLoaded, L'\0') != WideLoaded + 7 ||
        Wcsstr(WideSearchText, L"ucrt") != WideSearchText + 8 ||
        Wcsstr(WideSearchText, L"") != WideSearchText ||
        Wcsstr(WideSearchText, L"missing") != NULL ||
        Strlen(Destination) != strlen(Source))
    {
        printf("CHPE_UCRT_THUNK_FAIL call memcpy=%p strchr=%p strrchr=%p strstr=%p strlen=%p wcschr=%p wcsrchr=%p wcsstr=%p error=%lu\n", Memcpy, Strchr, Strrchr, Strstr, Strlen, Wcschr, Wcsrchr, Wcsstr, GetLastError());
        return 2;
    }

    printf("CHPE_UCRT_THUNK_PASS length=%llu\n", (unsigned long long)Strlen(Destination));
    return 0;
}
