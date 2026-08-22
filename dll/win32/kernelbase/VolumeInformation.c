/*
 * PROJECT:     ReactOS KernelBase
 * PURPOSE:     Volume information query by handle
 *
 * Copyright 1993 Erik Bos
 * Copyright 1996, 2004 Alexandre Julliard
 * Copyright 1999 Petr Tomasek
 * Copyright 2000 Andreas Mohr
 * Copyright 2003 Eric Pouech
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include <string.h>

#include <ntstatus.h>
#define WIN32_NO_STATUS
#include <windef.h>
#include <winbase.h>
#include <winternl.h>
#include <winioctl.h>
#include <ddk/wdm.h>

#include "wine/kernelbase.h"

BOOL
WINAPI
GetVolumeInformationByHandleW(
    _In_ HANDLE VolumeHandle,
    _Out_writes_opt_(VolumeNameSize) LPWSTR VolumeNameBuffer,
    _In_ DWORD VolumeNameSize,
    _Out_opt_ LPDWORD VolumeSerialNumber,
    _Out_opt_ LPDWORD MaximumComponentLength,
    _Out_opt_ LPDWORD FileSystemFlags,
    _Out_writes_opt_(FileSystemNameSize) LPWSTR FileSystemNameBuffer,
    _In_ DWORD FileSystemNameSize)
{
    IO_STATUS_BLOCK IoStatusBlock;

    if (VolumeNameBuffer || VolumeSerialNumber)
    {
        BYTE Buffer[sizeof(FILE_FS_VOLUME_INFORMATION) + MAX_PATH * sizeof(WCHAR)];
        PFILE_FS_VOLUME_INFORMATION VolumeInformation = (PFILE_FS_VOLUME_INFORMATION)Buffer;

        if (!set_ntstatus(NtQueryVolumeInformationFile(VolumeHandle,
                                                       &IoStatusBlock,
                                                       VolumeInformation,
                                                       sizeof(Buffer),
                                                       FileFsVolumeInformation)))
        {
            return FALSE;
        }

        if (VolumeNameBuffer)
        {
            DWORD RequiredLength = VolumeInformation->VolumeLabelLength / sizeof(WCHAR) + 1;

            if (VolumeNameSize < RequiredLength)
            {
                SetLastError(ERROR_BAD_LENGTH);
                return FALSE;
            }

            memcpy(VolumeNameBuffer,
                   VolumeInformation->VolumeLabel,
                   VolumeInformation->VolumeLabelLength);
            VolumeNameBuffer[RequiredLength - 1] = UNICODE_NULL;
        }

        if (VolumeSerialNumber)
            *VolumeSerialNumber = VolumeInformation->VolumeSerialNumber;
    }

    if (MaximumComponentLength || FileSystemFlags || FileSystemNameBuffer)
    {
        BYTE Buffer[sizeof(FILE_FS_ATTRIBUTE_INFORMATION) + MAX_PATH * sizeof(WCHAR)];
        PFILE_FS_ATTRIBUTE_INFORMATION AttributeInformation = (PFILE_FS_ATTRIBUTE_INFORMATION)Buffer;

        if (!set_ntstatus(NtQueryVolumeInformationFile(VolumeHandle,
                                                       &IoStatusBlock,
                                                       AttributeInformation,
                                                       sizeof(Buffer),
                                                       FileFsAttributeInformation)))
        {
            return FALSE;
        }

        if (FileSystemNameBuffer)
        {
            DWORD RequiredLength = AttributeInformation->FileSystemNameLength / sizeof(WCHAR) + 1;

            if (FileSystemNameSize < RequiredLength)
            {
                SetLastError(ERROR_BAD_LENGTH);
                return FALSE;
            }

            memcpy(FileSystemNameBuffer,
                   AttributeInformation->FileSystemName,
                   AttributeInformation->FileSystemNameLength);
            FileSystemNameBuffer[RequiredLength - 1] = UNICODE_NULL;
        }

        if (MaximumComponentLength)
            *MaximumComponentLength = AttributeInformation->MaximumComponentNameLength;
        if (FileSystemFlags)
            *FileSystemFlags = AttributeInformation->FileSystemAttributes;
    }

    return TRUE;
}
