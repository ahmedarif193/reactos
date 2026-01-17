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
  PROSSYM_HEADER RosSymHeader;

  RosSymHeader = (PROSSYM_HEADER) RawData;
  /* Validate header consistency (bounds only; entry size checked later) */
  if (RosSymHeader->SymbolsOffset < sizeof(ROSSYM_HEADER)
      || RosSymHeader->StringsOffset < RosSymHeader->SymbolsOffset + RosSymHeader->SymbolsLength
      || DataSize < RosSymHeader->StringsOffset + RosSymHeader->StringsLength)
    {
      DbgPrint("rossym: invalid header symOff=%lu symLen=%lu strOff=%lu strLen=%lu size=%Ix\n",
               RosSymHeader->SymbolsOffset,
               RosSymHeader->SymbolsLength,
               RosSymHeader->StringsOffset,
               RosSymHeader->StringsLength,
               (SIZE_T)DataSize);
      return FALSE;
    }

  /* Copy: handle on-disk entry size differences between 32-bit and 64-bit */
  {
    ULONG SymbolsLength = RosSymHeader->SymbolsLength;
    ULONG StringsLength = RosSymHeader->StringsLength;
    ULONG SymbolsOffset = RosSymHeader->SymbolsOffset;
    ULONG StringsOffset = RosSymHeader->StringsOffset;
    ULONG Count;
    SIZE_T allocSize;

/*
     * 64-bit platforms: ARM64, AMD64, x86_64
     * On-disk entries are packed to 4-byte alignment (8+4+4+4 = 20 bytes).
     * Runtime ROSSYM_ENTRY on 64-bit is 24 bytes due to ULONG_PTR alignment.
     * Prefer the raw format first to avoid ambiguity when SymbolsLength is
     * divisible by both 20 and sizeof(ROSSYM_ENTRY).
     */
#if defined(_WIN64) || defined(_M_AMD64) || defined(__x86_64__) || defined(_M_ARM64) || defined(__aarch64__)
    const ULONG rawEntrySize = 8 + 4 + 4 + 4;  /* 20 bytes on-disk format */
    if (SymbolsLength % rawEntrySize == 0)
    {
        Count = SymbolsLength / rawEntrySize;
    }
    else if (SymbolsLength % sizeof(ROSSYM_ENTRY) == 0)
    {
        Count = SymbolsLength / sizeof(ROSSYM_ENTRY);
    }
    else
    {
        DbgPrint("rossym: unexpected symbols length %lu (rawEntry=%lu, rossym=%lu)\n",
                 SymbolsLength,
                 rawEntrySize,
                 (ULONG)sizeof(ROSSYM_ENTRY));
        return FALSE;
    }
#else
    const ULONG rawEntrySize = sizeof(ROSSYM_ENTRY);
    Count = SymbolsLength / rawEntrySize;
#endif

    allocSize = sizeof(ROSSYM_INFO) + ((SIZE_T)Count * sizeof(ROSSYM_ENTRY)) + (SIZE_T)StringsLength + 1;
    *RosSymInfo = RosSymAllocMem(allocSize);
    if (NULL == *RosSymInfo)
    {
      DbgPrint("rossym: allocation failure size=%Ix entries=%lu strings=%lu\n",
               allocSize,
               Count,
               StringsLength);
      return FALSE;
    }

    (*RosSymInfo)->Symbols = (PROSSYM_ENTRY)((char *) *RosSymInfo + sizeof(ROSSYM_INFO));
    (*RosSymInfo)->SymbolsCount = Count;
    (*RosSymInfo)->Strings = (PCHAR) *RosSymInfo + sizeof(ROSSYM_INFO) + (Count * sizeof(ROSSYM_ENTRY));
    (*RosSymInfo)->StringsLength = StringsLength;

#if defined(_WIN64) || defined(_M_AMD64) || defined(__x86_64__) || defined(_M_ARM64) || defined(__aarch64__)
    if (SymbolsLength % rawEntrySize == 0)
    {
        /* Expand from 20-byte raw entries into 24-byte runtime entries */
        const unsigned char *src = (const unsigned char *)RosSymHeader + SymbolsOffset;
        PROSSYM_ENTRY dst = (*RosSymInfo)->Symbols;
        ULONG i;
        for (i = 0; i < Count; ++i)
        {
            /* Layout: [0..7]=Address(ULONGLONG), [8..11]=FunctionOffset, [12..15]=FileOffset, [16..19]=SourceLine */
            ULONGLONG addr;
            ULONG func, file, line;
            memcpy(&addr, src + 0, 8);
            memcpy(&func, src + 8, 4);
            memcpy(&file, src + 12, 4);
            memcpy(&line, src + 16, 4);
            dst[i].Address = (ULONG_PTR)addr;
            dst[i].FunctionOffset = func;
            dst[i].FileOffset = file;
            dst[i].SourceLine = line;
            src += rawEntrySize;
        }
    }
    else if (SymbolsLength % sizeof(ROSSYM_ENTRY) == 0)
    {
        /* Direct copy */
        memcpy((*RosSymInfo)->Symbols, (char *) RosSymHeader + SymbolsOffset, SymbolsLength);
    }
#else
    memcpy((*RosSymInfo)->Symbols, (char *) RosSymHeader + SymbolsOffset, SymbolsLength);
#endif

    memcpy((*RosSymInfo)->Strings, (char *) RosSymHeader + StringsOffset, StringsLength);
  }
  /* Make sure the last string is null terminated, we allocated an extra byte for that */
  (*RosSymInfo)->Strings[(*RosSymInfo)->StringsLength] = '\0';

  return TRUE;
}

/* EOF */
