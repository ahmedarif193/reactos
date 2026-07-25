/*
 * PROJECT:     ReactOS NTFS-3G Library
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Shared NTFS-3G interface for ReactOS hosts
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _NTFS3G_ROS_VOLUME NTFS3G_ROS_VOLUME;
typedef struct _NTFS3G_ROS_FILE NTFS3G_ROS_FILE;

typedef struct _NTFS3G_ROS_FILE_INFORMATION
{
    uint64_t FileId;
    uint64_t CreationTime;
    uint64_t LastAccessTime;
    uint64_t LastWriteTime;
    uint64_t ChangeTime;
    uint64_t AllocationSize;
    uint64_t FileSize;
    uint32_t Attributes;
    uint32_t LinkCount;
} NTFS3G_ROS_FILE_INFORMATION;

typedef struct _NTFS3G_ROS_BASIC_INFORMATION
{
    uint64_t CreationTime;
    uint64_t LastAccessTime;
    uint64_t LastWriteTime;
    uint64_t ChangeTime;
    uint32_t Attributes;
    uint32_t ValidFields;
} NTFS3G_ROS_BASIC_INFORMATION;

#define NTFS3G_ROS_BASIC_CREATION_TIME   0x00000001
#define NTFS3G_ROS_BASIC_LAST_ACCESS_TIME 0x00000002
#define NTFS3G_ROS_BASIC_LAST_WRITE_TIME 0x00000004
#define NTFS3G_ROS_BASIC_CHANGE_TIME     0x00000008
#define NTFS3G_ROS_BASIC_ATTRIBUTES      0x00000010

#define NTFS3G_ROS_MAX_NAME_LENGTH 255

typedef struct _NTFS3G_ROS_DIRECTORY_ENTRY
{
    NTFS3G_ROS_FILE_INFORMATION Information;
    uint16_t FileName[NTFS3G_ROS_MAX_NAME_LENGTH + 1];
    uint16_t FileNameLength;
} NTFS3G_ROS_DIRECTORY_ENTRY;

typedef struct _NTFS3G_ROS_DEVICE_OPERATIONS
{
    int (*Read)(void *Context,
                uint64_t Offset,
                void *Buffer,
                uint32_t Length,
                uint32_t *BytesRead);
    void (*Close)(void *Context);
    int (*Write)(void *Context,
                 uint64_t Offset,
                 const void *Buffer,
                 uint32_t Length,
                 uint32_t *BytesWritten);
    int (*Sync)(void *Context);
} NTFS3G_ROS_DEVICE_OPERATIONS;

#define NTFS3G_ROS_FILE_READONLY  0x00000001
#define NTFS3G_ROS_FILE_HIDDEN    0x00000002
#define NTFS3G_ROS_FILE_SYSTEM    0x00000004
#define NTFS3G_ROS_FILE_DIRECTORY 0x00000010
#define NTFS3G_ROS_FILE_ARCHIVE   0x00000020
#define NTFS3G_ROS_FILE_TEMPORARY 0x00000100
#define NTFS3G_ROS_FILE_OFFLINE   0x00001000
#define NTFS3G_ROS_FILE_NOT_CONTENT_INDEXED 0x00002000

#define NTFS3G_ROS_SEEK_SET 0
#define NTFS3G_ROS_SEEK_CUR 1
#define NTFS3G_ROS_SEEK_END 2

int
Ntfs3gRosMount(void *DeviceContext,
               const NTFS3G_ROS_DEVICE_OPERATIONS *Operations,
               uint64_t DeviceLength,
               uint32_t SectorSize,
               NTFS3G_ROS_VOLUME **Volume);

int
Ntfs3gRosUnmount(NTFS3G_ROS_VOLUME *Volume);

int
Ntfs3gRosFlushVolume(NTFS3G_ROS_VOLUME *Volume);

int
Ntfs3gRosOpenFile(NTFS3G_ROS_VOLUME *Volume,
                  const char *Path,
                  NTFS3G_ROS_FILE **File);

int
Ntfs3gRosOpenFileById(NTFS3G_ROS_VOLUME *Volume,
                      uint64_t FileId,
                      const char *Path,
                      NTFS3G_ROS_FILE **File);

int
Ntfs3gRosOpenFileByIdUtf16(NTFS3G_ROS_VOLUME *Volume,
                           uint64_t FileId,
                           const uint16_t *Path,
                           size_t PathLength,
                           NTFS3G_ROS_FILE **File);

int
Ntfs3gRosOpenFileUtf16(NTFS3G_ROS_VOLUME *Volume,
                       const uint16_t *Path,
                       size_t PathLength,
                       NTFS3G_ROS_FILE **File);

int
Ntfs3gRosCreateDirectory(NTFS3G_ROS_VOLUME *Volume,
                         const char *Path);

int
Ntfs3gRosCreateDirectoryUtf16(NTFS3G_ROS_VOLUME *Volume,
                              const uint16_t *Path,
                              size_t PathLength);

int
Ntfs3gRosCreateFile(NTFS3G_ROS_VOLUME *Volume,
                    const char *Path,
                    int ReplaceExisting,
                    NTFS3G_ROS_FILE **File);

int
Ntfs3gRosCreateFileUtf16(NTFS3G_ROS_VOLUME *Volume,
                         const uint16_t *Path,
                         size_t PathLength,
                         int ReplaceExisting,
                         NTFS3G_ROS_FILE **File);

int
Ntfs3gRosCloseFile(NTFS3G_ROS_FILE *File);

int
Ntfs3gRosFlushFile(NTFS3G_ROS_FILE *File);

int
Ntfs3gRosCanDeleteFile(NTFS3G_ROS_FILE *File);

/*
 * Delete the directory entry and consume File whether deletion succeeds or
 * fails.  Call this only after the final host reference has gone away.
 */
int
Ntfs3gRosDeleteFile(NTFS3G_ROS_FILE *File);

int
Ntfs3gRosRenameFileUtf16(NTFS3G_ROS_FILE *File,
                         NTFS3G_ROS_FILE *TargetDirectory,
                         const uint16_t *Name,
                         size_t NameLength,
                         int ReplaceExisting);

void
Ntfs3gRosTrimFile(NTFS3G_ROS_FILE *File);

int
Ntfs3gRosReadFile(NTFS3G_ROS_FILE *File,
                  void *Buffer,
                  size_t Length,
                  size_t *BytesRead);

int
Ntfs3gRosReadFileAt(NTFS3G_ROS_FILE *File,
                    uint64_t Offset,
                    void *Buffer,
                    size_t Length,
                    size_t *BytesRead);

int
Ntfs3gRosWriteFile(NTFS3G_ROS_FILE *File,
                   const void *Buffer,
                   size_t Length,
                   size_t *BytesWritten);

int
Ntfs3gRosWriteFileAt(NTFS3G_ROS_FILE *File,
                     uint64_t Offset,
                     const void *Buffer,
                     size_t Length,
                     size_t *BytesWritten);

int
Ntfs3gRosSetFileSize(NTFS3G_ROS_FILE *File,
                     uint64_t Size);

int
Ntfs3gRosSeekFile(NTFS3G_ROS_FILE *File,
                  int64_t Offset,
                  int Origin);

uint64_t
Ntfs3gRosGetFileSize(const NTFS3G_ROS_FILE *File);

uint64_t
Ntfs3gRosGetFilePosition(const NTFS3G_ROS_FILE *File);

uint32_t
Ntfs3gRosGetFileAttributes(const NTFS3G_ROS_FILE *File);

const char *
Ntfs3gRosGetFileName(const NTFS3G_ROS_FILE *File);

int
Ntfs3gRosGetFileInformation(const NTFS3G_ROS_FILE *File,
                            NTFS3G_ROS_FILE_INFORMATION *Information);

int
Ntfs3gRosSetBasicInformation(
    NTFS3G_ROS_FILE *File,
    const NTFS3G_ROS_BASIC_INFORMATION *Information);

int
Ntfs3gRosGetExtendedAttributes(
    NTFS3G_ROS_FILE *File,
    void *Buffer,
    size_t BufferLength,
    size_t *AttributeLength);

int
Ntfs3gRosSetExtendedAttributes(
    NTFS3G_ROS_FILE *File,
    const void *Buffer,
    size_t BufferLength);

int
Ntfs3gRosRemoveExtendedAttributes(NTFS3G_ROS_FILE *File);

int
Ntfs3gRosRestartDirectory(NTFS3G_ROS_FILE *File);

int
Ntfs3gRosReadDirectory(NTFS3G_ROS_FILE *File,
                       NTFS3G_ROS_DIRECTORY_ENTRY *Entry);

uint64_t
Ntfs3gRosGetDirectoryPosition(const NTFS3G_ROS_FILE *File);

int
Ntfs3gRosSetDirectoryPosition(NTFS3G_ROS_FILE *File,
                             uint64_t Position);

uint64_t
Ntfs3gRosGetVolumeSize(const NTFS3G_ROS_VOLUME *Volume);

uint32_t
Ntfs3gRosGetBytesPerSector(const NTFS3G_ROS_VOLUME *Volume);

uint32_t
Ntfs3gRosGetSectorsPerCluster(const NTFS3G_ROS_VOLUME *Volume);

uint64_t
Ntfs3gRosGetClusterCount(const NTFS3G_ROS_VOLUME *Volume);

uint64_t
Ntfs3gRosGetFreeClusterCount(const NTFS3G_ROS_VOLUME *Volume);

uint64_t
Ntfs3gRosGetVolumeSerialNumber(const NTFS3G_ROS_VOLUME *Volume);

int
Ntfs3gRosReadVolumeSerialNumber(NTFS3G_ROS_VOLUME *Volume,
                                uint64_t *SerialNumber);

const char *
Ntfs3gRosGetVolumeName(const NTFS3G_ROS_VOLUME *Volume);

int
Ntfs3gRosGetVolumeNameUtf16(const NTFS3G_ROS_VOLUME *Volume,
                            uint16_t *Buffer,
                            size_t BufferLength,
                            size_t *NameLength);

int
Ntfs3gRosSetVolumeNameUtf16(NTFS3G_ROS_VOLUME *Volume,
                            const uint16_t *Name,
                            size_t NameLength);

uint8_t
Ntfs3gRosGetMajorVersion(const NTFS3G_ROS_VOLUME *Volume);

uint8_t
Ntfs3gRosGetMinorVersion(const NTFS3G_ROS_VOLUME *Volume);

int
Ntfs3gRosIsReadOnly(const NTFS3G_ROS_VOLUME *Volume);

void *
Ntfs3gRosGetNativeVolume(NTFS3G_ROS_VOLUME *Volume);

/* User-mode convenience wrapper. */
int
Ntfs3gRosMountHandle(void *Handle,
                     int ReadOnly,
                     NTFS3G_ROS_VOLUME **Volume);

int
Ntfs3gRosMountPath(const char *Path,
                   int ReadOnly,
                   NTFS3G_ROS_VOLUME **Volume);

#ifdef __cplusplus
}
#endif
