/*
 *  FreeLoader NTFS support
 *  Copyright (C) 2004  Filip Navara  <xnavara@volny.cz>
 *  Copyright (C) 2009-2010  Hervé Poussineau
 *  Copyright (C) 2026  Ahmed ARIF  <arif.ing@outlook.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <freeldr.h>

#include <debug.h>
#include <ntfs3g_ros.h>

#include "host.h"

DBG_DEFAULT_CHANNEL(FILESYSTEM);

#define TAG_NTFS3G 'G3tN'

typedef struct _NTFS3G_FREELDR_DEVICE
{
    ULONG DeviceId;
} NTFS3G_FREELDR_DEVICE;

static NTFS3G_ROS_VOLUME *Ntfs3gVolumes[MAX_FDS];

static ARC_STATUS
Ntfs3gErrnoToArcStatus(int Error)
{
    switch (Error) {
        case EACCES:
        case EROFS:
            return EACCES;
        case EINVAL:
            return EINVAL;
        case ENOENT:
            return ENOENT;
        case ENOMEM:
            return ENOMEM;
        default:
            return EIO;
    }
}

static int
Ntfs3gArcStatusToErrno(ARC_STATUS Status)
{
    switch (Status) {
        case EACCES:
            return EACCES;
        case EINVAL:
            return EINVAL;
        case ENOMEM:
            return ENOMEM;
        default:
            return EIO;
    }
}

void *
Ntfs3gRosHostAllocate(size_t Size)
{
    if (Size > MAXULONG)
        return NULL;
    return FrLdrTempAlloc((ULONG)Size, TAG_NTFS3G);
}

void
Ntfs3gRosHostFree(void *Buffer)
{
    if (Buffer)
        FrLdrTempFree(Buffer, TAG_NTFS3G);
}

void
Ntfs3gRosHostAcquire(void)
{
}

void
Ntfs3gRosHostRelease(void)
{
}

int64_t
Ntfs3gRosHostGetTime(void)
{
    return 0;
}

void
Ntfs3gRosHostLog(int IsError,
                 const char *Message)
{
#if DBG
    if (IsError)
        ERR("NTFS3G: %s", Message);
    else
        TRACE("NTFS3G: %s", Message);
#else
    UNREFERENCED_PARAMETER(IsError);
    UNREFERENCED_PARAMETER(Message);
#endif
}

static int
Ntfs3gFreeLdrRead(void *OpaqueContext,
                  uint64_t Offset,
                  void *Buffer,
                  uint32_t Length,
                  uint32_t *BytesRead)
{
    NTFS3G_FREELDR_DEVICE *Context = OpaqueContext;
    LARGE_INTEGER Position;
    ARC_STATUS Status;
    ULONG Count;

    if (Offset > INT64_MAX)
        return EINVAL;
    Position.QuadPart = Offset;
    Status = ArcSeek(Context->DeviceId, &Position, SeekAbsolute);
    if (Status != ESUCCESS)
        return Ntfs3gArcStatusToErrno(Status);
    Status = ArcRead(Context->DeviceId, Buffer, Length, &Count);
    if (Status != ESUCCESS)
        return Ntfs3gArcStatusToErrno(Status);
    *BytesRead = Count;
    return 0;
}

static void
Ntfs3gFreeLdrClose(void *OpaqueContext)
{
    Ntfs3gRosHostFree(OpaqueContext);
}

static const NTFS3G_ROS_DEVICE_OPERATIONS Ntfs3gFreeLdrDeviceOperations = {
    Ntfs3gFreeLdrRead,
    Ntfs3gFreeLdrClose
};

static ARC_STATUS
NtfsClose(ULONG FileId)
{
    NTFS3G_ROS_FILE *File = FsGetDeviceSpecific(FileId);
    int Result = Ntfs3gRosCloseFile(File);

    if (Result < 0)
        return Ntfs3gErrnoToArcStatus(-Result);
    return ESUCCESS;
}

static ARC_STATUS
NtfsGetFileInformation(ULONG FileId,
                       FILEINFORMATION *Information)
{
    NTFS3G_ROS_FILE *File = FsGetDeviceSpecific(FileId);
    const char *Name = Ntfs3gRosGetFileName(File);
    uint32_t Attributes = Ntfs3gRosGetFileAttributes(File);
    size_t NameLength;

    RtlZeroMemory(Information, sizeof(*Information));
    Information->EndingAddress.QuadPart = Ntfs3gRosGetFileSize(File);
    Information->CurrentAddress.QuadPart = Ntfs3gRosGetFilePosition(File);
    if (Attributes & NTFS3G_ROS_FILE_READONLY)
        Information->Attributes |= ReadOnlyFile;
    if (Attributes & NTFS3G_ROS_FILE_HIDDEN)
        Information->Attributes |= HiddenFile;
    if (Attributes & NTFS3G_ROS_FILE_SYSTEM)
        Information->Attributes |= SystemFile;
    if (Attributes & NTFS3G_ROS_FILE_ARCHIVE)
        Information->Attributes |= ArchiveFile;
    if (Attributes & NTFS3G_ROS_FILE_DIRECTORY)
        Information->Attributes |= DirectoryFile;

    NameLength = strnlen(Name, sizeof(Information->FileName) - 1);
    Information->FileNameLength = (ULONG)NameLength;
    RtlCopyMemory(Information->FileName, Name, NameLength);
    Information->FileName[NameLength] = ANSI_NULL;
    return ESUCCESS;
}

static ARC_STATUS
NtfsOpen(CHAR *Path,
         OPENMODE OpenMode,
         ULONG *FileId)
{
    NTFS3G_ROS_FILE *File;
    NTFS3G_ROS_VOLUME *Volume;
    ULONG DeviceId;
    int Result;

    if (OpenMode != OpenReadOnly)
        return EACCES;

    DeviceId = FsGetDeviceId(*FileId);
    if (DeviceId >= MAX_FDS || !(Volume = Ntfs3gVolumes[DeviceId]))
        return ENODEV;
    Result = Ntfs3gRosOpenFile(Volume, Path, &File);
    if (Result < 0)
        return Ntfs3gErrnoToArcStatus(-Result);
    FsSetDeviceSpecific(*FileId, File);
    return ESUCCESS;
}

static ARC_STATUS
NtfsRead(ULONG FileId,
         VOID *Buffer,
         ULONG Length,
         ULONG *Count)
{
    NTFS3G_ROS_FILE *File = FsGetDeviceSpecific(FileId);
    size_t BytesRead;
    int Result;

    Result = Ntfs3gRosReadFile(File, Buffer, Length, &BytesRead);
    if (Result < 0)
        return Ntfs3gErrnoToArcStatus(-Result);
    *Count = (ULONG)BytesRead;
    return ESUCCESS;
}

static ARC_STATUS
NtfsSeek(ULONG FileId,
         LARGE_INTEGER *Position,
         SEEKMODE SeekMode)
{
    NTFS3G_ROS_FILE *File = FsGetDeviceSpecific(FileId);
    int Origin;
    int Result;

    if (SeekMode == SeekAbsolute)
        Origin = NTFS3G_ROS_SEEK_SET;
    else if (SeekMode == SeekRelative)
        Origin = NTFS3G_ROS_SEEK_CUR;
    else
        return EINVAL;
    Result = Ntfs3gRosSeekFile(File, Position->QuadPart, Origin);
    if (Result < 0)
        return Ntfs3gErrnoToArcStatus(-Result);
    return ESUCCESS;
}

ULONGLONG
NtfsGetVolumeSize(
    _In_ ULONG DeviceId)
{
    if (DeviceId >= MAX_FDS)
        return 0;
    return Ntfs3gRosGetVolumeSize(Ntfs3gVolumes[DeviceId]);
}

static const DEVVTBL NtfsFuncTable = {
    NtfsClose,
    NtfsGetFileInformation,
    NtfsOpen,
    NtfsRead,
    NtfsSeek,
    L"ntfs3g",
};

const DEVVTBL *
NtfsMount(
    _In_ ULONG DeviceId)
{
    NTFS3G_FREELDR_DEVICE *Context;
    FILEINFORMATION Information;
    uint64_t DeviceLength;
    int Result;

    if (DeviceId >= MAX_FDS)
        return NULL;
    if (Ntfs3gVolumes[DeviceId])
        return &NtfsFuncTable;
    if (ArcGetFileInformation(DeviceId, &Information) != ESUCCESS)
        return NULL;

    DeviceLength = Information.EndingAddress.QuadPart -
                   Information.StartingAddress.QuadPart;
    Context = Ntfs3gRosHostAllocate(sizeof(*Context));
    if (!Context)
        return NULL;
    Context->DeviceId = DeviceId;

    Result = Ntfs3gRosMount(Context, &Ntfs3gFreeLdrDeviceOperations,
                            DeviceLength, 512,
                            &Ntfs3gVolumes[DeviceId]);
    if (Result < 0) {
        TRACE("NTFS3G: mount failed with errno %d\n",
              -Result);
        return NULL;
    }
    TRACE("NTFS3G: mounted device %lu\n", DeviceId);
    return &NtfsFuncTable;
}
