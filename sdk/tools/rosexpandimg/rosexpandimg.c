/*
 * PROJECT:     ReactOS host tools
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Grow a disk image file and its last partition and NTFS volume to a new size
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <io.h>
#else
#include <sys/types.h>
#include <unistd.h>
#endif

#include "expandcore.h"

static int
SeekImage(FILE* File, uint64_t Offset)
{
#ifdef _WIN32
    return _fseeki64(File, (__int64)Offset, SEEK_SET);
#else
    return fseeko(File, (off_t)Offset, SEEK_SET);
#endif
}

static uint64_t
QueryImageSize(FILE* File)
{
#ifdef _WIN32
    if (_fseeki64(File, 0, SEEK_END) != 0)
        return 0;
    return (uint64_t)_ftelli64(File);
#else
    if (fseeko(File, 0, SEEK_END) != 0)
        return 0;
    return (uint64_t)ftello(File);
#endif
}

static int
ResizeImage(FILE* File, uint64_t Size)
{
    if (fflush(File) != 0)
        return -1;
#ifdef _WIN32
    return _chsize_s(_fileno(File), (__int64)Size) == 0 ? 0 : -1;
#else
    return ftruncate(fileno(File), (off_t)Size);
#endif
}

static int
ImageRead(void* Context, uint64_t Offset, uint32_t Length, void* Buffer)
{
    FILE* File = (FILE*)Context;

    return SeekImage(File, Offset) == 0 && fread(Buffer, 1, Length, File) == Length;
}

static int
ImageWrite(void* Context, uint64_t Offset, uint32_t Length, const void* Buffer)
{
    FILE* File = (FILE*)Context;

    return SeekImage(File, Offset) == 0 && fwrite(Buffer, 1, Length, File) == Length;
}

static void
ImageLog(void* Context, const char* Line)
{
    (void)Context;
    fputs(Line, stdout);
}

int
main(int argc, char** argv)
{
    EXPAND_DEVICE Device;
    EXPAND_PLAN Plan;
    uint64_t OldSize, NewSize;
    char* End;
    FILE* File;
    int Status;

    if (argc != 3)
    {
        fprintf(stderr, "Usage: %s <image> <size-MB>\n", argv[0]);
        return 1;
    }

    NewSize = strtoull(argv[2], &End, 10);
    if (*argv[2] == '\0' || *End != '\0' || NewSize == 0)
    {
        fprintf(stderr, "%s: invalid size '%s'\n", argv[0], argv[2]);
        return 1;
    }
    NewSize *= 1024 * 1024;

    File = fopen(argv[1], "r+b");
    if (!File)
    {
        perror(argv[1]);
        return 1;
    }

    OldSize = QueryImageSize(File);
    if (OldSize == 0)
    {
        fprintf(stderr, "%s: cannot query image size\n", argv[1]);
        fclose(File);
        return 1;
    }
    if (NewSize < OldSize)
        NewSize = OldSize;
    if (NewSize > OldSize && ResizeImage(File, NewSize) != 0)
    {
        fprintf(stderr, "%s: cannot extend image to %llu bytes\n", argv[1], (unsigned long long)NewSize);
        fclose(File);
        return 1;
    }

    memset(&Device, 0, sizeof(Device));
    Device.Context = File;
    Device.Read = ImageRead;
    Device.Write = ImageWrite;
    Device.Log = ImageLog;
    Device.DiskSize = NewSize;
    Device.SectorSize = 512;

    Status = ExpandBuildPlan(&Device, 0, 1024 * 1024, 100ULL * 1024 * 1024, &Plan);
    if (Status == EXPAND_OK)
        Status = ExpandApplyPlan(&Device, &Plan);
    if (fclose(File) != 0 && Status == EXPAND_OK)
        Status = EXPAND_IO_ERROR;

    if (Status == EXPAND_NOTHING_TO_DO)
    {
        printf("%s: %s\n", argv[1], ExpandStatusText(Status));
        return 0;
    }
    if (Status != EXPAND_OK)
    {
        fprintf(stderr, "%s: %s\n", argv[1], ExpandStatusText(Status));
        return 1;
    }

    printf("%s: partition %u grown %llu -> %llu bytes, clusters %llu -> %llu\n",
           argv[1],
           Plan.PartitionNumber,
           (unsigned long long)Plan.OldPartitionBytes,
           (unsigned long long)Plan.NewPartitionBytes,
           (unsigned long long)Plan.OldClusters,
           (unsigned long long)Plan.NewClusters);
    return 0;
}
