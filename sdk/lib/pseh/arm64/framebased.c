/*
 * PROJECT:         ReactOS SEH Library
 * LICENSE:         MIT (https://spdx.org/licenses/MIT)
 * FILE:            sdk/lib/pseh/arm64/framebased.c
 * PURPOSE:         Frame-based SEH glue for ARM64 targets
 * COPYRIGHT:       2025 ReactOS Contributors
 */

#include <windef.h>
#include <winnt.h>
#include <excpt.h>

#include <pseh/framebased/internal.h>

#if defined(_MSC_VER)
#include <intrin.h>
#define PSEH_RETURN_ADDRESS() _ReturnAddress()
#define PSEH_TLS __declspec(thread)
#else
#define PSEH_RETURN_ADDRESS() __builtin_return_address(0)
#define PSEH_TLS __thread
#endif

NTSYSAPI
VOID
NTAPI
RtlUnwind(
    _In_opt_ PVOID TargetFrame,
    _In_opt_ PVOID TargetIp,
    _In_opt_ PEXCEPTION_RECORD ExceptionRecord,
    _In_opt_ PVOID ReturnValue);

static PSEH_TLS _SEHRegistration_t *PsehRegistrationTop;

VOID
__cdecl
_SEHCleanHandlerEnvironment(VOID)
{
    /* No per-handler processor state to sanitize on ARM64. */
}

_SEHRegistration_t *
__cdecl
_SEHCurrentRegistration(VOID)
{
    return PsehRegistrationTop;
}

_SEHRegistration_t *
__cdecl
_SEHRegisterFrame(
    _In_ _SEHRegistration_t *Registration)
{
    Registration->SER_Prev = PsehRegistrationTop;
    PsehRegistrationTop = Registration;
    return Registration;
}

VOID
__cdecl
_SEHUnregisterFrame(VOID)
{
    if (PsehRegistrationTop != NULL)
    {
        PsehRegistrationTop = PsehRegistrationTop->SER_Prev;
    }
}

VOID
__cdecl
_SEHGlobalUnwind(
    _In_opt_ _SEHPortableFrame_t *TargetFrame)
{
    RtlUnwind(TargetFrame,
              PSEH_RETURN_ADDRESS(),
              NULL,
              NULL);
}

