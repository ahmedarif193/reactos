/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS kernel
 * FILE:            lib/rossym/zwfile.c
 * PURPOSE:         File I/O using native functions
 *
 * PROGRAMMERS:     Ge van Geldorp (gvg@reactos.com)
 */

#include <precomp.h>

#define NDEBUG
#include <debug.h>

NTSTATUS RosSymStatus;

BOOLEAN
RosSymZwReadFile(PVOID FileContext, PVOID Buffer, ULONG Size)
{
  IO_STATUS_BLOCK IoStatusBlock;

  DPRINT("RosSymZwReadFile: Reading %lu bytes\n", Size);
  RosSymStatus = ZwReadFile(*((HANDLE *) FileContext),
                      NULL, NULL, NULL,
                      &IoStatusBlock,
                      Buffer,
                      Size,
                      NULL, NULL);
  DPRINT("RosSymZwReadFile: ZwReadFile returned 0x%08x, read %lu bytes\n", RosSymStatus, (ULONG)IoStatusBlock.Information);

  return NT_SUCCESS(RosSymStatus) && IoStatusBlock.Information == Size;
}

BOOLEAN
RosSymZwSeekFile(PVOID FileContext, ULONG_PTR Position)
{
  IO_STATUS_BLOCK IoStatusBlock;
  FILE_POSITION_INFORMATION NewPosition;

  DPRINT("RosSymZwSeekFile: Seeking to position %lu\n", (ULONG)Position);
  NewPosition.CurrentByteOffset.u.HighPart = 0;
  NewPosition.CurrentByteOffset.u.LowPart = Position;
  RosSymStatus = ZwSetInformationFile(*((HANDLE *) FileContext),
                                &IoStatusBlock,
                                (PVOID) &NewPosition,
                                sizeof(FILE_POSITION_INFORMATION),
                                FilePositionInformation);
  DPRINT("RosSymZwSeekFile: ZwSetInformationFile returned 0x%08x\n", RosSymStatus);

  return NT_SUCCESS(RosSymStatus);
}

/* EOF */
