/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF <arif193@gmail.com>
 */
#pragma once

/* Only an entirely validated NOP stream can become a V3D queue marker.
 * Transfers, waits and signals must retain their execution semantics. */
static BOOLEAN
Rpi3Vc4IsOrderedNopStream(const VOID *Buffer, ULONG Length)
{
    const UCHAR *Bytes = Buffer;
    SOFTGPU_CMD Command;
    ULONG Offset;

    if (Buffer == NULL || Length == 0 || Length % sizeof(Command) != 0)
        return FALSE;
    for (Offset = 0; Offset < Length; Offset += sizeof(Command))
    {
        RtlCopyMemory(&Command, Bytes + Offset, sizeof(Command));
        if (Command.Magic != SOFTGPU_CMD_MAGIC ||
            Command.Size != sizeof(Command) ||
            Command.Op != SOFTGPU_CMD_OP_NOP)
            return FALSE;
    }
    return TRUE;
}
