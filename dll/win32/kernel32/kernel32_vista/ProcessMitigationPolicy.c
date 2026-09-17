/*
 * PROJECT:     ReactOS Win32 Base API
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Get/SetProcessMitigationPolicy
 * COPYRIGHT:   Adapted from Wine dlls/kernelbase/process.c
 */

#include "k32_vista.h"

typedef struct _K32_PROCESS_MITIGATION_DEP_POLICY
{
    DWORD Flags;
    BOOLEAN Permanent;
} K32_PROCESS_MITIGATION_DEP_POLICY, *PK32_PROCESS_MITIGATION_DEP_POLICY;

typedef struct _K32_PROCESS_MITIGATION_INFORMATION
{
    ULONG Policy;
    ULONG Flags;
} K32_PROCESS_MITIGATION_INFORMATION;

#define K32_PROCESS_SIDE_CHANNEL_ISOLATION_POLICY ((PROCESS_MITIGATION_POLICY)14)

/* One supported value bit for each creation policy implemented below. */
#define K32_MITIGATION_OPTIONS_WORD1 0x1111101111111101ULL

#define K32_MITIGATION_OPTION2_MODULE_TAMPERING       (0x1ULL << 12)
#define K32_MITIGATION_OPTION2_RESTRICT_BRANCH        (0x1ULL << 16)
#define K32_MITIGATION_OPTION2_RESTRICT_CORE_SHARING  (0x1ULL << 52)
#define K32_MITIGATION_OPTION2_DISABLE_FSCTL          (0x1ULL << 56)
#define K32_MITIGATION_OPTIONS_WORD2 \
    (K32_MITIGATION_OPTION2_MODULE_TAMPERING | \
     K32_MITIGATION_OPTION2_RESTRICT_BRANCH | \
     K32_MITIGATION_OPTION2_RESTRICT_CORE_SHARING | \
     K32_MITIGATION_OPTION2_DISABLE_FSCTL)

static BOOL
K32IsCurrentImageCfgEnabled(VOID)
{
    PIMAGE_LOAD_CONFIG_DIRECTORY LoadConfig;
    PIMAGE_NT_HEADERS NtHeaders;
    PVOID ImageBase = NtCurrentPeb()->ImageBaseAddress;
    ULONG Size;

    NtHeaders = RtlImageNtHeader(ImageBase);
    if (NtHeaders == NULL || !(NtHeaders->OptionalHeader.DllCharacteristics & IMAGE_DLLCHARACTERISTICS_GUARD_CF))
        return FALSE;
    LoadConfig = RtlImageDirectoryEntryToData(ImageBase, TRUE, IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG, &Size);
    return LoadConfig != NULL && Size >= RTL_SIZEOF_THROUGH_FIELD(IMAGE_LOAD_CONFIG_DIRECTORY, GuardFlags) &&
           (LoadConfig->GuardFlags & IMAGE_GUARD_CF_INSTRUMENTED) != 0;
}

BOOL
WINAPI
GetProcessMitigationPolicy(
    _In_ HANDLE hProcess,
    _In_ PROCESS_MITIGATION_POLICY MitigationPolicy,
    _Out_writes_bytes_(dwLength) PVOID lpBuffer,
    _In_ SIZE_T dwLength)
{
    PK32_PROCESS_MITIGATION_DEP_POLICY DepPolicy;
    const ULONGLONG OptionsMask[2] =
    {
        K32_MITIGATION_OPTIONS_WORD1,
        K32_MITIGATION_OPTIONS_WORD2
    };
#ifndef _WIN64
    DWORD Flags;
    BOOL Permanent;
#endif

    if ((ULONG)MitigationPolicy > (ULONG)K32_PROCESS_SIDE_CHANNEL_ISOLATION_POLICY || !lpBuffer)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    if (MitigationPolicy == ProcessMitigationOptionsMask)
    {
        if (dwLength < sizeof(OptionsMask[0]))
        {
            SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }

        RtlZeroMemory(lpBuffer, dwLength);
        RtlCopyMemory(lpBuffer,
                      OptionsMask,
                      min(dwLength, sizeof(OptionsMask)));
        return TRUE;
    }

    if (MitigationPolicy == ProcessControlFlowGuardPolicy)
    {
        if (dwLength != sizeof(DWORD) || hProcess != GetCurrentProcess())
        {
            SetLastError(hProcess != GetCurrentProcess() ? ERROR_NOT_SUPPORTED : ERROR_INVALID_PARAMETER);
            return FALSE;
        }
        *(PDWORD)lpBuffer = K32IsCurrentImageCfgEnabled() ? 1 : 0;
        return TRUE;
    }

    if (MitigationPolicy == ProcessDynamicCodePolicy ||
        MitigationPolicy == ProcessStrictHandleCheckPolicy ||
        MitigationPolicy == ProcessSignaturePolicy ||
        MitigationPolicy == ProcessSystemCallDisablePolicy ||
        MitigationPolicy == ProcessChildProcessPolicy ||
        MitigationPolicy == ProcessASLRPolicy ||
        MitigationPolicy == ProcessExtensionPointDisablePolicy ||
        MitigationPolicy == ProcessFontDisablePolicy ||
        MitigationPolicy == ProcessImageLoadPolicy ||
        MitigationPolicy == ProcessSystemCallFilterPolicy ||
        MitigationPolicy == ProcessPayloadRestrictionPolicy ||
        MitigationPolicy == K32_PROCESS_SIDE_CHANNEL_ISOLATION_POLICY)
    {
        K32_PROCESS_MITIGATION_INFORMATION Information = {MitigationPolicy, 0};
        NTSTATUS Status;

        if (dwLength != sizeof(DWORD))
        {
            SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
        Status = NtQueryInformationProcess(hProcess, ProcessMitigationPolicy, &Information, sizeof(Information), NULL);
        if (!NT_SUCCESS(Status))
        {
            BaseSetLastNTError(Status);
            return FALSE;
        }
        *(PDWORD)lpBuffer = Information.Flags;
        return TRUE;
    }

    if (MitigationPolicy != ProcessDEPPolicy || dwLength != sizeof(*DepPolicy))
    {
        SetLastError(ERROR_NOT_SUPPORTED);
        return FALSE;
    }

    DepPolicy = lpBuffer;
    RtlZeroMemory(DepPolicy, sizeof(*DepPolicy));
#ifdef _WIN64
    UNREFERENCED_PARAMETER(hProcess);
    DepPolicy->Flags = PROCESS_DEP_ENABLE;
    DepPolicy->Permanent = TRUE;
    return TRUE;
#else
    if (!GetProcessDEPPolicy(hProcess, &Flags, &Permanent))
        return FALSE;
    DepPolicy->Flags = Flags;
    DepPolicy->Permanent = Permanent;
    return TRUE;
#endif
}

BOOL
WINAPI
SetProcessMitigationPolicy(
    _In_ PROCESS_MITIGATION_POLICY MitigationPolicy,
    _In_reads_bytes_(dwLength) PVOID lpBuffer,
    _In_ SIZE_T dwLength)
{
    PK32_PROCESS_MITIGATION_DEP_POLICY DepPolicy;

    if ((ULONG)MitigationPolicy > (ULONG)K32_PROCESS_SIDE_CHANNEL_ISOLATION_POLICY)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    if (MitigationPolicy == ProcessDynamicCodePolicy ||
        MitigationPolicy == ProcessStrictHandleCheckPolicy ||
        MitigationPolicy == ProcessSignaturePolicy ||
        MitigationPolicy == ProcessSystemCallDisablePolicy ||
        MitigationPolicy == ProcessChildProcessPolicy ||
        MitigationPolicy == ProcessASLRPolicy ||
        MitigationPolicy == ProcessExtensionPointDisablePolicy ||
        MitigationPolicy == ProcessControlFlowGuardPolicy ||
        MitigationPolicy == ProcessFontDisablePolicy ||
        MitigationPolicy == ProcessImageLoadPolicy ||
        MitigationPolicy == ProcessSystemCallFilterPolicy ||
        MitigationPolicy == ProcessPayloadRestrictionPolicy ||
        MitigationPolicy == K32_PROCESS_SIDE_CHANNEL_ISOLATION_POLICY)
    {
        K32_PROCESS_MITIGATION_INFORMATION Information = {MitigationPolicy, 0};
        NTSTATUS Status;

        if (!lpBuffer || dwLength != sizeof(DWORD))
        {
            SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
        Information.Flags = *(PDWORD)lpBuffer;
        Status = NtSetInformationProcess(NtCurrentProcess(), ProcessMitigationPolicy, &Information, sizeof(Information));
        if (!NT_SUCCESS(Status))
        {
            BaseSetLastNTError(Status);
            return FALSE;
        }
        return TRUE;
    }

    if (MitigationPolicy != ProcessDEPPolicy || !lpBuffer || dwLength != sizeof(*DepPolicy))
    {
        SetLastError(ERROR_NOT_SUPPORTED);
        return FALSE;
    }

    DepPolicy = lpBuffer;
    if (DepPolicy->Flags & ~(PROCESS_DEP_ENABLE | PROCESS_DEP_DISABLE_ATL_THUNK_EMULATION))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

#ifdef _WIN64
    if (!(DepPolicy->Flags & PROCESS_DEP_ENABLE) || (DepPolicy->Flags & PROCESS_DEP_DISABLE_ATL_THUNK_EMULATION))
    {
        SetLastError(ERROR_NOT_SUPPORTED);
        return FALSE;
    }
    return TRUE;
#else
    return SetProcessDEPPolicy(DepPolicy->Flags);
#endif
}
