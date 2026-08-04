/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            ntoskrnl/mm/ARM3/kdbg.c
 * PURPOSE:         ARM Memory Manager Kernel Debugger routines
 * PROGRAMMERS:     ReactOS Portable Systems Group
 *                  Pierre Schweitzer
 */

/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#define MODULE_INVOLVED_IN_ARM3
#include <mm/ARM3/miarm.h>

/* GLOBALS ********************************************************************/

typedef struct _IRP_FIND_CTXT
{
    ULONG_PTR RestartAddress;
    ULONG_PTR SData;
    ULONG Criteria;
} IRP_FIND_CTXT, *PIRP_FIND_CTXT;

extern PVOID MmNonPagedPoolEnd0;
extern SIZE_T PoolBigPageTableSize;
extern PPOOL_TRACKER_BIG_PAGES PoolBigPageTable;

#define POOL_BIG_TABLE_ENTRY_FREE 0x1

/* Pool block/header/list access macros */
#define POOL_ENTRY(x)       (PPOOL_HEADER)((ULONG_PTR)(x) - sizeof(POOL_HEADER))
#define POOL_FREE_BLOCK(x)  (PLIST_ENTRY)((ULONG_PTR)(x)  + sizeof(POOL_HEADER))
#define POOL_BLOCK(x, i)    (PPOOL_HEADER)((ULONG_PTR)(x) + ((i) * POOL_BLOCK_SIZE))
#define POOL_NEXT_BLOCK(x)  POOL_BLOCK((x), (x)->BlockSize)
#define POOL_PREV_BLOCK(x)  POOL_BLOCK((x), -((x)->PreviousSize))

VOID MiDumpPoolConsumers(BOOLEAN CalledFromDbg, ULONG Tag, ULONG Mask, ULONG Flags);

/* PRIVATE FUNCTIONS **********************************************************/

#if DBG && defined(KDBG)

#include <kdbg/kdb.h>

BOOLEAN
ExpKdbgExtPool(
    ULONG Argc,
    PCHAR Argv[])
{
    ULONG_PTR Address = 0, Flags = 0;
    PVOID PoolPage;
    PPOOL_HEADER Entry;
    BOOLEAN ThisOne;
    PULONG Data;

    if (Argc > 1)
    {
        /* Get address */
        if (!KdbpGetHexNumber(Argv[1], &Address))
        {
            KdbpPrint("Invalid parameter: %s\n", Argv[1]);
            return TRUE;
        }
    }

    if (Argc > 2)
    {
        /* Get flags */
        if (!KdbpGetHexNumber(Argv[2], &Flags))
        {
            KdbpPrint("Invalid parameter: %s\n", Argv[2]);
            return TRUE;
        }
    }

    /* Check if we got an address */
    if (Address != 0)
    {
        /* Get the base page */
        PoolPage = PAGE_ALIGN(Address);
    }
    else
    {
        KdbpPrint("Heap is unimplemented\n");
        return TRUE;
    }

    /* No paging support! */
    if (!MmIsAddressValid(PoolPage))
    {
        KdbpPrint("Address not accessible!\n");
        return TRUE;
    }

    /* Get pool type */
    if ((Address >= (ULONG_PTR)MmPagedPoolStart) && (Address <= (ULONG_PTR)MmPagedPoolEnd))
        KdbpPrint("Allocation is from PagedPool region\n");
    else if ((Address >= (ULONG_PTR)MmNonPagedPoolStart) && (Address <= (ULONG_PTR)MmNonPagedPoolEnd))
        KdbpPrint("Allocation is from NonPagedPool region\n");
    else
    {
        KdbpPrint("Address 0x%p is not within any pool!\n", (PVOID)Address);
        return TRUE;
    }

    /* Loop all entries of that page */
    Entry = PoolPage;
    do
    {
        /* Check if the address is within that entry */
        ThisOne = ((Address >= (ULONG_PTR)Entry) &&
                   (Address < (ULONG_PTR)(Entry + Entry->BlockSize)));

        if (!(Flags & 1) || ThisOne)
        {
            /* Print the line */
            KdbpPrint("%c%p size: %4d previous size: %4d  %s  %.4s\n",
                     ThisOne ? '*' : ' ', Entry, Entry->BlockSize, Entry->PreviousSize,
                     (Flags & 0x80000000) ? "" : (Entry->PoolType ? "(Allocated)" : "(Free)     "),
                     (Flags & 0x80000000) ? "" : (PCHAR)&Entry->PoolTag);
        }

        if (Flags & 1)
        {
            Data = (PULONG)(Entry + 1);
            KdbpPrint("    %p  %08lx %08lx %08lx %08lx\n"
                     "    %p  %08lx %08lx %08lx %08lx\n",
                     &Data[0], Data[0], Data[1], Data[2], Data[3],
                     &Data[4], Data[4], Data[5], Data[6], Data[7]);
        }

        /* Go to next entry */
        Entry = POOL_BLOCK(Entry, Entry->BlockSize);
    }
    while ((Entry->BlockSize != 0) && ((ULONG_PTR)Entry < (ULONG_PTR)PoolPage + PAGE_SIZE));

    return TRUE;
}

static
VOID
ExpKdbgExtPoolUsedGetTag(PCHAR Arg, PULONG Tag, PULONG Mask)
{
    CHAR Tmp[4];
    SIZE_T Len;
    USHORT i;

    /* Get the tag */
    Len = strlen(Arg);
    if (Len > 4)
    {
        Len = 4;
    }

    /* Generate the mask to have wildcards support */
    for (i = 0; i < Len; ++i)
    {
        Tmp[i] = Arg[i];
        if (Tmp[i] != '?')
        {
            *Mask |= (0xFF << i * 8);
        }
    }

    /* Get the tag in the ulong form */
    *Tag = *((PULONG)Tmp);
}

BOOLEAN
ExpKdbgExtPoolUsed(
    ULONG Argc,
    PCHAR Argv[])
{
    ULONG Tag = 0;
    ULONG Mask = 0;
    ULONG_PTR Flags = 0;

    if (Argc > 1)
    {
        /* If we have 2+ args, easy: flags then tag */
        if (Argc > 2)
        {
            ExpKdbgExtPoolUsedGetTag(Argv[2], &Tag, &Mask);
            if (!KdbpGetHexNumber(Argv[1], &Flags))
            {
                KdbpPrint("Invalid parameter: %s\n", Argv[1]);
            }
        }
        else
        {
            /* Otherwise, try to find out whether that's flags */
            if (strlen(Argv[1]) == 1 ||
                (strlen(Argv[1]) == 3 && Argv[1][0] == '0' && (Argv[1][1] == 'x' || Argv[1][1] == 'X')))
            {
                /* Fallback: if reading flags failed, assume it's a tag */
                if (!KdbpGetHexNumber(Argv[1], &Flags))
                {
                    ExpKdbgExtPoolUsedGetTag(Argv[1], &Tag, &Mask);
                }
            }
            /* Or tag */
            else
            {
                ExpKdbgExtPoolUsedGetTag(Argv[1], &Tag, &Mask);
            }
        }
    }

    /* Call the dumper */
    MiDumpPoolConsumers(TRUE, Tag, Mask, Flags);

    return TRUE;
}

static
VOID
ExpKdbgExtPoolFindLargePool(
    ULONG Tag,
    ULONG Mask,
    VOID (NTAPI* FoundCallback)(PPOOL_TRACKER_BIG_PAGES, PVOID),
    PVOID CallbackContext)
{
    ULONG i;

    KdbpPrint("Scanning large pool allocation table for Tag: %.4s (%p : %p)\n", (PCHAR)&Tag, &PoolBigPageTable[0], &PoolBigPageTable[PoolBigPageTableSize - 1]);

    for (i = 0; i < PoolBigPageTableSize; i++)
    {
        /* Free entry? */
        if ((ULONG_PTR)PoolBigPageTable[i].Va & POOL_BIG_TABLE_ENTRY_FREE)
        {
            continue;
        }

        if ((PoolBigPageTable[i].Key & Mask) == (Tag & Mask))
        {
            if (FoundCallback != NULL)
            {
                FoundCallback(&PoolBigPageTable[i], CallbackContext);
            }
            else
            {
                /* Print the line */
                KdbpPrint("%p: tag %.4s, size: %I64x\n",
                          PoolBigPageTable[i].Va, (PCHAR)&PoolBigPageTable[i].Key,
                          PoolBigPageTable[i].NumberOfPages << PAGE_SHIFT);
            }
        }
    }
}

static
BOOLEAN
ExpKdbgExtValidatePoolHeader(
    PVOID BaseVa,
    PPOOL_HEADER Entry,
    POOL_TYPE BasePoolTye)
{
    /* Block size cannot be NULL or negative and it must cover the page */
    if (Entry->BlockSize <= 0)
    {
        return FALSE;
    }
    if (Entry->BlockSize * 8 + (ULONG_PTR)Entry - (ULONG_PTR)BaseVa > PAGE_SIZE)
    {
        return FALSE;
    }

    /*
     * PreviousSize cannot be 0 unless on page begin
     * And it cannot be bigger that our current
     * position in page
     */
    if (Entry->PreviousSize == 0 && BaseVa != Entry)
    {
        return FALSE;
    }
    if (Entry->PreviousSize * 8 > (ULONG_PTR)Entry - (ULONG_PTR)BaseVa)
    {
        return FALSE;
    }

    /* Must be paged pool */
    if (((Entry->PoolType - 1) & BASE_POOL_TYPE_MASK) != BasePoolTye)
    {
        return FALSE;
    }

    /* Match tag mask */
    if ((Entry->PoolTag & 0x00808080) != 0)
    {
        return FALSE;
    }

    return TRUE;
}

static
VOID
ExpKdbgExtPoolFindPagedPool(
    ULONG Tag,
    ULONG Mask,
    VOID (NTAPI* FoundCallback)(PPOOL_HEADER, PVOID),
    PVOID CallbackContext)
{
    ULONG i = 0;
    PPOOL_HEADER Entry;
    PVOID BaseVa;
    PMMPDE PointerPde;

    KdbpPrint("Searching Paged pool (%p : %p) for Tag: %.4s\n", MmPagedPoolStart, MmPagedPoolEnd, (PCHAR)&Tag);

    /*
     * To speed up paged pool search, we will use the allocation bipmap.
     * This is possible because we live directly in the kernel :-)
     */
    i = RtlFindSetBits(MmPagedPoolInfo.PagedPoolAllocationMap, 1, 0);
    while (i != 0xFFFFFFFF)
    {
        BaseVa = (PVOID)((ULONG_PTR)MmPagedPoolStart + (i << PAGE_SHIFT));
        Entry = BaseVa;

        /* Validate our address */
        if ((ULONG_PTR)BaseVa > (ULONG_PTR)MmPagedPoolEnd || (ULONG_PTR)BaseVa + PAGE_SIZE > (ULONG_PTR)MmPagedPoolEnd)
        {
            break;
        }

        /* Check whether we are beyond expansion */
        PointerPde = MiAddressToPde(BaseVa);
        if (PointerPde >= MmPagedPoolInfo.NextPdeForPagedPoolExpansion)
        {
            break;
        }

        /* Check if allocation is valid */
        if (MmIsAddressValid(BaseVa))
        {
            for (Entry = BaseVa;
                 (ULONG_PTR)Entry + sizeof(POOL_HEADER) < (ULONG_PTR)BaseVa + PAGE_SIZE;
                 Entry = (PVOID)((ULONG_PTR)Entry + 8))
            {
                /* Try to find whether we have a pool entry */
                if (!ExpKdbgExtValidatePoolHeader(BaseVa, Entry, PagedPool))
                {
                    continue;
                }

                if ((Entry->PoolTag & Mask) == (Tag & Mask))
                {
                    if (FoundCallback != NULL)
                    {
                        FoundCallback(Entry, CallbackContext);
                    }
                    else
                    {
                        /* Print the line */
                        KdbpPrint("%p size: %4d previous size: %4d  %s  %.4s\n",
                                  Entry, Entry->BlockSize, Entry->PreviousSize,
                                  Entry->PoolType ? "(Allocated)" : "(Free)     ",
                                  (PCHAR)&Entry->PoolTag);
                    }
                }
            }
        }

        i = RtlFindSetBits(MmPagedPoolInfo.PagedPoolAllocationMap, 1, i + 1);
    }
}

static
VOID
ExpKdbgExtPoolFindNonPagedPool(
    ULONG Tag,
    ULONG Mask,
    VOID (NTAPI* FoundCallback)(PPOOL_HEADER, PVOID),
    PVOID CallbackContext)
{
    PPOOL_HEADER Entry;
    PVOID BaseVa;

    KdbpPrint("Searching NonPaged pool (%p : %p) for Tag: %.4s\n", MmNonPagedPoolStart, MmNonPagedPoolEnd0, (PCHAR)&Tag);

    /* Brute force search: start browsing the whole non paged pool */
    for (BaseVa = MmNonPagedPoolStart;
         (ULONG_PTR)BaseVa + PAGE_SIZE <= (ULONG_PTR)MmNonPagedPoolEnd0;
         BaseVa = (PVOID)((ULONG_PTR)BaseVa + PAGE_SIZE))
    {
        Entry = BaseVa;

        /* Check whether we are beyond expansion */
        if (BaseVa >= MmNonPagedPoolExpansionStart)
        {
            break;
        }

        /* Check if allocation is valid */
        if (!MmIsAddressValid(BaseVa))
        {
            continue;
        }

        for (Entry = BaseVa;
             (ULONG_PTR)Entry + sizeof(POOL_HEADER) < (ULONG_PTR)BaseVa + PAGE_SIZE;
             Entry = (PVOID)((ULONG_PTR)Entry + 8))
        {
            /* Try to find whether we have a pool entry */
            if (!ExpKdbgExtValidatePoolHeader(BaseVa, Entry, NonPagedPool))
            {
                continue;
            }

            if ((Entry->PoolTag & Mask) == (Tag & Mask))
            {
                if (FoundCallback != NULL)
                {
                    FoundCallback(Entry, CallbackContext);
                }
                else
                {
                    /* Print the line */
                    KdbpPrint("%p size: %4d previous size: %4d  %s  %.4s\n",
                              Entry, Entry->BlockSize, Entry->PreviousSize,
                              Entry->PoolType ? "(Allocated)" : "(Free)     ",
                              (PCHAR)&Entry->PoolTag);
                }
            }
        }
    }
}

BOOLEAN
ExpKdbgExtPoolFind(
    ULONG Argc,
    PCHAR Argv[])
{
    ULONG Tag = 0;
    ULONG Mask = 0;
    ULONG PoolType = NonPagedPool;

    if (Argc == 1)
    {
        KdbpPrint("Specify a tag string\n");
        return TRUE;
    }

    /* First arg is tag */
    if (strlen(Argv[1]) != 1 || Argv[1][0] != '*')
    {
        ExpKdbgExtPoolUsedGetTag(Argv[1], &Tag, &Mask);
    }

    /* Second arg might be pool to search */
    if (Argc > 2)
    {
        PoolType = strtoul(Argv[2], NULL, 0);

        if (PoolType > 1)
        {
            KdbpPrint("Only (non) paged pool are supported\n");
            return TRUE;
        }
    }

    /* First search for large allocations */
    ExpKdbgExtPoolFindLargePool(Tag, Mask, NULL, NULL);

    if (PoolType == NonPagedPool)
    {
        ExpKdbgExtPoolFindNonPagedPool(Tag, Mask, NULL, NULL);
    }
    else if (PoolType == PagedPool)
    {
        ExpKdbgExtPoolFindPagedPool(Tag, Mask, NULL, NULL);
    }

    return TRUE;
}

VOID
NTAPI
ExpKdbgExtIrpFindPrint(
    PPOOL_HEADER Entry,
    PVOID Context)
{
    PIRP Irp;
    IRP IrpSnapshot;
    POOL_HEADER HeaderSnapshot;
    BOOLEAN IsComplete = FALSE;
    PIRP_FIND_CTXT FindCtxt = Context;
    PIO_STACK_LOCATION IoStackAddress = NULL;
    IO_STACK_LOCATION IoStackSnapshot;
    DEVICE_OBJECT DeviceSnapshot;
    DRIVER_OBJECT DriverSnapshot;
    MDL MdlSnapshot;
    UNICODE_STRING DriverNameSnapshot;
    BOOLEAN HaveIoStack = FALSE;
    BOOLEAN HaveDriverName = FALSE;
    PVOID MdlProcess = NULL;
    ULONG_PTR SData = FindCtxt->SData;
    ULONG Criteria = FindCtxt->Criteria;

    if (!NT_SUCCESS(KdbpSafeReadMemory(&HeaderSnapshot, Entry, sizeof(HeaderSnapshot))) ||
        HeaderSnapshot.PoolType == 0)
    {
        return;
    }

    /* Get the IRP */
    Irp = (PIRP)POOL_FREE_BLOCK(Entry);
    if (!NT_SUCCESS(KdbpSafeReadMemory(&IrpSnapshot, Irp, sizeof(IrpSnapshot))))
    {
        KdbpPrint("%p <unreadable IRP candidate>\n", Irp);
        return;
    }

    /* Bail out if not matching restart address */
    if ((ULONG_PTR)Irp < FindCtxt->RestartAddress)
    {
        return;
    }

    /* Avoid bogus IRP stack locations */
    if (IrpSnapshot.CurrentLocation <= IrpSnapshot.StackCount + 1)
    {
        IoStackAddress = IrpSnapshot.Tail.Overlay.CurrentStackLocation;
        if (IoStackAddress != NULL &&
            NT_SUCCESS(KdbpSafeReadMemory(&IoStackSnapshot, IoStackAddress, sizeof(IoStackSnapshot))))
        {
            HaveIoStack = TRUE;
        }

        /* Get associated driver */
        if (HaveIoStack &&
            IoStackSnapshot.DeviceObject != NULL &&
            NT_SUCCESS(KdbpSafeReadMemory(&DeviceSnapshot, IoStackSnapshot.DeviceObject, sizeof(DeviceSnapshot))) &&
            DeviceSnapshot.DriverObject != NULL &&
            NT_SUCCESS(KdbpSafeReadMemory(&DriverSnapshot, DeviceSnapshot.DriverObject, sizeof(DriverSnapshot))))
        {
            DriverNameSnapshot = DriverSnapshot.DriverName;
            HaveDriverName = TRUE;
        }
    }
    else
    {
        IsComplete = TRUE;
    }

    if (IrpSnapshot.MdlAddress != NULL &&
        NT_SUCCESS(KdbpSafeReadMemory(&MdlSnapshot, IrpSnapshot.MdlAddress, sizeof(MdlSnapshot))))
    {
        MdlProcess = MdlSnapshot.Process;
    }

    /* Display if: no data, no criteria or if criteria matches data */
    if (SData == 0 || Criteria == 0 ||
        (Criteria & 0x1 && HaveIoStack && SData == (ULONG_PTR)IoStackSnapshot.DeviceObject) ||
        (Criteria & 0x2 && SData == (ULONG_PTR)IrpSnapshot.Tail.Overlay.OriginalFileObject) ||
        (Criteria & 0x4 && MdlProcess != NULL && SData == (ULONG_PTR)MdlProcess) ||
        (Criteria & 0x8 && SData == (ULONG_PTR)IrpSnapshot.Tail.Overlay.Thread) ||
        (Criteria & 0x10 && SData == (ULONG_PTR)IrpSnapshot.UserEvent))
    {
        if (!IsComplete && HaveIoStack)
        {
            KdbpPrint("%p Thread %p current stack (%x, %x) belongs to ", Irp, IrpSnapshot.Tail.Overlay.Thread, IoStackSnapshot.MajorFunction, IoStackSnapshot.MinorFunction);
            if (HaveDriverName)
                KdbpPrintUnicodeString(&DriverNameSnapshot);
            else
                KdbpPrint("<unknown driver>");
            KdbpPrint("\n");
        }
        else if (!IsComplete)
        {
            KdbpPrint("%p Thread %p has an unreadable current stack %p\n", Irp, IrpSnapshot.Tail.Overlay.Thread, IoStackAddress);
        }
        else
        {
            KdbpPrint("%p Thread %p is complete (CurrentLocation %d > StackCount %d)\n", Irp, IrpSnapshot.Tail.Overlay.Thread, IrpSnapshot.CurrentLocation, IrpSnapshot.StackCount + 1);
        }
    }
}

BOOLEAN
ExpKdbgExtIrpFind(
    ULONG Argc,
    PCHAR Argv[])
{
    ULONG PoolType = NonPagedPool;
    IRP_FIND_CTXT FindCtxt;

    /* Pool type */
    if (Argc > 1)
    {
        PoolType = strtoul(Argv[1], NULL, 0);

        if (PoolType > 1)
        {
            KdbpPrint("Only (non) paged pool are supported\n");
            return TRUE;
        }
    }

    RtlZeroMemory(&FindCtxt, sizeof(IRP_FIND_CTXT));

    /* Restart address */
    if (Argc > 2)
    {
        if (!KdbpGetHexNumber(Argv[2], &FindCtxt.RestartAddress))
        {
            KdbpPrint("Invalid parameter: %s\n", Argv[2]);
            FindCtxt.RestartAddress = 0;
        }
    }

    if (Argc > 4)
    {
        if (!KdbpGetHexNumber(Argv[4], &FindCtxt.SData))
        {
            FindCtxt.SData = 0;
        }
        else
        {
            if (strcmp(Argv[3], "device") == 0)
            {
                FindCtxt.Criteria = 0x1;
            }
            else if (strcmp(Argv[3], "fileobject") == 0)
            {
                FindCtxt.Criteria = 0x2;
            }
            else if (strcmp(Argv[3], "mdlprocess") == 0)
            {
                FindCtxt.Criteria = 0x4;
            }
            else if (strcmp(Argv[3], "thread") == 0)
            {
                FindCtxt.Criteria = 0x8;
            }
            else if (strcmp(Argv[3], "userevent") == 0)
            {
                FindCtxt.Criteria = 0x10;
            }
            else if (strcmp(Argv[3], "arg") == 0)
            {
                FindCtxt.Criteria = 0x1f;
            }
        }
    }

    if (PoolType == NonPagedPool)
    {
        ExpKdbgExtPoolFindNonPagedPool(TAG_IRP, 0xFFFFFFFF, ExpKdbgExtIrpFindPrint, &FindCtxt);
    }
    else if (PoolType == PagedPool)
    {
        ExpKdbgExtPoolFindPagedPool(TAG_IRP, 0xFFFFFFFF, ExpKdbgExtIrpFindPrint, &FindCtxt);
    }

    return TRUE;
}

static BOOLEAN
ExpKdbgReadPte(IN PCSTR Level, IN PMMPTE PointerPte, OUT PMMPTE Pte)
{
    NTSTATUS Status;
    BOOLEAN Executable;

    Status = KdbpSafeReadMemory(Pte, PointerPte, sizeof(*Pte));
    if (!NT_SUCCESS(Status))
    {
        KdbpPrint("  %-3s %p <unreadable: 0x%08lx>\n", Level, PointerPte, Status);
        return FALSE;
    }

    KdbpPrint("  %-3s %p %016I64x ", Level, PointerPte, (ULONGLONG)Pte->u.Long);
    if (Pte->u.Hard.Valid)
    {
#if defined(_M_ARM64)
        /*
         * At upper levels, type 3 is a table descriptor.  Its leaf PXN/UXN,
         * AP, AF and nG bit positions are ignored; ARM64 uses different
         * *Table fields for hierarchical restrictions.  Do not present the
         * NT-compatible ignored policy bits as effective page permissions.
         */
        if ((strcmp(Level, "PTE") != 0) && !MI_IS_PAGE_LARGE(Pte))
        {
            KdbpPrint("valid table pfn %I64x\n", (ULONGLONG)Pte->u.Hard.PageFrameNumber);
            return TRUE;
        }

        /* UXN governs EL0 and PXN governs EL1. Report the permission that
         * corresponds to the descriptor owner instead of treating UXN as a
         * universal execute-disable bit. */
        Executable = Pte->u.Hard.Owner ?
                     MI_IS_PAGE_EXECUTABLE(Pte) :
                     MI_IS_PAGE_KERNEL_EXECUTABLE(Pte);
#elif defined(_M_AMD64)
        Executable = MI_IS_PAGE_EXECUTABLE(Pte);
        if ((strcmp(Level, "PTE") != 0) && !MI_IS_PAGE_LARGE(Pte))
        {
            /* AMD64 upper entries carry hierarchical access restrictions,
             * not a leaf page's final permissions. Label them as tables and
             * describe only the restrictions this level contributes. */
            KdbpPrint("valid table pfn %I64x %s %s %s %s\n",
                      (ULONGLONG)Pte->u.Hard.PageFrameNumber,
                      Pte->u.Hard.Owner ? "user-accessible" : "kernel-only",
                      MI_IS_PAGE_WRITEABLE(Pte) ? "writable" : "read-only",
                      Executable ? "execute" : "no-execute",
                      Pte->u.Hard.Accessed ? "accessed" : "not-accessed");
            return TRUE;
        }
#else
        Executable = MI_IS_PAGE_EXECUTABLE(Pte);
#endif
        KdbpPrint("valid pfn %I64x %s %s %s %s%s\n",
                  (ULONGLONG)Pte->u.Hard.PageFrameNumber,
                  Pte->u.Hard.Owner ? "user" : "kernel",
                  MI_IS_PAGE_WRITEABLE(Pte) ? "write" : "read",
                  Executable ? "execute" : "no-execute",
                  Pte->u.Hard.Accessed ? "accessed" : "not-accessed",
                  MI_IS_PAGE_DIRTY(Pte) ? " dirty" : "");
    }
    else if (Pte->u.Trans.Transition)
    {
        KdbpPrint("transition pfn %I64x protection 0x%lx\n", (ULONGLONG)Pte->u.Trans.PageFrameNumber, (ULONG)Pte->u.Trans.Protection);
    }
    else if (Pte->u.Soft.Prototype)
    {
        KdbpPrint("prototype address %p protection 0x%lx\n", (PVOID)(ULONG_PTR)((ULONGLONG)Pte->u.Proto.ProtoAddress << 4), (ULONG)Pte->u.Proto.Protection);
    }
    else if (Pte->u.Long != 0)
    {
        KdbpPrint("software pagefile %lx:%I64x protection 0x%lx\n", (ULONG)Pte->u.Soft.PageFileLow, (ULONGLONG)Pte->u.Soft.PageFileHigh, (ULONG)Pte->u.Soft.Protection);
    }
    else
    {
        KdbpPrint("not present\n");
    }
    return TRUE;
}

static VOID
ExpKdbgPrintPte(IN PVOID Address)
{
    MMPTE Entry;

    KdbpPrint("Virtual address %p\n", Address);
#if defined(_WIN64)
    if (!ExpKdbgReadPte("PXE", (PMMPTE)MiAddressToPxe(Address), &Entry) ||
        !Entry.u.Hard.Valid || MI_IS_PAGE_LARGE(&Entry))
    {
        return;
    }
    if (!ExpKdbgReadPte("PPE", (PMMPTE)MiAddressToPpe(Address), &Entry) ||
        !Entry.u.Hard.Valid || MI_IS_PAGE_LARGE(&Entry))
    {
        return;
    }
#endif
    if (!ExpKdbgReadPte("PDE", (PMMPTE)MiAddressToPde(Address), &Entry) ||
        !Entry.u.Hard.Valid || MI_IS_PAGE_LARGE(&Entry))
    {
        return;
    }
    (VOID)ExpKdbgReadPte("PTE", MiAddressToPte(Address), &Entry);
}

BOOLEAN
ExpKdbgExtPte(ULONG Argc, PCHAR Argv[])
{
    ULONG_PTR Address;

    if (Argc != 2)
    {
        KdbpPrint("Usage: !pte address\n");
        return TRUE;
    }
    if (!KdbpGetAddressExpression(Argv[1], &Address))
    {
        KdbpPrint("!pte: Invalid address '%s'.\n", Argv[1]);
        return TRUE;
    }
    ExpKdbgPrintPte((PVOID)Address);
    return TRUE;
}

BOOLEAN
ExpKdbgExtPfn(ULONG Argc, PCHAR Argv[])
{
    ULONG_PTR PfnValue;
    PMMPFN PfnAddress;
    MMPFN Pfn;
    NTSTATUS Status;

    if (Argc != 2)
    {
        KdbpPrint("Usage: !pfn page-frame-number\n");
        return TRUE;
    }
    if (!KdbpGetHexNumber(Argv[1], &PfnValue) ||
        PfnValue > MmHighestPhysicalPage ||
        MmPfnDatabase == NULL ||
        PfnValue > (MAXULONG_PTR - (ULONG_PTR)MmPfnDatabase) / sizeof(MMPFN))
    {
        KdbpPrint("!pfn: PFN '%s' is outside the database (highest %I64x).\n", Argv[1], (ULONGLONG)MmHighestPhysicalPage);
        return TRUE;
    }

    PfnAddress = MmPfnDatabase + PfnValue;
    Status = KdbpSafeReadMemory(&Pfn, PfnAddress, sizeof(Pfn));
    if (!NT_SUCCESS(Status))
    {
        KdbpPrint("!pfn: PFN entry %p is unreadable (0x%08lx).\n", PfnAddress, Status);
        return TRUE;
    }

    KdbpPrint("PFN %I64x at %p\n"
              "  PTE address:       %p\n"
              "  Original PTE:      %016I64x\n"
              "  Flink/Blink:       %I64x / %I64x\n"
              "  Reference/Share:   %u / %I64u\n"
              "  Location/Color:    %u / %u\n"
              "  Modified/RIP/WIP:  %u / %u / %u\n"
              "  Prototype/Cache:   %u / %u\n"
              "  PTE frame:         %I64x\n"
              "  Priority/Flags:    %u / IPE=%u VA=%u AWE=%u\n",
              (ULONGLONG)PfnValue,
              PfnAddress,
              Pfn.PteAddress,
              (ULONGLONG)Pfn.OriginalPte.u.Long,
              (ULONGLONG)Pfn.u1.Flink,
              (ULONGLONG)Pfn.u2.Blink,
              Pfn.u3.ReferenceCount,
              (ULONGLONG)Pfn.u2.ShareCount,
              Pfn.u3.e1.PageLocation,
              Pfn.u3.e1.PageColor,
              Pfn.u3.e1.Modified,
              Pfn.u3.e1.ReadInProgress,
              Pfn.u3.e1.WriteInProgress,
              Pfn.u3.e1.PrototypePte,
              Pfn.u3.e1.CacheAttribute,
              (ULONGLONG)Pfn.u4.PteFrame,
              (ULONG)Pfn.u4.Priority,
              (ULONG)Pfn.u4.InPageError,
              (ULONG)Pfn.u4.VerifierAllocation,
              (ULONG)Pfn.u4.AweAllocation);
    return TRUE;
}

static BOOLEAN
ExpKdbgReadVad(IN PMMVAD VadAddress, OUT PMMVAD Vad)
{
    return VadAddress != NULL &&
           NT_SUCCESS(KdbpSafeReadMemory(Vad, VadAddress, sizeof(*Vad)));
}

static VOID
ExpKdbgPrintVad(IN PMMVAD VadAddress, IN PMMVAD Vad)
{
    ULONG_PTR Start;
    ULONG_PTR End;

    if (Vad->StartingVpn > (MAXULONG_PTR >> PAGE_SHIFT) ||
        Vad->EndingVpn > (MAXULONG_PTR >> PAGE_SHIFT) ||
        Vad->EndingVpn < Vad->StartingVpn)
    {
        KdbpPrint("  %p <invalid VPN range %Ix-%Ix>\n", VadAddress, Vad->StartingVpn, Vad->EndingVpn);
        return;
    }
    Start = Vad->StartingVpn << PAGE_SHIFT;
    End = (Vad->EndingVpn << PAGE_SHIFT) | (PAGE_SIZE - 1);
    KdbpPrint("  %p %p-%p prot %02x type %u %s %s commit %Iu CA %p\n",
              VadAddress,
              (PVOID)Start,
              (PVOID)End,
              (ULONG)Vad->u.VadFlags.Protection,
              (ULONG)Vad->u.VadFlags.VadType,
              Vad->u.VadFlags.PrivateMemory ? "private" : "mapped",
              Vad->u.VadFlags.MemCommit ? "committed" : "reserved",
              (SIZE_T)Vad->u.VadFlags.CommitCharge,
              Vad->ControlArea);
}

static PMMVAD
ExpKdbgFindVad(IN PEPROCESS Process, IN PVOID Address, OUT PMMVAD Vad)
{
    EPROCESS ProcessSnapshot;
    PMMVAD Node;
    ULONG_PTR Vpn = (ULONG_PTR)Address >> PAGE_SHIFT;
    ULONG Count = 0;

    if (!NT_SUCCESS(KdbpSafeReadMemory(&ProcessSnapshot, Process, sizeof(ProcessSnapshot))))
    {
        return NULL;
    }
    Node = (PMMVAD)ProcessSnapshot.VadRoot.BalancedRoot.RightChild;
    while (Node != NULL && Count++ < 128)
    {
        PMMVAD Next;

        if (!ExpKdbgReadVad(Node, Vad))
            return NULL;
        if (Vpn < Vad->StartingVpn)
            Next = Vad->LeftChild;
        else if (Vpn > Vad->EndingVpn)
            Next = Vad->RightChild;
        else
            return Node;
        if (Next == Node)
            return NULL;
        Node = Next;
    }
    return NULL;
}

BOOLEAN
ExpKdbgExtVad(ULONG Argc, PCHAR Argv[])
{
    EPROCESS ProcessSnapshot;
    PMMVAD Stack[64];
    PMMVAD Node;
    MMVAD Vad;
    ULONG Depth = 0;
    ULONG Count = 0;
    ULONG_PTR Address;

    if (Argc > 2)
    {
        KdbpPrint("Usage: !vad [address]\n");
        return TRUE;
    }
    if (Argc == 2)
    {
        if (!KdbpGetAddressExpression(Argv[1], &Address))
        {
            KdbpPrint("!vad: Invalid address '%s'.\n", Argv[1]);
            return TRUE;
        }
        Node = ExpKdbgFindVad(KdbCurrentProcess, (PVOID)Address, &Vad);
        if (Node == NULL)
            KdbpPrint("!vad: No readable VAD contains %p.\n", (PVOID)Address);
        else
            ExpKdbgPrintVad(Node, &Vad);
        return TRUE;
    }

    if (!NT_SUCCESS(KdbpSafeReadMemory(&ProcessSnapshot, KdbCurrentProcess, sizeof(ProcessSnapshot))))
    {
        KdbpPrint("!vad: Current EPROCESS %p is unreadable.\n", KdbCurrentProcess);
        return TRUE;
    }
    KdbpPrint("VADs for process %p (maximum 65536, depth 64)\n", KdbCurrentProcess);
    Node = (PMMVAD)ProcessSnapshot.VadRoot.BalancedRoot.RightChild;
    while ((Node != NULL || Depth != 0) && Count < 65536)
    {
        while (Node != NULL)
        {
            if (Depth == RTL_NUMBER_OF(Stack) || !ExpKdbgReadVad(Node, &Vad))
            {
                KdbpPrint("!vad: Corrupt, too-deep, or unreadable tree at %p.\n", Node);
                return TRUE;
            }
            Stack[Depth++] = Node;
            if (Vad.LeftChild == Node)
            {
                KdbpPrint("!vad: Self-linked left child at %p.\n", Node);
                return TRUE;
            }
            Node = Vad.LeftChild;
        }
        Node = Stack[--Depth];
        if (!ExpKdbgReadVad(Node, &Vad))
        {
            KdbpPrint("!vad: VAD %p became unreadable.\n", Node);
            return TRUE;
        }
        ExpKdbgPrintVad(Node, &Vad);
        Count++;
        if (Vad.RightChild == Node)
        {
            KdbpPrint("!vad: Self-linked right child at %p.\n", Node);
            return TRUE;
        }
        Node = Vad.RightChild;
        if (KdbpIsOutputAborted())
            return TRUE;
    }
    if (Count == 65536)
        KdbpPrint("!vad: Enumeration stopped at the safety limit.\n");
    return TRUE;
}

BOOLEAN
ExpKdbgExtAddress(ULONG Argc, PCHAR Argv[])
{
    ULONG_PTR Address;
    MMVAD Vad;
    PMMVAD VadAddress;

    if (Argc != 2 || !KdbpGetAddressExpression(Argv[1], &Address))
    {
        KdbpPrint("Usage: !address address\n");
        return TRUE;
    }
    KdbpPrint("Address %p is %s address space.\n", (PVOID)Address, Address >= (ULONG_PTR)MmSystemRangeStart ? "system" : "user");
    VadAddress = ExpKdbgFindVad(KdbCurrentProcess, (PVOID)Address, &Vad);
    if (VadAddress != NULL)
        ExpKdbgPrintVad(VadAddress, &Vad);
    else if (Address < (ULONG_PTR)MmSystemRangeStart)
        KdbpPrint("  No readable VAD contains this user address.\n");
    ExpKdbgPrintPte((PVOID)Address);
    return TRUE;
}

BOOLEAN
ExpKdbgExtVm(ULONG Argc, PCHAR Argv[])
{
    LONGLONG ResidentAvailable;

    UNREFERENCED_PARAMETER(Argv);
    if (Argc != 1)
    {
        KdbpPrint("Usage: !vm\n");
        return TRUE;
    }
    ResidentAvailable = (LONGLONG)(LONG_PTR)MmResidentAvailablePages;
    KdbpPrint("Physical pages:      %I64u (highest PFN %I64x)\n"
              "Available/resident:  %I64u / %I64d pages\n"
              "Commit/limit:        %Iu / %Iu pages\n"
              "PFN database:        %p\n"
              "Nonpaged pool:       %p - %p\n"
              "Paged pool:          %p - %p\n"
              "System range start:  %p\n",
              (ULONGLONG)MmNumberOfPhysicalPages,
              (ULONGLONG)MmHighestPhysicalPage,
              (ULONGLONG)MmAvailablePages,
              ResidentAvailable,
              MmTotalCommittedPages,
              MmTotalCommitLimit,
              MmPfnDatabase,
              MmNonPagedPoolStart,
              MmNonPagedPoolEnd,
              MmPagedPoolStart,
              MmPagedPoolEnd,
              MmSystemRangeStart);
    if (ResidentAvailable < 0)
    {
        KdbpPrint("WARNING: Resident-available accounting is negative (raw 0x%I64x).\n", (ULONGLONG)MmResidentAvailablePages);
    }
    return TRUE;
}

#endif // DBG && defined(KDBG)

/* EOF */
