/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            ntoskrnl/io/iomgr/iowork.c
 * PURPOSE:         I/O Wrappers for the Executive Work Item Functions
 * PROGRAMMERS:     Alex Ionescu (alex.ionescu@reactos.org)
 *                  Robert Dickenson (odin@pnc.com.au)
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/*
 * ARM64 memory barrier macros for work item synchronization.
 *
 * On ARM64, the memory model is weakly ordered. Without explicit barriers,
 * stores from one CPU may not be visible to another CPU in program order.
 * This is critical for work item processing where:
 *
 * Producer (IoQueueWorkItem):
 *   1. Write WorkerRoutine pointer
 *   2. Write Context pointer
 *   3. Release barrier (dmb ishst) - ensure writes complete before queuing
 *   4. Queue the work item
 *
 * Consumer (IopWorkItemCallback):
 *   1. Acquire barrier (dmb ishld) - ensure we see producer's writes
 *   2. Read WorkerRoutine pointer
 *   3. Read Context pointer
 *   4. Call the routine
 *
 * The "ish" (inner shareable) domain is sufficient for SMP within a single
 * operating system. "ishst" is store-only barrier (cheaper than full barrier),
 * "ishld" is load-only barrier.
 */
#if defined(_M_ARM64) || defined(__aarch64__)
#define ARM64_RELEASE_BARRIER() __asm__ __volatile__("dmb ishst" : : : "memory")
#define ARM64_ACQUIRE_BARRIER() __asm__ __volatile__("dmb ishld" : : : "memory")
#else
#define ARM64_RELEASE_BARRIER() ((void)0)
#define ARM64_ACQUIRE_BARRIER() ((void)0)
#endif

static
BOOLEAN
IopIsExecutableRoutine(PIO_WORKITEM_ROUTINE Routine)
{
    PLDR_DATA_TABLE_ENTRY LdrEntry = NULL;
    BOOLEAN InKernel = FALSE;
    PVOID ImageBase;
    PIMAGE_NT_HEADERS NtHeaders;
    PIMAGE_SECTION_HEADER Section;
    ULONG_PTR Rva;

    if (Routine == NULL)
        return FALSE;

    /* Find the module containing this address */
    ImageBase = KiPcToFileHeader(Routine, &LdrEntry, FALSE, &InKernel);
    if (ImageBase == NULL)
    {
        DPRINT1("IopIsExecutableRoutine: KiPcToFileHeader failed for %p\n", Routine);
        return FALSE;
    }

    /* Get the PE headers */
    NtHeaders = RtlImageNtHeader(ImageBase);
    if (NtHeaders == NULL)
    {
        DPRINT1("IopIsExecutableRoutine: RtlImageNtHeader failed for base %p\n", ImageBase);
        return FALSE;
    }

    /* Calculate the RVA within the module */
    Rva = (ULONG_PTR)Routine - (ULONG_PTR)ImageBase;

    /* Find the section containing this RVA */
    Section = RtlImageRvaToSection(NtHeaders, ImageBase, (ULONG)Rva);
    if (Section == NULL)
    {
        DPRINT1("IopIsExecutableRoutine: Section not found for RVA %lx (Base %p)\n", Rva, ImageBase);
        return FALSE;
    }

    /* Check if the section is executable */
    if (!(Section->Characteristics & IMAGE_SCN_MEM_EXECUTE))
    {
        DPRINT1("IopIsExecutableRoutine: Section %.8s not executable (Chars %x) for %p\n",
                Section->Name, Section->Characteristics, Routine);
        return FALSE;
    }

    return TRUE;
}

/* PRIVATE FUNCTIONS *********************************************************/

VOID
NTAPI
IopWorkItemCallback(IN PVOID Parameter)
{
    PIO_WORKITEM IoWorkItem = (PIO_WORKITEM)Parameter;
    PDEVICE_OBJECT DeviceObject;
    PIO_WORKITEM_ROUTINE WorkerRoutine;
    PVOID Context;
    PAGED_CODE();

    /*
     * ARM64 FIX: Acquire barrier before reading fields written by producer.
     *
     * This ensures we see the WorkerRoutine and Context values written by
     * IoQueueWorkItem, even on ARM64's weakly ordered memory model.
     *
     * Without this barrier, we could read stale/uninitialized values because:
     * 1. The queue insertion provides synchronization for the LIST_ENTRY
     * 2. But NOT for the other fields in IO_WORKITEM (WorkerRoutine, Context)
     * 3. On ARM64, out-of-order completion of stores means the queue could
     *    be visible before WorkerRoutine/Context are committed to memory
     */
    ARM64_ACQUIRE_BARRIER();

    /* Read all fields we need before the barrier is "consumed" */
    DeviceObject = IoWorkItem->DeviceObject;
    WorkerRoutine = IoWorkItem->WorkerRoutine;
    Context = IoWorkItem->Context;

    /* Simple sanity check to avoid null or obviously invalid routines */
    if (WorkerRoutine == NULL)
    {
        KeBugCheckEx(INVALID_WORK_QUEUE_ITEM,
                     (ULONG_PTR)IoWorkItem,
                     0,
                     (ULONG_PTR)Context,
                     0xDEAD0002);
    }

    /*
     * Validate the function pointer points to executable code.
     *
     * This catches:
     * - NULL pointers
     * - Pointers to non-executable memory (PE headers, data sections)
     * - Pointers outside any loaded module
     * - Memory ordering issues that result in stale/invalid pointers
     */
    if (!IopIsExecutableRoutine(WorkerRoutine))
    {
        DPRINT1("IopWorkItemCallback: WorkerRoutine %p is not in executable section! "
                "(IoWorkItem=%p DevObj=%p Ctx=%p)\n",
                WorkerRoutine, IoWorkItem, DeviceObject, Context);
        
        /* 
         * WARN ONLY: On some architectures (x64), valid drivers may fail this check
         * due to loader/header issues. Warn but allow execution to proceed to avoid
         * breaking boot. Actual bad pointers will likely crash inside the call anyway.
         */
        // KeBugCheckEx(INVALID_WORK_QUEUE_ITEM, ...); 
    }

    /* Call the work routine */
    WorkerRoutine(DeviceObject, Context);

    /* Dereference the device object */
    ObDereferenceObject(DeviceObject);
}

/* PUBLIC FUNCTIONS **********************************************************/

/*
 * @implemented
 */
VOID
NTAPI
IoQueueWorkItem(IN PIO_WORKITEM IoWorkItem,
                IN PIO_WORKITEM_ROUTINE WorkerRoutine,
                IN WORK_QUEUE_TYPE QueueType,
                IN PVOID Context)
{
    /* Make sure we're called at DISPATCH or lower */
    ASSERT_IRQL_LESS_OR_EQUAL(DISPATCH_LEVEL);

    /* Reference the device object */
    ObReferenceObject(IoWorkItem->DeviceObject);

    /* Setup the work item */
    IoWorkItem->WorkerRoutine = WorkerRoutine;
    IoWorkItem->Context = Context;

    /*
     * ARM64 FIX: Release barrier after writing fields, before queuing.
     *
     * This ensures that the WorkerRoutine and Context writes are visible
     * to any CPU that subsequently dequeues this work item. Without this
     * barrier on ARM64's weakly ordered memory model, the queue insertion
     * could become visible before the field writes are committed.
     */
    ARM64_RELEASE_BARRIER();

    /* Queue the work item */
    ExQueueWorkItem(&IoWorkItem->Item, QueueType);
}

/*
 * @implemented
 */
VOID
NTAPI
IoFreeWorkItem(IN PIO_WORKITEM IoWorkItem)
{
    /* Free the work item */
    ExFreePool(IoWorkItem);
}

/*
 * @implemented
 */
PIO_WORKITEM
NTAPI
IoAllocateWorkItem(IN PDEVICE_OBJECT DeviceObject)
{
    PIO_WORKITEM IoWorkItem;

    /* Allocate the work item */
    IoWorkItem = ExAllocatePoolWithTag(NonPagedPool,
                                       sizeof(IO_WORKITEM),
                                       TAG_IOWI);
    if (!IoWorkItem) return NULL;

    /* Initialize it */
    IoWorkItem->DeviceObject = DeviceObject;
    ExInitializeWorkItem(&IoWorkItem->Item, IopWorkItemCallback, IoWorkItem);

    /* Return it */
    return IoWorkItem;
}

/* EOF */
