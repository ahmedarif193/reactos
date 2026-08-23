/*
 * PROJECT:     ReactOS Win32 Base API
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Process and thread attribute lists
 * COPYRIGHT:   Adapted from Wine dlls/kernelbase/process.c
 */

#include "k32_vista.h"

typedef BASE_PROC_THREAD_ATTRIBUTE K32_PROC_THREAD_ATTRIBUTE;
typedef PBASE_PROC_THREAD_ATTRIBUTE PK32_PROC_THREAD_ATTRIBUTE;
typedef BASE_PROC_THREAD_ATTRIBUTE_LIST K32_PROC_THREAD_ATTRIBUTE_LIST;
typedef PBASE_PROC_THREAD_ATTRIBUTE_LIST PK32_PROC_THREAD_ATTRIBUTE_LIST;

static SIZE_T K32AttributeListSize(_In_ DWORD AttributeCount)
{
    return FIELD_OFFSET(K32_PROC_THREAD_ATTRIBUTE_LIST, Attributes) + AttributeCount * sizeof(K32_PROC_THREAD_ATTRIBUTE);
}

static
DWORD
K32ValidateAttribute(
    _In_ DWORD_PTR Attribute,
    _In_ SIZE_T Size)
{
    switch (Attribute)
    {
        case PROC_THREAD_ATTRIBUTE_PARENT_PROCESS:
            return Size == sizeof(HANDLE) ? ERROR_SUCCESS : ERROR_BAD_LENGTH;

        case PROC_THREAD_ATTRIBUTE_HANDLE_LIST:
        case PROC_THREAD_ATTRIBUTE_JOB_LIST:
            return Size != 0 && Size % sizeof(HANDLE) == 0 ? ERROR_SUCCESS : ERROR_BAD_LENGTH;

        case PROC_THREAD_ATTRIBUTE_MITIGATION_POLICY:
            return (Size == sizeof(DWORD) || Size == sizeof(DWORD64) || Size == 2 * sizeof(DWORD64)) ? ERROR_SUCCESS : ERROR_BAD_LENGTH;

        case PROC_THREAD_ATTRIBUTE_CHILD_PROCESS_POLICY:
        case PROC_THREAD_ATTRIBUTE_ALL_APPLICATION_PACKAGES_POLICY:
        case PROC_THREAD_ATTRIBUTE_DESKTOP_APP_POLICY:
        case PROC_THREAD_ATTRIBUTE_PROTECTION_LEVEL:
            return Size == sizeof(DWORD) ? ERROR_SUCCESS : ERROR_BAD_LENGTH;

        default:
            return ERROR_NOT_SUPPORTED;
    }
}

BOOL
WINAPI
InitializeProcThreadAttributeList(
    _Out_opt_ LPPROC_THREAD_ATTRIBUTE_LIST lpAttributeList,
    _In_ DWORD dwAttributeCount,
    _In_ DWORD dwFlags,
    _Inout_ PSIZE_T lpSize)
{
    SIZE_T Needed;

    if (dwFlags != 0 || !lpSize)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    Needed = K32AttributeListSize(dwAttributeCount);
    if (!lpAttributeList || *lpSize < Needed)
    {
        *lpSize = Needed;
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }

    *lpSize = Needed;
    RtlZeroMemory(lpAttributeList, Needed);
    ((PK32_PROC_THREAD_ATTRIBUTE_LIST)lpAttributeList)->Size = dwAttributeCount;
    return TRUE;
}

BOOL
WINAPI
UpdateProcThreadAttribute(
    _Inout_ LPPROC_THREAD_ATTRIBUTE_LIST lpAttributeList,
    _In_ DWORD dwFlags,
    _In_ DWORD_PTR Attribute,
    _In_ PVOID lpValue,
    _In_ SIZE_T cbSize,
    _Out_opt_ PVOID lpPreviousValue,
    _In_opt_ PSIZE_T lpReturnSize)
{
    PK32_PROC_THREAD_ATTRIBUTE_LIST List = (PK32_PROC_THREAD_ATTRIBUTE_LIST)lpAttributeList;
    DWORD Mask;
    DWORD Error;
    PK32_PROC_THREAD_ATTRIBUTE Entry;

    if (!List || !lpValue || dwFlags != 0 || lpPreviousValue || lpReturnSize)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    Error = K32ValidateAttribute(Attribute, cbSize);
    if (Error != ERROR_SUCCESS)
    {
        SetLastError(Error);
        return FALSE;
    }

    if (List->Count >= List->Size)
    {
        SetLastError(ERROR_GEN_FAILURE);
        return FALSE;
    }

    Mask = 1u << (Attribute & PROC_THREAD_ATTRIBUTE_NUMBER);
    if (List->Mask & Mask)
    {
        SetLastError(ERROR_OBJECT_NAME_EXISTS);
        return FALSE;
    }

    Entry = &List->Attributes[List->Count++];
    Entry->Attribute = Attribute;
    Entry->Size = cbSize;
    Entry->Value = lpValue;
    List->Mask |= Mask;
    return TRUE;
}

VOID
WINAPI
DeleteProcThreadAttributeList(
    _Inout_ LPPROC_THREAD_ATTRIBUTE_LIST lpAttributeList)
{
    UNREFERENCED_PARAMETER(lpAttributeList);
}
