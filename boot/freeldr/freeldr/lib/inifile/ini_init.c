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
#ifdef UEFIBOOT
#include <uefildr.h>
#endif

#include <debug.h>
DBG_DEFAULT_CHANNEL(INIFILE);

BOOLEAN IniFileInitialize(VOID)
{
    FILEINFORMATION FileInformation;
    ULONG FileId; // File handle for freeldr.ini
    PCHAR FreeLoaderIniFileData;
    ULONG FreeLoaderIniFileSize, Count;
    ARC_STATUS Status;
    BOOLEAN Success;

    TRACE("IniFileInitialize()\n");

#ifdef UEFIBOOT
    {
        extern EFI_SYSTEM_TABLE *GlobalSystemTable;
        WCHAR BootPathW[MAX_PATH];
        ULONG i;
        for (i = 0; i < MAX_PATH && FrLdrBootPath[i]; i++)
            BootPathW[i] = FrLdrBootPath[i];
        BootPathW[i] = 0;
        GlobalSystemTable->ConOut->OutputString(GlobalSystemTable->ConOut, L"IniFileInitialize: Boot path: ");
        GlobalSystemTable->ConOut->OutputString(GlobalSystemTable->ConOut, BootPathW);
        GlobalSystemTable->ConOut->OutputString(GlobalSystemTable->ConOut, L"\r\n");
    }
#endif

    /* Try to open freeldr.ini */
#ifdef UEFIBOOT
    GlobalSystemTable->ConOut->OutputString(GlobalSystemTable->ConOut, L"IniFileInitialize: About to open freeldr.ini\r\n");
#endif
    Status = FsOpenFile("freeldr.ini", FrLdrBootPath, OpenReadOnly, &FileId);
#ifdef UEFIBOOT
    {
        WCHAR StatusStr[20];
        ULONG val = Status;
        ULONG i = 0;
        if (val == 0) {
            StatusStr[i++] = L'0';
        } else {
            while (val > 0 && i < 19) {
                StatusStr[i++] = L'0' + (val % 10);
                val /= 10;
            }
        }
        StatusStr[i] = 0;
        GlobalSystemTable->ConOut->OutputString(GlobalSystemTable->ConOut, L"IniFileInitialize: FsOpenFile returned status: ");
        GlobalSystemTable->ConOut->OutputString(GlobalSystemTable->ConOut, StatusStr);
        GlobalSystemTable->ConOut->OutputString(GlobalSystemTable->ConOut, L"\r\n");
    }
#endif
    if (Status != ESUCCESS)
    {
        ERR("Error while opening freeldr.ini, Status: %d\n", Status);
#ifdef UEFIBOOT
        {
            extern EFI_SYSTEM_TABLE *GlobalSystemTable;
            GlobalSystemTable->ConOut->OutputString(GlobalSystemTable->ConOut, L"IniFileInitialize: Failed to open freeldr.ini\r\n");
        }
#endif

        /* Try to open boot.ini */
        Status = FsOpenFile("boot.ini", FrLdrBootPath, OpenReadOnly, &FileId);
        if (Status != ESUCCESS)
        {
            ERR("Error while opening boot.ini, Status: %d\n", Status);
#ifdef UEFIBOOT
            {
                extern EFI_SYSTEM_TABLE *GlobalSystemTable;
                GlobalSystemTable->ConOut->OutputString(GlobalSystemTable->ConOut, L"IniFileInitialize: Failed to open boot.ini too\r\n");
            }
#endif
            UiMessageBoxCritical("Error opening freeldr.ini/boot.ini or file not found.\nYou need to re-install FreeLoader.");
            return FALSE;
        }
    }

    /* Get the file size */
#ifdef UEFIBOOT
    GlobalSystemTable->ConOut->OutputString(GlobalSystemTable->ConOut, L"IniFileInitialize: Getting file information\r\n");
#endif
    Status = ArcGetFileInformation(FileId, &FileInformation);
#ifdef UEFIBOOT
    GlobalSystemTable->ConOut->OutputString(GlobalSystemTable->ConOut, L"IniFileInitialize: ArcGetFileInformation done\r\n");
#endif
    if (Status != ESUCCESS || FileInformation.EndingAddress.HighPart != 0)
    {
        UiMessageBoxCritical("Error while getting informations about freeldr.ini.\nYou need to re-install FreeLoader.");
        ArcClose(FileId);
        return FALSE;
    }
    FreeLoaderIniFileSize = FileInformation.EndingAddress.LowPart;
#ifdef UEFIBOOT
    {
        WCHAR SizeStr[20];
        ULONG val = FreeLoaderIniFileSize;
        ULONG i = 0;
        if (val == 0) {
            SizeStr[i++] = L'0';
        } else {
            while (val > 0 && i < 19) {
                SizeStr[i++] = L'0' + (val % 10);
                val /= 10;
            }
        }
        SizeStr[i] = 0;
        GlobalSystemTable->ConOut->OutputString(GlobalSystemTable->ConOut, L"IniFileInitialize: File size: ");
        GlobalSystemTable->ConOut->OutputString(GlobalSystemTable->ConOut, SizeStr);
        GlobalSystemTable->ConOut->OutputString(GlobalSystemTable->ConOut, L"\r\n");
    }
#endif

    /* Allocate memory to cache the whole freeldr.ini */
#ifdef UEFIBOOT
    GlobalSystemTable->ConOut->OutputString(GlobalSystemTable->ConOut, L"IniFileInitialize: Allocating memory\r\n");
#endif
    FreeLoaderIniFileData = FrLdrTempAlloc(FreeLoaderIniFileSize, TAG_INI_FILE);
    if (!FreeLoaderIniFileData)
    {
#ifdef UEFIBOOT
        GlobalSystemTable->ConOut->OutputString(GlobalSystemTable->ConOut, L"IniFileInitialize: Memory allocation failed!\r\n");
#endif
        UiMessageBoxCritical("Out of memory while loading freeldr.ini.");
        ArcClose(FileId);
        return FALSE;
    }
#ifdef UEFIBOOT
    GlobalSystemTable->ConOut->OutputString(GlobalSystemTable->ConOut, L"IniFileInitialize: Memory allocated, reading file\r\n");
#endif

    /* Load freeldr.ini from the disk */
    Status = ArcRead(FileId, FreeLoaderIniFileData, FreeLoaderIniFileSize, &Count);
#ifdef UEFIBOOT
    GlobalSystemTable->ConOut->OutputString(GlobalSystemTable->ConOut, L"IniFileInitialize: ArcRead done\r\n");
#endif
    if (Status != ESUCCESS || Count != FreeLoaderIniFileSize)
    {
        ERR("Error while reading freeldr.ini, Status: %d\n", Status);
        UiMessageBoxCritical("Error while reading freeldr.ini.");
        ArcClose(FileId);
        FrLdrTempFree(FreeLoaderIniFileData, TAG_INI_FILE);
        return FALSE;
    }

    /* Parse the .ini file data */
#ifdef UEFIBOOT
    GlobalSystemTable->ConOut->OutputString(GlobalSystemTable->ConOut, L"IniFileInitialize: Parsing ini file\r\n");
#endif
    Success = IniParseFile(FreeLoaderIniFileData, FreeLoaderIniFileSize);
#ifdef UEFIBOOT
    GlobalSystemTable->ConOut->OutputString(GlobalSystemTable->ConOut, L"IniFileInitialize: IniParseFile done\r\n");
#endif

    /* Do some cleanup, and return */
#ifdef UEFIBOOT
    GlobalSystemTable->ConOut->OutputString(GlobalSystemTable->ConOut, L"IniFileInitialize: Closing file\r\n");
#endif
    ArcClose(FileId);
#ifdef UEFIBOOT
    GlobalSystemTable->ConOut->OutputString(GlobalSystemTable->ConOut, L"IniFileInitialize: Freeing memory\r\n");
#endif
    FrLdrTempFree(FreeLoaderIniFileData, TAG_INI_FILE);
#ifdef UEFIBOOT
    GlobalSystemTable->ConOut->OutputString(GlobalSystemTable->ConOut, L"IniFileInitialize: Done\r\n");
#endif

    return Success;
}
