/*
 * PROJECT:     ReactOS FAT/exFAT Image Creator
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     FatFs media-access adapter for host image files
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>

#ifdef _WIN32
#include <io.h>
typedef __int64 HOST_FILE_OFFSET;
#define host_fseek _fseeki64
#define host_ftell _ftelli64
#else
#include <unistd.h>
typedef off_t HOST_FILE_OFFSET;
#define host_fseek fseeko
#define host_ftell ftello
#endif

#include <ff.h>
#include <diskio.h>
#include "fatten_diskio.h"

#define IMAGE_DRIVE_COUNT 1
#define IMAGE_SECTOR_SIZE 512
#define FAT_SECTORS_PER_TRACK 63
#define VHD_FOOTER_SIZE 512
#define VHD_PARTITION_START 2048
#define VHD_TIMESTAMP_EPOCH 946684800ULL
#define MBR_PARTITION_TABLE 446

typedef struct _IMAGE_DRIVE
{
    FILE* Handle;
    LBA_t SectorCount;
    int FixedVhd;
    int FooterValid;
    BYTE Footer[VHD_FOOTER_SIZE];
} IMAGE_DRIVE;

static IMAGE_DRIVE Drives[IMAGE_DRIVE_COUNT];

#if FF_MULTI_PARTITION
PARTITION VolToPart[FF_VOLUMES] = { { 0, 0 } };
#endif

void* ff_memalloc(UINT Size)
{
    return malloc(Size);
}

void ff_memfree(void* Buffer)
{
    free(Buffer);
}

static int seek_to_offset(IMAGE_DRIVE* Drive, LBA_t Offset)
{
    const LBA_t MaxOffset = ((LBA_t)1 << (sizeof(HOST_FILE_OFFSET) * 8 - 1)) - 1;

    if (Offset > MaxOffset)
        return -1;

    return host_fseek(Drive->Handle, (HOST_FILE_OFFSET)Offset, SEEK_SET);
}

static int seek_to_sector(IMAGE_DRIVE* Drive, LBA_t Sector)
{
    const LBA_t MaxOffset = ((LBA_t)1 << (sizeof(HOST_FILE_OFFSET) * 8 - 1)) - 1;

    if (Sector > MaxOffset / IMAGE_SECTOR_SIZE)
        return -1;

    return seek_to_offset(Drive, Sector * IMAGE_SECTOR_SIZE);
}

static int resize_image(IMAGE_DRIVE* Drive, LBA_t Size)
{
    const LBA_t MaxOffset = ((LBA_t)1 << (sizeof(HOST_FILE_OFFSET) * 8 - 1)) - 1;

    if (Size > MaxOffset || fflush(Drive->Handle) != 0)
        return -1;

#ifdef _WIN32
    return _chsize_s(_fileno(Drive->Handle), (HOST_FILE_OFFSET)Size) == 0 ? 0 : -1;
#else
    return ftruncate(fileno(Drive->Handle), (HOST_FILE_OFFSET)Size);
#endif
}

static DWORD read_be32(const BYTE* Buffer)
{
    return ((DWORD)Buffer[0] << 24) | ((DWORD)Buffer[1] << 16) |
           ((DWORD)Buffer[2] << 8) | Buffer[3];
}

static QWORD read_be64(const BYTE* Buffer)
{
    return ((QWORD)read_be32(Buffer) << 32) | read_be32(Buffer + 4);
}

static void write_be16(BYTE* Buffer, WORD Value)
{
    Buffer[0] = (BYTE)(Value >> 8);
    Buffer[1] = (BYTE)Value;
}

static void write_be32(BYTE* Buffer, DWORD Value)
{
    Buffer[0] = (BYTE)(Value >> 24);
    Buffer[1] = (BYTE)(Value >> 16);
    Buffer[2] = (BYTE)(Value >> 8);
    Buffer[3] = (BYTE)Value;
}

static void write_be64(BYTE* Buffer, QWORD Value)
{
    write_be32(Buffer, (DWORD)(Value >> 32));
    write_be32(Buffer + 4, (DWORD)Value);
}

static DWORD vhd_checksum(BYTE* Footer)
{
    DWORD Sum = 0;
    DWORD StoredChecksum = read_be32(Footer + 64);
    UINT Index;

    memset(Footer + 64, 0, sizeof(DWORD));
    for (Index = 0; Index < VHD_FOOTER_SIZE; Index++)
        Sum += Footer[Index];
    write_be32(Footer + 64, StoredChecksum);
    return ~Sum;
}

static DWORD vhd_timestamp(void)
{
    const char* SourceDateEpoch = getenv("SOURCE_DATE_EPOCH");
    unsigned long long Seconds = (unsigned long long)time(NULL);

    if (SourceDateEpoch && *SourceDateEpoch)
    {
        char* End;
        unsigned long long Parsed = strtoull(SourceDateEpoch, &End, 10);

        if (End != SourceDateEpoch && *End == '\0')
            Seconds = Parsed;
    }

    return Seconds > VHD_TIMESTAMP_EPOCH ? (DWORD)(Seconds - VHD_TIMESTAMP_EPOCH) : 0;
}

static QWORD mix_uuid_seed(QWORD Value)
{
    Value += 0x9E3779B97F4A7C15ULL;
    Value = (Value ^ (Value >> 30)) * 0xBF58476D1CE4E5B9ULL;
    Value = (Value ^ (Value >> 27)) * 0x94D049BB133111EBULL;
    return Value ^ (Value >> 31);
}

static void calculate_vhd_geometry(LBA_t SectorCount, WORD* Cylinders, BYTE* Heads, BYTE* SectorsPerTrack)
{
    LBA_t TotalSectors = SectorCount;
    LBA_t CylinderTimesHeads;

    if (TotalSectors > 65535ULL * 16 * 255)
        TotalSectors = 65535ULL * 16 * 255;

    if (TotalSectors >= 65535ULL * 16 * 63)
    {
        *SectorsPerTrack = 255;
        *Heads = 16;
        CylinderTimesHeads = TotalSectors / *SectorsPerTrack;
    }
    else
    {
        *SectorsPerTrack = 17;
        CylinderTimesHeads = TotalSectors / *SectorsPerTrack;
        *Heads = (BYTE)((CylinderTimesHeads + 1023) / 1024);
        if (*Heads < 4)
            *Heads = 4;
        if (CylinderTimesHeads >= (LBA_t)*Heads * 1024 || *Heads > 16)
        {
            *SectorsPerTrack = 31;
            *Heads = 16;
            CylinderTimesHeads = TotalSectors / *SectorsPerTrack;
        }
        if (CylinderTimesHeads >= (LBA_t)*Heads * 1024)
        {
            *SectorsPerTrack = 63;
            *Heads = 16;
            CylinderTimesHeads = TotalSectors / *SectorsPerTrack;
        }
    }

    *Cylinders = (WORD)(CylinderTimesHeads / *Heads);
}

static void create_vhd_footer(IMAGE_DRIVE* Drive)
{
    BYTE* Footer = Drive->Footer;
    QWORD CurrentSize = Drive->SectorCount * IMAGE_SECTOR_SIZE;
    QWORD Seed = mix_uuid_seed(CurrentSize ^ vhd_timestamp());
    WORD Cylinders;
    BYTE Heads;
    BYTE SectorsPerTrack;
    UINT Index;

    memset(Footer, 0, VHD_FOOTER_SIZE);
    memcpy(Footer, "conectix", 8);
    write_be32(Footer + 8, 2);
    write_be32(Footer + 12, 0x00010000);
    write_be64(Footer + 16, ~(QWORD)0);
    write_be32(Footer + 24, vhd_timestamp());
    memcpy(Footer + 28, "rOS ", 4);
    write_be32(Footer + 32, 0x00010000);
    memcpy(Footer + 36, "Lnx ", 4);
    write_be64(Footer + 40, CurrentSize);
    write_be64(Footer + 48, CurrentSize);
    calculate_vhd_geometry(Drive->SectorCount, &Cylinders, &Heads, &SectorsPerTrack);
    write_be16(Footer + 56, Cylinders);
    Footer[58] = Heads;
    Footer[59] = SectorsPerTrack;
    write_be32(Footer + 60, 2);
    for (Index = 0; Index < 16; Index++)
    {
        if ((Index & 7) == 0)
            Seed = mix_uuid_seed(Seed);
        Footer[68 + Index] = (BYTE)(Seed >> ((Index & 7) * 8));
    }
    Footer[68 + 6] = (Footer[68 + 6] & 0x0F) | 0x40;
    Footer[68 + 8] = (Footer[68 + 8] & 0x3F) | 0x80;
    write_be32(Footer + 64, vhd_checksum(Footer));
    Drive->FooterValid = 1;
}

static int detect_vhd_footer(IMAGE_DRIVE* Drive)
{
    HOST_FILE_OFFSET FileSize;
    QWORD CurrentSize;
    DWORD StoredChecksum;

    if (host_fseek(Drive->Handle, 0, SEEK_END) != 0 || (FileSize = host_ftell(Drive->Handle)) < VHD_FOOTER_SIZE ||
        host_fseek(Drive->Handle, FileSize - VHD_FOOTER_SIZE, SEEK_SET) != 0 ||
        fread(Drive->Footer, 1, VHD_FOOTER_SIZE, Drive->Handle) != VHD_FOOTER_SIZE ||
        memcmp(Drive->Footer, "conectix", 8) != 0 || read_be32(Drive->Footer + 60) != 2)
    {
        return 0;
    }

    CurrentSize = read_be64(Drive->Footer + 48);
    StoredChecksum = read_be32(Drive->Footer + 64);
    if (CurrentSize % IMAGE_SECTOR_SIZE != 0 || CurrentSize + VHD_FOOTER_SIZE != (QWORD)FileSize ||
        StoredChecksum != vhd_checksum(Drive->Footer))
    {
        return 0;
    }

    Drive->SectorCount = CurrentSize / IMAGE_SECTOR_SIZE;
    Drive->FixedVhd = 1;
    Drive->FooterValid = 1;
    return 1;
}

static void encode_chs(LBA_t Sector, BYTE* Chs)
{
    LBA_t Cylinder = Sector / (255 * 63);
    BYTE Head = (BYTE)((Sector / 63) % 255);
    BYTE SectorNumber = (BYTE)(Sector % 63) + 1;

    if (Cylinder > 1023)
    {
        Chs[0] = 0xFE;
        Chs[1] = 0xFF;
        Chs[2] = 0xFF;
        return;
    }

    Chs[0] = Head;
    Chs[1] = (BYTE)(SectorNumber | ((Cylinder >> 2) & 0xC0));
    Chs[2] = (BYTE)Cylinder;
}

DSTATUS disk_openimage(BYTE PhysicalDrive, const char* ImageFileName)
{
    IMAGE_DRIVE* Drive;

    if (PhysicalDrive >= IMAGE_DRIVE_COUNT)
        return STA_NOINIT;

    Drive = &Drives[PhysicalDrive];
    if (Drive->Handle)
        return 0;

    Drive->Handle = fopen(ImageFileName, "r+b");
    if (!Drive->Handle)
        Drive->Handle = fopen(ImageFileName, "w+b");

    if (Drive->Handle)
        detect_vhd_footer(Drive);

    return Drive->Handle ? 0 : STA_NOINIT;
}

DRESULT disk_cleanup(BYTE PhysicalDrive)
{
    IMAGE_DRIVE* Drive;
    DRESULT Result = RES_OK;

    if (PhysicalDrive >= IMAGE_DRIVE_COUNT)
        return RES_PARERR;

    Drive = &Drives[PhysicalDrive];
    if (Drive->Handle)
    {
        if (Drive->FixedVhd)
        {
            if (!Drive->FooterValid)
                create_vhd_footer(Drive);
            if (seek_to_offset(Drive, Drive->SectorCount * IMAGE_SECTOR_SIZE) != 0 ||
                fwrite(Drive->Footer, 1, VHD_FOOTER_SIZE, Drive->Handle) != VHD_FOOTER_SIZE ||
                resize_image(Drive, Drive->SectorCount * IMAGE_SECTOR_SIZE + VHD_FOOTER_SIZE) != 0)
            {
                Result = RES_ERROR;
            }
        }
        if (fclose(Drive->Handle) != 0)
            Result = RES_ERROR;
        Drive->Handle = NULL;
    }
    Drive->SectorCount = 0;
    Drive->FixedVhd = 0;
    Drive->FooterValid = 0;
    return Result;
}

DRESULT disk_set_sector_count(BYTE PhysicalDrive, LBA_t SectorCount, int AlignToTrack)
{
    IMAGE_DRIVE* Drive;
    LBA_t ReportedCount;

    if (PhysicalDrive >= IMAGE_DRIVE_COUNT || SectorCount == 0)
        return RES_PARERR;

    Drive = &Drives[PhysicalDrive];
    if (!Drive->Handle)
        return RES_NOTRDY;

    ReportedCount = AlignToTrack ?
                        (SectorCount / FAT_SECTORS_PER_TRACK) * FAT_SECTORS_PER_TRACK :
                        SectorCount;
    if (ReportedCount == 0 || SectorCount > ((LBA_t)-1) / IMAGE_SECTOR_SIZE ||
        resize_image(Drive, SectorCount * IMAGE_SECTOR_SIZE) != 0)
        return RES_PARERR;

    Drive->SectorCount = ReportedCount;
    Drive->FixedVhd = 0;
    Drive->FooterValid = 0;
#if FF_MULTI_PARTITION
    VolToPart[0].pt = 0;
#endif
    return RES_OK;
}

DRESULT disk_prepare_vhd(BYTE PhysicalDrive, LBA_t SectorCount)
{
    IMAGE_DRIVE* Drive;
    BYTE Mbr[IMAGE_SECTOR_SIZE];
    BYTE* Partition = Mbr + MBR_PARTITION_TABLE;
    LBA_t PartitionSectors;

    if (PhysicalDrive >= IMAGE_DRIVE_COUNT || SectorCount <= VHD_PARTITION_START ||
        SectorCount > 0xFFFFFFFFULL)
    {
        return RES_PARERR;
    }

    Drive = &Drives[PhysicalDrive];
    if (!Drive->Handle)
        return RES_NOTRDY;
    if (resize_image(Drive, SectorCount * IMAGE_SECTOR_SIZE) != 0)
        return RES_ERROR;

    Drive->SectorCount = SectorCount;
    Drive->FixedVhd = 1;
    Drive->FooterValid = 0;
    PartitionSectors = SectorCount - VHD_PARTITION_START;

    memset(Mbr, 0, sizeof(Mbr));
    Partition[0] = 0x80;
    encode_chs(VHD_PARTITION_START, Partition + 1);
    Partition[4] = 0x07;
    encode_chs(SectorCount - 1, Partition + 5);
    Partition[8] = (BYTE)VHD_PARTITION_START;
    Partition[9] = (BYTE)(VHD_PARTITION_START >> 8);
    Partition[10] = (BYTE)(VHD_PARTITION_START >> 16);
    Partition[11] = (BYTE)(VHD_PARTITION_START >> 24);
    Partition[12] = (BYTE)PartitionSectors;
    Partition[13] = (BYTE)(PartitionSectors >> 8);
    Partition[14] = (BYTE)(PartitionSectors >> 16);
    Partition[15] = (BYTE)(PartitionSectors >> 24);
    Mbr[510] = 0x55;
    Mbr[511] = 0xAA;

#if FF_MULTI_PARTITION
    VolToPart[0].pd = PhysicalDrive;
    VolToPart[0].pt = 1;
#endif
    return disk_write(PhysicalDrive, Mbr, 0, 1);
}

DRESULT disk_set_partition_type(BYTE PhysicalDrive, BYTE PartitionType)
{
    BYTE Mbr[IMAGE_SECTOR_SIZE];

    if (disk_read(PhysicalDrive, Mbr, 0, 1) != RES_OK || Mbr[510] != 0x55 || Mbr[511] != 0xAA)
        return RES_ERROR;

    Mbr[MBR_PARTITION_TABLE + 4] = PartitionType;
    return disk_write(PhysicalDrive, Mbr, 0, 1);
}

DSTATUS disk_initialize(BYTE PhysicalDrive)
{
    return disk_status(PhysicalDrive);
}

DSTATUS disk_status(BYTE PhysicalDrive)
{
    return (PhysicalDrive < IMAGE_DRIVE_COUNT && Drives[PhysicalDrive].Handle) ? 0 : STA_NOINIT;
}

DRESULT disk_read(BYTE PhysicalDrive, BYTE* Buffer, LBA_t Sector, UINT Count)
{
    IMAGE_DRIVE* Drive;

    if (PhysicalDrive >= IMAGE_DRIVE_COUNT || !Buffer || Count == 0)
        return RES_PARERR;

    Drive = &Drives[PhysicalDrive];
    if (!Drive->Handle)
        return RES_NOTRDY;
    if ((Drive->SectorCount && (Sector >= Drive->SectorCount || Count > Drive->SectorCount - Sector)) ||
        seek_to_sector(Drive, Sector) != 0)
    {
        return RES_ERROR;
    }

    return fread(Buffer, IMAGE_SECTOR_SIZE, Count, Drive->Handle) == Count ? RES_OK : RES_ERROR;
}

DRESULT disk_write(BYTE PhysicalDrive, const BYTE* Buffer, LBA_t Sector, UINT Count)
{
    IMAGE_DRIVE* Drive;

    if (PhysicalDrive >= IMAGE_DRIVE_COUNT || !Buffer || Count == 0)
        return RES_PARERR;

    Drive = &Drives[PhysicalDrive];
    if (!Drive->Handle)
        return RES_NOTRDY;
    if ((Drive->SectorCount && (Sector >= Drive->SectorCount || Count > Drive->SectorCount - Sector)) ||
        seek_to_sector(Drive, Sector) != 0)
    {
        return RES_ERROR;
    }

    return fwrite(Buffer, IMAGE_SECTOR_SIZE, Count, Drive->Handle) == Count ? RES_OK : RES_ERROR;
}

DRESULT disk_ioctl(BYTE PhysicalDrive, BYTE Command, void* Buffer)
{
    IMAGE_DRIVE* Drive;
    HOST_FILE_OFFSET Size;

    if (PhysicalDrive >= IMAGE_DRIVE_COUNT)
        return RES_PARERR;

    Drive = &Drives[PhysicalDrive];
    if (!Drive->Handle)
        return RES_NOTRDY;

    switch (Command)
    {
        case CTRL_SYNC:
            return fflush(Drive->Handle) == 0 ? RES_OK : RES_ERROR;

        case GET_SECTOR_COUNT:
            if (!Buffer)
                return RES_PARERR;
            if (Drive->SectorCount == 0)
            {
                if (host_fseek(Drive->Handle, 0, SEEK_END) != 0)
                    return RES_ERROR;
                Size = host_ftell(Drive->Handle);
                if (Size < 0)
                    return RES_ERROR;
                Drive->SectorCount = (LBA_t)Size / IMAGE_SECTOR_SIZE;
            }
            *(LBA_t*)Buffer = Drive->SectorCount;
            return RES_OK;

        case GET_SECTOR_SIZE:
            if (!Buffer)
                return RES_PARERR;
            *(WORD*)Buffer = IMAGE_SECTOR_SIZE;
            return RES_OK;

        case GET_BLOCK_SIZE:
            if (!Buffer)
                return RES_PARERR;
            *(DWORD*)Buffer = 1;
            return RES_OK;

        default:
            return RES_PARERR;
    }
}
