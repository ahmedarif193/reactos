/*
 * PROJECT:     ReactOS Kernel-Mode Driver Framework
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     KMDF shared context-type initialization
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#include <ntddk.h>
#include "wdf.h"
#include <fxldr.h>

#define WDF_TYPE_INIT_START_SECTION_NAME ".kmdftypeinit$a"
#define WDF_TYPE_INIT_END_SECTION_NAME ".kmdftypeinit$c"

#pragma section(WDF_TYPE_INIT_START_SECTION_NAME, read, write)
#pragma section(WDF_TYPE_INIT_END_SECTION_NAME, read, write)
#pragma comment(linker, "/merge:.kmdftypeinit=.data")

__declspec(allocate(WDF_TYPE_INIT_START_SECTION_NAME))
MARKER_TYPE __KMDF_TYPE_INIT_START;

__declspec(allocate(WDF_TYPE_INIT_END_SECTION_NAME))
PVOID __KMDF_TYPE_INIT_END;

static
PWDF_OBJECT_CONTEXT_TYPE_INFO
FxGetNextObjectContextTypeInfo(
    _In_reads_bytes_((const BYTE *)End - Current) const BYTE *Current,
    _In_ const PWDF_OBJECT_CONTEXT_TYPE_INFO End)
{
    C_ASSERT(TYPE_ALIGNMENT(WDF_OBJECT_CONTEXT_TYPE_INFO) % sizeof(PVOID) == 0);
    C_ASSERT(sizeof(WDF_OBJECT_CONTEXT_TYPE_INFO) % sizeof(PVOID) == 0);
    C_ASSERT(FIELD_OFFSET(WDF_OBJECT_CONTEXT_TYPE_INFO, Size) == 0);

    while (Current + sizeof(PVOID) <= (const BYTE *)End &&
           *(PVOID UNALIGNED const *)Current == NULL)
    {
        Current += sizeof(PVOID);
    }

    if (Current >= (const BYTE *)End)
        return End;

    if (Current + sizeof(WDF_OBJECT_CONTEXT_TYPE_INFO) <= (const BYTE *)End &&
        ((PWDF_OBJECT_CONTEXT_TYPE_INFO)Current)->Size ==
            sizeof(WDF_OBJECT_CONTEXT_TYPE_INFO))
    {
        return (PWDF_OBJECT_CONTEXT_TYPE_INFO)Current;
    }

    return NULL;
}

NTSTATUS
FxStubInitTypes(VOID)
{
    const PWDF_OBJECT_CONTEXT_TYPE_INFO End =
        (PWDF_OBJECT_CONTEXT_TYPE_INFO)&__KMDF_TYPE_INIT_END;
    const BYTE *Current =
        (const BYTE *)&__KMDF_TYPE_INIT_START + sizeof(__KMDF_TYPE_INIT_START);
    PWDF_OBJECT_CONTEXT_TYPE_INFO TypeInfo;

    if ((const BYTE *)&__KMDF_TYPE_INIT_START > (const BYTE *)End)
        return STATUS_INVALID_IMAGE_FORMAT;

    for (;;)
    {
        TypeInfo = FxGetNextObjectContextTypeInfo(Current, End);
        if (TypeInfo == End)
            return STATUS_SUCCESS;
        if (TypeInfo == NULL)
            return STATUS_INVALID_IMAGE_FORMAT;

        if (TypeInfo->EvtDriverGetUniqueContextType != NULL)
        {
            TypeInfo->UniqueType = TypeInfo->EvtDriverGetUniqueContextType();
        }

        Current = (const BYTE *)TypeInfo + TypeInfo->Size;
    }
}
