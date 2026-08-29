/*
 * PROJECT:     ReactOS Kernel-Mode Driver Framework
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     KMDF client class-extension binding lifecycle
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#include <ntddk.h>
#include "wdf.h"
#include <fxldr.h>

#define WDF_CLASS_BIND_START_SECTION_NAME ".kmdfclassbind$a"
#define WDF_CLASS_BIND_END_SECTION_NAME ".kmdfclassbind$c"
#define WDF_CLASS_BIND_STATE_SECTION_NAME ".kmdfclassbind$d"

#pragma section(WDF_CLASS_BIND_START_SECTION_NAME, read, write)
#pragma section(WDF_CLASS_BIND_END_SECTION_NAME, read, write)
#pragma section(WDF_CLASS_BIND_STATE_SECTION_NAME, read, write)
#pragma comment(linker, "/merge:.kmdfclassbind=.data")

__declspec(allocate(WDF_CLASS_BIND_START_SECTION_NAME))
MARKER_TYPE __KMDF_CLASS_BIND_START;

__declspec(allocate(WDF_CLASS_BIND_END_SECTION_NAME))
PVOID __KMDF_CLASS_BIND_END;

__declspec(allocate(WDF_CLASS_BIND_STATE_SECTION_NAME))
static PWDF_CLASS_BIND_INFO __KMDF_CLASS_BIND_LAST_BOUND =
    (PWDF_CLASS_BIND_INFO)&__KMDF_CLASS_BIND_START;

static
PWDF_CLASS_BIND_INFO
FxGetNextClassBindInfo(
    _In_reads_bytes_((const BYTE *)End - Current) const BYTE *Current,
    _In_ const PWDF_CLASS_BIND_INFO End)
{
    C_ASSERT(TYPE_ALIGNMENT(WDF_CLASS_BIND_INFO) % sizeof(PVOID) == 0);
    C_ASSERT(sizeof(WDF_CLASS_BIND_INFO) % sizeof(PVOID) == 0);
    C_ASSERT(FIELD_OFFSET(WDF_CLASS_BIND_INFO, Size) == 0);

    while (Current + sizeof(PVOID) <= (const BYTE *)End &&
           *(PVOID UNALIGNED const *)Current == NULL)
    {
        Current += sizeof(PVOID);
    }

    if (Current >= (const BYTE *)End)
        return End;

    if (Current + sizeof(WDF_CLASS_BIND_INFO) <= (const BYTE *)End &&
        ((PWDF_CLASS_BIND_INFO)Current)->Size == sizeof(WDF_CLASS_BIND_INFO))
    {
        return (PWDF_CLASS_BIND_INFO)Current;
    }

#if WDF_STUB_VERSION >= 25
    C_ASSERT(TYPE_ALIGNMENT(WDF_CLASS_BIND_INFO2) % sizeof(PVOID) == 0);
    C_ASSERT(sizeof(WDF_CLASS_BIND_INFO2) % sizeof(PVOID) == 0);

    if (Current + sizeof(WDF_CLASS_BIND_INFO2) <= (const BYTE *)End &&
        ((PWDF_CLASS_BIND_INFO)Current)->Size == sizeof(WDF_CLASS_BIND_INFO2))
    {
        return (PWDF_CLASS_BIND_INFO)Current;
    }
#endif

    return NULL;
}

NTSTATUS
FxStubBindClasses(
    _In_ PWDF_BIND_INFO BindInfo)
{
    const PWDF_CLASS_BIND_INFO End =
        (PWDF_CLASS_BIND_INFO)&__KMDF_CLASS_BIND_END;
    const BYTE *Current =
        (const BYTE *)&__KMDF_CLASS_BIND_START + sizeof(__KMDF_CLASS_BIND_START);
    PWDF_CLASS_BIND_INFO ClassBindInfo;
    NTSTATUS Status;

    if ((const BYTE *)&__KMDF_CLASS_BIND_START > (const BYTE *)End)
        return STATUS_INVALID_IMAGE_FORMAT;

    for (;;)
    {
        ClassBindInfo = FxGetNextClassBindInfo(Current, End);
        if (ClassBindInfo == End)
            return STATUS_SUCCESS;
        if (ClassBindInfo == NULL)
            return STATUS_INVALID_IMAGE_FORMAT;

        __KMDF_CLASS_BIND_LAST_BOUND = ClassBindInfo;

        if (ClassBindInfo->ClientBindClass != NULL)
        {
            Status = ClassBindInfo->ClientBindClass(WdfVersionBindClass,
                                                    BindInfo,
                                                    WdfDriverGlobals,
                                                    ClassBindInfo);
        }
        else
        {
            Status = WdfVersionBindClass(BindInfo,
                                         WdfDriverGlobals,
                                         ClassBindInfo);
        }

        if (!NT_SUCCESS(Status))
            return Status;

        Current = (const BYTE *)ClassBindInfo + ClassBindInfo->Size;
    }
}

BOOLEAN
FxStubIsClassBound(VOID)
{
    return __KMDF_CLASS_BIND_LAST_BOUND !=
           (PWDF_CLASS_BIND_INFO)&__KMDF_CLASS_BIND_START;
}

VOID
FxStubUnbindClasses(
    _In_ PWDF_BIND_INFO BindInfo)
{
    const BYTE *Current;
    const PWDF_CLASS_BIND_INFO End = __KMDF_CLASS_BIND_LAST_BOUND;
    const BYTE *Last;
    PWDF_CLASS_BIND_INFO ClassBindInfo;

    if (End == (PWDF_CLASS_BIND_INFO)&__KMDF_CLASS_BIND_START)
        return;

    Current = (const BYTE *)&__KMDF_CLASS_BIND_START +
              sizeof(__KMDF_CLASS_BIND_START);
    Last = (const BYTE *)End + End->Size;

    for (;;)
    {
        ClassBindInfo = FxGetNextClassBindInfo(Current,
                                               (PWDF_CLASS_BIND_INFO)Last);
        if (ClassBindInfo == (PWDF_CLASS_BIND_INFO)Last)
            return;
        if (ClassBindInfo == NULL)
            return;

        if (ClassBindInfo->ClientUnbindClass != NULL)
        {
            ClassBindInfo->ClientUnbindClass(WdfVersionUnbindClass,
                                             BindInfo,
                                             WdfDriverGlobals,
                                             ClassBindInfo);
        }
        else
        {
            WdfVersionUnbindClass(BindInfo,
                                  WdfDriverGlobals,
                                  ClassBindInfo);
        }

        Current = (const BYTE *)ClassBindInfo + ClassBindInfo->Size;
    }
}
