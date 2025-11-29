/*
 *  FreeLoader
 *  Copyright (C) 2009     Hervé Poussineau  <hpoussin@reactos.org>
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
DBG_DEFAULT_CHANNEL(INIFILE);

#ifdef UEFIBOOT
#include <uefildr.h>
#include <arch/uefi/machuefi.h>
#endif // UEFIBOOT

#ifndef PARTITION_KEEP
#define PARTITION_KEEP ((ULONG)-1)
#endif

#ifdef UEFIBOOT
static
BOOLEAN
IniExtractRdiskIndex(
    _Out_ PULONG DiskIndex)
{
    static const CHAR RDiskToken[] = "rdisk(";
    PCSTR Token;
    ULONG Value = 0;

    Token = strstr(FrLdrBootPath, RDiskToken);
    if (!Token)
        return FALSE;

    Token += strlen(RDiskToken);
    if (!isdigit((unsigned char)*Token))
        return FALSE;

    while (isdigit((unsigned char)*Token))
    {
        Value = (Value * 10) + (*Token - '0');
        ++Token;
    }

    if (*Token != ')')
        return FALSE;

    *DiskIndex = Value;
    return TRUE;
}

static
BOOLEAN
IniBuildDiskLevelBootPath(
    _Out_writes_z_(BufferSize) PCHAR Buffer,
    _In_ SIZE_T BufferSize)
{
    static const CHAR PartitionToken[] = "partition(";
    NTSTATUS Status;
    PCHAR Token;
    PCHAR Digits;
    PCHAR Closing;

    Status = RtlStringCbCopyA(Buffer, BufferSize, FrLdrBootPath);
    if (!NT_SUCCESS(Status))
        return FALSE;

    Token = strstr(Buffer, PartitionToken);
    if (!Token)
        return FALSE;

    Digits = Token + strlen(PartitionToken);
    Closing = strchr(Digits, ')');
    if (!Closing)
        return FALSE;

    if (Digits[0] == '0' && Digits + 1 == Closing)
    {
        /* Already pointing to partition(0); nothing to do. */
        return FALSE;
    }

    Digits[0] = '0';
    memmove(Digits + 1, Closing, strlen(Closing) + 1);
    return TRUE;
}
#endif // UEFIBOOT

static
ARC_STATUS
IniTryOpenFreeldrIni(
    _In_ PCSTR DevicePath,
    _In_ ULONG PartitionOverride,
    _Out_ PULONG FileId)
{
    ARC_STATUS Status;

    Status = FsOpenFile("freeldr.ini", DevicePath, OpenReadOnly, FileId);
    if (Status == ESUCCESS)
    {
        if (DevicePath != FrLdrBootPath)
        {
            RtlStringCbCopyA(FrLdrBootPath, sizeof(FrLdrBootPath), DevicePath);
        }

        if (PartitionOverride != PARTITION_KEEP)
        {
            FrldrBootPartition = PartitionOverride;
        }
    }

    return Status;
}

BOOLEAN IniFileInitialize(VOID)
{
    FILEINFORMATION FileInformation;
    ULONG FileId; // File handle for freeldr.ini
    PCHAR FreeLoaderIniFileData;
    ULONG FreeLoaderIniFileSize, Count;
    ARC_STATUS Status;
    BOOLEAN Success;

    TRACE("IniFileInitialize()\n");

    /* Try to open freeldr.ini */
    Status = IniTryOpenFreeldrIni(FrLdrBootPath, PARTITION_KEEP, &FileId);
    if (Status != ESUCCESS)
    {
        CHAR FallbackPath[MAX_PATH];
        SIZE_T len;
        ERR("Error while opening freeldr.ini, Status: %d\n", Status);

        /*
         * Fallback 1: If the current boot path points to rdisk(>0),
         * retry with rdisk(0) to guard against off-by-one disk index
         * issues on some UEFI firmwares.
         */
        *FallbackPath = '\0';
        len = min(sizeof(FallbackPath)/sizeof(CHAR), strlen(FrLdrBootPath));
        if (len < sizeof(FallbackPath)/sizeof(CHAR))
        {
            RtlStringCbCopyA(FallbackPath, sizeof(FallbackPath), FrLdrBootPath);
            CHAR *p = strstr(FallbackPath, "rdisk(");
            if (p)
            {
                /* Move to the digit and replace any non-zero by '0' */
                p += 6;
                if (*p && *p != '0')
                {
                    *p = '0';
                    TRACE("Retrying freeldr.ini with fallback path: '%s'\n", FallbackPath);
                    Status = IniTryOpenFreeldrIni(FallbackPath, PARTITION_KEEP, &FileId);
                }
            }
        }

        /* Fallback 2: Try a canonical first-disk, first-partition path. */
        if (Status != ESUCCESS)
        {
            static const CHAR CanonicalPath[] = "multi(0)disk(0)rdisk(0)partition(1)";
            TRACE("Retrying freeldr.ini with canonical path: '%s'\n", CanonicalPath);
            Status = IniTryOpenFreeldrIni(CanonicalPath, 1, &FileId);
        }

#ifdef UEFIBOOT
        /*
         * Fallback 3: Some firmware expose the UEFI boot image partition
         * (FAT ESP) instead of the ISO root. If all previous attempts fail,
         * try reopening the raw disk (partition 0) so the ISO filesystem
         * can be detected.
         */
        if (Status != ESUCCESS)
        {
            CHAR DiskPath[MAX_PATH];

            if (IniBuildDiskLevelBootPath(DiskPath, sizeof(DiskPath)))
            {
                TRACE("Retrying freeldr.ini with disk-level path: '%s'\n", DiskPath);
                Status = IniTryOpenFreeldrIni(DiskPath, 0, &FileId);
            }
        }

        if (Status != ESUCCESS)
        {
            CHAR PartitionPath[MAX_PATH];
            ULONG BootDisk;

            if (IniExtractRdiskIndex(&BootDisk))
            {
                for (ULONG Partition = 1; Partition <= 16 && Status != ESUCCESS; ++Partition)
                {
                    if (Partition == FrldrBootPartition)
                        continue;

                    RtlStringCbPrintfA(PartitionPath, sizeof(PartitionPath),
                                       "multi(0)disk(0)rdisk(%lu)partition(%lu)",
                                       BootDisk, Partition);

                    if (!FsIsDeviceRegistered(PartitionPath))
                        continue;

                    TRACE("Retrying freeldr.ini with alternate partition path: '%s'\n",
                          PartitionPath);
                    Status = IniTryOpenFreeldrIni(PartitionPath, Partition, &FileId);
                    if (Status == ESUCCESS)
                        break;
                }
            }
        }

        if (Status != ESUCCESS)
        {
            ULONG CdromCount = UefiGetCdromCount();
            for (ULONG CdIndex = 0; CdIndex < CdromCount && Status != ESUCCESS; ++CdIndex)
            {
                CHAR CdPath[MAX_PATH];

                RtlStringCbPrintfA(CdPath, sizeof(CdPath),
                                   "multi(0)disk(0)cdrom(%lu)", CdIndex);

                if (!FsIsDeviceRegistered(CdPath))
                    continue;

                TRACE("Retrying freeldr.ini with cdrom path: '%s'\n", CdPath);
                Status = IniTryOpenFreeldrIni(CdPath, 0xFF, &FileId);
                if (Status == ESUCCESS)
                    break;
            }
        }
#endif

        if (Status != ESUCCESS)
        {
            /* Try to open boot.ini (legacy) before bailing out */
            ERR("Fallbacks failed for freeldr.ini (Status=%d). Trying boot.ini...\n", Status);
            Status = FsOpenFile("boot.ini", FrLdrBootPath, OpenReadOnly, &FileId);
            if (Status != ESUCCESS)
            {
                ERR("Error while opening boot.ini, Status: %d\n", Status);
                UiMessageBoxCritical("Error opening freeldr.ini/boot.ini or file not found.\nYou need to re-install FreeLoader.");
                return FALSE;
            }
        }
    }

    /* If we opened successfully, normalize FrLdrBootPath to the actual device */
#ifdef UEFIBOOT
    {
        PCCHAR ProvenArc = UefiGetArcPathForFileId(FileId);
        if (ProvenArc && *ProvenArc)
        {
            if (_stricmp(FrLdrBootPath, ProvenArc) != 0)
            {
                TRACE("IniFileInitialize: Using proven boot path from FileId: '%s'\n", ProvenArc);
                RtlStringCbCopyA(FrLdrBootPath, sizeof(FrLdrBootPath), ProvenArc);
            }

            /* Update the partition hint based on the proven ARC path */
            if (strstr(ProvenArc, "cdrom(") != NULL)
            {
                FrldrBootPartition = 0xFF;
            }
            else
            {
                static const CHAR PartitionTok[] = "partition(";
                PCSTR p = strstr(ProvenArc, PartitionTok);
                if (p)
                {
                    ULONG part = 0;
                    p += sizeof(PartitionTok) - 1;
                    while (isdigit((unsigned char)*p))
                    {
                        part = (part * 10) + (*p - '0');
                        ++p;
                    }
                    if (*p == ')')
                        FrldrBootPartition = part;
                }
            }
        }
    }
#endif


    TRACE("IniFileInitialize: final boot path is '%s' (partition=%lu)\n", FrLdrBootPath, (ULONG)FrldrBootPartition);

    /* Get the file size */
    Status = ArcGetFileInformation(FileId, &FileInformation);
    if (Status != ESUCCESS || FileInformation.EndingAddress.HighPart != 0)
    {
        UiMessageBoxCritical("Error while getting informations about freeldr.ini.\nYou need to re-install FreeLoader.");
        ArcClose(FileId);
        return FALSE;
    }
    FreeLoaderIniFileSize = FileInformation.EndingAddress.LowPart;

    /* Allocate memory to cache the whole freeldr.ini */
    FreeLoaderIniFileData = FrLdrTempAlloc(FreeLoaderIniFileSize, TAG_INI_FILE);
    if (!FreeLoaderIniFileData)
    {
        UiMessageBoxCritical("Out of memory while loading freeldr.ini.");
        ArcClose(FileId);
        return FALSE;
    }

    /* Load freeldr.ini from the disk */
    Status = ArcRead(FileId, FreeLoaderIniFileData, FreeLoaderIniFileSize, &Count);
    if (Status != ESUCCESS || Count != FreeLoaderIniFileSize)
    {
        ERR("Error while reading freeldr.ini, Status: %d\n", Status);
        UiMessageBoxCritical("Error while reading freeldr.ini.");
        ArcClose(FileId);
        FrLdrTempFree(FreeLoaderIniFileData, TAG_INI_FILE);
        return FALSE;
    }

    /* Parse the .ini file data */
// #ifdef UEFIBOOT
//     FrLdrUefiEndEarlyLogForwarding();
// #endif
    Success = IniParseFile(FreeLoaderIniFileData, FreeLoaderIniFileSize);

    /* Do some cleanup, and return */
    ArcClose(FileId);
    FrLdrTempFree(FreeLoaderIniFileData, TAG_INI_FILE);

    return Success;
}
