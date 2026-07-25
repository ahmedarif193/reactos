/*
 * PROJECT:     ReactOS NTFS-3G Library
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Shared NTFS-3G file interface
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "config.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "attrib.h"
#include "dir.h"
#include "ea.h"
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
    size_t Length;

    Normalized = strdup(Path);
    if (!Normalized)
        return NULL;
    for (Character = Normalized; *Character; ++Character) {
        if (*Character == '\\')
            *Character = '/';
    }
    Length = strlen(Normalized);
    while (Length > 1 && Normalized[Length - 1] == '/')
        Normalized[--Length] = '\0';
    return Normalized;
}

static int
Ntfs3gRosUtf16PathToUtf8(const uint16_t *Path,
                         size_t PathLength,
                         char **Utf8Path)
{
    static const char RootPath[] = "/";
    int Error;
    int Result;

    if ((!Path && PathLength) || !Utf8Path || PathLength > INT_MAX) {
        errno = EINVAL;
        return -EINVAL;
    }
    if (!PathLength) {
        *Utf8Path = strdup(RootPath);
        return *Utf8Path ? 0 : -errno;
    }

    Ntfs3gRosHostAcquire();
    Result = ntfs_ucstombs((const ntfschar *)Path, (int)PathLength,
                           Utf8Path, 0);
    Error = Result < 0 ? errno : 0;
    Ntfs3gRosHostRelease();
    errno = Error;
    return Result < 0 ? -Error : 0;
}

static char *
Ntfs3gRosGetParentPath(const char *Path,
                       const char **Name)
{
    const char *Separator;
    char *Parent;
    size_t Length;

    Separator = strrchr(Path, '/');
    *Name = Separator ? Separator + 1 : Path;
    if (!**Name || !strcmp(*Name, ".") || !strcmp(*Name, "..")) {
        errno = EINVAL;
        return NULL;
    }

    if (!Separator || Separator == Path)
        return strdup("/");

    Length = Separator - Path;
    Parent = malloc(Length + 1);
    if (!Parent)
        return NULL;
    memcpy(Parent, Path, Length);
    Parent[Length] = '\0';
    return Parent;
}

static void
Ntfs3gRosFillFileInformation(const ntfs_inode *Inode,
                             const ntfs_attr *Data,
                             NTFS3G_ROS_FILE_INFORMATION *Information);

static NTFS3G_ROS_FILE *
Ntfs3gRosAllocateFile(ntfs_inode *Inode,
                      ntfs_attr *Data,
                      char *Path)
{
    NTFS3G_ROS_FILE *File;
    char *Name;

    File = calloc(1, sizeof(*File));
    if (!File)
        return NULL;

    Name = strrchr(Path, '/');
    File->Inode = Inode;
    File->Data = Data;
    File->Path = Path;
    File->Name = Name ? Name + 1 : Path;
    Ntfs3gRosFillFileInformation(Inode, Data, &File->Information);
    return File;
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

static ntfs_inode *
Ntfs3gRosCreateInode(NTFS3G_ROS_VOLUME *Volume,
                     const char *Path,
                     mode_t Type)
{
    const char *Name;
    char *ParentPath = NULL;
    ntfs_inode *Parent = NULL;
    ntfs_inode *Inode = NULL;
    ntfschar *UnicodeName = NULL;
    int NameLength;
    int Error;

    ParentPath = Ntfs3gRosGetParentPath(Path, &Name);
    if (!ParentPath)
        return NULL;
    Parent = ntfs_pathname_to_inode(Volume->Native, NULL, ParentPath);
    if (!Parent)
        goto done;
    if (!(Parent->mrec->flags & MFT_RECORD_IS_DIRECTORY)) {
        errno = ENOTDIR;
        goto done;
    }

    NameLength = ntfs_mbstoucs(Name, &UnicodeName);
    if (NameLength < 0)
        goto done;
    if (NameLength > NTFS3G_ROS_MAX_NAME_LENGTH) {
        errno = ENAMETOOLONG;
        goto done;
    }

    Inode = ntfs_create(Parent, const_cpu_to_le32(0), UnicodeName,
                        (uint8_t)NameLength, Type);

done:
    Error = errno;
    free(UnicodeName);
    if (Parent)
        ntfs_inode_close(Parent);
    free(ParentPath);
    errno = Error;
    return Inode;
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

static int
Ntfs3gRosWriteFileLocked(NTFS3G_ROS_FILE *File,
                         uint64_t Offset,
                         const void *Buffer,
                         size_t Length,
                         size_t *BytesWritten)
{
    size_t Request = Length > INT64_MAX ? INT64_MAX : Length;
    int64_t Result;

    Result = ntfs_attr_pwrite(File->Data, Offset, Request, Buffer);
    if (Result < 0)
        return -1;
    *BytesWritten = (size_t)Result;
    Ntfs3gRosFillFileInformation(File->Inode, File->Data,
                                 &File->Information);
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

    HostFile = Ntfs3gRosAllocateFile(Inode, Data, Normalized);
    if (!HostFile)
        goto error;

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
    char *Utf8Path = NULL;
    int Error;
    int Result;

    if (!Volume || (!Path && PathLength) || !File || PathLength > INT_MAX) {
        errno = EINVAL;
        return -EINVAL;
    }
    Result = Ntfs3gRosUtf16PathToUtf8(Path, PathLength, &Utf8Path);
    if (Result)
        return Result;

    Result = Ntfs3gRosOpenFile(Volume, Utf8Path, File);
    Error = Result ? errno : 0;
    free(Utf8Path);
    errno = Error;
    return Result;
}

int
Ntfs3gRosCreateDirectoryUtf16(NTFS3G_ROS_VOLUME *Volume,
                              const uint16_t *Path,
                              size_t PathLength)
{
    char *Utf8Path = NULL;
    int Error;
    int Result;

    if (!Volume) {
        errno = EINVAL;
        return -EINVAL;
    }
    Result = Ntfs3gRosUtf16PathToUtf8(Path, PathLength, &Utf8Path);
    if (Result)
        return Result;
    Result = Ntfs3gRosCreateDirectory(Volume, Utf8Path);
    Error = Result ? errno : 0;
    free(Utf8Path);
    errno = Error;
    return Result;
}

int
Ntfs3gRosCreateDirectory(NTFS3G_ROS_VOLUME *Volume,
                         const char *Path)
{
    char *Normalized = NULL;
    ntfs_inode *Inode = NULL;
    int Created = 0;
    int Error;

    if (!Volume || !Path) {
        errno = EINVAL;
        return -EINVAL;
    }
    if (NVolReadOnly(Volume->Native)) {
        errno = EROFS;
        return -EROFS;
    }

    Ntfs3gRosHostAcquire();
    Normalized = Ntfs3gRosNormalizePath(Path);
    if (!Normalized)
        goto done;
    Inode = ntfs_pathname_to_inode(Volume->Native, NULL, Normalized);
    if (Inode) {
        Error = (Inode->mrec->flags & MFT_RECORD_IS_DIRECTORY) ?
            EEXIST : ENOTDIR;
        ntfs_inode_close(Inode);
        Inode = NULL;
        errno = Error;
        goto done;
    }
    if (errno != ENOENT)
        goto done;

    Inode = Ntfs3gRosCreateInode(Volume, Normalized, S_IFDIR);
    if (Inode && ntfs_inode_close(Inode)) {
        Inode = NULL;
        goto done;
    }
    if (Inode)
        Created = 1;

done:
    Error = Created ? 0 : errno;
    free(Normalized);
    Ntfs3gRosHostRelease();
    errno = Error;
    return Error ? -Error : 0;
}

int
Ntfs3gRosCreateFile(NTFS3G_ROS_VOLUME *Volume,
                    const char *Path,
                    int ReplaceExisting,
                    NTFS3G_ROS_FILE **File)
{
    NTFS3G_ROS_FILE *HostFile = NULL;
    char *Normalized = NULL;
    ntfs_inode *Inode = NULL;
    ntfs_attr *Data = NULL;
    int Error;
    int Created = 0;

    if (!Volume || !Path || !File) {
        errno = EINVAL;
        return -EINVAL;
    }
    if (NVolReadOnly(Volume->Native)) {
        errno = EROFS;
        return -EROFS;
    }

    *File = NULL;
    Ntfs3gRosHostAcquire();
    Normalized = Ntfs3gRosNormalizePath(Path);
    if (!Normalized)
        goto error;

    Inode = ntfs_pathname_to_inode(Volume->Native, NULL, Normalized);
    if (Inode) {
        if (!ReplaceExisting) {
            errno = EEXIST;
            goto error;
        }
        if (Inode->mrec->flags & MFT_RECORD_IS_DIRECTORY) {
            errno = EISDIR;
            goto error;
        }
    } else {
        if (errno != ENOENT)
            goto error;
        Inode = Ntfs3gRosCreateInode(Volume, Normalized, S_IFREG);
        if (!Inode)
            goto error;
        Created = 1;
    }

    Data = ntfs_attr_open(Inode, AT_DATA, AT_UNNAMED, 0);
    if (!Data)
        goto error;
    if (ReplaceExisting && ntfs_attr_truncate(Data, 0))
        goto error;
    if (Created && ntfs_inode_sync(Inode))
        goto error;

    HostFile = Ntfs3gRosAllocateFile(Inode, Data, Normalized);
    if (!HostFile)
        goto error;
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
Ntfs3gRosCreateFileUtf16(NTFS3G_ROS_VOLUME *Volume,
                         const uint16_t *Path,
                         size_t PathLength,
                         int ReplaceExisting,
                         NTFS3G_ROS_FILE **File)
{
    char *Utf8Path = NULL;
    int Error;
    int Result;

    if (!Volume || !File) {
        errno = EINVAL;
        return -EINVAL;
    }
    Result = Ntfs3gRosUtf16PathToUtf8(Path, PathLength, &Utf8Path);
    if (Result)
        return Result;
    Result = Ntfs3gRosCreateFile(Volume, Utf8Path, ReplaceExisting, File);
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
Ntfs3gRosFlushFile(NTFS3G_ROS_FILE *File)
{
    int Error = 0;

    if (!File || !File->Inode) {
        errno = EINVAL;
        return -EINVAL;
    }

    Ntfs3gRosHostAcquire();
    if ((NInoDirty(File->Inode) ||
         NInoAttrListDirty(File->Inode)) &&
        ntfs_inode_sync(File->Inode))
        Error = errno;
    Ntfs3gRosHostRelease();
    errno = Error;
    return Error ? -Error : 0;
}

int
Ntfs3gRosCanDeleteFile(NTFS3G_ROS_FILE *File)
{
    int Error = 0;

    if (!File || !File->Inode) {
        errno = EINVAL;
        return -EINVAL;
    }

    Ntfs3gRosHostAcquire();
    if (NVolReadOnly(File->Inode->vol)) {
        Error = EROFS;
    } else if (!File->Path || !strcmp(File->Path, "/")) {
        Error = EBUSY;
    } else if ((File->Inode->mrec->flags & MFT_RECORD_IS_DIRECTORY) &&
               ntfs_check_empty_dir(File->Inode)) {
        Error = errno;
    }
    Ntfs3gRosHostRelease();
    errno = Error;
    return Error ? -Error : 0;
}

int
Ntfs3gRosDeleteFile(NTFS3G_ROS_FILE *File)
{
    const char *Name;
    char *ParentPath = NULL;
    ntfs_inode *Parent = NULL;
    ntfs_inode *Inode;
    ntfs_volume *Volume;
    ntfschar *UnicodeName = NULL;
    int NameLength;
    int Error = 0;
    int Result = -1;

    if (!File || !File->Inode) {
        errno = EINVAL;
        return -EINVAL;
    }

    Ntfs3gRosHostAcquire();
    Inode = File->Inode;
    Volume = Inode->vol;
    if (NVolReadOnly(Volume)) {
        Error = EROFS;
        goto done;
    }
    if (!File->Path || !strcmp(File->Path, "/")) {
        Error = EBUSY;
        goto done;
    }

    ParentPath = Ntfs3gRosGetParentPath(File->Path, &Name);
    if (!ParentPath) {
        Error = errno;
        goto done;
    }
    Parent = ntfs_pathname_to_inode(Volume, NULL, ParentPath);
    if (!Parent) {
        Error = errno;
        goto done;
    }
    if (!(Parent->mrec->flags & MFT_RECORD_IS_DIRECTORY)) {
        Error = ENOTDIR;
        goto done;
    }

    NameLength = ntfs_mbstoucs(Name, &UnicodeName);
    if (NameLength < 0) {
        Error = errno;
        goto done;
    }
    if (NameLength > NTFS3G_ROS_MAX_NAME_LENGTH) {
        Error = ENAMETOOLONG;
        goto done;
    }

    if (File->Data) {
        ntfs_attr_close(File->Data);
        File->Data = NULL;
    }
    File->Inode = NULL;
    Result = ntfs_delete(Volume,
                         File->Path,
                         Inode,
                         Parent,
                         UnicodeName,
                         (uint8_t)NameLength);
    Error = Result ? errno : 0;
    Inode = NULL;
    Parent = NULL;

done:
    if (Parent)
        ntfs_inode_close(Parent);
    if (File->Data)
        ntfs_attr_close(File->Data);
    if (File->Inode)
        ntfs_inode_close(File->Inode);
    free(UnicodeName);
    free(ParentPath);
    free(File->Path);
    free(File);
    Ntfs3gRosHostRelease();
    errno = Error;
    return Error ? -Error : 0;
}

static char *
Ntfs3gRosBuildChildPath(const char *Directory,
                        const char *Name)
{
    char *Path;
    size_t DirectoryLength;
    size_t NameLength;
    int AddSeparator;

    DirectoryLength = strlen(Directory);
    NameLength = strlen(Name);
    AddSeparator =
        DirectoryLength && Directory[DirectoryLength - 1] != '/';
    if (DirectoryLength > SIZE_MAX - NameLength - AddSeparator - 1) {
        errno = ENAMETOOLONG;
        return NULL;
    }

    Path = malloc(DirectoryLength + AddSeparator + NameLength + 1);
    if (!Path)
        return NULL;
    memcpy(Path, Directory, DirectoryLength);
    if (AddSeparator)
        Path[DirectoryLength++] = '/';
    memcpy(Path + DirectoryLength, Name, NameLength + 1);
    return Path;
}

static int
Ntfs3gRosUnlinkPathLocked(ntfs_volume *Volume,
                          const char *Path)
{
    const char *Name;
    char *ParentPath = NULL;
    ntfs_inode *Inode = NULL;
    ntfs_inode *Parent = NULL;
    ntfschar *UnicodeName = NULL;
    int NameLength;
    int Error;
    int Result = -1;

    ParentPath = Ntfs3gRosGetParentPath(Path, &Name);
    if (!ParentPath)
        goto done;
    Inode = ntfs_pathname_to_inode(Volume, NULL, Path);
    if (!Inode)
        goto done;
    Parent = ntfs_pathname_to_inode(Volume, NULL, ParentPath);
    if (!Parent)
        goto done;
    NameLength = ntfs_mbstoucs(Name, &UnicodeName);
    if (NameLength < 0)
        goto done;
    if (NameLength > NTFS3G_ROS_MAX_NAME_LENGTH) {
        errno = ENAMETOOLONG;
        goto done;
    }

    Result = ntfs_delete(Volume,
                         Path,
                         Inode,
                         Parent,
                         UnicodeName,
                         (uint8_t)NameLength);
    Inode = NULL;
    Parent = NULL;

done:
    Error = Result ? errno : 0;
    if (Parent)
        ntfs_inode_close(Parent);
    if (Inode)
        ntfs_inode_close(Inode);
    free(UnicodeName);
    free(ParentPath);
    errno = Error;
    return Result;
}

static int
Ntfs3gRosReopenFileLocked(NTFS3G_ROS_FILE *File,
                          ntfs_volume *Volume,
                          const char *Path)
{
    ntfs_inode *Inode;
    ntfs_attr *Data = NULL;

    Inode = ntfs_pathname_to_inode(Volume, NULL, Path);
    if (!Inode) {
        ntfs_log_error("ReactOS reopen lookup failed for '%s': %s\n",
                       Path, strerror(errno));
        return -1;
    }
    if (!(Inode->mrec->flags & MFT_RECORD_IS_DIRECTORY)) {
        Data = ntfs_attr_open(Inode, AT_DATA, AT_UNNAMED, 0);
        if (!Data) {
            int Error = errno;

            ntfs_log_error(
                "ReactOS reopen data failed for '%s' inode %lld: %s\n",
                Path, (long long)Inode->mft_no, strerror(Error));
            ntfs_inode_close(Inode);
            errno = Error;
            return -1;
        }
    }
    File->Inode = Inode;
    File->Data = Data;
    return 0;
}

static int
Ntfs3gRosReopenFileByReferenceLocked(NTFS3G_ROS_FILE *File,
                                     ntfs_volume *Volume,
                                     MFT_REF Reference,
                                     const char *Path)
{
    ntfs_inode *Inode;
    ntfs_attr *Data = NULL;

    Inode = ntfs_inode_open(Volume, Reference);
    if (!Inode) {
        ntfs_log_error(
            "ReactOS reopen inode failed for '%s' reference %llu: %s\n",
            Path, (long long)Reference, strerror(errno));
        return -1;
    }
    if (!(Inode->mrec->flags & MFT_RECORD_IS_DIRECTORY)) {
        Data = ntfs_attr_open(Inode, AT_DATA, AT_UNNAMED, 0);
        if (!Data) {
            int Error = errno;

            ntfs_log_error(
                "ReactOS reopen data failed for '%s' inode %lld: %s\n",
                Path, (long long)Inode->mft_no, strerror(Error));
            ntfs_inode_close(Inode);
            errno = Error;
            return -1;
        }
    }
    File->Inode = Inode;
    File->Data = Data;
    return 0;
}

int
Ntfs3gRosRenameFileUtf16(NTFS3G_ROS_FILE *File,
                         NTFS3G_ROS_FILE *TargetDirectory,
                         const uint16_t *Name,
                         size_t NameLength,
                         int ReplaceExisting)
{
    ntfs_volume *Volume;
    ntfs_inode *SourceParent = NULL;
    ntfs_inode *SourceInode;
    ntfs_inode *TargetParent = NULL;
    ntfs_inode *Existing = NULL;
    ntfschar *UnicodeSourceName = NULL;
    ntfschar *UnicodeName = NULL;
    char *Utf8Name = NULL;
    char *SourceParentPath = NULL;
    char *TargetPath = NULL;
    const char *SourceName;
    const char *TargetParentPath;
    MFT_REF FileReference;
    MFT_REF TargetParentReference;
    uint64_t ExistingReference;
    size_t Index;
    size_t SourcePathLength;
    int UnicodeNameLength;
    int UnicodeSourceNameLength;
    int SourceIsDirectory;
    int TargetIsDirectory;
    int Error = 0;
    int Result = -1;

    if (!File || !File->Inode || !File->Path ||
        (!Name && NameLength) || !NameLength ||
        NameLength > NTFS3G_ROS_MAX_NAME_LENGTH) {
        errno = EINVAL;
        return -EINVAL;
    }
    for (Index = 0; Index < NameLength; ++Index) {
        if (!Name[Index] || Name[Index] == '/' || Name[Index] == '\\') {
            errno = EINVAL;
            return -EINVAL;
        }
    }
    if ((NameLength == 1 && Name[0] == '.') ||
        (NameLength == 2 && Name[0] == '.' && Name[1] == '.')) {
        errno = EINVAL;
        return -EINVAL;
    }

    Volume = File->Inode->vol;
    FileReference = MK_MREF(
        File->Inode->mft_no,
        le16_to_cpu(File->Inode->mrec->sequence_number));
    if (NVolReadOnly(Volume)) {
        errno = EROFS;
        return -EROFS;
    }
    if (!strcmp(File->Path, "/")) {
        errno = EBUSY;
        return -EBUSY;
    }
    if (TargetDirectory &&
        (!TargetDirectory->Inode ||
         TargetDirectory->Inode->vol != Volume ||
         !(TargetDirectory->Inode->mrec->flags &
           MFT_RECORD_IS_DIRECTORY))) {
        errno = EXDEV;
        return -EXDEV;
    }

    Ntfs3gRosHostAcquire();
    UnicodeNameLength = ntfs_ucstombs(
        (const ntfschar *)Name, (int)NameLength, &Utf8Name, 0);
    if (UnicodeNameLength < 0)
        goto done;
    UnicodeNameLength = ntfs_mbstoucs(Utf8Name, &UnicodeName);
    if (UnicodeNameLength < 0)
        goto done;
    if (!UnicodeNameLength ||
        UnicodeNameLength > NTFS3G_ROS_MAX_NAME_LENGTH) {
        errno = UnicodeNameLength ? ENAMETOOLONG : EINVAL;
        goto done;
    }

    SourceParentPath =
        Ntfs3gRosGetParentPath(File->Path, &SourceName);
    if (!SourceParentPath)
        goto done;
    UnicodeSourceNameLength =
        ntfs_mbstoucs(SourceName, &UnicodeSourceName);
    if (UnicodeSourceNameLength < 0)
        goto done;
    if (!UnicodeSourceNameLength ||
        UnicodeSourceNameLength > NTFS3G_ROS_MAX_NAME_LENGTH) {
        errno = UnicodeSourceNameLength ? ENAMETOOLONG : EINVAL;
        goto done;
    }

    if (TargetDirectory) {
        TargetParentPath = TargetDirectory->Path;
    } else {
        TargetParentPath = SourceParentPath;
    }
    if (!TargetParentPath) {
        errno = EINVAL;
        goto done;
    }
    TargetParent = ntfs_pathname_to_inode(
        Volume, NULL, TargetParentPath);
    if (!TargetParent)
        goto done;
    if (!(TargetParent->mrec->flags & MFT_RECORD_IS_DIRECTORY)) {
        errno = ENOTDIR;
        goto done;
    }

    SourceIsDirectory =
        (File->Inode->mrec->flags & MFT_RECORD_IS_DIRECTORY) != 0;
    SourcePathLength = strlen(File->Path);
    if (SourceIsDirectory &&
        !strncmp(TargetParentPath, File->Path, SourcePathLength) &&
        (TargetParentPath[SourcePathLength] == '\0' ||
         TargetParentPath[SourcePathLength] == '/')) {
        errno = EINVAL;
        goto done;
    }

    TargetPath = Ntfs3gRosBuildChildPath(TargetParentPath, Utf8Name);
    if (!TargetPath)
        goto done;
    if (!strcmp(TargetPath, File->Path)) {
        Result = 0;
        goto done;
    }

    ExistingReference = ntfs_inode_lookup_by_name(
        TargetParent, UnicodeName, UnicodeNameLength);
    if (ExistingReference != (uint64_t)-1) {
        Existing = ntfs_inode_open(
            Volume, MREF(ExistingReference));
        if (!Existing)
            goto done;
        if (Existing->mft_no == File->Inode->mft_no) {
            errno = EEXIST;
            goto done;
        }
        if (!ReplaceExisting) {
            errno = EEXIST;
            goto done;
        }
        TargetIsDirectory =
            (Existing->mrec->flags & MFT_RECORD_IS_DIRECTORY) != 0;
        if (SourceIsDirectory != TargetIsDirectory) {
            errno = SourceIsDirectory ? ENOTDIR : EISDIR;
            goto done;
        }
        ntfs_inode_close(Existing);
        Existing = NULL;
        ntfs_inode_close(TargetParent);
        TargetParent = NULL;
        if (Ntfs3gRosUnlinkPathLocked(Volume, TargetPath))
            goto done;
        TargetParent = ntfs_pathname_to_inode(
            Volume, NULL, TargetParentPath);
        if (!TargetParent)
            goto done;
    } else if (errno != ENOENT) {
        goto done;
    }

    if (ntfs_link(File->Inode,
                  TargetParent,
                  UnicodeName,
                  (uint8_t)UnicodeNameLength)) {
        goto done;
    }
    /*
     * Make the destination index durable before the source inode is synced.
     * Keep the canonical source inode, however: reopening the old pathname
     * here can retrieve an older cached inode view whose link count does not
     * include the new name.
     */
    TargetParentReference = MK_MREF(
        TargetParent->mft_no,
        le16_to_cpu(TargetParent->mrec->sequence_number));
    ntfs_inode_invalidate(Volume, TargetParentReference);
    if (ntfs_inode_close(TargetParent))
        goto done;
    TargetParent = NULL;
    SourceParent = ntfs_pathname_to_inode(
        Volume, NULL, SourceParentPath);
    if (!SourceParent)
        goto done;
    if (File->Data) {
        ntfs_attr_close(File->Data);
        File->Data = NULL;
    }
    ntfs_inode_invalidate(Volume, FileReference);
    SourceInode = File->Inode;
    File->Inode = NULL;
    if (ntfs_delete(Volume,
                    File->Path,
                    SourceInode,
                    SourceParent,
                    UnicodeSourceName,
                    (uint8_t)UnicodeSourceNameLength)) {
        SourceParent = NULL;
        Error = errno;
        if (Ntfs3gRosReopenFileByReferenceLocked(
                File, Volume, FileReference, File->Path) &&
            Ntfs3gRosReopenFileLocked(
                File, Volume, TargetPath)) {
            Error = EIO;
        }
        errno = Error;
        goto done;
    }
    SourceParent = NULL;
    if (Ntfs3gRosReopenFileByReferenceLocked(
            File, Volume, FileReference, TargetPath)) {
        Result = -1;
        goto done;
    }

    free(File->Path);
    File->Path = TargetPath;
    TargetPath = NULL;
    File->Name = strrchr(File->Path, '/');
    File->Name = File->Name ? File->Name + 1 : File->Path;
    Ntfs3gRosFillFileInformation(File->Inode,
                                 File->Data,
                                 &File->Information);
    Result = 0;

done:
    Error = Result ? errno : 0;
    if (Existing)
        ntfs_inode_close(Existing);
    if (SourceParent)
        ntfs_inode_close(SourceParent);
    if (TargetParent)
        ntfs_inode_close(TargetParent);
    free(TargetPath);
    free(SourceParentPath);
    free(UnicodeSourceName);
    free(UnicodeName);
    free(Utf8Name);
    Ntfs3gRosHostRelease();
    errno = Error;
    return Error ? -Error : 0;
}

void
Ntfs3gRosTrimFile(NTFS3G_ROS_FILE *File)
{
    if (!File || !File->Data)
        return;

    Ntfs3gRosHostAcquire();
    if (NAttrNonResident(File->Data) && File->Data->rl &&
        !NAttrRunlistDirty(File->Data)) {
        free(File->Data->rl);
        File->Data->rl = NULL;
        File->Data->unused_runs = 0;
        NAttrClearFullyMapped(File->Data);
    }
    Ntfs3gRosHostRelease();
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
Ntfs3gRosWriteFile(NTFS3G_ROS_FILE *File,
                   const void *Buffer,
                   size_t Length,
                   size_t *BytesWritten)
{
    int Result;
    int Error;

    if (!File || !File->Data || (!Buffer && Length) || !BytesWritten ||
        File->Position > INT64_MAX) {
        errno = !File || !BytesWritten || File->Position > INT64_MAX ?
            EINVAL : EISDIR;
        return -errno;
    }
    if (NVolReadOnly(File->Inode->vol)) {
        errno = EROFS;
        return -EROFS;
    }

    *BytesWritten = 0;
    Ntfs3gRosHostAcquire();
    Result = Ntfs3gRosWriteFileLocked(File, File->Position, Buffer,
                                      Length, BytesWritten);
    Error = Result ? errno : 0;
    if (!Result)
        File->Position += *BytesWritten;
    Ntfs3gRosHostRelease();
    errno = Error;
    return Result ? -Error : 0;
}

int
Ntfs3gRosWriteFileAt(NTFS3G_ROS_FILE *File,
                     uint64_t Offset,
                     const void *Buffer,
                     size_t Length,
                     size_t *BytesWritten)
{
    int Result;
    int Error;

    if (!File || !File->Data || (!Buffer && Length) || !BytesWritten ||
        Offset > INT64_MAX) {
        errno = !File || !BytesWritten || Offset > INT64_MAX ?
            EINVAL : EISDIR;
        return -errno;
    }
    if (NVolReadOnly(File->Inode->vol)) {
        errno = EROFS;
        return -EROFS;
    }

    *BytesWritten = 0;
    Ntfs3gRosHostAcquire();
    Result = Ntfs3gRosWriteFileLocked(File, Offset, Buffer, Length,
                                      BytesWritten);
    Error = Result ? errno : 0;
    Ntfs3gRosHostRelease();
    errno = Error;
    return Result ? -Error : 0;
}

int
Ntfs3gRosSetFileSize(NTFS3G_ROS_FILE *File,
                     uint64_t Size)
{
    int Result;
    int Error;

    if (!File || !File->Data || Size > INT64_MAX) {
        errno = !File || Size > INT64_MAX ? EINVAL : EISDIR;
        return -errno;
    }
    if (NVolReadOnly(File->Inode->vol)) {
        errno = EROFS;
        return -EROFS;
    }

    Ntfs3gRosHostAcquire();
    Result = ntfs_attr_truncate_solid(File->Data, (int64_t)Size);
    Error = Result ? errno : 0;
    if (!Result) {
        Ntfs3gRosFillFileInformation(File->Inode, File->Data,
                                     &File->Information);
        if (File->Position > Size)
            File->Position = Size;
    }
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
Ntfs3gRosSetBasicInformation(
    NTFS3G_ROS_FILE *File,
    const NTFS3G_ROS_BASIC_INFORMATION *Information)
{
    const uint32_t ValidMask =
        NTFS3G_ROS_BASIC_CREATION_TIME |
        NTFS3G_ROS_BASIC_LAST_ACCESS_TIME |
        NTFS3G_ROS_BASIC_LAST_WRITE_TIME |
        NTFS3G_ROS_BASIC_CHANGE_TIME |
        NTFS3G_ROS_BASIC_ATTRIBUTES;
    const uint32_t AttributeMask =
        NTFS3G_ROS_FILE_READONLY |
        NTFS3G_ROS_FILE_HIDDEN |
        NTFS3G_ROS_FILE_SYSTEM |
        NTFS3G_ROS_FILE_ARCHIVE |
        NTFS3G_ROS_FILE_TEMPORARY |
        NTFS3G_ROS_FILE_OFFLINE |
        NTFS3G_ROS_FILE_NOT_CONTENT_INDEXED;
    ntfs_inode *Inode;
    uint32_t Attributes;
    int Error = 0;

    if (!File || !Information ||
        (Information->ValidFields & ~ValidMask)) {
        errno = EINVAL;
        return -EINVAL;
    }
    if (!Information->ValidFields) {
        errno = 0;
        return 0;
    }

    Ntfs3gRosHostAcquire();
    Inode = File->Inode;
    if (!Inode) {
        Error = EINVAL;
        goto done;
    }
    if (NVolReadOnly(Inode->vol)) {
        Error = EROFS;
        goto done;
    }

    if (Information->ValidFields & NTFS3G_ROS_BASIC_CREATION_TIME)
        Inode->creation_time =
            cpu_to_sle64(Information->CreationTime);
    if (Information->ValidFields & NTFS3G_ROS_BASIC_LAST_ACCESS_TIME)
        Inode->last_access_time =
            cpu_to_sle64(Information->LastAccessTime);
    if (Information->ValidFields & NTFS3G_ROS_BASIC_LAST_WRITE_TIME)
        Inode->last_data_change_time =
            cpu_to_sle64(Information->LastWriteTime);
    if (Information->ValidFields & NTFS3G_ROS_BASIC_CHANGE_TIME) {
        Inode->last_mft_change_time =
            cpu_to_sle64(Information->ChangeTime);
    } else {
        Inode->last_mft_change_time = ntfs_current_time();
    }

    if (Information->ValidFields & NTFS3G_ROS_BASIC_ATTRIBUTES) {
        Attributes = le32_to_cpu(Inode->flags);
        Attributes &= ~AttributeMask;
        Attributes |= Information->Attributes & AttributeMask;
        Inode->flags = cpu_to_le32(Attributes);
    }

    /*
     * Keep $STANDARD_INFORMATION, every FILE_NAME attribute, and the parent
     * directory index in one coherent state.  Clearing TimesSet makes the
     * normal inode synchronizer copy the exact NTFS timestamps above instead
     * of retaining an earlier explicit-time snapshot.  Path lookup can have
     * left a redundant, clean view of this inode in the NTFS-3G nidata cache
     * while the shared ReactOS FCB kept the canonical inode open.  Drop that
     * view before publishing the metadata change, or a close/reopen can
     * resurrect the old attributes and timestamps.
     */
    ntfs_inode_invalidate(
        Inode->vol,
        MK_MREF(Inode->mft_no,
                le16_to_cpu(Inode->mrec->sequence_number)));
    clear_nino_flag(Inode, TimesSet);
    NInoFileNameSetDirty(Inode);
    ntfs_inode_mark_dirty(Inode);
    if (ntfs_inode_sync(Inode)) {
        Error = errno ? errno : EIO;
        goto done;
    }
    Ntfs3gRosFillFileInformation(
        Inode, File->Data, &File->Information);

done:
    Ntfs3gRosHostRelease();
    errno = Error;
    return Error ? -Error : 0;
}

int
Ntfs3gRosGetExtendedAttributes(
    NTFS3G_ROS_FILE *File,
    void *Buffer,
    size_t BufferLength,
    size_t *AttributeLength)
{
    int Result;
    int Error;

    if (!File || !File->Inode || !AttributeLength ||
        (!Buffer && BufferLength)) {
        errno = EINVAL;
        return -EINVAL;
    }

    Ntfs3gRosHostAcquire();
    Result = ntfs_get_ntfs_ea(File->Inode, Buffer, BufferLength);
    Error = Result < 0 ? -Result : 0;
    Ntfs3gRosHostRelease();
    if (Result < 0) {
        errno = Error;
        return -Error;
    }

    *AttributeLength = (size_t)Result;
    if (Buffer && (size_t)Result > BufferLength) {
        errno = ERANGE;
        return -ERANGE;
    }
    errno = 0;
    return 0;
}

int
Ntfs3gRosSetExtendedAttributes(
    NTFS3G_ROS_FILE *File,
    const void *Buffer,
    size_t BufferLength)
{
    int Result;
    int Error;

    if (!File || !File->Inode || !Buffer || !BufferLength) {
        errno = EINVAL;
        return -EINVAL;
    }

    Ntfs3gRosHostAcquire();
    if (NVolReadOnly(File->Inode->vol)) {
        Error = EROFS;
        Result = -Error;
    } else {
        Result = ntfs_set_ntfs_ea(
            File->Inode, (const char *)Buffer, BufferLength, 0);
        Error = Result < 0 ? -Result : 0;
        if (!Result && ntfs_inode_sync(File->Inode)) {
            Error = errno ? errno : EIO;
            Result = -Error;
        }
    }
    Ntfs3gRosHostRelease();
    errno = Error;
    return Result < 0 ? -Error : 0;
}

int
Ntfs3gRosRemoveExtendedAttributes(NTFS3G_ROS_FILE *File)
{
    int Result;
    int Error;

    if (!File || !File->Inode) {
        errno = EINVAL;
        return -EINVAL;
    }

    Ntfs3gRosHostAcquire();
    if (NVolReadOnly(File->Inode->vol)) {
        Error = EROFS;
        Result = -Error;
    } else {
        Result = ntfs_remove_ntfs_ea(File->Inode);
        Error = Result < 0 ? errno : 0;
        if (!Result && ntfs_inode_sync(File->Inode)) {
            Error = errno ? errno : EIO;
            Result = -1;
        }
    }
    Ntfs3gRosHostRelease();
    errno = Error;
    return Result < 0 ? -Error : 0;
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
