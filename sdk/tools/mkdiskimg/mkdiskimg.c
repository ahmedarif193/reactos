/*
 * PROJECT:     ReactOS Disk Image Creator
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Assembles a bootable MBR disk image from one or more partition images
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.img@outlook.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SECTOR_SIZE 512
#define MBR_BOOT_CODE_SIZE 440
#define MBR_PARTITION_TABLE_OFFSET 446
#define MBR_SIGNATURE_OFFSET 510
#define MBR_MAX_PARTITIONS 4

#define GPT_HEADER_LBA 1
#define GPT_PART_ARRAY_LBA 2
#define GPT_NUMBER_OF_ENTRIES 128
#define GPT_SIZE_OF_ENTRY 128
#define GPT_PART_ARRAY_BYTES (GPT_NUMBER_OF_ENTRIES * GPT_SIZE_OF_ENTRY)
#define GPT_PART_ARRAY_SECTORS (GPT_PART_ARRAY_BYTES / SECTOR_SIZE)
#define GPT_HEADER_STRUCT_SIZE 92
#define GPT_REVISION 0x00010000U
#define GPT_FIRST_USABLE_LBA (GPT_PART_ARRAY_LBA + GPT_PART_ARRAY_SECTORS)
#define GPT_BACKUP_RESERVED_SECTORS (1 + GPT_PART_ARRAY_SECTORS)
#define MBR_PARTITION_TYPE_PROTECTIVE 0xEE

/* FAT/FAT32 BPB field offsets (from start of VBR) */
#define FAT_BPB_BYTES_PER_SECTOR_OFFSET 11
#define FAT_BPB_RESERVED_SECTORS_OFFSET 14
#define FAT_BPB_NUMBER_OF_FATS_OFFSET 16
#define FAT32_MEDIA_DESCRIPTOR_OFFSET 21
#define FAT_BPB_SECTORS_PER_FAT16_OFFSET 22
#define FAT32_HIDDEN_SECTORS_OFFSET 28
#define FAT32_SECTORS_PER_FAT_OFFSET 36
#define FAT32_BACKUP_BOOT_SECTOR_OFFSET 50
#define FAT32_BOOT_DRIVE_OFFSET 64
#define FAT16_BOOT_DRIVE_OFFSET 36

/* NTFS BPB / bootstrap offsets (from start of VBR) */
#define NTFS_OEM_NAME_OFFSET 3
#define NTFS_OEM_NAME_LENGTH 8
#define NTFS_BOOT_DRIVE_OFFSET 36
#define NTFS_BOOT_CODE_OFFSET 84
#define NTFS_BOOTSTRAP_SECTORS 5

/* Fixed disk media descriptor (0xF8) vs removable (0xF0) */
#define MEDIA_DESCRIPTOR_FIXED 0xF8
/* 0xFF tells boot code to use BIOS-provided drive number in DL */
#define BOOT_DRIVE_AUTO 0xFF

/* Conventional translation geometry for LBA-assisted partition tables */
#define CHS_SECTORS_PER_TRACK 63
#define CHS_HEADS 255

/* Default values */
#define DEFAULT_START_SECTOR 2048
#define DEFAULT_PARTITION_TYPE 0x0C /* FAT32 LBA */

/* VHD layout constants */
#define VHD_FOOTER_SIZE 512
#define VHD_DYNAMIC_HEADER_SIZE 1024
#define VHD_BLOCK_SIZE (2 * 1024 * 1024)
#define VHD_DISK_TYPE_DYNAMIC 3
#define VHD_TIMESTAMP_BASE 946684800UL /* 2000-01-01 00:00:00 UTC */

typedef enum _OUTPUT_FORMAT
{
    OUTPUT_FORMAT_RAW = 0,
    OUTPUT_FORMAT_VHD
} OUTPUT_FORMAT;

typedef enum _PARTITION_FILESYSTEM
{
    PARTITION_FILESYSTEM_FAT = 0,
    PARTITION_FILESYSTEM_NTFS
} PARTITION_FILESYSTEM;

#pragma pack(push, 1)
typedef struct _PARTITION_ENTRY
{
    unsigned char Status;       /* 0x80 = active */
    unsigned char CHSFirst[3];  /* CHS of first sector */
    unsigned char Type;         /* Partition type */
    unsigned char CHSLast[3];   /* CHS of last sector */
    unsigned int  LBAStart;     /* LBA of first sector */
    unsigned int  LBASize;      /* Number of sectors */
} PARTITION_ENTRY;
#pragma pack(pop)

typedef struct _PARTITION_DESC
{
    const char* path;
    const char* vbr_path;
    unsigned int start_sector;
    unsigned char type;
    int has_start;
    int has_type;
    int active;

    /* Runtime state populated during processing */
    FILE* file;
    long size;
    unsigned int sectors;
    unsigned char vbr[SECTOR_SIZE];
    PARTITION_FILESYSTEM filesystem;
    unsigned char gpt_unique_guid[16];
} PARTITION_DESC;

#pragma pack(push, 1)
typedef struct _GPT_HEADER
{
    unsigned char Signature[8];
    unsigned int Revision;
    unsigned int HeaderSize;
    unsigned int HeaderCRC32;
    unsigned int Reserved;
    unsigned long long MyLBA;
    unsigned long long AlternateLBA;
    unsigned long long FirstUsableLBA;
    unsigned long long LastUsableLBA;
    unsigned char DiskGUID[16];
    unsigned long long PartitionEntryLBA;
    unsigned int NumberOfEntries;
    unsigned int SizeOfEntry;
    unsigned int PartitionArrayCRC32;
} GPT_HEADER;

typedef struct _GPT_ENTRY
{
    unsigned char PartitionTypeGUID[16];
    unsigned char UniqueGUID[16];
    unsigned long long FirstLBA;
    unsigned long long LastLBA;
    unsigned long long Attributes;
    unsigned short PartitionName[36];
} GPT_ENTRY;
#pragma pack(pop)

static const unsigned char GPT_TYPE_ESP[16] =
{
    0x28, 0x73, 0x2A, 0xC1, 0x1F, 0xF8, 0xD2, 0x11,
    0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B
};

static const unsigned char GPT_TYPE_BASIC_DATA[16] =
{
    0xA2, 0xA0, 0xD0, 0xEB, 0xE5, 0xB9, 0x33, 0x44,
    0x87, 0xC0, 0x68, 0xB6, 0xB7, 0x26, 0x99, 0xC7
};

static void print_usage(const char* name)
{
    printf("Usage: %s -o <output> -mbr <mbr.bin>\n", name);
    printf("         -partition <img> [-start <sector>] [-type <hex>] [-active] [-vbr <boot.bin>]\n");
    printf("         [-partition <img> ...]  (up to %d slots)\n", MBR_MAX_PARTITIONS);
    printf("         [-format <raw|vhd>] [-vhd] [-gpt]\n\n");
    printf("  -o <output>         Output image file\n");
    printf("  -mbr <mbr.bin>      MBR boot code binary (first 440 bytes used)\n");
    printf("  -partition <img>    Raw partition image (FAT or NTFS); starts a new slot\n");
    printf("  -start <sector>     Partition start sector (default: %d for the first slot)\n", DEFAULT_START_SECTOR);
    printf("  -type <hex>         Partition type ID (default: 0x%02X)\n", DEFAULT_PARTITION_TYPE);
    printf("  -active             Set the active/bootable flag on the current slot\n");
    printf("  -vbr <boot.bin>     Optional filesystem VBR/bootstrap blob for the current slot\n");
    printf("  -format <raw|vhd>   Output container format (default: raw)\n");
    printf("  -vhd                Shorthand for -format vhd\n");
    printf("  -gpt                Write hybrid GPT (protective MBR + GPT primary/backup) for UEFI + BIOS\n");
}

static unsigned int crc32_update(unsigned int crc, const unsigned char* data, size_t len)
{
    size_t i;
    int j;

    for (i = 0; i < len; i++)
    {
        crc ^= data[i];
        for (j = 0; j < 8; j++)
            crc = (crc >> 1) ^ (0xEDB88320U & -(int)(crc & 1));
    }
    return crc;
}

static unsigned int crc32_compute(const unsigned char* data, size_t len)
{
    return crc32_update(0xFFFFFFFFU, data, len) ^ 0xFFFFFFFFU;
}

static void write_le32(unsigned char* ptr, unsigned int value)
{
    ptr[0] = (unsigned char)(value & 0xFF);
    ptr[1] = (unsigned char)((value >> 8) & 0xFF);
    ptr[2] = (unsigned char)((value >> 16) & 0xFF);
    ptr[3] = (unsigned char)((value >> 24) & 0xFF);
}

static void write_le64(unsigned char* ptr, unsigned long long value)
{
    int i;
    for (i = 0; i < 8; i++)
        ptr[i] = (unsigned char)((value >> (8 * i)) & 0xFF);
}

static void generate_random_guid(unsigned char guid[16])
{
    static int seeded = 0;
    int i;

    if (!seeded)
    {
        unsigned int seed = (unsigned int)time(NULL) ^ (unsigned int)clock();
        srand(seed);
        seeded = 1;
    }
    for (i = 0; i < 16; i++)
        guid[i] = (unsigned char)(rand() & 0xFF);

    guid[7] = (unsigned char)((guid[7] & 0x0F) | 0x40);
    guid[8] = (unsigned char)((guid[8] & 0x3F) | 0x80);
}

static const unsigned char* gpt_type_for_mbr_type(unsigned char mbr_type)
{
    if (mbr_type == 0xEF)
        return GPT_TYPE_ESP;
    return GPT_TYPE_BASIC_DATA;
}

static int copy_file_data(FILE* dst, FILE* src, long size)
{
    unsigned char buf[32768];
    long remaining = size;

    while (remaining > 0)
    {
        size_t chunk = remaining < (long)sizeof(buf) ? (size_t)remaining : sizeof(buf);
        size_t rd = fread(buf, 1, chunk, src);
        if (rd == 0)
            return -1;
        if (fwrite(buf, 1, rd, dst) != rd)
            return -1;
        remaining -= (long)rd;
    }
    return remaining == 0 ? 0 : -1;
}

static int write_zero_bytes(FILE* file, unsigned long long size)
{
    unsigned char zero[32768];

    memset(zero, 0, sizeof(zero));
    while (size > 0)
    {
        size_t chunk = size < sizeof(zero) ? (size_t)size : sizeof(zero);
        if (fwrite(zero, 1, chunk, file) != chunk)
            return -1;
        size -= chunk;
    }
    return 0;
}

static unsigned short read_le16(const unsigned char* ptr)
{
    return (unsigned short)((unsigned short)ptr[0] |
                            ((unsigned short)ptr[1] << 8));
}

static unsigned int read_le32(const unsigned char* ptr)
{
    return (unsigned int)((unsigned int)ptr[0] |
                          ((unsigned int)ptr[1] << 8) |
                          ((unsigned int)ptr[2] << 16) |
                          ((unsigned int)ptr[3] << 24));
}

static void write_chs(unsigned int lba, unsigned char chs[3])
{
    unsigned int cylinders, heads, sectors;

    sectors = (lba % CHS_SECTORS_PER_TRACK) + 1;
    lba /= CHS_SECTORS_PER_TRACK;
    heads = lba % CHS_HEADS;
    cylinders = lba / CHS_HEADS;

    if (cylinders > 1023)
    {
        cylinders = 1023;
        heads = CHS_HEADS - 1;
        sectors = CHS_SECTORS_PER_TRACK;
    }

    chs[0] = (unsigned char)heads;
    chs[1] = (unsigned char)((sectors & 0x3F) | ((cylinders >> 2) & 0xC0));
    chs[2] = (unsigned char)(cylinders & 0xFF);
}

static int write_byte_at(FILE* file, long offset, unsigned char value)
{
    if (fseek(file, offset, SEEK_SET) != 0)
        return -1;
    return fwrite(&value, 1, 1, file) == 1 ? 0 : -1;
}

static int write_le32_at(FILE* file, long offset, unsigned int value)
{
    unsigned char data[4];

    data[0] = (unsigned char)(value & 0xFF);
    data[1] = (unsigned char)((value >> 8) & 0xFF);
    data[2] = (unsigned char)((value >> 16) & 0xFF);
    data[3] = (unsigned char)((value >> 24) & 0xFF);

    if (fseek(file, offset, SEEK_SET) != 0)
        return -1;
    return fwrite(data, sizeof(data), 1, file) == 1 ? 0 : -1;
}

static int write_data_at(FILE* file, long offset, const void* data, size_t size)
{
    if (fseek(file, offset, SEEK_SET) != 0)
        return -1;
    return fwrite(data, 1, size, file) == size ? 0 : -1;
}

static void write_be32(unsigned char* ptr, unsigned int value)
{
    ptr[0] = (unsigned char)((value >> 24) & 0xFF);
    ptr[1] = (unsigned char)((value >> 16) & 0xFF);
    ptr[2] = (unsigned char)((value >> 8) & 0xFF);
    ptr[3] = (unsigned char)(value & 0xFF);
}

static void write_be64(unsigned char* ptr, unsigned long long value)
{
    ptr[0] = (unsigned char)((value >> 56) & 0xFF);
    ptr[1] = (unsigned char)((value >> 48) & 0xFF);
    ptr[2] = (unsigned char)((value >> 40) & 0xFF);
    ptr[3] = (unsigned char)((value >> 32) & 0xFF);
    ptr[4] = (unsigned char)((value >> 24) & 0xFF);
    ptr[5] = (unsigned char)((value >> 16) & 0xFF);
    ptr[6] = (unsigned char)((value >> 8) & 0xFF);
    ptr[7] = (unsigned char)(value & 0xFF);
}

static unsigned int calculate_checksum(const unsigned char* data, size_t size)
{
    unsigned int sum = 0;
    size_t i;

    for (i = 0; i < size; i++)
        sum += data[i];

    return ~sum;
}

static unsigned long long round_up_to_sector(unsigned long long value)
{
    return (value + (SECTOR_SIZE - 1)) & ~((unsigned long long)SECTOR_SIZE - 1);
}

static int buffer_is_zero(const unsigned char* data, size_t size)
{
    size_t i;

    for (i = 0; i < size; i++)
    {
        if (data[i] != 0)
            return 0;
    }
    return 1;
}

static PARTITION_FILESYSTEM detect_partition_filesystem(const unsigned char* vbr)
{
    static const unsigned char ntfs_oem_name[NTFS_OEM_NAME_LENGTH] =
        { 'N', 'T', 'F', 'S', ' ', ' ', ' ', ' ' };

    if (memcmp(vbr + NTFS_OEM_NAME_OFFSET, ntfs_oem_name, NTFS_OEM_NAME_LENGTH) == 0)
        return PARTITION_FILESYSTEM_NTFS;

    return PARTITION_FILESYSTEM_FAT;
}

static int install_ntfs_vbr(FILE* output,
                            long vbr_offset,
                            long partition_size,
                            const unsigned char* partition_vbr,
                            const char* vbr_path)
{
    FILE* vbr_file = NULL;
    unsigned char* ntfs_bootstrap = NULL;
    unsigned char first_sector[SECTOR_SIZE];
    long ntfs_bootstrap_size;
    size_t bootstrap_bytes_to_write;
    int ret = -1;

    if (!vbr_path)
        return 0;

    if (partition_size < NTFS_BOOTSTRAP_SECTORS * SECTOR_SIZE)
    {
        fprintf(stderr, "Error: NTFS partition image is too small for bootstrap installation.\n");
        return -1;
    }

    vbr_file = fopen(vbr_path, "rb");
    if (!vbr_file)
    {
        fprintf(stderr, "Error: Cannot open VBR file '%s'.\n", vbr_path);
        goto cleanup;
    }

    if (fseek(vbr_file, 0, SEEK_END) != 0)
    {
        fprintf(stderr, "Error: Cannot seek VBR file '%s'.\n", vbr_path);
        goto cleanup;
    }

    ntfs_bootstrap_size = ftell(vbr_file);
    if (ntfs_bootstrap_size < SECTOR_SIZE)
    {
        fprintf(stderr, "Error: NTFS VBR file '%s' is too small.\n", vbr_path);
        goto cleanup;
    }
    if (ntfs_bootstrap_size > NTFS_BOOTSTRAP_SECTORS * SECTOR_SIZE)
    {
        fprintf(stderr, "Error: NTFS VBR file '%s' exceeds %u sectors.\n",
                vbr_path, NTFS_BOOTSTRAP_SECTORS);
        goto cleanup;
    }
    if (fseek(vbr_file, 0, SEEK_SET) != 0)
    {
        fprintf(stderr, "Error: Cannot rewind VBR file '%s'.\n", vbr_path);
        goto cleanup;
    }

    ntfs_bootstrap = (unsigned char*)malloc((size_t)ntfs_bootstrap_size);
    if (!ntfs_bootstrap)
    {
        fprintf(stderr, "Error: Cannot allocate memory for NTFS VBR installation.\n");
        goto cleanup;
    }

    if (fread(ntfs_bootstrap, 1, (size_t)ntfs_bootstrap_size, vbr_file) != (size_t)ntfs_bootstrap_size)
    {
        fprintf(stderr, "Error: Cannot read NTFS VBR file '%s'.\n", vbr_path);
        goto cleanup;
    }

    memcpy(first_sector, partition_vbr, sizeof(first_sector));
    memcpy(first_sector, ntfs_bootstrap, 3);
    if (NTFS_BOOT_CODE_OFFSET < SECTOR_SIZE)
    {
        size_t first_sector_bootstrap_size = (size_t)ntfs_bootstrap_size;
        if (first_sector_bootstrap_size > SECTOR_SIZE)
            first_sector_bootstrap_size = SECTOR_SIZE;

        if (first_sector_bootstrap_size > NTFS_BOOT_CODE_OFFSET)
        {
            memcpy(first_sector + NTFS_BOOT_CODE_OFFSET,
                   ntfs_bootstrap + NTFS_BOOT_CODE_OFFSET,
                   first_sector_bootstrap_size - NTFS_BOOT_CODE_OFFSET);
        }
    }
    first_sector[NTFS_BOOT_DRIVE_OFFSET] = BOOT_DRIVE_AUTO;
    first_sector[MBR_SIGNATURE_OFFSET] = 0x55;
    first_sector[MBR_SIGNATURE_OFFSET + 1] = 0xAA;

    if (write_data_at(output, vbr_offset, first_sector, sizeof(first_sector)) != 0)
    {
        fprintf(stderr, "Error: Cannot install NTFS VBR first sector.\n");
        goto cleanup;
    }

    if (fseek(output, vbr_offset + SECTOR_SIZE, SEEK_SET) != 0 ||
        write_zero_bytes(output, (NTFS_BOOTSTRAP_SECTORS - 1) * SECTOR_SIZE) != 0)
    {
        fprintf(stderr, "Error: Cannot clear NTFS bootstrap extension sectors.\n");
        goto cleanup;
    }

    if (ntfs_bootstrap_size > SECTOR_SIZE)
    {
        bootstrap_bytes_to_write = (size_t)(ntfs_bootstrap_size - SECTOR_SIZE);
        if (write_data_at(output,
                          vbr_offset + SECTOR_SIZE,
                          ntfs_bootstrap + SECTOR_SIZE,
                          bootstrap_bytes_to_write) != 0)
        {
            fprintf(stderr, "Error: Cannot install NTFS bootstrap extension sectors.\n");
            goto cleanup;
        }
    }

    ret = 0;

cleanup:
    free(ntfs_bootstrap);
    if (vbr_file)
        fclose(vbr_file);
    return ret;
}

static void generate_vhd_uuid(unsigned char uuid[16])
{
    static int seeded = 0;
    int i;

    if (!seeded)
    {
        unsigned int seed = (unsigned int)time(NULL) ^ (unsigned int)clock();
        srand(seed);
        seeded = 1;
    }

    for (i = 0; i < 16; i++)
        uuid[i] = (unsigned char)(rand() & 0xFF);

    uuid[6] = (unsigned char)((uuid[6] & 0x0F) | 0x40);
    uuid[8] = (unsigned char)((uuid[8] & 0x3F) | 0x80);
}

static void calculate_vhd_geometry(unsigned long long size_bytes,
                                   unsigned short* cylinders,
                                   unsigned char* heads,
                                   unsigned char* sectors)
{
    unsigned long total_sectors;
    unsigned long cylinder_times_heads;

    total_sectors = (unsigned long)(size_bytes / SECTOR_SIZE);
    if (total_sectors > 65535UL * 16UL * 255UL)
        total_sectors = 65535UL * 16UL * 255UL;

    if (total_sectors >= 65535UL * 16UL * 63UL)
    {
        *sectors = 255;
        *heads = 16;
        cylinder_times_heads = (total_sectors + (*sectors - 1)) / *sectors;
    }
    else
    {
        *sectors = 17;
        cylinder_times_heads = (total_sectors + (*sectors - 1)) / *sectors;
        *heads = (unsigned char)((cylinder_times_heads + 1023UL) / 1024UL);

        if (*heads < 4)
            *heads = 4;

        if (cylinder_times_heads >= (unsigned long)(*heads) * 1024UL || *heads > 16)
        {
            *sectors = 31;
            *heads = 16;
            cylinder_times_heads = (total_sectors + (*sectors - 1)) / *sectors;
        }

        if (cylinder_times_heads >= (unsigned long)(*heads) * 1024UL)
        {
            *sectors = 63;
            *heads = 16;
            cylinder_times_heads = (total_sectors + (*sectors - 1)) / *sectors;
        }
    }

    *cylinders = (unsigned short)((cylinder_times_heads + (*heads - 1)) / *heads);
    if (*cylinders == 0)
        *cylinders = 1;
}

static void build_vhd_footer(unsigned char footer[VHD_FOOTER_SIZE],
                             unsigned long long disk_size,
                             const unsigned char uuid[16])
{
    unsigned short cylinders;
    unsigned char heads;
    unsigned char sectors;
    unsigned int timestamp = 0;
    time_t now;

    memset(footer, 0, VHD_FOOTER_SIZE);

    memcpy(footer + 0, "conectix", 8);
    write_be32(footer + 8, 0x00000002);
    write_be32(footer + 12, 0x00010000);
    write_be64(footer + 16, VHD_FOOTER_SIZE);

    now = time(NULL);
    if (now > (time_t)VHD_TIMESTAMP_BASE)
        timestamp = (unsigned int)(now - (time_t)VHD_TIMESTAMP_BASE);

    write_be32(footer + 24, timestamp);
    memcpy(footer + 28, "ROS ", 4);
    write_be32(footer + 32, 0x00010000);
    memcpy(footer + 36, "Wi2k", 4);
    write_be64(footer + 40, disk_size);
    write_be64(footer + 48, disk_size);

    calculate_vhd_geometry(disk_size, &cylinders, &heads, &sectors);
    footer[56] = (unsigned char)((cylinders >> 8) & 0xFF);
    footer[57] = (unsigned char)(cylinders & 0xFF);
    footer[58] = heads;
    footer[59] = sectors;

    write_be32(footer + 60, VHD_DISK_TYPE_DYNAMIC);
    memcpy(footer + 68, uuid, 16);
    footer[84] = 0;

    write_be32(footer + 64, 0);
    write_be32(footer + 64, calculate_checksum(footer, VHD_FOOTER_SIZE));
}

static void build_vhd_dynamic_header(unsigned char header[VHD_DYNAMIC_HEADER_SIZE],
                                     unsigned int block_count,
                                     unsigned long long table_offset)
{
    memset(header, 0, VHD_DYNAMIC_HEADER_SIZE);

    memcpy(header + 0, "cxsparse", 8);
    write_be64(header + 8, ~0ULL);
    write_be64(header + 16, table_offset);
    write_be32(header + 24, 0x00010000);
    write_be32(header + 28, block_count);
    write_be32(header + 32, VHD_BLOCK_SIZE);

    write_be32(header + 36, 0);
    write_be32(header + 36, calculate_checksum(header, VHD_DYNAMIC_HEADER_SIZE));
}

static int write_dynamic_vhd(FILE* dst, FILE* src, unsigned long long virtual_size)
{
    unsigned char footer[VHD_FOOTER_SIZE];
    unsigned char header[VHD_DYNAMIC_HEADER_SIZE];
    unsigned char uuid[16];
    unsigned char* block_buffer = NULL;
    unsigned char* bitmap_buffer = NULL;
    unsigned int* bat = NULL;
    unsigned int sectors_per_block;
    unsigned int block_count;
    unsigned int bitmap_bytes;
    unsigned int bitmap_size;
    unsigned long long bat_bytes;
    unsigned long long bat_size;
    unsigned long long table_offset;
    unsigned long long block_index;
    int ret = -1;

    sectors_per_block = VHD_BLOCK_SIZE / SECTOR_SIZE;
    block_count = (unsigned int)((virtual_size + VHD_BLOCK_SIZE - 1) / VHD_BLOCK_SIZE);
    bitmap_bytes = (sectors_per_block + 7) / 8;
    bitmap_size = (unsigned int)round_up_to_sector(bitmap_bytes);
    bat_bytes = (unsigned long long)block_count * sizeof(unsigned int);
    bat_size = round_up_to_sector(bat_bytes);
    table_offset = VHD_FOOTER_SIZE + VHD_DYNAMIC_HEADER_SIZE;

    generate_vhd_uuid(uuid);
    build_vhd_footer(footer, virtual_size, uuid);
    build_vhd_dynamic_header(header, block_count, table_offset);

    bat = (unsigned int*)malloc(block_count * sizeof(unsigned int));
    block_buffer = (unsigned char*)malloc(VHD_BLOCK_SIZE);
    bitmap_buffer = (unsigned char*)malloc(bitmap_size);
    if ((!bat && block_count != 0) || !block_buffer || !bitmap_buffer)
        goto cleanup;

    for (block_index = 0; block_index < block_count; block_index++)
        bat[block_index] = 0xFFFFFFFFU;

    if (fwrite(footer, 1, sizeof(footer), dst) != sizeof(footer))
        goto cleanup;
    if (fwrite(header, 1, sizeof(header), dst) != sizeof(header))
        goto cleanup;
    if (write_zero_bytes(dst, bat_size) != 0)
        goto cleanup;

    if (fseek(src, 0, SEEK_SET) != 0)
        goto cleanup;

    for (block_index = 0; block_index < block_count; block_index++)
    {
        unsigned long long remaining = virtual_size - block_index * (unsigned long long)VHD_BLOCK_SIZE;
        unsigned int block_bytes = remaining > VHD_BLOCK_SIZE ? VHD_BLOCK_SIZE : (unsigned int)remaining;
        unsigned int data_sectors = (block_bytes + SECTOR_SIZE - 1) / SECTOR_SIZE;
        long block_file_offset;
        unsigned int sector_index;

        memset(block_buffer, 0, VHD_BLOCK_SIZE);
        memset(bitmap_buffer, 0, bitmap_size);

        if (fread(block_buffer, 1, block_bytes, src) != block_bytes)
            goto cleanup;

        if (buffer_is_zero(block_buffer, block_bytes))
            continue;

        block_file_offset = ftell(dst);
        if (block_file_offset < 0 || (block_file_offset % SECTOR_SIZE) != 0)
            goto cleanup;
        bat[block_index] = (unsigned int)(block_file_offset / SECTOR_SIZE);

        for (sector_index = 0; sector_index < data_sectors; sector_index++)
            bitmap_buffer[sector_index / 8] |= (unsigned char)(0x80 >> (sector_index % 8));

        if (fwrite(bitmap_buffer, 1, bitmap_size, dst) != bitmap_size)
            goto cleanup;
        if (fwrite(block_buffer, 1, block_bytes, dst) != block_bytes)
            goto cleanup;
        if (block_bytes < VHD_BLOCK_SIZE &&
            write_zero_bytes(dst, VHD_BLOCK_SIZE - block_bytes) != 0)
            goto cleanup;
    }

    if (fwrite(footer, 1, sizeof(footer), dst) != sizeof(footer))
        goto cleanup;

    if (fseek(dst, (long)table_offset, SEEK_SET) != 0)
        goto cleanup;

    for (block_index = 0; block_index < block_count; block_index++)
    {
        unsigned char entry[4];
        write_be32(entry, bat[block_index]);
        if (fwrite(entry, 1, sizeof(entry), dst) != sizeof(entry))
            goto cleanup;
    }

    ret = 0;

cleanup:
    free(bitmap_buffer);
    free(block_buffer);
    free(bat);
    return ret;
}

static int patch_fat_vbr(FILE* output, const PARTITION_DESC* part)
{
    long vbr_offset = (long)part->start_sector * SECTOR_SIZE;
    unsigned short bytes_per_sector;
    unsigned short reserved_sectors;
    unsigned short backup_boot_sector = 0;
    unsigned short sectors_per_fat_16;
    unsigned char number_of_fats;
    unsigned int sectors_per_fat;
    long fat_offset;
    long fat_size;
    unsigned int fat_index;
    int is_fat32;
    unsigned char media_byte = MEDIA_DESCRIPTOR_FIXED;
    unsigned char boot_drive = BOOT_DRIVE_AUTO;
    long boot_drive_offset;
    static const unsigned char fat32_reserved_entries[12] =
    {
        0xF8, 0xFF, 0xFF, 0x0F,
        0xFF, 0xFF, 0xFF, 0x0F,
        0xF8, 0xFF, 0xFF, 0x0F
    };

    bytes_per_sector = read_le16(part->vbr + FAT_BPB_BYTES_PER_SECTOR_OFFSET);
    reserved_sectors = read_le16(part->vbr + FAT_BPB_RESERVED_SECTORS_OFFSET);
    number_of_fats = part->vbr[FAT_BPB_NUMBER_OF_FATS_OFFSET];
    sectors_per_fat_16 = read_le16(part->vbr + FAT_BPB_SECTORS_PER_FAT16_OFFSET);
    is_fat32 = (sectors_per_fat_16 == 0);
    sectors_per_fat = sectors_per_fat_16;
    if (is_fat32)
    {
        sectors_per_fat = read_le32(part->vbr + FAT32_SECTORS_PER_FAT_OFFSET);
        backup_boot_sector = read_le16(part->vbr + FAT32_BACKUP_BOOT_SECTOR_OFFSET);
    }

    /* FAT32 BootDrive lives at offset 64; FAT12/FAT16 put it at offset 36. */
    boot_drive_offset = is_fat32 ? FAT32_BOOT_DRIVE_OFFSET : NTFS_BOOT_DRIVE_OFFSET;

    if (bytes_per_sector == 0 || reserved_sectors == 0 || number_of_fats == 0 || sectors_per_fat == 0)
    {
        fprintf(stderr, "Error: Partition image '%s' has an invalid FAT BPB.\n", part->path);
        return -1;
    }

    /* Patch HiddenSectors in primary BPB to match partition start */
    if (write_le32_at(output, vbr_offset + FAT32_HIDDEN_SECTORS_OFFSET, part->start_sector) != 0)
    {
        fprintf(stderr, "Error: Cannot patch HiddenSectors in BPB.\n");
        return -1;
    }

    if (backup_boot_sector != 0)
    {
        long backup_vbr_offset = vbr_offset + (long)backup_boot_sector * bytes_per_sector;

        if (backup_vbr_offset + FAT32_HIDDEN_SECTORS_OFFSET + 4 > vbr_offset + part->size ||
            write_le32_at(output, backup_vbr_offset + FAT32_HIDDEN_SECTORS_OFFSET, part->start_sector) != 0)
        {
            fprintf(stderr, "Error: Cannot patch HiddenSectors in backup BPB.\n");
            return -1;
        }
    }

    /* Patch MediaDescriptor to 0xF8 (fixed disk); FatFs may have written 0xF0 (removable) */
    if (write_byte_at(output, vbr_offset + FAT32_MEDIA_DESCRIPTOR_OFFSET, media_byte) != 0)
    {
        fprintf(stderr, "Error: Cannot patch media descriptor in BPB.\n");
        return -1;
    }

    if (backup_boot_sector != 0)
    {
        long backup_vbr_offset = vbr_offset + (long)backup_boot_sector * bytes_per_sector;

        if (backup_vbr_offset + FAT32_MEDIA_DESCRIPTOR_OFFSET + 1 > vbr_offset + part->size ||
            write_byte_at(output, backup_vbr_offset + FAT32_MEDIA_DESCRIPTOR_OFFSET, media_byte) != 0)
        {
            fprintf(stderr, "Error: Cannot patch media descriptor in backup BPB.\n");
            return -1;
        }
    }

    fat_offset = vbr_offset + (long)reserved_sectors * bytes_per_sector;
    fat_size = (long)sectors_per_fat * bytes_per_sector;

    for (fat_index = 0; fat_index < number_of_fats; fat_index++)
    {
        long current_fat_offset = fat_offset + (long)fat_index * fat_size;

        if (current_fat_offset + 1 > vbr_offset + part->size ||
            write_byte_at(output, current_fat_offset, media_byte) != 0)
        {
            fprintf(stderr, "Error: Cannot patch FAT%u media descriptor.\n", fat_index + 1);
            return -1;
        }

        /* Only FAT32 needs the 12-byte reserved entries (entry 0 = media+EOF32, entry 1 = clean flag,
         * entry 2 = EOC for $ROOTDIR). FAT12/FAT16 use different-width entries; writing this
         * pattern to them overwrites real cluster chains and corrupts the filesystem. */
        if (is_fat32 && fat_size >= (long)sizeof(fat32_reserved_entries) &&
            write_data_at(output, current_fat_offset, fat32_reserved_entries, sizeof(fat32_reserved_entries)) != 0)
        {
            fprintf(stderr, "Error: Cannot patch FAT%u reserved entries.\n", fat_index + 1);
            return -1;
        }
    }

    /* Patch BootDrive to 0xFF (auto-detect from BIOS DL register) */
    if (write_byte_at(output, vbr_offset + boot_drive_offset, boot_drive) != 0)
    {
        fprintf(stderr, "Error: Cannot patch BootDrive in BPB.\n");
        return -1;
    }

    if (backup_boot_sector != 0)
    {
        long backup_vbr_offset = vbr_offset + (long)backup_boot_sector * bytes_per_sector;

        if (backup_vbr_offset + boot_drive_offset + 1 > vbr_offset + part->size ||
            write_byte_at(output, backup_vbr_offset + boot_drive_offset, boot_drive) != 0)
        {
            fprintf(stderr, "Error: Cannot patch BootDrive in backup BPB.\n");
            return -1;
        }
    }

    return 0;
}

static int open_partition(PARTITION_DESC* part)
{
    part->file = fopen(part->path, "rb");
    if (!part->file)
    {
        fprintf(stderr, "Error: Cannot open partition image '%s'.\n", part->path);
        return -1;
    }

    if (fseek(part->file, 0, SEEK_END) != 0)
    {
        fprintf(stderr, "Error: Cannot seek partition image '%s'.\n", part->path);
        return -1;
    }

    part->size = ftell(part->file);
    if (part->size <= 0 || (part->size % SECTOR_SIZE) != 0)
    {
        fprintf(stderr, "Error: Partition image '%s' size (%ld) is not a multiple of %d.\n",
                part->path, part->size, SECTOR_SIZE);
        return -1;
    }

    part->sectors = (unsigned int)(part->size / SECTOR_SIZE);

    if (fseek(part->file, 0, SEEK_SET) != 0)
    {
        fprintf(stderr, "Error: Cannot rewind partition image '%s'.\n", part->path);
        return -1;
    }

    if (fread(part->vbr, 1, sizeof(part->vbr), part->file) != sizeof(part->vbr))
    {
        fprintf(stderr, "Error: Cannot read boot sector from '%s'.\n", part->path);
        return -1;
    }

    if (fseek(part->file, 0, SEEK_SET) != 0)
    {
        fprintf(stderr, "Error: Cannot rewind partition image '%s'.\n", part->path);
        return -1;
    }

    part->filesystem = detect_partition_filesystem(part->vbr);
    return 0;
}

static int partitions_overlap(const PARTITION_DESC* a, const PARTITION_DESC* b)
{
    unsigned long long a_end = (unsigned long long)a->start_sector + a->sectors;
    unsigned long long b_end = (unsigned long long)b->start_sector + b->sectors;

    return !(a_end <= b->start_sector || b_end <= a->start_sector);
}

static int write_gpt(FILE* output,
                     unsigned long long total_sectors,
                     const PARTITION_DESC* parts,
                     int num_partitions,
                     const unsigned char disk_guid[16])
{
    unsigned char* array = NULL;
    unsigned char header[SECTOR_SIZE];
    unsigned int array_crc;
    unsigned int header_crc;
    unsigned long long backup_array_lba;
    unsigned long long backup_header_lba;
    unsigned long long last_usable_lba;
    int p;
    int ret = -1;

    if (total_sectors <= GPT_FIRST_USABLE_LBA + GPT_BACKUP_RESERVED_SECTORS)
    {
        fprintf(stderr, "Error: Disk is too small for GPT layout.\n");
        return -1;
    }

    backup_header_lba = total_sectors - 1;
    backup_array_lba = total_sectors - GPT_BACKUP_RESERVED_SECTORS;
    last_usable_lba = backup_array_lba - 1;

    array = (unsigned char*)malloc(GPT_PART_ARRAY_BYTES);
    if (!array)
    {
        fprintf(stderr, "Error: Cannot allocate GPT partition array.\n");
        return -1;
    }
    memset(array, 0, GPT_PART_ARRAY_BYTES);

    for (p = 0; p < num_partitions; p++)
    {
        unsigned char* entry = array + (unsigned long)p * GPT_SIZE_OF_ENTRY;
        const unsigned char* type_guid = gpt_type_for_mbr_type(parts[p].type);
        unsigned long long first_lba = (unsigned long long)parts[p].start_sector;
        unsigned long long last_lba = first_lba + parts[p].sectors - 1;

        if (first_lba < GPT_FIRST_USABLE_LBA || last_lba > last_usable_lba)
        {
            fprintf(stderr,
                    "Error: Partition %d LBA range [%llu, %llu] is outside GPT usable range [%llu, %llu].\n",
                    p, first_lba, last_lba, (unsigned long long)GPT_FIRST_USABLE_LBA, last_usable_lba);
            goto cleanup;
        }

        memcpy(entry + 0, type_guid, 16);
        memcpy(entry + 16, parts[p].gpt_unique_guid, 16);
        write_le64(entry + 32, first_lba);
        write_le64(entry + 40, last_lba);
    }

    array_crc = crc32_compute(array, GPT_PART_ARRAY_BYTES);

    if (fseek(output, (long)(GPT_PART_ARRAY_LBA * SECTOR_SIZE), SEEK_SET) != 0 ||
        fwrite(array, 1, GPT_PART_ARRAY_BYTES, output) != GPT_PART_ARRAY_BYTES)
    {
        fprintf(stderr, "Error: Cannot write primary GPT partition array.\n");
        goto cleanup;
    }

    memset(header, 0, sizeof(header));
    memcpy(header + 0, "EFI PART", 8);
    write_le32(header + 8, GPT_REVISION);
    write_le32(header + 12, GPT_HEADER_STRUCT_SIZE);
    write_le32(header + 16, 0);
    write_le32(header + 20, 0);
    write_le64(header + 24, GPT_HEADER_LBA);
    write_le64(header + 32, backup_header_lba);
    write_le64(header + 40, GPT_FIRST_USABLE_LBA);
    write_le64(header + 48, last_usable_lba);
    memcpy(header + 56, disk_guid, 16);
    write_le64(header + 72, GPT_PART_ARRAY_LBA);
    write_le32(header + 80, GPT_NUMBER_OF_ENTRIES);
    write_le32(header + 84, GPT_SIZE_OF_ENTRY);
    write_le32(header + 88, array_crc);
    header_crc = crc32_compute(header, GPT_HEADER_STRUCT_SIZE);
    write_le32(header + 16, header_crc);

    if (fseek(output, (long)(GPT_HEADER_LBA * SECTOR_SIZE), SEEK_SET) != 0 ||
        fwrite(header, 1, SECTOR_SIZE, output) != SECTOR_SIZE)
    {
        fprintf(stderr, "Error: Cannot write primary GPT header.\n");
        goto cleanup;
    }

    {
        unsigned long long backup_array_offset = backup_array_lba * SECTOR_SIZE;
        if (fseek(output, (long)backup_array_offset, SEEK_SET) != 0 ||
            fwrite(array, 1, GPT_PART_ARRAY_BYTES, output) != GPT_PART_ARRAY_BYTES)
        {
            fprintf(stderr, "Error: Cannot write backup GPT partition array.\n");
            goto cleanup;
        }
    }

    memset(header, 0, sizeof(header));
    memcpy(header + 0, "EFI PART", 8);
    write_le32(header + 8, GPT_REVISION);
    write_le32(header + 12, GPT_HEADER_STRUCT_SIZE);
    write_le32(header + 16, 0);
    write_le32(header + 20, 0);
    write_le64(header + 24, backup_header_lba);
    write_le64(header + 32, GPT_HEADER_LBA);
    write_le64(header + 40, GPT_FIRST_USABLE_LBA);
    write_le64(header + 48, last_usable_lba);
    memcpy(header + 56, disk_guid, 16);
    write_le64(header + 72, backup_array_lba);
    write_le32(header + 80, GPT_NUMBER_OF_ENTRIES);
    write_le32(header + 84, GPT_SIZE_OF_ENTRY);
    write_le32(header + 88, array_crc);
    header_crc = crc32_compute(header, GPT_HEADER_STRUCT_SIZE);
    write_le32(header + 16, header_crc);

    if (fseek(output, (long)(backup_header_lba * SECTOR_SIZE), SEEK_SET) != 0 ||
        fwrite(header, 1, SECTOR_SIZE, output) != SECTOR_SIZE)
    {
        fprintf(stderr, "Error: Cannot write backup GPT header.\n");
        goto cleanup;
    }

    ret = 0;

cleanup:
    free(array);
    return ret;
}

static int sort_partitions_by_start(PARTITION_DESC* parts, int count, int* order)
{
    int i, j;

    for (i = 0; i < count; i++)
        order[i] = i;

    /* Simple insertion sort; count <= MBR_MAX_PARTITIONS */
    for (i = 1; i < count; i++)
    {
        int key = order[i];
        for (j = i - 1;
             j >= 0 && parts[order[j]].start_sector > parts[key].start_sector;
             j--)
        {
            order[j + 1] = order[j];
        }
        order[j + 1] = key;
    }

    return 0;
}

int main(int argc, char* argv[])
{
    const char* output_path = NULL;
    const char* mbr_path = NULL;
    OUTPUT_FORMAT output_format = OUTPUT_FORMAT_RAW;
    int gpt_mode = 0;

    PARTITION_DESC parts[MBR_MAX_PARTITIONS];
    int num_partitions = 0;
    int current = -1;
    int any_active = 0;
    int order[MBR_MAX_PARTITIONS];
    unsigned char disk_guid[16];

    FILE* f_output = NULL;
    FILE* f_final = NULL;
    FILE* f_mbr = NULL;

    unsigned char mbr_sector[SECTOR_SIZE];
    unsigned long long total_size = 0;
    unsigned long long total_sectors;
    unsigned long long current_offset;
    int i, p, ret = 1;

    memset(parts, 0, sizeof(parts));

    /* Parse arguments */
    for (i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc)
        {
            output_path = argv[++i];
        }
        else if (strcmp(argv[i], "-mbr") == 0 && i + 1 < argc)
        {
            mbr_path = argv[++i];
        }
        else if (strcmp(argv[i], "-partition") == 0 && i + 1 < argc)
        {
            if (num_partitions >= MBR_MAX_PARTITIONS)
            {
                fprintf(stderr, "Error: Too many -partition slots (max %d).\n", MBR_MAX_PARTITIONS);
                print_usage(argv[0]);
                return 1;
            }
            current = num_partitions++;
            parts[current].path = argv[++i];
            parts[current].type = DEFAULT_PARTITION_TYPE;
            parts[current].start_sector = DEFAULT_START_SECTOR;
        }
        else if (strcmp(argv[i], "-vbr") == 0 && i + 1 < argc)
        {
            if (current < 0)
            {
                fprintf(stderr, "Error: -vbr must follow a -partition.\n");
                return 1;
            }
            parts[current].vbr_path = argv[++i];
        }
        else if (strcmp(argv[i], "-start") == 0 && i + 1 < argc)
        {
            if (current < 0)
            {
                fprintf(stderr, "Error: -start must follow a -partition.\n");
                return 1;
            }
            parts[current].start_sector = (unsigned int)strtoul(argv[++i], NULL, 10);
            parts[current].has_start = 1;
        }
        else if (strcmp(argv[i], "-type") == 0 && i + 1 < argc)
        {
            if (current < 0)
            {
                fprintf(stderr, "Error: -type must follow a -partition.\n");
                return 1;
            }
            parts[current].type = (unsigned char)strtoul(argv[++i], NULL, 16);
            parts[current].has_type = 1;
        }
        else if (strcmp(argv[i], "-active") == 0)
        {
            if (current < 0)
            {
                fprintf(stderr, "Error: -active must follow a -partition.\n");
                return 1;
            }
            parts[current].active = 1;
            any_active = 1;
        }
        else if (strcmp(argv[i], "-format") == 0 && i + 1 < argc)
        {
            const char* format = argv[++i];
            if (strcmp(format, "raw") == 0)
                output_format = OUTPUT_FORMAT_RAW;
            else if (strcmp(format, "vhd") == 0)
                output_format = OUTPUT_FORMAT_VHD;
            else
            {
                fprintf(stderr, "Error: Unknown format '%s'.\n", format);
                print_usage(argv[0]);
                return 1;
            }
        }
        else if (strcmp(argv[i], "-vhd") == 0)
        {
            output_format = OUTPUT_FORMAT_VHD;
        }
        else if (strcmp(argv[i], "-gpt") == 0)
        {
            gpt_mode = 1;
        }
        else
        {
            fprintf(stderr, "Error: Unknown argument '%s'\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    if (!output_path || !mbr_path || num_partitions == 0)
    {
        fprintf(stderr, "Error: Missing required arguments.\n");
        print_usage(argv[0]);
        return 1;
    }

    /* Preserve legacy single-partition behavior: implicit active */
    if (!any_active && num_partitions == 1)
        parts[0].active = 1;

    /* Read MBR boot code */
    f_mbr = fopen(mbr_path, "rb");
    if (!f_mbr)
    {
        fprintf(stderr, "Error: Cannot open MBR file '%s'.\n", mbr_path);
        goto cleanup;
    }

    memset(mbr_sector, 0, SECTOR_SIZE);
    if (fread(mbr_sector, 1, MBR_BOOT_CODE_SIZE, f_mbr) != MBR_BOOT_CODE_SIZE)
    {
        fprintf(stderr, "Error: Cannot read MBR boot code from '%s'.\n", mbr_path);
        goto cleanup;
    }
    fclose(f_mbr);
    f_mbr = NULL;

    if (gpt_mode && num_partitions >= MBR_MAX_PARTITIONS)
    {
        fprintf(stderr,
                "Error: -gpt needs one free MBR slot for the protective entry; use up to %d -partition.\n",
                MBR_MAX_PARTITIONS - 1);
        goto cleanup;
    }

    /* Open each partition, read its VBR, and compute sector count */
    for (p = 0; p < num_partitions; p++)
    {
        if (open_partition(&parts[p]) != 0)
            goto cleanup;

        if ((unsigned long long)parts[p].start_sector + parts[p].sectors > 0xFFFFFFFFULL)
        {
            fprintf(stderr, "Error: Partition %d start/size exceeds MBR addressing limits.\n", p);
            goto cleanup;
        }

        if (gpt_mode)
            generate_random_guid(parts[p].gpt_unique_guid);
    }

    if (gpt_mode)
        generate_random_guid(disk_guid);

    /* Sort partitions by start sector so we can write them in order */
    sort_partitions_by_start(parts, num_partitions, order);

    /* Reject overlapping or mis-aligned layouts */
    for (i = 0; i < num_partitions; i++)
    {
        PARTITION_DESC* a = &parts[order[i]];

        if (a->start_sector == 0)
        {
            fprintf(stderr, "Error: Partition %d cannot start at sector 0 (reserved for MBR).\n", order[i]);
            goto cleanup;
        }

        if (i > 0)
        {
            PARTITION_DESC* prev = &parts[order[i - 1]];

            if (partitions_overlap(prev, a))
            {
                fprintf(stderr, "Error: Partitions %d and %d overlap.\n", order[i - 1], order[i]);
                goto cleanup;
            }
        }
    }

    /* Build partition table entries in slot order (not sorted order) */
    for (p = 0; p < num_partitions; p++)
    {
        PARTITION_ENTRY* entry = (PARTITION_ENTRY*)(mbr_sector + MBR_PARTITION_TABLE_OFFSET + p * sizeof(PARTITION_ENTRY));
        unsigned int end_sector = parts[p].start_sector + parts[p].sectors - 1;

        entry->Status = parts[p].active ? (unsigned char)0x80 : (unsigned char)0x00;
        entry->Type = parts[p].type;
        entry->LBAStart = parts[p].start_sector;
        entry->LBASize = parts[p].sectors;
        write_chs(parts[p].start_sector, entry->CHSFirst);
        write_chs(end_sector, entry->CHSLast);
    }

    /* Write boot signature */
    mbr_sector[MBR_SIGNATURE_OFFSET]     = 0x55;
    mbr_sector[MBR_SIGNATURE_OFFSET + 1] = 0xAA;

    /* Compute total virtual-disk size as the end of the last partition */
    for (p = 0; p < num_partitions; p++)
    {
        unsigned long long end = ((unsigned long long)parts[p].start_sector + parts[p].sectors) * SECTOR_SIZE;
        if (end > total_size)
            total_size = end;
    }

    /* GPT needs a backup header + array trailer at the very end of the disk */
    if (gpt_mode)
        total_size += (unsigned long long)GPT_BACKUP_RESERVED_SECTORS * SECTOR_SIZE;

    total_sectors = total_size / SECTOR_SIZE;

    /* Hybrid MBR: drop a 0xEE protective covering [1, first_partition - 1] so UEFI sees GPT
     * without overlapping the real MBR entries that legacy BIOS chainloads from. */
    if (gpt_mode)
    {
        PARTITION_ENTRY* pe;
        unsigned int first_lba = parts[order[0]].start_sector;
        unsigned int protective_size = first_lba > 1 ? first_lba - 1 : 1;
        unsigned int protective_end = protective_size;

        pe = (PARTITION_ENTRY*)(mbr_sector + MBR_PARTITION_TABLE_OFFSET +
                                num_partitions * sizeof(PARTITION_ENTRY));
        pe->Status = 0x00;
        pe->Type = MBR_PARTITION_TYPE_PROTECTIVE;
        pe->LBAStart = 1;
        pe->LBASize = protective_size;
        write_chs(1, pe->CHSFirst);
        write_chs(protective_end, pe->CHSLast);
    }

    /* Raw images are written directly; VHD images are wrapped after staging the raw disk. */
    if (output_format == OUTPUT_FORMAT_RAW)
    {
        f_output = fopen(output_path, "w+b");
        if (!f_output)
        {
            fprintf(stderr, "Error: Cannot create output file '%s'.\n", output_path);
            goto cleanup;
        }
    }
    else
    {
        f_output = tmpfile();
        if (!f_output)
        {
            fprintf(stderr, "Error: Cannot create temporary raw image for VHD output.\n");
            goto cleanup;
        }
    }

    /* Write MBR sector */
    if (fwrite(mbr_sector, SECTOR_SIZE, 1, f_output) != 1)
    {
        fprintf(stderr, "Error: Cannot write MBR sector.\n");
        goto cleanup;
    }

    current_offset = SECTOR_SIZE;

    /* Stream each partition into place in ascending-start order, with zero-fill gaps */
    for (i = 0; i < num_partitions; i++)
    {
        PARTITION_DESC* part = &parts[order[i]];
        unsigned long long part_offset = (unsigned long long)part->start_sector * SECTOR_SIZE;

        if (part_offset < current_offset)
        {
            fprintf(stderr, "Error: Internal error — partition %d offset regressed.\n", order[i]);
            goto cleanup;
        }

        if (part_offset > current_offset &&
            write_zero_bytes(f_output, part_offset - current_offset) != 0)
        {
            fprintf(stderr, "Error: Cannot write gap sectors before partition %d.\n", order[i]);
            goto cleanup;
        }

        if (copy_file_data(f_output, part->file, part->size) != 0)
        {
            fprintf(stderr, "Error: Cannot write partition %d data.\n", order[i]);
            goto cleanup;
        }

        current_offset = part_offset + (unsigned long long)part->size;
    }

    /* Patch each partition's VBR (FAT or NTFS) in the staged image */
    for (p = 0; p < num_partitions; p++)
    {
        PARTITION_DESC* part = &parts[p];
        long vbr_offset = (long)part->start_sector * SECTOR_SIZE;

        if (part->filesystem == PARTITION_FILESYSTEM_FAT)
        {
            if (patch_fat_vbr(f_output, part) != 0)
                goto cleanup;
        }
        else
        {
            if (install_ntfs_vbr(f_output, vbr_offset, part->size, part->vbr, part->vbr_path) != 0)
                goto cleanup;
        }
    }

    /* Extend raw image to include the reserved backup-GPT trailer and lay down the GPT */
    if (gpt_mode)
    {
        if (fseek(f_output, (long)(current_offset), SEEK_SET) != 0 ||
            write_zero_bytes(f_output,
                             total_size - current_offset) != 0)
        {
            fprintf(stderr, "Error: Cannot extend image for GPT backup area.\n");
            goto cleanup;
        }

        if (write_gpt(f_output, total_sectors, parts, num_partitions, disk_guid) != 0)
            goto cleanup;
    }

    if (output_format == OUTPUT_FORMAT_VHD)
    {
        if (fflush(f_output) != 0 || fseek(f_output, 0, SEEK_SET) != 0)
        {
            fprintf(stderr, "Error: Cannot rewind staged raw image for VHD output.\n");
            goto cleanup;
        }

        f_final = fopen(output_path, "w+b");
        if (!f_final)
        {
            fprintf(stderr, "Error: Cannot create output file '%s'.\n", output_path);
            goto cleanup;
        }

        if (write_dynamic_vhd(f_final, f_output, total_size) != 0)
        {
            fprintf(stderr, "Error: Cannot write dynamic VHD image.\n");
            goto cleanup;
        }
    }

    printf("Created %s '%s' (%llu bytes virtual disk, %d %s partition%s)\n",
           output_format == OUTPUT_FORMAT_VHD ? "dynamic VHD image" : "disk image",
           output_path,
           total_size,
           num_partitions,
           gpt_mode ? "GPT" : "MBR",
           num_partitions == 1 ? "" : "s");
    ret = 0;

cleanup:
    for (p = 0; p < num_partitions; p++)
    {
        if (parts[p].file)
            fclose(parts[p].file);
    }
    if (f_final) fclose(f_final);
    if (f_output) fclose(f_output);
    if (f_mbr) fclose(f_mbr);
    return ret;
}
