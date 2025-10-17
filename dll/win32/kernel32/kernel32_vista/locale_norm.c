/*
 * PROJECT:         ReactOS system libraries
 * LICENSE:         GPL-2.0-or-later - See COPYING in the top level directory
 * PURPOSE:         Vista locale normalization APIs
 * COPYRIGHT:       2025 Ahmed ARIF (arif.ing@outlook.com)
 */

#include "k32_vista.h"

#include <ndk/rtlfuncs.h>

#ifndef NTDDI_VISTA
#define NTDDI_VISTA 0x06000000
#endif

#if NTDDI_VERSION < NTDDI_VISTA
#undef NTDDI_VERSION
#define NTDDI_VERSION NTDDI_VISTA
#endif

NTSYSAPI
NTSTATUS
NTAPI
RtlNormalizeString(ULONG NormForm,
                   PCWSTR SourceString,
                   INT SourceCch,
                   PWSTR DestinationString,
                   PINT DestinationCch);

NTSYSAPI
NTSTATUS
NTAPI
RtlIsNormalizedString(ULONG NormForm,
                      PCWSTR SourceString,
                      INT SourceCch,
                      PBOOLEAN Normalized);

#define NDEBUG
#include <debug.h>

static BOOL
K32VistaMapNormalizationForm(NORM_FORM Form,
                             ULONG* RtlForm)
{
    switch (Form)
    {
        case NormalizationC:
            *RtlForm = NormalizationC;
            return TRUE;
        case NormalizationD:
            *RtlForm = NormalizationD;
            return TRUE;
        case NormalizationKC:
            *RtlForm = NormalizationKC;
            return TRUE;
        case NormalizationKD:
            *RtlForm = NormalizationKD;
            return TRUE;
        case NormalizationOther:
        default:
            SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
    }
}

static INT
K32VistaResolveLength(INT Length,
                      LPCWSTR Source)
{
    if (Length >= 0)
        return Length;

    return wcslen(Source);
}

INT
WINAPI
NormalizeString(NORM_FORM Form,
                LPCWSTR SourceString,
                INT SourceCch,
                LPWSTR DestinationString,
                INT DestinationCch)
{
    ULONG RtlForm;
    INT SourceLength;
    INT DestinationLength;
    NTSTATUS Status;

    if (!SourceString)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }

    if (!K32VistaMapNormalizationForm(Form, &RtlForm))
        return 0;

    SourceLength = K32VistaResolveLength(SourceCch, SourceString);

    if (DestinationString)
    {
        DestinationLength = DestinationCch;
    }
    else
    {
        DestinationLength = 0;
    }

    /* Fast path: compute required length */
    Status = RtlNormalizeString(RtlForm,
                                SourceString,
                                SourceLength,
                                NULL,
                                &DestinationLength);
    if (Status == STATUS_BUFFER_TOO_SMALL)
    {
        if (!DestinationString)
        {
            return DestinationLength;
        }
    }
    else if (!NT_SUCCESS(Status))
    {
        if (Status == STATUS_INVALID_PARAMETER)
            SetLastError(ERROR_INVALID_PARAMETER);
        else
            BaseSetLastNTError(Status);
        return 0;
    }

    if (DestinationString)
    {
        INT ActualLength = DestinationCch;

        Status = RtlNormalizeString(RtlForm,
                                    SourceString,
                                    SourceLength,
                                    DestinationString,
                                    &ActualLength);
        if (!NT_SUCCESS(Status))
        {
            if (Status == STATUS_BUFFER_TOO_SMALL)
                SetLastError(ERROR_INSUFFICIENT_BUFFER);
            else if (Status == STATUS_INVALID_PARAMETER)
                SetLastError(ERROR_INVALID_PARAMETER);
            else
                BaseSetLastNTError(Status);
            return 0;
        }

        return ActualLength;
    }

    return DestinationLength;
}

BOOL
WINAPI
IsNormalizedString(NORM_FORM Form,
                   LPCWSTR SourceString,
                   INT SourceCch)
{
    ULONG RtlForm;
    INT SourceLength;
    BOOLEAN Normalized = FALSE;
    NTSTATUS Status;

    if (!SourceString)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    if (!K32VistaMapNormalizationForm(Form, &RtlForm))
        return FALSE;

    SourceLength = K32VistaResolveLength(SourceCch, SourceString);

    Status = RtlIsNormalizedString(RtlForm,
                                   SourceString,
                                   SourceLength,
                                   &Normalized);

    if (!NT_SUCCESS(Status))
    {
        if (Status == STATUS_INVALID_PARAMETER)
            SetLastError(ERROR_INVALID_PARAMETER);
        else
            BaseSetLastNTError(Status);
        return FALSE;
    }

    return Normalized ? TRUE : FALSE;
}
