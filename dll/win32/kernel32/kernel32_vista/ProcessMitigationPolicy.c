/*
 * PROJECT:     ReactOS Win32 Base API
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Get/SetProcessMitigationPolicy
 * COPYRIGHT:   Adapted from Wine dlls/kernelbase/process.c
 */

#include "k32_vista.h"

typedef enum _PROCESS_MITIGATION_POLICY
{
    ProcessDEPPolicy,
    ProcessASLRPolicy,
    ProcessDynamicCodePolicy,
    ProcessStrictHandleCheckPolicy,
    ProcessSystemCallDisablePolicy,
    ProcessMitigationOptionsMask,
    ProcessExtensionPointDisablePolicy,
    ProcessControlFlowGuardPolicy,
    ProcessSignaturePolicy,
    ProcessFontDisablePolicy,
    ProcessImageLoadPolicy,
    ProcessSystemCallFilterPolicy,
    ProcessPayloadRestrictionPolicy,
    ProcessChildProcessPolicy,
    ProcessSideChannelIsolationPolicy,
    ProcessUserShadowStackPolicy,
    ProcessRedirectionTrustPolicy,
    ProcessUserPointerAuthPolicy,
    ProcessSEHOPPolicy,
    ProcessActivationContextTrustPolicy,
    MaxProcessMitigationPolicy
} PROCESS_MITIGATION_POLICY;

typedef struct _K32_PROCESS_MITIGATION_DEP_POLICY
{
    DWORD Flags;
    BOOLEAN Permanent;
} K32_PROCESS_MITIGATION_DEP_POLICY, *PK32_PROCESS_MITIGATION_DEP_POLICY;

#define K32_MITIGATION_OPTION_DEP_ENABLE 0x1ULL

BOOL
WINAPI
GetProcessMitigationPolicy(
    _In_ HANDLE hProcess,
    _In_ PROCESS_MITIGATION_POLICY MitigationPolicy,
    _Out_writes_bytes_(dwLength) PVOID lpBuffer,
    _In_ SIZE_T dwLength)
{
    PK32_PROCESS_MITIGATION_DEP_POLICY DepPolicy;
    ULONGLONG OptionsMask = 0;
    DWORD Flags;
    BOOL Permanent;

    if ((ULONG)MitigationPolicy >= MaxProcessMitigationPolicy || !lpBuffer)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    if (MitigationPolicy == ProcessMitigationOptionsMask)
    {
        if (dwLength < sizeof(OptionsMask))
        {
            SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }

        /*
         * Creation mitigation flags use two bits per option. Do not advertise
         * policies that ReactOS accepts but does not enforce. DEP is mandatory
         * on 64-bit builds and is the only creation policy reported for now.
         */
#ifdef _WIN64
        OptionsMask = K32_MITIGATION_OPTION_DEP_ENABLE;
#endif
        RtlZeroMemory(lpBuffer, dwLength);
        RtlCopyMemory(lpBuffer, &OptionsMask, sizeof(OptionsMask));
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

    if (MitigationPolicy >= MaxProcessMitigationPolicy)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
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
