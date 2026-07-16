/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS kernel
 * FILE:            lib/rossym/frommem.c
 * PURPOSE:         Creating rossym info from an in-memory image
 *
 * PROGRAMMERS:     Ge van Geldorp (gvg@reactos.com)
 */

#include <ntdef.h>
#include <reactos/rossym.h>
#include "rossympriv.h"

#define NDEBUG
#include <debug.h>

BOOLEAN
RosSymCreateFromRaw(PVOID RawData, ULONG_PTR DataSize, PROSSYM_INFO *RosSymInfo)
{
  ROSSYM_HEADER RosSymHeader;
  ULONG_PTR SymbolsEnd, StringsEnd, AllocationSize;
  ULONG Index;

  if (RosSymInfo == NULL)
    {
      return FALSE;
    }
  *RosSymInfo = NULL;

  if (RawData == NULL || DataSize < sizeof(ROSSYM_HEADER))
    {
      DPRINT1("Invalid ROSSYM_HEADER\n");
      return FALSE;
    }

  memcpy(&RosSymHeader, RawData, sizeof(RosSymHeader));
  SymbolsEnd = (ULONG_PTR)RosSymHeader.SymbolsOffset + RosSymHeader.SymbolsLength;
  StringsEnd = (ULONG_PTR)RosSymHeader.StringsOffset + RosSymHeader.StringsLength;
  if (RosSymHeader.SymbolsOffset < sizeof(ROSSYM_HEADER)
      || SymbolsEnd > DataSize
      || RosSymHeader.StringsOffset < SymbolsEnd
      || StringsEnd > DataSize
      || 0 != (RosSymHeader.SymbolsLength % sizeof(ROSSYM_ENTRY))
      || (RosSymHeader.SymbolsLength != 0 && RosSymHeader.StringsLength == 0))
    {
      DPRINT1("Invalid ROSSYM_HEADER\n");
      return FALSE;
    }

  AllocationSize = sizeof(ROSSYM_INFO);
  if (RosSymHeader.SymbolsLength > (ULONG_PTR)-1 - AllocationSize)
    {
      DPRINT1("ROSSYM data is too large\n");
      return FALSE;
    }
  AllocationSize += RosSymHeader.SymbolsLength;
  if (RosSymHeader.StringsLength >= (ULONG_PTR)-1 - AllocationSize)
    {
      DPRINT1("ROSSYM data is too large\n");
      return FALSE;
    }
  AllocationSize += RosSymHeader.StringsLength + 1;

  /* Copy */
  *RosSymInfo = RosSymAllocMem(AllocationSize);
  if (NULL == *RosSymInfo)
    {
      DPRINT1("Failed to allocate memory for rossym\n");
      return FALSE;
    }
  (*RosSymInfo)->Symbols = (PROSSYM_ENTRY)((char *) *RosSymInfo + sizeof(ROSSYM_INFO));
  (*RosSymInfo)->SymbolsCount = RosSymHeader.SymbolsLength / sizeof(ROSSYM_ENTRY);
  (*RosSymInfo)->Strings = (PCHAR) *RosSymInfo + sizeof(ROSSYM_INFO) + RosSymHeader.SymbolsLength;
  (*RosSymInfo)->StringsLength = RosSymHeader.StringsLength;
  memcpy((*RosSymInfo)->Symbols, (char *) RawData + RosSymHeader.SymbolsOffset,
         RosSymHeader.SymbolsLength);
  memcpy((*RosSymInfo)->Strings, (char *) RawData + RosSymHeader.StringsOffset,
         RosSymHeader.StringsLength);
  /* Make sure the last string is null terminated, we allocated an extra byte for that */
  (*RosSymInfo)->Strings[(*RosSymInfo)->StringsLength] = '\0';

  for (Index = 0; Index < (*RosSymInfo)->SymbolsCount; Index++)
    {
      PROSSYM_ENTRY Entry = &(*RosSymInfo)->Symbols[Index];

      if ((Index != 0 && Entry->Address < Entry[-1].Address)
          || Entry->FunctionOffset >= (*RosSymInfo)->StringsLength
          || Entry->FileOffset >= (*RosSymInfo)->StringsLength
          || memchr((*RosSymInfo)->Strings + Entry->FunctionOffset,
                    '\0',
                    (*RosSymInfo)->StringsLength - Entry->FunctionOffset) == NULL
          || memchr((*RosSymInfo)->Strings + Entry->FileOffset,
                    '\0',
                    (*RosSymInfo)->StringsLength - Entry->FileOffset) == NULL)
        {
          DPRINT1("Invalid ROSSYM entry\n");
          RosSymFreeMem(*RosSymInfo);
          *RosSymInfo = NULL;
          return FALSE;
        }
    }

  return TRUE;
}

/* EOF */
