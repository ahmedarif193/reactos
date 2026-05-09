/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS kernel
 * FILE:            lib/rossym/zwfile.c
 * PURPOSE:         File I/O using native functions
 *
 * PROGRAMMERS:     Ge van Geldorp (gvg@reactos.com)
 */

#include <wdm.h>
#include <reactos/rossym.h>
#include "rossympriv.h"

#undef NDEBUG
#include <debug.h>

BOOLEAN
RosSymZwReadFile(PVOID FileContext, PVOID Buffer, ULONG Size)
{
  NTSTATUS Status;
  IO_STATUS_BLOCK IoStatusBlock;

  DPRINT1("RosSymZwReadFile: Reading %lu bytes\n", Size);
  Status = ZwReadFile(*((HANDLE *) FileContext),
                      NULL, NULL, NULL,
                      &IoStatusBlock,
                      Buffer,
                      Size,
                      NULL, NULL);
  DPRINT1("RosSymZwReadFile: ZwReadFile returned 0x%08x, read %lu bytes\n", Status, (ULONG)IoStatusBlock.Information);

  return NT_SUCCESS(Status) && IoStatusBlock.Information == Size;
}

BOOLEAN
RosSymZwSeekFile(PVOID FileContext, ULONG_PTR Position)
{
  NTSTATUS Status;
  IO_STATUS_BLOCK IoStatusBlock;
  FILE_POSITION_INFORMATION NewPosition;

  DPRINT1("RosSymZwSeekFile: Seeking to position %lu\n", (ULONG)Position);
  NewPosition.CurrentByteOffset.u.HighPart = 0;
  NewPosition.CurrentByteOffset.u.LowPart = Position;
  Status = ZwSetInformationFile(*((HANDLE *) FileContext),
                                &IoStatusBlock,
                                (PVOID) &NewPosition,
                                sizeof(FILE_POSITION_INFORMATION),
                                FilePositionInformation);
  DPRINT1("RosSymZwSeekFile: ZwSetInformationFile returned 0x%08x\n", Status);

  return NT_SUCCESS(Status);
}
