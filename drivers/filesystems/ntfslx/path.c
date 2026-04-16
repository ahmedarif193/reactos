/*
 * NTFS pathname traversal helpers for the staged ReactOS-compatible port.
 *
 * Integration note:
 * This file is intentionally self-contained and does not depend on any FCB,
 * CCB, or directory-enumeration implementation.  The traversal routine only
 * requires a narrow callback that can resolve one normalized path component
 * from the current node and return the next node.  IRP_MJ_CREATE glue can
 * adapt the real directory lookup logic to that contract later without
 * changing the path-walking code below.
 *
 * Normalization assumptions:
 * - paths are native NT paths, not Win32/DOS paths;
 * - backslash is the only separator handled here;
 * - repeated separators are collapsed;
 * - leading separators are ignored so traversal starts at the filesystem root;
 * - trailing separators are ignored unless the path is exactly root;
 * - dot segments are preserved for the callback layer to interpret.
 */

#include "ntfslx.h"

#include <debug.h>

#ifndef NTFSLX_PATH_TRACE
#define NTFSLX_PATH_TRACE DbgPrint
#endif

static __inline BOOLEAN
NtfslxPathIsSeparator(_In_ WCHAR Character)
{
    return (Character == L'\\');
}

static __inline BOOLEAN
NtfslxPathIsValidUnicodeString(_In_ PCUNICODE_STRING Path)
{
    return ((Path != NULL) &&
            ((Path->Length & (sizeof(WCHAR) - 1)) == 0) &&
            ((Path->Length == 0) || (Path->Buffer != NULL)));
}

static __inline USHORT
NtfslxPathSkipSeparators(_In_ PCUNICODE_STRING Path,
                         _In_ USHORT Offset)
{
    USHORT CurrentOffset = Offset;

    while (CurrentOffset < Path->Length)
    {
        if (!NtfslxPathIsSeparator(Path->Buffer[CurrentOffset / sizeof(WCHAR)]))
        {
            break;
        }

        CurrentOffset += sizeof(WCHAR);
    }

    return CurrentOffset;
}

static __inline VOID
NtfslxPathInitComponent(_Out_ PUNICODE_STRING Component,
                        _In_ PCUNICODE_STRING Path,
                        _In_ USHORT Offset,
                        _In_ USHORT Length)
{
    Component->Buffer = &Path->Buffer[Offset / sizeof(WCHAR)];
    Component->Length = Length;
    Component->MaximumLength = Length;
}

BOOLEAN
NTAPI
NtfslxPathIsAbsolute(
    _In_ PCUNICODE_STRING Path)
{
    return NtfslxPathIsValidUnicodeString(Path) &&
           (Path->Length != 0) &&
           NtfslxPathIsSeparator(Path->Buffer[0]);
}

BOOLEAN
NTAPI
NtfslxPathIsDotComponent(
    _In_ PUNICODE_STRING Component)
{
    return (Component != NULL) &&
           (Component->Length == sizeof(WCHAR)) &&
           (Component->Buffer != NULL) &&
           (Component->Buffer[0] == L'.');
}

BOOLEAN
NTAPI
NtfslxPathIsDotDotComponent(
    _In_ PUNICODE_STRING Component)
{
    return (Component != NULL) &&
           (Component->Length == 2 * sizeof(WCHAR)) &&
           (Component->Buffer != NULL) &&
           (Component->Buffer[0] == L'.') &&
           (Component->Buffer[1] == L'.');
}

BOOLEAN
NTAPI
NtfslxPathGetNextComponent(
    _In_ PCUNICODE_STRING Path,
    _Inout_ PUSHORT Offset,
    _Out_ PUNICODE_STRING Component,
    _Out_opt_ PBOOLEAN IsLastComponent)
{
    USHORT StartOffset;
    USHORT EndOffset;
    USHORT NextOffset;

    if (!NtfslxPathIsValidUnicodeString(Path) ||
        (Offset == NULL) ||
        (Component == NULL))
    {
        return FALSE;
    }

    if (*Offset > Path->Length)
    {
        return FALSE;
    }

    StartOffset = NtfslxPathSkipSeparators(Path, *Offset);
    if (StartOffset >= Path->Length)
    {
        *Offset = StartOffset;
        return FALSE;
    }

    EndOffset = StartOffset;
    while (EndOffset < Path->Length &&
           !NtfslxPathIsSeparator(Path->Buffer[EndOffset / sizeof(WCHAR)]))
    {
        EndOffset += sizeof(WCHAR);
    }

    NtfslxPathInitComponent(Component, Path, StartOffset, EndOffset - StartOffset);

    NextOffset = NtfslxPathSkipSeparators(Path, EndOffset);
    *Offset = NextOffset;

    if (IsLastComponent != NULL)
    {
        *IsLastComponent = (NextOffset >= Path->Length);
    }

    return TRUE;
}

NTSTATUS
NTAPI
NtfslxTraversePath(
    _In_ PCUNICODE_STRING Path,
    _In_ PVOID RootNode,
    _In_ PNTFSLX_PATH_COMPONENT_LOOKUP_ROUTINE LookupComponent,
    _In_ PVOID LookupContext,
    _Out_opt_ PVOID *ResolvedNode,
    _Out_opt_ PUNICODE_STRING FinalComponent,
    _Out_opt_ PULONG ComponentCount)
{
    USHORT Offset;
    UNICODE_STRING Component;
    BOOLEAN IsLastComponent;
    PVOID CurrentNode;
    PVOID NextNode;
    ULONG Count;
    NTSTATUS Status;

    if (!NtfslxPathIsValidUnicodeString(Path) ||
        (LookupComponent == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }

    Offset = 0;
    CurrentNode = RootNode;
    Count = 0;

    if (ResolvedNode != NULL)
    {
        *ResolvedNode = RootNode;
    }

    if (FinalComponent != NULL)
    {
        FinalComponent->Buffer = NULL;
        FinalComponent->Length = 0;
        FinalComponent->MaximumLength = 0;
    }

    if (Path->Length == 0)
    {
        if (ComponentCount != NULL)
        {
            *ComponentCount = 0;
        }

        return STATUS_SUCCESS;
    }

    while (NtfslxPathGetNextComponent(Path, &Offset, &Component, &IsLastComponent))
    {
        NTFSLX_PATH_TRACE("ntfslx: traverse component='%wZ' final=%u current=%p\n",
                          &Component,
                          IsLastComponent,
                          CurrentNode);

        Status = LookupComponent(LookupContext,
                                 CurrentNode,
                                 &Component,
                                 IsLastComponent,
                                 &NextNode);
        if (!NT_SUCCESS(Status))
        {
            NTFSLX_PATH_TRACE("ntfslx: traverse lookup failed component='%wZ' status=0x%08lx current=%p\n",
                              &Component,
                              Status,
                              CurrentNode);
            return Status;
        }

        NTFSLX_PATH_TRACE("ntfslx: traverse advanced component='%wZ' next=%p\n",
                          &Component,
                          NextNode);

        CurrentNode = NextNode;
        Count++;

        if (FinalComponent != NULL)
        {
            *FinalComponent = Component;
        }
    }

    if (ResolvedNode != NULL)
    {
        *ResolvedNode = CurrentNode;
    }

    if (ComponentCount != NULL)
    {
        *ComponentCount = Count;
    }

    return STATUS_SUCCESS;
}
