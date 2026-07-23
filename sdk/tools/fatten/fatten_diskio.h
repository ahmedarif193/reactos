#pragma once

#include <ff.h>
#include <diskio.h>

DSTATUS disk_openimage(BYTE PhysicalDrive, const char* ImageFileName);
DRESULT disk_cleanup(BYTE PhysicalDrive);
DRESULT disk_set_sector_count(BYTE PhysicalDrive, LBA_t SectorCount, int AlignToTrack);
DRESULT disk_prepare_vhd(BYTE PhysicalDrive, LBA_t SectorCount);
DRESULT disk_set_partition_type(BYTE PhysicalDrive, BYTE PartitionType);
