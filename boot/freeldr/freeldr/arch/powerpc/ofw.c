/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Open Firmware client interface for the little-endian loader
 */

#include <freeldr.h>

#include <debug.h>
DBG_DEFAULT_CHANNEL(HWDETECT);

PPPC_STAGE0_INFO OfwStage0Info;
OFW_HANDLE OfwStdin;
OFW_HANDLE OfwStdout;
BOOLEAN OfwAvailable;

/* Client interface cells are big-endian; strings and buffers are bytes. */
#define OFW_MAX_CELLS 16

LONG
OfwCall(
    _In_z_ PCSTR Service,
    _In_ ULONG ArgCount,
    _In_ ULONG RetCount,
    _Inout_ PULONG Cells)
{
    ULONG Args[3 + OFW_MAX_CELLS];
    ULONG i;
    LONG Result;

    if (!OfwAvailable || (ArgCount + RetCount > OFW_MAX_CELLS))
        return -1;

    Args[0] = _byteswap_ulong((ULONG)(ULONG_PTR)Service);
    Args[1] = _byteswap_ulong(ArgCount);
    Args[2] = _byteswap_ulong(RetCount);
    for (i = 0; i < ArgCount; i++)
        Args[3 + i] = _byteswap_ulong(Cells[i]);
    for (i = 0; i < RetCount; i++)
        Args[3 + ArgCount + i] = 0;

    Result = OfwGate(Args, OfwStage0Info->BeGate, OfwStage0Info->OfEntry);

    for (i = 0; i < RetCount; i++)
        Cells[ArgCount + i] = _byteswap_ulong(Args[3 + ArgCount + i]);

    return Result;
}

OFW_HANDLE
OfwFindDevice(_In_z_ PCSTR Path)
{
    ULONG Cells[2] = { (ULONG)(ULONG_PTR)Path, 0 };

    if (OfwCall("finddevice", 1, 1, Cells) != 0 || Cells[1] == (ULONG)-1)
        return OFW_INVALID;
    return Cells[1];
}

OFW_HANDLE
OfwPeer(_In_ OFW_HANDLE Phandle)
{
    ULONG Cells[2] = { Phandle, 0 };

    if (OfwCall("peer", 1, 1, Cells) != 0 || Cells[1] == (ULONG)-1)
        return OFW_INVALID;
    return Cells[1];
}

OFW_HANDLE
OfwChild(_In_ OFW_HANDLE Phandle)
{
    ULONG Cells[2] = { Phandle, 0 };

    if (OfwCall("child", 1, 1, Cells) != 0 || Cells[1] == (ULONG)-1)
        return OFW_INVALID;
    return Cells[1];
}

LONG
OfwGetPropLen(_In_ OFW_HANDLE Phandle, _In_z_ PCSTR Name)
{
    ULONG Cells[3] = { Phandle, (ULONG)(ULONG_PTR)Name, 0 };

    if (OfwCall("getproplen", 2, 1, Cells) != 0)
        return -1;
    return (LONG)Cells[2];
}

LONG
OfwGetProp(
    _In_ OFW_HANDLE Phandle,
    _In_z_ PCSTR Name,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length)
{
    ULONG Cells[5] = { Phandle, (ULONG)(ULONG_PTR)Name, (ULONG)(ULONG_PTR)Buffer, Length, 0 };

    if (OfwCall("getprop", 4, 1, Cells) != 0)
        return -1;
    return (LONG)Cells[4];
}

BOOLEAN
OfwGetPropCell(_In_ OFW_HANDLE Phandle, _In_z_ PCSTR Name, _Out_ PULONG Value)
{
    ULONG Cell;

    if (OfwGetProp(Phandle, Name, &Cell, sizeof(Cell)) != sizeof(Cell))
        return FALSE;
    *Value = OfwCellToHost(Cell);
    return TRUE;
}

LONG
OfwPackageToPath(_In_ OFW_HANDLE Phandle, _Out_writes_(Length) PCHAR Buffer, _In_ ULONG Length)
{
    ULONG Cells[4] = { Phandle, (ULONG)(ULONG_PTR)Buffer, Length - 1, 0 };
    LONG Result;

    if (OfwCall("package-to-path", 3, 1, Cells) != 0)
        return -1;
    Result = (LONG)Cells[3];
    if (Result < 0)
        return -1;
    if ((ULONG)Result >= Length)
        Result = Length - 1;
    Buffer[Result] = ANSI_NULL;
    return Result;
}

OFW_HANDLE
OfwOpen(_In_z_ PCSTR Path)
{
    ULONG Cells[2] = { (ULONG)(ULONG_PTR)Path, 0 };

    if (OfwCall("open", 1, 1, Cells) != 0 || Cells[1] == (ULONG)-1)
        return OFW_INVALID;
    return Cells[1];
}

VOID
OfwClose(_In_ OFW_HANDLE Ihandle)
{
    ULONG Cells[1] = { Ihandle };

    OfwCall("close", 1, 0, Cells);
}

LONG
OfwRead(_In_ OFW_HANDLE Ihandle, _Out_writes_bytes_(Length) PVOID Buffer, _In_ ULONG Length)
{
    ULONG Cells[4] = { Ihandle, (ULONG)(ULONG_PTR)Buffer, Length, 0 };

    if (OfwCall("read", 3, 1, Cells) != 0)
        return -1;
    return (LONG)Cells[3];
}

LONG
OfwWrite(_In_ OFW_HANDLE Ihandle, _In_reads_bytes_(Length) const VOID *Buffer, _In_ ULONG Length)
{
    ULONG Cells[4] = { Ihandle, (ULONG)(ULONG_PTR)Buffer, Length, 0 };

    if (OfwCall("write", 3, 1, Cells) != 0)
        return -1;
    return (LONG)Cells[3];
}

LONG
OfwSeek(_In_ OFW_HANDLE Ihandle, _In_ ULONGLONG Position)
{
    ULONG Cells[4] = { Ihandle, (ULONG)(Position >> 32), (ULONG)Position, 0 };

    if (OfwCall("seek", 3, 1, Cells) != 0)
        return -1;
    return (LONG)Cells[3];
}

ULONG
OfwClaim(_In_ ULONG Virt, _In_ ULONG Size, _In_ ULONG Align)
{
    ULONG Cells[4] = { Virt, Size, Align, 0 };

    if (OfwCall("claim", 3, 1, Cells) != 0)
        return (ULONG)-1;
    return Cells[3];
}

ULONG
OfwMilliseconds(VOID)
{
    ULONG Cells[1] = { 0 };

    if (OfwCall("milliseconds", 0, 1, Cells) != 0)
        return 0;
    return Cells[0];
}

/* call-method with one result: ( method ihandle -- catch-result result ) */
LONG
OfwCallMethod1(_In_z_ PCSTR Method, _In_ OFW_HANDLE Ihandle, _Out_ PULONG Result)
{
    ULONG Cells[4] = { (ULONG)(ULONG_PTR)Method, Ihandle, 0, 0 };

    if (OfwCall("call-method", 2, 2, Cells) != 0 || Cells[2] != 0)
        return -1;
    *Result = Cells[3];
    return 0;
}

DECLSPEC_NORETURN
VOID
OfwExit(VOID)
{
    ULONG Cells[1];

    OfwCall("exit", 0, 0, Cells);
    for (;;)
        NOTHING;
}

BOOLEAN
OfwInitialize(_In_ PPPC_STAGE0_INFO Info)
{
    OFW_HANDLE Chosen;

    if (!Info || Info->Magic != PPC_STAGE0_MAGIC || Info->Version != PPC_STAGE0_VERSION)
        return FALSE;

    OfwStage0Info = Info;
    OfwAvailable = TRUE;

    Chosen = OfwFindDevice("/chosen");
    if (Chosen == OFW_INVALID)
        return FALSE;

    OfwGetPropCell(Chosen, "stdout", &OfwStdout);
    OfwGetPropCell(Chosen, "stdin", &OfwStdin);
    return TRUE;
}
