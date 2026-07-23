/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     NTFS-3G file-system driver context blocks
 * COPYRIGHT:   Copyright 2024 Carl Bialorucki <carl.bialorucki@reactos.org>
 *              Copyright 2024 Justin Miller <justin.miller@reactos.org>
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

typedef struct _VolumeContextBlock
{
    PNTFS3G_ROS_KM_VOLUME Volume;
    PDEVICE_OBJECT StorageDevice;
    PFILE_OBJECT StreamFileObject;
} VolumeContextBlock, *PVolumeContextBlock;

typedef struct _FileContextBlock
{
    FSRTL_ADVANCED_FCB_HEADER CommonFCBHeader;
    SECTION_OBJECT_POINTERS SectionObjectPointers;
    FILE_LOCK FileLock;
    ERESOURCE MainResource;
    ERESOURCE PagingIoResource;
    FAST_MUTEX HeaderMutex;
    NTFS3G_ROS_FILE *File;
    NTFS3G_ROS_FILE_INFORMATION Information;
    UNICODE_STRING FileName;
    UNICODE_STRING DirectoryPattern;
    BOOLEAN DirectoryQueryStarted;
} FileContextBlock, *PFileContextBlock;
