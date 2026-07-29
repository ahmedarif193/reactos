/*
 * Stub INT10 helpers for architectures without BIOS real-mode support.
 */

#include "videoprt.h"

NTSTATUS
NTAPI
IntInitializeVideoAddressSpace(VOID)
{
    /* No BIOS real-mode address space exists on this architecture. */
    return STATUS_SUCCESS;
}

VP_STATUS
NTAPI
IntInt10AllocateBuffer(
   IN PVOID Context,
   OUT PUSHORT Seg,
   OUT PUSHORT Off,
   IN OUT PULONG Length)
{
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(Seg);
    UNREFERENCED_PARAMETER(Off);
    UNREFERENCED_PARAMETER(Length);
    return ERROR_INVALID_FUNCTION;
}

VP_STATUS
NTAPI
IntInt10FreeBuffer(
   IN PVOID Context,
   IN USHORT Seg,
   IN USHORT Off)
{
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(Seg);
    UNREFERENCED_PARAMETER(Off);
    return ERROR_INVALID_FUNCTION;
}

VP_STATUS
NTAPI
IntInt10ReadMemory(
   IN PVOID Context,
   IN USHORT Seg,
   IN USHORT Off,
   OUT PVOID Buffer,
   IN ULONG Length)
{
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(Seg);
    UNREFERENCED_PARAMETER(Off);
    UNREFERENCED_PARAMETER(Buffer);
    UNREFERENCED_PARAMETER(Length);
    return ERROR_INVALID_FUNCTION;
}

VP_STATUS
NTAPI
IntInt10WriteMemory(
   IN PVOID Context,
   IN USHORT Seg,
   IN USHORT Off,
   IN PVOID Buffer,
   IN ULONG Length)
{
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(Seg);
    UNREFERENCED_PARAMETER(Off);
    UNREFERENCED_PARAMETER(Buffer);
    UNREFERENCED_PARAMETER(Length);
    return ERROR_INVALID_FUNCTION;
}

VP_STATUS
NTAPI
IntInt10CallBios(
   IN PVOID Context,
   IN OUT PINT10_BIOS_ARGUMENTS BiosArguments)
{
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(BiosArguments);
    return ERROR_INVALID_FUNCTION;
}
