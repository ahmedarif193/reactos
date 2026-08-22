/*
 * COPYRIGHT:         See COPYING in the top level directory
 * PROJECT:           ReactOS kernel
 * PURPOSE:           GDI Driver Memory Management Functions
 * FILE:              win32ss/gdi/eng/mem.c
 * PROGRAMER:         Jason Filby
 */

#include <win32k.h>

#define NDEBUG
#include <debug.h>

/*
 * @implemented
 */
_Must_inspect_result_
_When_(fl & FL_ZERO_MEMORY, _Ret_opt_bytecount_(cjMemSize))
_When_(!(fl & FL_ZERO_MEMORY), _Ret_opt_bytecap_(cjMemSize))
__drv_allocatesMem(Mem)
ENGAPI
PVOID
APIENTRY
EngAllocMem(
    _In_ ULONG fl,
    _In_ ULONG cjMemSize,
    _In_ ULONG ulTag)
{
    PVOID pvBaseAddress;

    pvBaseAddress = ExAllocatePoolWithTag((fl & FL_NONPAGED_MEMORY) ?
                                                    NonPagedPool : PagedPool,
                                          cjMemSize,
                                          ulTag);

    if (pvBaseAddress == NULL)
        return NULL;

    if (fl & FL_ZERO_MEMORY)
        RtlZeroMemory(pvBaseAddress, cjMemSize);

    return pvBaseAddress;
}

/*
 * @implemented
 */
VOID
APIENTRY
EngFreeMem(PVOID pvBaseAddress)
{
    /* Windows allows to pass NULL */
    if (pvBaseAddress)
    {
        /* Use 0 as tag, which equals a call to ExFreePool */
        ExFreePoolWithTag(pvBaseAddress, 0);
    }
}

/*
 * @implemented
 */
_Must_inspect_result_
_Ret_opt_bytecount_(cjMemSize)
__drv_allocatesMem(UserMem)
ENGAPI
PVOID
APIENTRY
EngAllocUserMem(
    _In_ SIZE_T cjMemSize,
    _In_ ULONG ulTag)
{
    PVOID pvBaseAddress = NULL;
    NTSTATUS Status;

    Status = ZwAllocateVirtualMemory(NtCurrentProcess(),
                                     &pvBaseAddress,
                                     0,
                                     &cjMemSize,
                                     MEM_COMMIT | MEM_RESERVE,
                                     PAGE_READWRITE);

    if (!NT_SUCCESS(Status))
    {
        return NULL;
    }

    /* TODO: Add allocation info to AVL tree (stored inside W32PROCESS structure) */
    //hSecure = EngSecureMem(pvBaseAddress, cj);

    return pvBaseAddress;
}

/*
 * @implemented
 */
VOID
APIENTRY
EngFreeUserMem(PVOID pvBaseAddress)
{
    SIZE_T cjSize = 0;

    ZwFreeVirtualMemory(NtCurrentProcess(),
                        &pvBaseAddress,
                        &cjSize,
                        MEM_RELEASE);

  /* TODO: Remove allocation info from AVL tree */
}

/*
 * @implemented
 */
HANDLE
APIENTRY
EngSecureMem(PVOID Address, ULONG Length)
{
    return MmSecureVirtualMemory(Address, Length, PAGE_READWRITE);
}

HANDLE
APIENTRY
EngSecureMemForRead(PVOID Address, ULONG Length)
{
    return MmSecureVirtualMemory(Address, Length, PAGE_READONLY);
}

/*
 * @implemented
 */
VOID APIENTRY
EngUnsecureMem(HANDLE Mem)
{
    MmUnsecureVirtualMemory((PVOID)Mem);
}

/* EOF */
