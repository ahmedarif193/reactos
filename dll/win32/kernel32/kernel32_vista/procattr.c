/*
 * PROJECT:         ReactOS system libraries
 * LICENSE:         GPL-2.0-or-later - See COPYING in the top level directory
 * PURPOSE:         Vista process attribute APIs
 * COPYRIGHT:       2025 Ahmed ARIF (arif.ing@outlook.com)
 */

#include "k32_vista.h"

#define NDEBUG
#include <debug.h>

struct K32_PROC_THREAD_ATTRIBUTE
{
    DWORD_PTR Attribute;
    SIZE_T Size;
    PVOID Value;
};

struct _PROC_THREAD_ATTRIBUTE_LIST
{
    DWORD Mask;
    DWORD Size;
    DWORD Count;
    DWORD Pad;
    DWORD_PTR Unknown;
    struct K32_PROC_THREAD_ATTRIBUTE Attrs[1];
};

static DWORD
K32ValidateProcThreadAttribute(DWORD_PTR Attribute,
                               SIZE_T Size)
{
    switch (Attribute)
    {
        case PROC_THREAD_ATTRIBUTE_PARENT_PROCESS:
            if (Size != sizeof(HANDLE)) return ERROR_BAD_LENGTH;
            break;

#ifdef PROC_THREAD_ATTRIBUTE_EXTENDED_FLAGS
        case PROC_THREAD_ATTRIBUTE_EXTENDED_FLAGS:
            if (Size != sizeof(ULONG)) return ERROR_BAD_LENGTH;
            break;
#endif

        case PROC_THREAD_ATTRIBUTE_HANDLE_LIST:
            if ((Size / sizeof(HANDLE)) * sizeof(HANDLE) != Size)
                return ERROR_BAD_LENGTH;
            break;

#ifdef PROC_THREAD_ATTRIBUTE_JOB_LIST
        case PROC_THREAD_ATTRIBUTE_JOB_LIST:
            if ((Size / sizeof(HANDLE)) * sizeof(HANDLE) != Size)
                return ERROR_BAD_LENGTH;
            break;
#endif

        case PROC_THREAD_ATTRIBUTE_IDEAL_PROCESSOR:
            if (Size != sizeof(PROCESSOR_NUMBER)) return ERROR_BAD_LENGTH;
            break;

#ifdef PROC_THREAD_ATTRIBUTE_CHILD_PROCESS_POLICY
        case PROC_THREAD_ATTRIBUTE_CHILD_PROCESS_POLICY:
            if (Size != sizeof(DWORD) && Size != sizeof(DWORD64))
                return ERROR_BAD_LENGTH;
            break;
#endif

#ifdef PROC_THREAD_ATTRIBUTE_MITIGATION_POLICY
        case PROC_THREAD_ATTRIBUTE_MITIGATION_POLICY:
            if (Size != sizeof(DWORD) && Size != sizeof(DWORD64) &&
                Size != sizeof(DWORD64) * 2)
                return ERROR_BAD_LENGTH;
            break;
#endif

#if defined(PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE) && defined(HPCON)
        case PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE:
            if (Size != sizeof(HPCON)) return ERROR_BAD_LENGTH;
            break;
#elif defined(PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE)
        case PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE:
            return ERROR_NOT_SUPPORTED;
#endif

#ifdef PROC_THREAD_ATTRIBUTE_MACHINE_TYPE
        case PROC_THREAD_ATTRIBUTE_MACHINE_TYPE:
            if (Size != sizeof(USHORT)) return ERROR_BAD_LENGTH;
            break;
#endif

        default:
            DPRINT1("Unhandled attribute 0x%Ix\n", Attribute & PROC_THREAD_ATTRIBUTE_NUMBER);
            return ERROR_NOT_SUPPORTED;
    }

    return ERROR_SUCCESS;
}

static DWORD
K32Win32ModeToRtlMode(DWORD Mode)
{
    DWORD RtlMode = 0;

    if (Mode & SEM_FAILCRITICALERRORS) RtlMode |= 0x10;
    if (Mode & SEM_NOGPFAULTERRORBOX) RtlMode |= 0x20;
    if (Mode & SEM_NOOPENFILEERRORBOX) RtlMode |= 0x40;

    return RtlMode;
}

static DWORD
K32RtlModeToWin32Mode(DWORD Mode)
{
    DWORD Win32Mode = 0;

    if (Mode & 0x10) Win32Mode |= SEM_FAILCRITICALERRORS;
    if (Mode & 0x20) Win32Mode |= SEM_NOGPFAULTERRORBOX;
    if (Mode & 0x40) Win32Mode |= SEM_NOOPENFILEERRORBOX;

    return Win32Mode;
}

/*
 * @implemented
 */
BOOL
WINAPI
InitializeProcThreadAttributeList(struct _PROC_THREAD_ATTRIBUTE_LIST *AttributeList,
                                   DWORD AttributeCount,
                                   DWORD Flags,
                                   PSIZE_T AttributeListSize)
{
    SIZE_T RequiredSize;

    if (!AttributeListSize || Flags != 0)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    RequiredSize = FIELD_OFFSET(struct _PROC_THREAD_ATTRIBUTE_LIST, Attrs[AttributeCount]);

    if (!AttributeList || *AttributeListSize < RequiredSize)
    {
        *AttributeListSize = RequiredSize;
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }

    RtlZeroMemory(AttributeList, *AttributeListSize);

    AttributeList->Mask = 0;
    AttributeList->Size = AttributeCount;
    AttributeList->Count = 0;
    AttributeList->Unknown = 0;

    *AttributeListSize = RequiredSize;
    return TRUE;
}

/*
 * @implemented
 */
BOOL
WINAPI
UpdateProcThreadAttribute(struct _PROC_THREAD_ATTRIBUTE_LIST *AttributeList,
                          DWORD Flags,
                          DWORD_PTR Attribute,
                          PVOID Value,
                          SIZE_T Size,
                          PVOID PreviousValue,
                          PSIZE_T ReturnSize)
{
    DWORD MaskBit;
    struct K32_PROC_THREAD_ATTRIBUTE *Entry;
    DWORD Error;

    UNREFERENCED_PARAMETER(Flags);
    UNREFERENCED_PARAMETER(PreviousValue);
    UNREFERENCED_PARAMETER(ReturnSize);

    if (!AttributeList)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    if (AttributeList->Count >= AttributeList->Size)
    {
        SetLastError(ERROR_GEN_FAILURE);
        return FALSE;
    }

    Error = K32ValidateProcThreadAttribute(Attribute, Size);
    if (Error != ERROR_SUCCESS)
    {
        SetLastError(Error);
        return FALSE;
    }

    MaskBit = 1u << (Attribute & PROC_THREAD_ATTRIBUTE_NUMBER);
    if (AttributeList->Mask & MaskBit)
    {
        SetLastError(ERROR_OBJECT_NAME_EXISTS);
        return FALSE;
    }

    AttributeList->Mask |= MaskBit;

    Entry = &AttributeList->Attrs[AttributeList->Count++];
    Entry->Attribute = Attribute;
    Entry->Size = Size;
    Entry->Value = Value;

    return TRUE;
}

/*
 * @implemented
 */
VOID
WINAPI
DeleteProcThreadAttributeList(struct _PROC_THREAD_ATTRIBUTE_LIST *AttributeList)
{
    if (AttributeList)
        RtlZeroMemory(AttributeList, FIELD_OFFSET(struct _PROC_THREAD_ATTRIBUTE_LIST, Attrs[0]));
}

/*
 * @implemented
 */
DWORD
WINAPI
GetThreadErrorMode(VOID)
{
    return K32RtlModeToWin32Mode(RtlGetThreadErrorMode());
}

/*
 * @implemented
 */
BOOL
WINAPI
SetThreadErrorMode(DWORD NewMode,
                   LPDWORD OldMode)
{
    ULONG PreviousMode;
    NTSTATUS Status;
    DWORD AllowedFlags = SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX;

    if (NewMode & ~AllowedFlags)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    Status = RtlSetThreadErrorMode(K32Win32ModeToRtlMode(NewMode),
                                   OldMode ? &PreviousMode : NULL);
    if (!NT_SUCCESS(Status))
    {
        BaseSetLastNTError(Status);
        return FALSE;
    }

    if (OldMode)
        *OldMode = K32RtlModeToWin32Mode(PreviousMode);

    return TRUE;
}
