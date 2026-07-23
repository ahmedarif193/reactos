/*
 * PROJECT:     ReactOS NTFS-3G Library
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Shared read-only NTFS-3G file interface
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "config.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "attrib.h"
#include "dir.h"
#include "host.h"
#include "inode.h"
#include "layout.h"
#include "ntfs3g_ros.h"
#include "reactos_volume.h"
#include "unistr.h"
#include "volume.h"

struct _NTFS3G_ROS_FILE
{
    ntfs_inode *Inode;
    ntfs_attr *Data;
    char *Path;
    const char *Name;
    uint64_t Position;
    uint64_t DirectoryPosition;
    NTFS3G_ROS_FILE_INFORMATION Information;
};

typedef struct _NTFS3G_ROS_READ_DIRECTORY_CONTEXT
{
    ntfs_volume *Volume;
    NTFS3G_ROS_DIRECTORY_ENTRY *Entry;
    int Found;
    int Error;
} NTFS3G_ROS_READ_DIRECTORY_CONTEXT;

static int
Ntfs3gRosAddOffset(uint64_t Base,
                   int64_t Offset,
                   int64_t *Result)
{
    uint64_t Magnitude;

    if (Base > INT64_MAX)
        return -1;
    if (Offset >= 0) {
        if (Base > (uint64_t)INT64_MAX - (uint64_t)Offset)
            return -1;
    } else {
        Magnitude = (uint64_t)(-(Offset + 1)) + 1;
        if (Base < Magnitude)
            return -1;
    }
    *Result = (int64_t)Base + Offset;
    return 0;
}

static char *
Ntfs3gRosNormalizePath(const char *Path)
{
    char *Normalized;
    char *Character;

    Normalized = strdup(Path);
    if (!Normalized)
        return NULL;
    for (Character = Normalized; *Character; ++Character) {
        if (*Character == '\\')
            *Character = '/';
    }
    return Normalized;
}

static void
Ntfs3gRosFillFileInformation(const ntfs_inode *Inode,
                             const ntfs_attr *Data,
                             NTFS3G_ROS_FILE_INFORMATION *Information)
{
    int IsDirectory = (Inode->mrec->flags & MFT_RECORD_IS_DIRECTORY) != 0;

    memset(Information, 0, sizeof(*Information));
    Information->FileId = Inode->mft_no;
    Information->CreationTime = sle64_to_cpu(Inode->creation_time);
    Information->LastAccessTime = sle64_to_cpu(Inode->last_access_time);
    Information->LastWriteTime = sle64_to_cpu(Inode->last_data_change_time);
    Information->ChangeTime = sle64_to_cpu(Inode->last_mft_change_time);
    Information->AllocationSize = IsDirectory ? 0 :
        (Data ? Data->allocated_size : Inode->allocated_size);
    Information->FileSize = IsDirectory ? 0 :
        (Data ? Data->data_size : Inode->data_size);
    Information->Attributes = le32_to_cpu(Inode->flags);
    if (IsDirectory)
        Information->Attributes |= NTFS3G_ROS_FILE_DIRECTORY;
    Information->LinkCount = le16_to_cpu(Inode->mrec->link_count);
}

static int
Ntfs3gRosFillDirectoryEntry(void *OpaqueContext,
                            const ntfschar *Name,
                            int NameLength,
                            int NameType,
                            int64_t Position,
                            MFT_REF Reference,
                            unsigned int Type)
{
    NTFS3G_ROS_READ_DIRECTORY_CONTEXT *Context = OpaqueContext;
    ntfs_inode *Inode;
    int Index;

    (void)Position;
    (void)Type;
    if (NameType == FILE_NAME_DOS)
        return 0;
    if (NameLength < 0 || NameLength > NTFS3G_ROS_MAX_NAME_LENGTH) {
        Context->Error = EIO;
        return 1;
    }

    Inode = ntfs_inode_open(Context->Volume, Reference);
    if (!Inode) {
        Context->Error = errno;
        return 1;
    }

    Ntfs3gRosFillFileInformation(Inode, NULL,
                                 &Context->Entry->Information);
    for (Index = 0; Index < NameLength; ++Index)
        Context->Entry->FileName[Index] = le16_to_cpu(Name[Index]);
    Context->Entry->FileName[NameLength] = 0;
    Context->Entry->FileNameLength = (uint16_t)NameLength;
    Context->Found = 1;
    ntfs_inode_close(Inode);
    return 1;
}

static int
Ntfs3gRosReadFileLocked(NTFS3G_ROS_FILE *File,
                        uint64_t Offset,
                        void *Buffer,
                        size_t Length,
                        size_t *BytesRead)
{
    size_t Request = Length > INT64_MAX ? INT64_MAX : Length;
    int64_t Result;

    Result = ntfs_attr_pread(File->Data, Offset, Request, Buffer);
    if (Result < 0)
        return -1;
    *BytesRead = (size_t)Result;
    return 0;
}

int
Ntfs3gRosOpenFile(NTFS3G_ROS_VOLUME *Volume,
                  const char *Path,
                  NTFS3G_ROS_FILE **File)
{
    NTFS3G_ROS_FILE *HostFile = NULL;
    ntfs_inode *Inode = NULL;
    ntfs_attr *Data = NULL;
    char *Normalized = NULL;
    char *Name;
    int Error;

    if (!Volume || !Path || !File) {
        errno = EINVAL;
        return -EINVAL;
    }

    *File = NULL;
    Ntfs3gRosHostAcquire();
    Normalized = Ntfs3gRosNormalizePath(Path);
    if (!Normalized)
        goto error;

    Inode = ntfs_pathname_to_inode(Volume->Native, NULL, Normalized);
    if (!Inode)
        goto error;
    if (!(Inode->mrec->flags & MFT_RECORD_IS_DIRECTORY)) {
        Data = ntfs_attr_open(Inode, AT_DATA, AT_UNNAMED, 0);
        if (!Data)
            goto error;
    }

    HostFile = calloc(1, sizeof(*HostFile));
    if (!HostFile)
        goto error;

    Name = strrchr(Normalized, '/');
    HostFile->Inode = Inode;
    HostFile->Data = Data;
    HostFile->Path = Normalized;
    HostFile->Name = Name ? Name + 1 : Normalized;
    Ntfs3gRosFillFileInformation(Inode, Data, &HostFile->Information);
    *File = HostFile;
    Ntfs3gRosHostRelease();
    errno = 0;
    return 0;

error:
    Error = errno;
    free(HostFile);
    if (Data)
        ntfs_attr_close(Data);
    if (Inode)
        ntfs_inode_close(Inode);
    free(Normalized);
    Ntfs3gRosHostRelease();
    errno = Error;
    return -Error;
}

int
Ntfs3gRosOpenFileUtf16(NTFS3G_ROS_VOLUME *Volume,
                       const uint16_t *Path,
                       size_t PathLength,
                       NTFS3G_ROS_FILE **File)
{
    static const char RootPath[] = "/";
    ntfschar *LittleEndianPath;
    char *Utf8Path = NULL;
    size_t Index;
    int Error;
    int Result;

    if (!Volume || (!Path && PathLength) || !File || PathLength > INT_MAX) {
        errno = EINVAL;
        return -EINVAL;
    }
    if (!PathLength)
        return Ntfs3gRosOpenFile(Volume, RootPath, File);
    if (PathLength > (SIZE_MAX / sizeof(*LittleEndianPath)) - 1) {
        errno = ENOMEM;
        return -ENOMEM;
    }

    LittleEndianPath = malloc((PathLength + 1) * sizeof(*LittleEndianPath));
    if (!LittleEndianPath)
        return -errno;
    for (Index = 0; Index < PathLength; ++Index)
        LittleEndianPath[Index] = cpu_to_le16(Path[Index]);
    LittleEndianPath[PathLength] = 0;

    Ntfs3gRosHostAcquire();
    Result = ntfs_ucstombs(LittleEndianPath, (int)PathLength,
                           &Utf8Path, 0);
    Error = Result < 0 ? errno : 0;
    Ntfs3gRosHostRelease();
    free(LittleEndianPath);
    if (Result < 0) {
        errno = Error;
        return -Error;
    }

    Result = Ntfs3gRosOpenFile(Volume, Utf8Path, File);
    Error = Result ? errno : 0;
    free(Utf8Path);
    errno = Error;
    return Result;
}

int
Ntfs3gRosCloseFile(NTFS3G_ROS_FILE *File)
{
    int Error = 0;

    if (!File) {
        errno = EINVAL;
        return -EINVAL;
    }

    Ntfs3gRosHostAcquire();
    if (File->Data)
        ntfs_attr_close(File->Data);
    if (ntfs_inode_close(File->Inode))
        Error = errno;
    free(File->Path);
    free(File);
    Ntfs3gRosHostRelease();
    errno = Error;
    return Error ? -Error : 0;
}

int
Ntfs3gRosReadFile(NTFS3G_ROS_FILE *File,
                  void *Buffer,
                  size_t Length,
                  size_t *BytesRead)
{
    if (!File || (!Buffer && Length) || !BytesRead) {
        errno = EINVAL;
        return -EINVAL;
    }
    if (!File->Data) {
        errno = EISDIR;
        return -EISDIR;
    }

    *BytesRead = 0;
    Ntfs3gRosHostAcquire();
    {
        int Result = Ntfs3gRosReadFileLocked(File, File->Position,
                                             Buffer, Length, BytesRead);
        int Error = Result ? errno : 0;

        if (!Result)
            File->Position += *BytesRead;
        Ntfs3gRosHostRelease();
        errno = Error;
        return Result ? -Error : 0;
    }
}

int
Ntfs3gRosReadFileAt(NTFS3G_ROS_FILE *File,
                    uint64_t Offset,
                    void *Buffer,
                    size_t Length,
                    size_t *BytesRead)
{
    int Result;
    int Error;

    if (!File || !File->Data || (!Buffer && Length) || !BytesRead ||
        Offset > INT64_MAX) {
        errno = !File || !BytesRead || Offset > INT64_MAX ? EINVAL : EISDIR;
        return -errno;
    }

    *BytesRead = 0;
    Ntfs3gRosHostAcquire();
    Result = Ntfs3gRosReadFileLocked(File, Offset, Buffer, Length, BytesRead);
    Error = Result ? errno : 0;
    Ntfs3gRosHostRelease();
    errno = Error;
    return Result ? -Error : 0;
}

int
Ntfs3gRosSeekFile(NTFS3G_ROS_FILE *File,
                  int64_t Offset,
                  int Origin)
{
    int64_t Position;
    int Error = 0;

    if (!File) {
        errno = EINVAL;
        return -EINVAL;
    }
    if (!File->Data) {
        errno = EISDIR;
        return -EISDIR;
    }

    Ntfs3gRosHostAcquire();
    switch (Origin) {
        case NTFS3G_ROS_SEEK_SET:
            Position = Offset;
            break;
        case NTFS3G_ROS_SEEK_CUR:
            if (Ntfs3gRosAddOffset(File->Position, Offset, &Position)) {
                Error = EINVAL;
                goto done;
            }
            break;
        case NTFS3G_ROS_SEEK_END:
            if (Ntfs3gRosAddOffset(File->Information.FileSize,
                                   Offset, &Position)) {
                Error = EINVAL;
                goto done;
            }
            break;
        default:
            Error = EINVAL;
            goto done;
    }
    if (Position < 0 || (uint64_t)Position > File->Information.FileSize) {
        Error = EINVAL;
        goto done;
    }
    File->Position = Position;

done:
    Ntfs3gRosHostRelease();
    errno = Error;
    return Error ? -Error : 0;
}

uint64_t
Ntfs3gRosGetFileSize(const NTFS3G_ROS_FILE *File)
{
    return File ? File->Information.FileSize : 0;
}

uint64_t
Ntfs3gRosGetFilePosition(const NTFS3G_ROS_FILE *File)
{
    return File ? File->Position : 0;
}

uint32_t
Ntfs3gRosGetFileAttributes(const NTFS3G_ROS_FILE *File)
{
    return File ? File->Information.Attributes : 0;
}

const char *
Ntfs3gRosGetFileName(const NTFS3G_ROS_FILE *File)
{
    return File ? File->Name : NULL;
}

int
Ntfs3gRosGetFileInformation(const NTFS3G_ROS_FILE *File,
                            NTFS3G_ROS_FILE_INFORMATION *Information)
{
    if (!File || !Information) {
        errno = EINVAL;
        return -EINVAL;
    }

    *Information = File->Information;
    errno = 0;
    return 0;
}

int
Ntfs3gRosRestartDirectory(NTFS3G_ROS_FILE *File)
{
    if (!File || File->Data) {
        errno = !File ? EINVAL : ENOTDIR;
        return -errno;
    }
    File->DirectoryPosition = 0;
    errno = 0;
    return 0;
}

int
Ntfs3gRosReadDirectory(NTFS3G_ROS_FILE *File,
                       NTFS3G_ROS_DIRECTORY_ENTRY *Entry)
{
    NTFS3G_ROS_READ_DIRECTORY_CONTEXT Context;
    int64_t Position;
    int WasCaseSensitive;
    int Result;
    int Error;

    if (!File || File->Data || !Entry || File->DirectoryPosition > INT64_MAX) {
        errno = !File || !Entry || File->DirectoryPosition > INT64_MAX ?
            EINVAL : ENOTDIR;
        return -errno;
    }

    memset(Entry, 0, sizeof(*Entry));
    memset(&Context, 0, sizeof(Context));
    Context.Volume = File->Inode->vol;
    Context.Entry = Entry;
    Position = File->DirectoryPosition;

    Ntfs3gRosHostAcquire();
    WasCaseSensitive = NVolCaseSensitive(File->Inode->vol);
    if (!WasCaseSensitive)
        NVolSetCaseSensitive(File->Inode->vol);
    Result = ntfs_readdir(File->Inode, &Position, &Context,
                          Ntfs3gRosFillDirectoryEntry);
    if (!WasCaseSensitive)
        NVolClearCaseSensitive(File->Inode->vol);
    Error = errno;
    if (Context.Found)
        File->DirectoryPosition = (uint64_t)Position + 1;
    Ntfs3gRosHostRelease();

    if (Context.Found) {
        errno = 0;
        return 1;
    }
    if (Context.Error) {
        errno = Context.Error;
        return -Context.Error;
    }
    errno = Result ? Error : 0;
    return Result ? -Error : 0;
}

uint64_t
Ntfs3gRosGetDirectoryPosition(const NTFS3G_ROS_FILE *File)
{
    return File ? File->DirectoryPosition : 0;
}

int
Ntfs3gRosSetDirectoryPosition(NTFS3G_ROS_FILE *File,
                             uint64_t Position)
{
    if (!File || File->Data || Position > INT64_MAX) {
        errno = !File || Position > INT64_MAX ? EINVAL : ENOTDIR;
        return -errno;
    }
    File->DirectoryPosition = Position;
    errno = 0;
    return 0;
}
