/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS kernel
 * FILE:            lib/rossym/fromfile.c
 * PURPOSE:         Creating rossym info from a file
 *
 * PROGRAMMERS:     Ge van Geldorp (gvg@reactos.com)
 */

#include <ntdef.h>
#include <reactos/rossym.h>
#include "rossympriv.h"
#include <ntimage.h>

#define NDEBUG
#include <debug.h>

BOOLEAN
RosSymCreateFromFile(PVOID FileContext, PROSSYM_INFO *RosSymInfo)
{
  IMAGE_DOS_HEADER DosHeader;
  IMAGE_NT_HEADERS NtHeaders;
  PIMAGE_SECTION_HEADER SectionHeaders, SectionHeader;
  unsigned SectionIndex;
  char SectionName[IMAGE_SIZEOF_SHORT_NAME];
  PVOID RawData;
  BOOLEAN Result;

  /* Load DOS header */
  if (! RosSymReadFile(FileContext, &DosHeader, sizeof(IMAGE_DOS_HEADER)))
    {
      DPRINT1("Failed to read DOS header\n");
      return FALSE;
    }
  if (! ROSSYM_IS_VALID_DOS_HEADER(&DosHeader))
    {
      DPRINT1("Image doesn't have a valid DOS header\n");
      return FALSE;
    }

  /* Load NT headers */
  if (! RosSymSeekFile(FileContext, DosHeader.e_lfanew))
    {
      DPRINT1("Failed seeking to NT headers\n");
      return FALSE;
    }
  if (! RosSymReadFile(FileContext, &NtHeaders, sizeof(IMAGE_NT_HEADERS)))
    {
      DPRINT1("Failed to read NT headers\n");
      return FALSE;
    }
  if (! ROSSYM_IS_VALID_NT_HEADERS(&NtHeaders))
    {
      DPRINT1("Image doesn't have a valid PE header\n");
      return FALSE;
    }

  /* Load section headers */
  if (! RosSymSeekFile(FileContext, (char *) IMAGE_FIRST_SECTION(&NtHeaders) -
                                    (char *) &NtHeaders + DosHeader.e_lfanew))
    {
      DPRINT1("Failed seeking to section headers\n");
      return FALSE;
    }
  SectionHeaders = RosSymAllocMem(NtHeaders.FileHeader.NumberOfSections
                                  * sizeof(IMAGE_SECTION_HEADER));
  if (NULL == SectionHeaders)
    {
      DPRINT1("Failed to allocate memory for %u section headers\n",
              NtHeaders.FileHeader.NumberOfSections);
      return FALSE;
    }
  if (! RosSymReadFile(FileContext, SectionHeaders,
                       NtHeaders.FileHeader.NumberOfSections
                       * sizeof(IMAGE_SECTION_HEADER)))
    {
      RosSymFreeMem(SectionHeaders);
      DPRINT1("Failed to read section headers\n");
      return FALSE;
    }

  /* Search for the section header */
  strncpy(SectionName, ROSSYM_SECTION_NAME, IMAGE_SIZEOF_SHORT_NAME);
  SectionHeader = SectionHeaders;
  for (SectionIndex = 0; SectionIndex < NtHeaders.FileHeader.NumberOfSections; SectionIndex++)
    {
      if (0 == memcmp(SectionName, SectionHeader->Name, IMAGE_SIZEOF_SHORT_NAME))
        {
          break;
        }
      SectionHeader++;
    }
  if (NtHeaders.FileHeader.NumberOfSections <= SectionIndex)
    {
      RosSymFreeMem(SectionHeaders);
      DPRINT("No %s section found\n", ROSSYM_SECTION_NAME);
      return FALSE;
    }

  if (SectionHeader->SizeOfRawData < sizeof(ROSSYM_HEADER))
    {
      RosSymFreeMem(SectionHeaders);
      DPRINT1("Invalid %s section\n", ROSSYM_SECTION_NAME);
      return FALSE;
    }

  RawData = RosSymAllocMem(SectionHeader->SizeOfRawData);
  if (RawData == NULL)
    {
      RosSymFreeMem(SectionHeaders);
      DPRINT1("Failed to allocate memory for %s section\n", ROSSYM_SECTION_NAME);
      return FALSE;
    }

  /* Load the raw section and use the common validated decoder. */
  if (! RosSymSeekFile(FileContext, SectionHeader->PointerToRawData))
    {
      RosSymFreeMem(RawData);
      RosSymFreeMem(SectionHeaders);
      DPRINT1("Failed seeking to section data\n");
      return FALSE;
    }
  if (! RosSymReadFile(FileContext, RawData, SectionHeader->SizeOfRawData))
    {
      RosSymFreeMem(RawData);
      RosSymFreeMem(SectionHeaders);
      DPRINT1("Failed to read %s section\n", ROSSYM_SECTION_NAME);
      return FALSE;
    }

  Result = RosSymCreateFromRaw(RawData, SectionHeader->SizeOfRawData, RosSymInfo);
  RosSymFreeMem(RawData);
  RosSymFreeMem(SectionHeaders);
  return Result;
}

/* EOF */
