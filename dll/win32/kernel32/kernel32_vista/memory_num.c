/*
 * PROJECT:         ReactOS system libraries
 * LICENSE:         GPL-2.0-or-later - See COPYING in the top level directory
 * PURPOSE:         Vista NUMA memory APIs
 * COPYRIGHT:       2025 Ahmed ARIF (arif.ing@outlook.com)
 */

#include "k32_vista.h"

#define NDEBUG
#include <debug.h>

LPVOID
WINAPI
VirtualAllocExNuma(HANDLE ProcessHandle,
                   LPVOID BaseAddress,
                   SIZE_T Size,
                   DWORD AllocationType,
                   DWORD Protect,
                   DWORD Preferred)
{
    if (Preferred != 0)
        DPRINT1("VirtualAllocExNuma: ignoring preferred node %lu\n", Preferred);

    return VirtualAllocEx(ProcessHandle,
                          BaseAddress,
                          Size,
                          AllocationType,
                          Protect);
}

LPVOID
WINAPI
MapViewOfFileExNuma(HANDLE FileMappingHandle,
                    DWORD DesiredAccess,
                    DWORD FileOffsetHigh,
                    DWORD FileOffsetLow,
                    SIZE_T NumberOfBytesToMap,
                    LPVOID BaseAddress,
                    DWORD Preferred)
{
    if (Preferred != 0)
        DPRINT1("MapViewOfFileExNuma: ignoring preferred node %lu\n", Preferred);

    return MapViewOfFileEx(FileMappingHandle,
                           DesiredAccess,
                           FileOffsetHigh,
                           FileOffsetLow,
                           NumberOfBytesToMap,
                           BaseAddress);
}

HANDLE
WINAPI
CreateFileMappingNumaW(HANDLE FileHandle,
                       LPSECURITY_ATTRIBUTES FileMappingAttributes,
                       DWORD FlProtect,
                       DWORD MaximumSizeHigh,
                       DWORD MaximumSizeLow,
                       LPCWSTR Name,
                       DWORD Preferred)
{
    if (Preferred != 0)
        DPRINT1("CreateFileMappingNumaW: ignoring preferred node %lu\n", Preferred);

    return CreateFileMappingW(FileHandle,
                              FileMappingAttributes,
                              FlProtect,
                              MaximumSizeHigh,
                              MaximumSizeLow,
                              Name);
}

HANDLE
WINAPI
CreateFileMappingNumaA(HANDLE FileHandle,
                       LPSECURITY_ATTRIBUTES FileMappingAttributes,
                       DWORD FlProtect,
                       DWORD MaximumSizeHigh,
                       DWORD MaximumSizeLow,
                       LPCSTR Name,
                       DWORD Preferred)
{
    PUNICODE_STRING UnicodeName;

    if (Preferred != 0)
        DPRINT1("CreateFileMappingNumaA: ignoring preferred node %lu\n", Preferred);

    if (!Name)
    {
        return CreateFileMappingNumaW(FileHandle,
                                      FileMappingAttributes,
                                      FlProtect,
                                      MaximumSizeHigh,
                                      MaximumSizeLow,
                                      NULL,
                                      Preferred);
    }

    UnicodeName = K32VistaAnsiToStaticUnicode(Name);
    if (!UnicodeName)
        return NULL;

    return CreateFileMappingNumaW(FileHandle,
                                  FileMappingAttributes,
                                  FlProtect,
                                  MaximumSizeHigh,
                                  MaximumSizeLow,
                                  UnicodeName->Buffer,
                                  Preferred);
}
