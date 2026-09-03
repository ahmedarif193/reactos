/*
 * PROJECT:     ReactOS storage expansion tool
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Grow the last MBR partition and its NTFS volume to the end of the disk
 * COPYRIGHT:   Copyright 2026 Ahmed Arif
 */

#ifndef _ROSEXPANDSTORAGE_CORE_H_
#define _ROSEXPANDSTORAGE_CORE_H_

#include <stdint.h>

typedef struct _EXPAND_DEVICE EXPAND_DEVICE;

struct _EXPAND_DEVICE
{
    void* Context;
    int (*Read)(void* Context, uint64_t Offset, uint32_t Length, void* Buffer);
    int (*Write)(void* Context, uint64_t Offset, uint32_t Length, const void* Buffer);
    void (*Log)(void* Context, const char* Line);
    uint64_t DiskSize;
    uint32_t SectorSize;
};

#define EXPAND_OK               0
#define EXPAND_IO_ERROR         1
#define EXPAND_NO_MBR           2
#define EXPAND_GPT_UNSUPPORTED  3
#define EXPAND_NOT_LAST         4
#define EXPAND_NOTHING_TO_DO    5
#define EXPAND_NOT_NTFS         6
#define EXPAND_CORRUPT          7
#define EXPAND_NO_ROOM          8
#define EXPAND_BAD_PARAMETER    9
#define EXPAND_TOO_LARGE        10

typedef struct _EXPAND_PLAN
{
    uint32_t PartitionSlot;
    uint32_t PartitionNumber;
    uint32_t PartitionType;
    uint64_t PartitionStart;
    uint64_t OldPartitionBytes;
    uint64_t NewPartitionBytes;
    uint64_t PartitionGain;
    uint64_t VolumeGain;
    uint64_t OldClusters;
    uint64_t NewClusters;
    uint64_t OldVolumeSectors;
    uint64_t NewVolumeSectors;
    uint32_t BytesPerSector;
    uint32_t ClusterSize;
    uint32_t BitmapExtraClusters;
    int RewritePartitionTable;
    int RewriteVolume;
} EXPAND_PLAN;

int ExpandBuildPlan(EXPAND_DEVICE* Device,
                    uint32_t PartitionNumber,
                    uint64_t PadBytes,
                    uint64_t MinimumGain,
                    EXPAND_PLAN* Plan);

int ExpandApplyPlan(EXPAND_DEVICE* Device, const EXPAND_PLAN* Plan);

const char* ExpandStatusText(int Status);

#endif /* _ROSEXPANDSTORAGE_CORE_H_ */
