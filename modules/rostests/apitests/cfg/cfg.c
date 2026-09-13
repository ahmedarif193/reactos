/*
 * PROJECT:     ReactOS API tests
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Native-paired ARM64 Control Flow Guard loader tests
 */

#include <apitest.h>
#include <windows.h>

#ifndef IMAGE_GUARD_FLAG_FID_SUPPRESSED
#define IMAGE_GUARD_FLAG_FID_SUPPRESSED 0x01
#endif
#ifndef IMAGE_GUARD_CF_FUNCTION_TABLE_SIZE_MASK
#define IMAGE_GUARD_CF_FUNCTION_TABLE_SIZE_MASK 0xf0000000
#define IMAGE_GUARD_CF_FUNCTION_TABLE_SIZE_SHIFT 28
#endif

#define STATUS_STACK_BUFFER_OVERRUN ((DWORD)0xc0000409)
#define STATUS_INSTRUCTION_MISALIGNMENT ((DWORD)0xc00000aa)

typedef BOOL (WINAPI *PFN_GET_PROCESS_MITIGATION_POLICY)(HANDLE, DWORD, PVOID, SIZE_T);

typedef struct _CFG_IMAGE
{
    HMODULE Module;
    const IMAGE_NT_HEADERS *NtHeaders;
    const IMAGE_LOAD_CONFIG_DIRECTORY *LoadConfig;
    SIZE_T ImageSize;
    const void *CheckTarget;
    const void *DispatchTarget;
} CFG_IMAGE;

typedef struct _CFG_CHILD_EXPECTATION
{
    const WCHAR *Mode;
    DWORD ExitCode;
} CFG_CHILD_EXPECTATION;

static const CFG_CHILD_EXPECTATION ChildExpectations[] =
{
    {L"valid-local",       0},
    {L"valid-floating-arguments", 0},
    {L"valid-vector-arguments", 0},
    {L"valid-guarded",     0},
    {L"plain-interior",    0},
    {L"invalid-aligned",   STATUS_STACK_BUFFER_OVERRUN},
    {L"invalid-misaligned", STATUS_INSTRUCTION_MISALIGNMENT},
    {L"dynamic-default",   0},
    {L"dynamic-invalid",   STATUS_STACK_BUFFER_OVERRUN},
    {L"dynamic-register",  0},
    {L"dynamic-marked",    0}
};

static BOOL
AddressInImage(const void *Address, const CFG_IMAGE *Image, SIZE_T Size)
{
    ULONG_PTR Value = (ULONG_PTR)Address;
    ULONG_PTR Base = (ULONG_PTR)Image->Module;

    return Value >= Base && Value - Base <= Image->ImageSize && Size <= Image->ImageSize - (Value - Base);
}

static BOOL
ReadImage(HMODULE Module, CFG_IMAGE *Image)
{
    const IMAGE_DATA_DIRECTORY *Directory;
    const IMAGE_DOS_HEADER *DosHeader = (const IMAGE_DOS_HEADER *)Module;
    const IMAGE_NT_HEADERS *NtHeaders;

    ZeroMemory(Image, sizeof(*Image));
    Image->Module = Module;
    if (Module == NULL || DosHeader->e_magic != IMAGE_DOS_SIGNATURE)
        return FALSE;
    NtHeaders = (const IMAGE_NT_HEADERS *)((const BYTE *)Module + DosHeader->e_lfanew);
    if (NtHeaders->Signature != IMAGE_NT_SIGNATURE)
        return FALSE;
    Image->NtHeaders = NtHeaders;
    Image->ImageSize = NtHeaders->OptionalHeader.SizeOfImage;
    Directory = &NtHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG];
    if (Directory->VirtualAddress != 0 && Directory->Size >= RTL_SIZEOF_THROUGH_FIELD(IMAGE_LOAD_CONFIG_DIRECTORY, GuardFlags) && Directory->VirtualAddress <= Image->ImageSize && Directory->Size <= Image->ImageSize - Directory->VirtualAddress)
        Image->LoadConfig = (const IMAGE_LOAD_CONFIG_DIRECTORY *)((const BYTE *)Module + Directory->VirtualAddress);
    return TRUE;
}

static BOOL
SectionIsExecutable(const CFG_IMAGE *Image, DWORD Rva)
{
    const IMAGE_SECTION_HEADER *Section = IMAGE_FIRST_SECTION(Image->NtHeaders);
    WORD Index;

    for (Index = 0; Index < Image->NtHeaders->FileHeader.NumberOfSections; ++Index, ++Section)
    {
        DWORD Size = max(Section->Misc.VirtualSize, Section->SizeOfRawData);

        if (Rva >= Section->VirtualAddress && Rva - Section->VirtualAddress < Size)
            return (Section->Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0;
    }
    return FALSE;
}

static BOOL
TestGuardedImage(HMODULE Module, const char *Name, const void *RequiredTarget, CFG_IMAGE *Image)
{
    const BYTE *Table;
    ULONGLONG Count, Index;
    SIZE_T Stride;
    DWORD PreviousRva = 0;
    DWORD RequiredRva = RequiredTarget != NULL ? (DWORD)((const BYTE *)RequiredTarget - (const BYTE *)Module) : 0;
    BOOL RequiredFound = RequiredTarget == NULL;

    ok(ReadImage(Module, Image), "%s is not a valid mapped PE image\n", Name);
    if (Image->NtHeaders == NULL)
        return FALSE;
    ok((Image->NtHeaders->OptionalHeader.DllCharacteristics & IMAGE_DLLCHARACTERISTICS_GUARD_CF) != 0, "%s lacks IMAGE_DLLCHARACTERISTICS_GUARD_CF\n", Name);
    ok(Image->LoadConfig != NULL, "%s has no complete load configuration\n", Name);
    if (Image->LoadConfig == NULL)
        return FALSE;
    ok((Image->LoadConfig->GuardFlags & IMAGE_GUARD_CF_INSTRUMENTED) != 0, "%s is not CFG instrumented\n", Name);
    ok((Image->LoadConfig->GuardFlags & IMAGE_GUARD_CF_FUNCTION_TABLE_PRESENT) != 0, "%s has no Guard FID table flag\n", Name);
    Count = Image->LoadConfig->GuardCFFunctionCount;
    Stride = sizeof(DWORD) + ((Image->LoadConfig->GuardFlags & IMAGE_GUARD_CF_FUNCTION_TABLE_SIZE_MASK) >> IMAGE_GUARD_CF_FUNCTION_TABLE_SIZE_SHIFT);
    ok(Count != 0 && Count < 0x100000, "%s has invalid Guard FID count %I64u\n", Name, Count);
    ok(Stride >= sizeof(DWORD) && Stride <= 32, "%s has invalid Guard FID stride %Iu\n", Name, Stride);
    Table = (const BYTE *)(ULONG_PTR)Image->LoadConfig->GuardCFFunctionTable;
    ok(AddressInImage(Table, Image, (SIZE_T)Count * Stride), "%s Guard FID table is outside the image\n", Name);
    if (!AddressInImage(Table, Image, (SIZE_T)Count * Stride))
        return FALSE;
    for (Index = 0; Index < Count; ++Index)
    {
        DWORD Rva;

        CopyMemory(&Rva, Table + (SIZE_T)Index * Stride, sizeof(Rva));
        ok(Rva < Image->ImageSize, "%s Guard FID %I64u has out-of-image RVA %lx\n", Name, Index, Rva);
        ok(SectionIsExecutable(Image, Rva), "%s Guard FID %I64u RVA %lx is not executable\n", Name, Index, Rva);
        if (Index != 0)
            ok(Rva > PreviousRva, "%s Guard FID table is not strictly sorted at %I64u\n", Name, Index);
        if (Rva == RequiredRva && (Stride == sizeof(DWORD) || !(Table[(SIZE_T)Index * Stride + sizeof(DWORD)] & IMAGE_GUARD_FLAG_FID_SUPPRESSED)))
            RequiredFound = TRUE;
        PreviousRva = Rva;
    }
    ok(RequiredFound, "%s required target RVA %lx is absent or suppressed\n", Name, RequiredRva);
    ok(AddressInImage((const void *)(ULONG_PTR)Image->LoadConfig->GuardCFCheckFunctionPointer, Image, sizeof(PVOID)), "%s check slot is outside the image\n", Name);
    ok(AddressInImage((const void *)(ULONG_PTR)Image->LoadConfig->GuardCFDispatchFunctionPointer, Image, sizeof(PVOID)), "%s dispatch slot is outside the image\n", Name);
    if (!AddressInImage((const void *)(ULONG_PTR)Image->LoadConfig->GuardCFCheckFunctionPointer, Image, sizeof(PVOID)) || !AddressInImage((const void *)(ULONG_PTR)Image->LoadConfig->GuardCFDispatchFunctionPointer, Image, sizeof(PVOID)))
        return FALSE;
    Image->CheckTarget = *(const void * const *)(ULONG_PTR)Image->LoadConfig->GuardCFCheckFunctionPointer;
    Image->DispatchTarget = *(const void * const *)(ULONG_PTR)Image->LoadConfig->GuardCFDispatchFunctionPointer;
    ok(Image->CheckTarget != NULL, "%s resolved check target is NULL\n", Name);
    ok(Image->DispatchTarget != NULL, "%s resolved dispatch target is NULL\n", Name);
    ok(!AddressInImage(Image->CheckTarget, Image, 1), "%s check target remained image-local\n", Name);
    ok(!AddressInImage(Image->DispatchTarget, Image, 1), "%s dispatch target remained image-local\n", Name);
    return TRUE;
}

static void
TestPlainImage(HMODULE Module)
{
    CFG_IMAGE Image;

    ok(ReadImage(Module, &Image), "cfg_plain.dll is not a valid PE image\n");
    if (Image.NtHeaders == NULL)
        return;
    ok((Image.NtHeaders->OptionalHeader.DllCharacteristics & IMAGE_DLLCHARACTERISTICS_GUARD_CF) == 0, "cfg_plain.dll unexpectedly has the CFG DLL characteristic\n");
    if (Image.LoadConfig != NULL)
        ok((Image.LoadConfig->GuardFlags & IMAGE_GUARD_CF_INSTRUMENTED) == 0, "cfg_plain.dll unexpectedly has CFG instrumentation flags\n");
}

static BOOL
GetSiblingPath(WCHAR Path[MAX_PATH], const WCHAR *Name)
{
    WCHAR *Separator;
    DWORD Length = GetModuleFileNameW(NULL, Path, MAX_PATH);

    if (Length == 0 || Length >= MAX_PATH)
        return FALSE;
    Separator = wcsrchr(Path, L'\\');
    if (Separator == NULL || (SIZE_T)(Separator - Path) + 1 + lstrlenW(Name) >= MAX_PATH)
        return FALSE;
    lstrcpyW(Separator + 1, Name);
    return TRUE;
}

static DWORD
RunChild(const WCHAR *Mode)
{
    PROCESS_INFORMATION ProcessInformation;
    STARTUPINFOW StartupInfo;
    WCHAR ChildPath[MAX_PATH];
    WCHAR CommandLine[MAX_PATH + 80];
    DWORD ExitCode = 0x103;
    DWORD WaitResult;
    BOOL Created;

    if (!GetSiblingPath(ChildPath, L"cfg_child.exe"))
        return ERROR_BAD_PATHNAME;
    if (lstrlenW(ChildPath) + lstrlenW(Mode) + 4 >= ARRAYSIZE(CommandLine))
        return ERROR_BAD_COMMAND;
    lstrcpyW(CommandLine, L"\"");
    lstrcatW(CommandLine, ChildPath);
    lstrcatW(CommandLine, L"\" ");
    lstrcatW(CommandLine, Mode);
    ZeroMemory(&StartupInfo, sizeof(StartupInfo));
    StartupInfo.cb = sizeof(StartupInfo);
    ZeroMemory(&ProcessInformation, sizeof(ProcessInformation));
    Created = CreateProcessW(ChildPath, CommandLine, NULL, NULL, FALSE, 0, NULL, NULL, &StartupInfo, &ProcessInformation);
    ok(Created, "CreateProcessW(%ls) failed: %lu\n", Mode, GetLastError());
    if (!Created)
        return GetLastError();
    WaitResult = WaitForSingleObject(ProcessInformation.hProcess, 30000);
    ok_eq_ulong(WaitResult, WAIT_OBJECT_0);
    if (WaitResult != WAIT_OBJECT_0)
        TerminateProcess(ProcessInformation.hProcess, 0xdead);
    ok(GetExitCodeProcess(ProcessInformation.hProcess, &ExitCode), "GetExitCodeProcess(%ls) failed: %lu\n", Mode, GetLastError());
    CloseHandle(ProcessInformation.hThread);
    CloseHandle(ProcessInformation.hProcess);
    return ExitCode;
}

static void
TestProcessPolicy(void)
{
    PFN_GET_PROCESS_MITIGATION_POLICY GetPolicy;
    HMODULE MemoryApi;
    FARPROC SetValidTargets;
    DWORD Policy = 0;
    BOOL Result;

    GetPolicy = (PFN_GET_PROCESS_MITIGATION_POLICY)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetProcessMitigationPolicy");
    MemoryApi = LoadLibraryW(L"api-ms-win-core-memory-l1-1-2.dll");
    SetValidTargets = MemoryApi != NULL ? GetProcAddress(MemoryApi, "SetProcessValidCallTargets") : NULL;
    ok(GetPolicy != NULL, "GetProcessMitigationPolicy is not exported\n");
    ok(SetValidTargets != NULL, "SetProcessValidCallTargets is not exported\n");
    if (GetPolicy == NULL)
        return;
    Result = GetPolicy(GetCurrentProcess(), 7, &Policy, sizeof(Policy));
    ok(Result, "GetProcessMitigationPolicy(CFG) failed: %lu\n", GetLastError());
    if (Result)
        ok((Policy & 1) != 0, "CFG-enabled image has process policy %#lx\n", Policy);
    trace("CFGOBS|policy|result=%u|error=%lu|flags=%08lx|settargets=%p\n", Result, GetLastError(), Policy, SetValidTargets);
}

START_TEST(cfg)
{
    CFG_IMAGE MainImage, GuardedImage;
    HMODULE GuardedModule, PlainModule;
    FARPROC GuardedTarget;
    ULONG Index;

    TestProcessPolicy();
    GuardedModule = LoadLibraryW(L"cfg_guarded.dll");
    PlainModule = LoadLibraryW(L"cfg_plain.dll");
    ok(GuardedModule != NULL, "LoadLibrary(cfg_guarded.dll) failed: %lu\n", GetLastError());
    ok(PlainModule != NULL, "LoadLibrary(cfg_plain.dll) failed: %lu\n", GetLastError());
    GuardedTarget = GuardedModule != NULL ? GetProcAddress(GuardedModule, "CfgGuardedTarget") : NULL;
    ok(GuardedTarget != NULL, "CfgGuardedTarget is absent\n");
    if (TestGuardedImage(GetModuleHandleW(NULL), "cfg_apitest.exe", NULL, &MainImage) && GuardedTarget != NULL && TestGuardedImage(GuardedModule, "cfg_guarded.dll", GuardedTarget, &GuardedImage))
    {
        ok(MainImage.CheckTarget == GuardedImage.CheckTarget, "CFG check targets differ: %p versus %p\n", MainImage.CheckTarget, GuardedImage.CheckTarget);
        ok(MainImage.DispatchTarget == GuardedImage.DispatchTarget, "CFG dispatch targets differ: %p versus %p\n", MainImage.DispatchTarget, GuardedImage.DispatchTarget);
        trace("CFGOBS|shared-targets|check=%p|dispatch=%p\n", MainImage.CheckTarget, MainImage.DispatchTarget);
    }
    if (PlainModule != NULL)
        TestPlainImage(PlainModule);

    for (Index = 0; Index < ARRAYSIZE(ChildExpectations); ++Index)
    {
        DWORD ExitCode = RunChild(ChildExpectations[Index].Mode);

        trace("CFGOBS|child|mode=%ls|exit=%08lx\n", ChildExpectations[Index].Mode, ExitCode);
        ok_eq_ulong(ExitCode, ChildExpectations[Index].ExitCode);
    }
}
