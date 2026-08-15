/*
 * PROJECT:         ReactOS PCI Bus Driver
 * FILE:            drivers/bus/pci/config.c
 * PURPOSE:         PCI Configuration Space Access Helpers
 *                  and IPI-synchronized Config Access for Debug Devices
 */

#include "pci.h"

#define NDEBUG
#include <debug.h>

/* ---- IPI-based config space access for debug devices ---- */

typedef NTSTATUS
(NTAPI *PPCI_IPI_FUNCTION)(
    _In_ PVOID Context,
    _In_ PVOID Parameter2);

typedef struct _PCI_IPI_CONTEXT
{
    volatile LONG CountDown;
    volatile LONG CompletionFlag;
    PPCI_IPI_FUNCTION FunctionToCall;
    PVOID Context;
    PVOID Parameter2;
    NTSTATUS Result;
} PCI_IPI_CONTEXT, *PPCI_IPI_CONTEXT;

/**
 * @brief Check if a device is owned by the kernel debugger.
 */
BOOLEAN
PciIsDeviceDebugging(
    _In_ ULONG BusNumber,
    _In_ PCI_SLOT_NUMBER SlotNumber)
{
    ULONG i;

    if (!HasDebuggingDevice)
        return FALSE;

    for (i = 0; i < RTL_NUMBER_OF(PciDebuggingDevice); ++i)
    {
        if (PciDebuggingDevice[i].InUse &&
            PciDebuggingDevice[i].BusNumber == BusNumber &&
            PciDebuggingDevice[i].DeviceNumber == SlotNumber.u.bits.DeviceNumber &&
            PciDebuggingDevice[i].FunctionNumber == SlotNumber.u.bits.FunctionNumber)
        {
            return TRUE;
        }
    }

    return FALSE;
}

/**
 * @brief Read PCI config space with KD-safe bracket for debugging devices.
 *
 * For devices owned by the kernel debugger, we bracket the config access
 * with KdDisableDebugger/KdEnableDebugger to prevent interference.
 */
ULONG
PciReadDeviceConfig(
    _In_ PPCI_DEVICE Device,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Offset,
    _In_ ULONG Length)
{
    ULONG BytesRead;
    BOOLEAN NeedKdBracket = Device->IsDebuggingDevice;

    if (NeedKdBracket)
    {
        KdDisableDebugger();
    }

    BytesRead = PciHalReadConfig(Device->Segment,
                                 Device->BusNumber,
                                 Device->SlotNumber.u.AsULONG,
                                 Buffer,
                                 Offset,
                                 Length);

    if (NeedKdBracket)
    {
        KdEnableDebugger();
    }

    return BytesRead;
}

/**
 * @brief Write PCI config space with KD-safe bracket for debugging devices.
 */
ULONG
PciWriteDeviceConfig(
    _In_ PPCI_DEVICE Device,
    _In_reads_bytes_(Length) PVOID Buffer,
    _In_ ULONG Offset,
    _In_ ULONG Length)
{
    ULONG BytesWritten;
    BOOLEAN NeedKdBracket = Device->IsDebuggingDevice;

    if (NeedKdBracket)
    {
        KdDisableDebugger();
    }

    BytesWritten = PciHalWriteConfig(Device->Segment,
                                     Device->BusNumber,
                                     Device->SlotNumber.u.AsULONG,
                                     Buffer,
                                     Offset,
                                     Length);

    if (NeedKdBracket)
    {
        KdEnableDebugger();
    }

    return BytesWritten;
}

/**
 * @brief IPI broadcast worker callback for synchronized config space access.
 *
 * Called on every processor via KeIpiGenericCall. The last processor to arrive
 * (CountDown reaches 0) executes the actual work function. All other processors
 * spin-wait until the work is complete, ensuring no concurrent PCI config space
 * access from any processor during the operation.
 */
static
_IRQL_requires_(IPI_LEVEL)
ULONG_PTR
NTAPI
PciIpiGenericCallHandler(
    _In_ ULONG_PTR Argument)
{
    PPCI_IPI_CONTEXT IpiContext = (PPCI_IPI_CONTEXT)Argument;
    LONG Remaining;

    /* Atomically decrement the countdown */
    Remaining = InterlockedDecrement(&IpiContext->CountDown);

    if (Remaining == 0)
    {
        /* Last processor to arrive: execute the work function */
        IpiContext->Result = IpiContext->FunctionToCall(IpiContext->Context,
                                                       IpiContext->Parameter2);

        /* Signal completion to all spinning processors */
        InterlockedExchange(&IpiContext->CompletionFlag, 0);
    }
    else
    {
        /* Not the last processor: spin-wait for completion */
        while (InterlockedCompareExchange(&IpiContext->CompletionFlag, 1, 1) != 0)
        {
            YieldProcessor();
        }
    }

    return 0;
}

/**
 * @brief Execute a function under IPI synchronization across all processors.
 *
 * Used for debug devices where PCI config space access must not race with
 * debugger I/O on other processors. All processors rendezvous via IPI,
 * ensuring exclusive access to the PCI config space.
 */
NTSTATUS
PciExecuteIpiConfig(
    _In_ PPCI_IPI_FUNCTION FunctionToCall,
    _In_ PVOID Context,
    _In_opt_ PVOID Parameter2)
{
    PCI_IPI_CONTEXT IpiContext;

    KAFFINITY ActiveProcessors;
    ULONG ProcessorCount;

    ActiveProcessors = KeQueryActiveProcessors();
    for (ProcessorCount = 0; ActiveProcessors != 0; ActiveProcessors >>= 1)
    {
        ProcessorCount += (ULONG)(ActiveProcessors & 1);
    }

    RtlZeroMemory(&IpiContext, sizeof(IpiContext));
    IpiContext.CountDown = (LONG)ProcessorCount;
    IpiContext.CompletionFlag = 1;
    IpiContext.FunctionToCall = FunctionToCall;
    IpiContext.Context = Context;
    IpiContext.Parameter2 = Parameter2;

    KeIpiGenericCall(PciIpiGenericCallHandler, (ULONG_PTR)&IpiContext);

    return IpiContext.Result;
}

/**
 * @brief Enable or disable I/O and memory space decoding for a PCI device.
 *
 * Reads the PCI Command register, sets or clears the IO_SPACE and MEMORY_SPACE
 * bits, and writes it back.
 *
 * When called via PciExecuteIpiConfig, Context is the PPCI_DEVICE and
 * Parameter2 is (PVOID)(ULONG_PTR)Enable (TRUE to enable, FALSE to disable).
 */
NTSTATUS
NTAPI
PciDecodeEnable(
    _In_ PVOID Context,
    _In_ PVOID Parameter2)
{
    PPCI_DEVICE Device = (PPCI_DEVICE)Context;
    BOOLEAN Enable = (BOOLEAN)(ULONG_PTR)Parameter2;
    USHORT Command;

    /* Read current command register */
    PciReadDeviceConfig(Device, &Command,
                        FIELD_OFFSET(PCI_COMMON_CONFIG, Command),
                        sizeof(USHORT));

    if (Enable)
    {
        Command |= (PCI_ENABLE_IO_SPACE | PCI_ENABLE_MEMORY_SPACE);
    }
    else
    {
        Command &= ~(PCI_ENABLE_IO_SPACE | PCI_ENABLE_MEMORY_SPACE);
    }

    /* Write back modified command register */
    PciWriteDeviceConfig(Device, &Command,
                         FIELD_OFFSET(PCI_COMMON_CONFIG, Command),
                         sizeof(USHORT));

    return STATUS_SUCCESS;
}
