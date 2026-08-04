/*
 * PROJECT:         ReactOS PCI Bus driver
 * FILE:            pci.c
 * PURPOSE:         Driver entry
 * PROGRAMMERS:     Casper S. Hornstrup (chorns@users.sourceforge.net)
 * UPDATE HISTORY:
 *      10-09-2001  CSH  Created
 */

#include <initguid.h>
#include "pci.h"

#include <stdio.h>
#include <wdmguid.h>
#include <ndk/halfuncs.h>

#define NDEBUG
#include <debug.h>

/*
 * ACPI device interface state.
 * Used to communicate with acpi.sys via device interface instead of direct import.
 */
static PFILE_OBJECT AcpiInterfaceFileObject = NULL;
static PDEVICE_OBJECT AcpiInterfaceDeviceObject = NULL;
static BOOLEAN AcpiInterfaceInitialized = FALSE;
static FAST_MUTEX AcpiInterfaceMutex;
static BOOLEAN AcpiInterfaceMutexInitialized = FALSE;

static DRIVER_DISPATCH PciDispatchDeviceControl;
static NTSTATUS NTAPI PciDispatchDeviceControl(IN PDEVICE_OBJECT DeviceObject, IN PIRP Irp);

static DRIVER_ADD_DEVICE PciAddDevice;
static NTSTATUS NTAPI PciAddDevice(IN PDRIVER_OBJECT DriverObject, IN PDEVICE_OBJECT PhysicalDeviceObject);

static DRIVER_DISPATCH PciPowerControl;
static NTSTATUS NTAPI PciPowerControl(IN PDEVICE_OBJECT DeviceObject, IN PIRP Irp);

static DRIVER_DISPATCH PciPnpControl;
static NTSTATUS NTAPI PciPnpControl(IN PDEVICE_OBJECT DeviceObject, IN PIRP Irp);

static DRIVER_DISPATCH PciSystemControl;
static NTSTATUS NTAPI PciSystemControl(IN PDEVICE_OBJECT DeviceObject, IN PIRP Irp);

/*** PUBLIC ******************************************************************/

PPCI_DRIVER_EXTENSION DriverExtension = NULL;
BOOLEAN HasDebuggingDevice = FALSE;
PCI_TYPE1_CFG_CYCLE_BITS PciDebuggingDevice[2] = {0};
BOOLEAN PciMsiEnabledByPolicy = TRUE;
BOOLEAN PciMsixEnabledByPolicy = TRUE;
UNICODE_STRING PciRegistryPath = {0, 0, NULL};
KEVENT PciGlobalLock;
BOOLEAN PciLockDeviceResources = FALSE;
LIST_ENTRY PciFdoExtensionListHead;
KSPIN_LOCK PciGlobalSpinLock;
BOOLEAN PciEnableNativeModeATA = FALSE;

/* State transition validity table: PciStateTransitionValid[CurrentState][NewState] */
static const BOOLEAN PciStateTransitionValid[PciMaxObjectState][PciMaxObjectState] = {
    /* From PciNotStarted:     can go to Started, Deleted, SurpriseRemoved */
    { FALSE, TRUE, TRUE, FALSE, TRUE, FALSE },
    /* From PciStarted:        can go to Stopped, Deleted, SurpriseRemoved */
    { FALSE, FALSE, TRUE, TRUE, TRUE, FALSE },
    /* From PciDeleted:        terminal state, no transitions */
    { FALSE, FALSE, FALSE, FALSE, FALSE, FALSE },
    /* From PciStopped:        can go to Started, Deleted, SurpriseRemoved */
    { FALSE, TRUE, TRUE, FALSE, TRUE, FALSE },
    /* From PciSurpriseRemoved: can go to Deleted */
    { FALSE, FALSE, TRUE, FALSE, FALSE, FALSE },
    /* unused state 5 */
    { FALSE, FALSE, FALSE, FALSE, FALSE, FALSE },
};

NTSTATUS
PciBeginStateTransition(
    _Inout_ PFDO_DEVICE_EXTENSION DeviceExtension,
    _In_ PCI_DEVICE_STATE NewState)
{
    PCI_DEVICE_STATE CurrentState;

    CurrentState = DeviceExtension->State;

    ASSERT(CurrentState < PciMaxObjectState);
    ASSERT(NewState < PciMaxObjectState);

    if (!PciStateTransitionValid[CurrentState][NewState])
    {
        DPRINT1("PCI: Invalid state transition from %d to %d\n",
                CurrentState, NewState);
        return STATUS_INVALID_DEVICE_REQUEST;
    }

    /* Save current state as previous and set new state */
    DeviceExtension->PreviousState = CurrentState;
    DeviceExtension->State = NewState;

    return STATUS_SUCCESS;
}

VOID
PciCommitStateTransition(
    _Inout_ PFDO_DEVICE_EXTENSION DeviceExtension,
    _In_ PCI_DEVICE_STATE NewState)
{
    ASSERT(DeviceExtension->State == NewState);

    /* Clear PreviousState by setting it to the same as current */
    DeviceExtension->PreviousState = NewState;
}

NTSTATUS
PciCancelStateTransition(
    _Inout_ PFDO_DEVICE_EXTENSION DeviceExtension,
    _In_ PCI_DEVICE_STATE NewState)
{
    UNREFERENCED_PARAMETER(NewState);

    /* Restore state from PreviousState */
    DeviceExtension->State = DeviceExtension->PreviousState;

    return STATUS_SUCCESS;
}

/*** PRIVATE *****************************************************************/

/**
 * @brief Opens the ACPI device interface for PCI method evaluation.
 *
 * This function opens the ACPI-PCI device interface registered by acpi.sys
 * and caches the file object and device object for subsequent IOCTL calls.
 * The interface is opened lazily on first use.
 *
 * @return STATUS_SUCCESS if the interface was opened or already open.
 * @return STATUS_NOT_FOUND if the interface is not registered.
 * @return Other NTSTATUS error codes on failure.
 */
static
NTSTATUS
PciOpenAcpiInterface(VOID)
{
    NTSTATUS Status;
    PWSTR SymbolicLinkList;
    PWSTR SymbolicLink;
    UNICODE_STRING SymbolicLinkName;
    PFILE_OBJECT LocalFileObject = NULL;
    PDEVICE_OBJECT LocalDeviceObject = NULL;

    /* Quick check without lock for the common case */
    if (AcpiInterfaceInitialized)
    {
        return STATUS_SUCCESS;
    }

    /* Acquire mutex for exclusive access during initialization */
    ExAcquireFastMutex(&AcpiInterfaceMutex);

    /* Double-check after acquiring mutex */
    if (AcpiInterfaceInitialized)
    {
        ExReleaseFastMutex(&AcpiInterfaceMutex);
        return STATUS_SUCCESS;
    }

    /* Get the list of device interfaces with this GUID */
    Status = IoGetDeviceInterfaces(
        &GUID_ACPI_PCI_INTERFACE,
        NULL,
        0,
        &SymbolicLinkList);

    if (!NT_SUCCESS(Status))
    {
        DPRINT("PCI: Failed to get ACPI PCI interface list (0x%08lx)\n", Status);
        ExReleaseFastMutex(&AcpiInterfaceMutex);
        return Status;
    }

    /* Use the first interface in the list (multi-sz string) */
    SymbolicLink = SymbolicLinkList;
    if (!SymbolicLink || *SymbolicLink == UNICODE_NULL)
    {
        DPRINT("PCI: ACPI PCI interface not found\n");
        ExFreePool(SymbolicLinkList);
        ExReleaseFastMutex(&AcpiInterfaceMutex);
        return STATUS_NOT_FOUND;
    }

    RtlInitUnicodeString(&SymbolicLinkName, SymbolicLink);

    DPRINT("PCI: Opening ACPI interface: %wZ\n", &SymbolicLinkName);

    /*
     * Open the device interface.
     * FILE_READ_ATTRIBUTES is sufficient for sending IOCTLs; it avoids
     * potential access issues that FILE_READ_DATA/FILE_WRITE_DATA might cause.
     */
    Status = IoGetDeviceObjectPointer(
        &SymbolicLinkName,
        FILE_READ_ATTRIBUTES,
        &LocalFileObject,
        &LocalDeviceObject);

    ExFreePool(SymbolicLinkList);

    if (!NT_SUCCESS(Status))
    {
        DPRINT("PCI: Failed to open ACPI interface (0x%08lx)\n", Status);
        ExReleaseFastMutex(&AcpiInterfaceMutex);
        return Status;
    }

    /* Store the cached objects and mark as initialized */
    AcpiInterfaceFileObject = LocalFileObject;
    AcpiInterfaceDeviceObject = LocalDeviceObject;
    AcpiInterfaceInitialized = TRUE;

    ExReleaseFastMutex(&AcpiInterfaceMutex);

    DPRINT("PCI: ACPI interface opened successfully (FileObject %p, DeviceObject %p)\n",
           AcpiInterfaceFileObject, AcpiInterfaceDeviceObject);

    return STATUS_SUCCESS;
}

/**
 * @brief Closes the ACPI device interface and releases cached objects.
 *
 * This function should be called during driver unload to properly
 * dereference the file object obtained from IoGetDeviceObjectPointer.
 */
static
VOID
PciCloseAcpiInterface(VOID)
{
    PFILE_OBJECT LocalFileObject;

    ExAcquireFastMutex(&AcpiInterfaceMutex);

    if (AcpiInterfaceInitialized && AcpiInterfaceFileObject != NULL)
    {
        /* Save the file object pointer before clearing globals */
        LocalFileObject = AcpiInterfaceFileObject;

        /* Clear globals first */
        AcpiInterfaceFileObject = NULL;
        AcpiInterfaceDeviceObject = NULL;
        AcpiInterfaceInitialized = FALSE;

        ExReleaseFastMutex(&AcpiInterfaceMutex);

        /*
         * Dereference the file object outside the lock.
         * IoGetDeviceObjectPointer returns a referenced file object
         * that we must dereference when done.
         */
        ObDereferenceObject(LocalFileObject);

        DPRINT("PCI: ACPI interface closed\n");
    }
    else
    {
        ExReleaseFastMutex(&AcpiInterfaceMutex);
    }
}

/**
 * @brief Evaluates an ACPI method for a PCI device via the ACPI device interface.
 *
 * This function sends IOCTL_ACPI_EVAL_METHOD_FOR_PCI to the ACPI driver via
 * its device interface. The IOCTL includes the PCI device location and the
 * standard ACPI evaluation input buffer.
 *
 * @param Segment       PCI segment number (typically 0)
 * @param Bus           PCI bus number
 * @param Device        PCI device (slot) number
 * @param Function      PCI function number
 * @param InputBuffer   Standard ACPI evaluation input buffer
 * @param InputBufferSize Size of the input buffer
 * @param OutputBuffer  Buffer to receive evaluation results (may be NULL)
 * @param OutputBufferSize Size of the output buffer
 * @param BytesReturned Receives the number of bytes returned (may be NULL)
 *
 * @return STATUS_SUCCESS on success
 * @return STATUS_NOT_FOUND if the ACPI interface or device is not available
 * @return Other NTSTATUS error codes on failure
 */
NTSTATUS
PciAcpiEvalMethod(
    _In_ ULONG Segment,
    _In_ ULONG Bus,
    _In_ ULONG Device,
    _In_ ULONG Function,
    _In_ PACPI_EVAL_INPUT_BUFFER InputBuffer,
    _In_ ULONG InputBufferSize,
    _Out_writes_bytes_opt_(OutputBufferSize) PACPI_EVAL_OUTPUT_BUFFER OutputBuffer,
    _In_ ULONG OutputBufferSize,
    _Out_opt_ PULONG BytesReturned)
{
    NTSTATUS Status;
    PACPI_PCI_EVAL_INPUT_BUFFER PciBuffer = NULL;
    ULONG PciBufferSize;
    KEVENT Event;
    IO_STATUS_BLOCK IoStatusBlock;
    PIRP Irp;

    if (BytesReturned)
        *BytesReturned = 0;

    /* Open the ACPI interface if not already done */
    Status = PciOpenAcpiInterface();
    if (!NT_SUCCESS(Status))
    {
        DPRINT("PCI: ACPI interface not available (0x%08lx)\n", Status);
        return Status;
    }

    /*
     * Validate and compute buffer size using safe arithmetic.
     * Prevent integer overflow when computing total buffer size.
     */
    Status = RtlULongAdd(sizeof(ACPI_PCI_EVAL_INPUT_BUFFER), InputBufferSize, &PciBufferSize);
    if (!NT_SUCCESS(Status))
    {
        DPRINT("PCI: Input buffer size too large (overflow)\n");
        return STATUS_INTEGER_OVERFLOW;
    }

    /* Sanity check: reject unreasonably large buffers */
    if (PciBufferSize > 64 * 1024)
    {
        DPRINT("PCI: Input buffer size too large (%lu bytes)\n", PciBufferSize);
        return STATUS_INVALID_BUFFER_SIZE;
    }

    PciBuffer = ExAllocatePoolWithTag(NonPagedPool, PciBufferSize, TAG_PCI_ACPI);
    if (!PciBuffer)
    {
        DPRINT("PCI: Failed to allocate buffer for ACPI IOCTL\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    PciBuffer->Signature = ACPI_PCI_EVAL_INPUT_BUFFER_SIGNATURE;
    PciBuffer->Segment = Segment;
    PciBuffer->Bus = Bus;
    PciBuffer->Device = Device;
    PciBuffer->Function = Function;
    PciBuffer->TotalSize = PciBufferSize;
    PciBuffer->InputBufferOffset = sizeof(ACPI_PCI_EVAL_INPUT_BUFFER);
    PciBuffer->InputBufferSize = InputBufferSize;

    /* Copy the embedded ACPI input buffer */
    RtlCopyMemory(
        (PUCHAR)PciBuffer + PciBuffer->InputBufferOffset,
        InputBuffer,
        InputBufferSize);

    /* Initialize event for synchronous IRP */
    KeInitializeEvent(&Event, NotificationEvent, FALSE);

    /* Build the IOCTL IRP */
    Irp = IoBuildDeviceIoControlRequest(
        IOCTL_ACPI_EVAL_METHOD_FOR_PCI,
        AcpiInterfaceDeviceObject,
        PciBuffer,
        PciBufferSize,
        OutputBuffer,
        OutputBufferSize,
        FALSE,
        &Event,
        &IoStatusBlock);

    if (!Irp)
    {
        DPRINT("PCI: Failed to build IRP for ACPI IOCTL\n");
        ExFreePoolWithTag(PciBuffer, TAG_PCI_ACPI);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Send the IRP */
    Status = IoCallDriver(AcpiInterfaceDeviceObject, Irp);

    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatusBlock.Status;
    }

    ExFreePoolWithTag(PciBuffer, TAG_PCI_ACPI);

    if (NT_SUCCESS(Status) && BytesReturned)
    {
        *BytesReturned = (ULONG)IoStatusBlock.Information;
    }

    DPRINT("PCI: ACPI IOCTL completed with status 0x%08lx, bytes %lu\n",
           Status, (ULONG)IoStatusBlock.Information);

    return Status;
}

NTSTATUS
PciAcpiSetPower(_In_ ULONG Segment, _In_ ULONG Bus, _In_ ULONG Device, _In_ ULONG Function, _In_ ULONG State)
{
    ACPI_PCI_SET_POWER_INPUT_BUFFER InputBuffer;
    KEVENT Event;
    IO_STATUS_BLOCK IoStatusBlock;
    PIRP Irp;
    NTSTATUS Status;

    Status = PciOpenAcpiInterface();
    if (!NT_SUCCESS(Status))
        return Status;

    RtlZeroMemory(&InputBuffer, sizeof(InputBuffer));
    InputBuffer.Signature = ACPI_PCI_SET_POWER_INPUT_BUFFER_SIGNATURE;
    InputBuffer.Segment = Segment;
    InputBuffer.Bus = Bus;
    InputBuffer.Device = Device;
    InputBuffer.Function = Function;
    InputBuffer.State = State;
    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp = IoBuildDeviceIoControlRequest(IOCTL_ACPI_SET_POWER_FOR_PCI, AcpiInterfaceDeviceObject, &InputBuffer, sizeof(InputBuffer), NULL, 0, FALSE, &Event, &IoStatusBlock);
    if (!Irp)
        return STATUS_INSUFFICIENT_RESOURCES;

    Status = IoCallDriver(AcpiInterfaceDeviceObject, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatusBlock.Status;
    }
    return Status;
}

static NTSTATUS
NTAPI
PciDispatchDeviceControl(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp)
{
    PIO_STACK_LOCATION IrpSp;
    NTSTATUS Status;
    PCOMMON_DEVICE_EXTENSION CommonExtension;

    DPRINT("Called. IRP is at (0x%p)\n", Irp);

    CommonExtension = (PCOMMON_DEVICE_EXTENSION)DeviceObject->DeviceExtension;
    Irp->IoStatus.Information = 0;

    IrpSp = IoGetCurrentIrpStackLocation(Irp);

    /*
     * For PDOs, forward ACPI IOCTLs to the ACPI driver.
     * This allows drivers above the PCI PDO (like xHCI) to evaluate
     * ACPI methods on the corresponding ACPI namespace node.
     */
    if (!CommonExtension->IsFDO)
    {
        switch (IrpSp->Parameters.DeviceIoControl.IoControlCode)
        {
            case IOCTL_ACPI_EVAL_METHOD:
            {
                PPDO_DEVICE_EXTENSION PdoExtension;
                PFDO_DEVICE_EXTENSION FdoExtension;
                ULONG Segment = 0;
                PACPI_EVAL_INPUT_BUFFER InputBuffer;
                ULONG InputBufferSize;
                PACPI_EVAL_OUTPUT_BUFFER OutputBuffer;
                ULONG OutputBufferSize;
                ULONG BytesReturned;

                PdoExtension = (PPDO_DEVICE_EXTENSION)DeviceObject->DeviceExtension;
                if (PdoExtension->Fdo)
                {
                    FdoExtension = (PFDO_DEVICE_EXTENSION)PdoExtension->Fdo->DeviceExtension;
                    Segment = FdoExtension->BusSegment;
                }

                InputBuffer = (PACPI_EVAL_INPUT_BUFFER)Irp->AssociatedIrp.SystemBuffer;
                InputBufferSize = IrpSp->Parameters.DeviceIoControl.InputBufferLength;
                OutputBuffer = (PACPI_EVAL_OUTPUT_BUFFER)Irp->AssociatedIrp.SystemBuffer;
                OutputBufferSize = IrpSp->Parameters.DeviceIoControl.OutputBufferLength;

                DPRINT("PCI: Forwarding IOCTL_ACPI_EVAL_METHOD for device %u:%lu:%u:%u\n",
                       Segment,
                       PdoExtension->PciDevice->BusNumber,
                       PdoExtension->PciDevice->SlotNumber.u.bits.DeviceNumber,
                       PdoExtension->PciDevice->SlotNumber.u.bits.FunctionNumber);

                /* Use the device interface to evaluate the ACPI method */
                Status = PciAcpiEvalMethod(
                    Segment,
                    PdoExtension->PciDevice->BusNumber,
                    PdoExtension->PciDevice->SlotNumber.u.bits.DeviceNumber,
                    PdoExtension->PciDevice->SlotNumber.u.bits.FunctionNumber,
                    InputBuffer,
                    InputBufferSize,
                    OutputBuffer,
                    OutputBufferSize,
                    &BytesReturned);

                if (NT_SUCCESS(Status))
                {
                    Irp->IoStatus.Information = BytesReturned;
                }
                break;
            }

            default:
                DPRINT("Unknown IOCTL 0x%X on PDO\n", IrpSp->Parameters.DeviceIoControl.IoControlCode);
                Status = STATUS_NOT_IMPLEMENTED;
                break;
        }
    }
    else
    {
        /* FDO - just return not implemented for now */
        switch (IrpSp->Parameters.DeviceIoControl.IoControlCode)
        {
            default:
                DPRINT("Unknown IOCTL 0x%X on FDO\n", IrpSp->Parameters.DeviceIoControl.IoControlCode);
                Status = STATUS_NOT_IMPLEMENTED;
                break;
        }
    }

    if (Status != STATUS_PENDING)
    {
        Irp->IoStatus.Status = Status;

        DPRINT("Completing IRP at 0x%p\n", Irp);

        IoCompleteRequest(Irp, IO_NO_INCREMENT);
    }

    DPRINT("Leaving. Status 0x%X\n", Status);

    return Status;
}


static NTSTATUS
NTAPI
PciPnpControl(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp)
/*
 * FUNCTION: Handle Plug and Play IRPs
 * ARGUMENTS:
 *     DeviceObject = Pointer to PDO or FDO
 *     Irp          = Pointer to IRP that should be handled
 * RETURNS:
 *     Status
 */
{
    PCOMMON_DEVICE_EXTENSION DeviceExtension;
    NTSTATUS Status;

    DeviceExtension = (PCOMMON_DEVICE_EXTENSION)DeviceObject->DeviceExtension;

    DPRINT("IsFDO %u\n", DeviceExtension->IsFDO);

    if (DeviceExtension->IsFDO)
    {
        Status = FdoPnpControl(DeviceObject, Irp);
    }
    else
    {
        Status = PdoPnpControl(DeviceObject, Irp);
    }

    return Status;
}


static NTSTATUS
NTAPI
PciPowerControl(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp)
/*
 * FUNCTION: Handle power management IRPs
 * ARGUMENTS:
 *     DeviceObject = Pointer to PDO or FDO
 *     Irp          = Pointer to IRP that should be handled
 * RETURNS:
 *     Status
 */
{
    PCOMMON_DEVICE_EXTENSION DeviceExtension;
    NTSTATUS Status;

    DeviceExtension = (PCOMMON_DEVICE_EXTENSION)DeviceObject->DeviceExtension;

    if (DeviceExtension->IsFDO)
    {
        Status = FdoPowerControl(DeviceObject, Irp);
    }
    else
    {
        Status = PdoPowerControl(DeviceObject, Irp);
    }

    return Status;
}


static NTSTATUS
NTAPI
PciSystemControl(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp)
/*
 * FUNCTION: Handle IRP_MJ_SYSTEM_CONTROL (WMI) IRPs
 * ARGUMENTS:
 *     DeviceObject = Pointer to PDO or FDO
 *     Irp          = Pointer to IRP that should be handled
 * RETURNS:
 *     Status
 */
{
    PCOMMON_DEVICE_EXTENSION DeviceExtension;

    DeviceExtension = (PCOMMON_DEVICE_EXTENSION)DeviceObject->DeviceExtension;

    if (DeviceExtension->IsFDO)
    {
        PFDO_DEVICE_EXTENSION FdoDeviceExtension;

        FdoDeviceExtension = (PFDO_DEVICE_EXTENSION)DeviceObject->DeviceExtension;

        /* Forward to lower device */
        IoSkipCurrentIrpStackLocation(Irp);
        return IoCallDriver(FdoDeviceExtension->Ldo, Irp);
    }
    else
    {
        /* Complete the IRP with its current status */
        NTSTATUS Status = Irp->IoStatus.Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return Status;
    }
}


static NTSTATUS
NTAPI
PciAddDevice(
    IN PDRIVER_OBJECT DriverObject,
    IN PDEVICE_OBJECT PhysicalDeviceObject)
{
    PFDO_DEVICE_EXTENSION DeviceExtension;
    PDEVICE_OBJECT Fdo;
    NTSTATUS Status;

    DPRINT("Called\n");
    if (PhysicalDeviceObject == NULL)
        return STATUS_SUCCESS;

    Status = IoCreateDevice(DriverObject,
                            sizeof(FDO_DEVICE_EXTENSION),
                            NULL,
                            FILE_DEVICE_BUS_EXTENDER,
                            FILE_DEVICE_SECURE_OPEN,
                            TRUE,
                            &Fdo);
    if (!NT_SUCCESS(Status))
    {
        DPRINT("IoCreateDevice() failed with status 0x%X\n", Status);
        return Status;
    }

    DeviceExtension = (PFDO_DEVICE_EXTENSION)Fdo->DeviceExtension;

    RtlZeroMemory(DeviceExtension, sizeof(FDO_DEVICE_EXTENSION));

    IoInitializeRemoveLock(&DeviceExtension->RemoveLock, TAG_PCI, 0, 0);

    DeviceExtension->Common.IsFDO = TRUE;
    DeviceExtension->Common.DeviceObject = Fdo;
    DeviceExtension->Common.DevicePowerState = PowerDeviceD0;
    DeviceExtension->MsiSupported = TRUE;

    DeviceExtension->Ldo = IoAttachDeviceToDeviceStack(Fdo,
                                                       PhysicalDeviceObject);

    DeviceExtension->State = PciNotStarted;

    Fdo->Flags &= ~DO_DEVICE_INITIALIZING;

    //Fdo->Flags |= DO_POWER_PAGABLE;

    DPRINT("Done AddDevice\n");

    return STATUS_SUCCESS;
}

DRIVER_UNLOAD PciUnload;

VOID
NTAPI
PciUnload(
    IN PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);

    /* Restore HAL dispatch table hooks */
    if (PcipSavedAssignSlotResources)
    {
        HalPciAssignSlotResources = PcipSavedAssignSlotResources;
        PcipSavedAssignSlotResources = NULL;
    }
    if (PcipSavedTranslateBusAddress)
    {
        HalPciTranslateBusAddress = PcipSavedTranslateBusAddress;
        PcipSavedTranslateBusAddress = NULL;
    }

    /* Free the registry path copy */
    if (PciRegistryPath.Buffer)
    {
        ExFreePoolWithTag(PciRegistryPath.Buffer, TAG_PCI);
        PciRegistryPath.Buffer = NULL;
        PciRegistryPath.Length = 0;
        PciRegistryPath.MaximumLength = 0;
    }

    /*
     * Close the ACPI device interface and dereference the cached file object.
     * This must be done before driver unload completes to avoid resource leaks.
     */
    PciCloseAcpiInterface();

    /* The driver object extension is destroyed by the I/O manager */
}

static
CODE_SEG("INIT")
VOID
PciLocateKdDevices(VOID)
{
    ULONG i;
    NTSTATUS Status;
    WCHAR KeyNameBuffer[16];
    ULONG BusNumber, SlotNumber;
    RTL_QUERY_REGISTRY_TABLE QueryTable[3];

    RtlZeroMemory(QueryTable, sizeof(QueryTable));
    QueryTable[0].Flags = RTL_QUERY_REGISTRY_DIRECT | RTL_QUERY_REGISTRY_REQUIRED;
    QueryTable[0].Name = L"Bus";
    QueryTable[0].EntryContext = &BusNumber;
    QueryTable[1].Flags = RTL_QUERY_REGISTRY_DIRECT | RTL_QUERY_REGISTRY_REQUIRED;
    QueryTable[1].Name = L"Slot";
    QueryTable[1].EntryContext = &SlotNumber;

    for (i = 0; i < RTL_NUMBER_OF(PciDebuggingDevice); ++i)
    {
        PCI_SLOT_NUMBER PciSlot;

        RtlStringCbPrintfW(KeyNameBuffer, sizeof(KeyNameBuffer), L"PCI\\Debug\\%d", i);

        Status = RtlQueryRegistryValues(RTL_REGISTRY_SERVICES,
                                        KeyNameBuffer,
                                        QueryTable,
                                        NULL,
                                        NULL);
        if (!NT_SUCCESS(Status))
            return;

        HasDebuggingDevice = TRUE;

        PciSlot.u.AsULONG = SlotNumber;
        PciDebuggingDevice[i].DeviceNumber = PciSlot.u.bits.DeviceNumber;
        PciDebuggingDevice[i].FunctionNumber = PciSlot.u.bits.FunctionNumber;
        PciDebuggingDevice[i].BusNumber = BusNumber;
        PciDebuggingDevice[i].InUse = TRUE;

        DPRINT1("PCI debugging device %02x:%02x.%x\n",
                BusNumber,
                PciSlot.u.bits.DeviceNumber,
                PciSlot.u.bits.FunctionNumber);
    }
}

static
CODE_SEG("INIT")
VOID
PciReadInterruptPolicy(
    _In_opt_ PUNICODE_STRING RegistryPath)
{
    RTL_QUERY_REGISTRY_TABLE QueryTable[3];
    ULONG EnableMsi = 1;
    ULONG EnableMsix = 1;
    WCHAR ParametersBuffer[512];
    UNICODE_STRING ParametersPath;
    NTSTATUS Status;

    /*
     * On x86 ACPI systems, MSI is always architecturally supported.
     * Per-root-bridge _OSC evaluation may disable it later in FdoStartDevice.
     * This matches Windows pci.sys behavior — MSI support is determined
     * entirely within the PCI driver, not via HAL exports.
     */
    PciMsiEnabledByPolicy = TRUE;
    PciMsixEnabledByPolicy = TRUE;

    if (!RegistryPath || RegistryPath->Length == 0)
        return;

    ParametersPath.Buffer = ParametersBuffer;
    ParametersPath.MaximumLength = sizeof(ParametersBuffer);
    ParametersPath.Length = 0;

    if (RegistryPath->Length >= ParametersPath.MaximumLength)
        return;

    RtlCopyUnicodeString(&ParametersPath, RegistryPath);
    Status = RtlAppendUnicodeToString(&ParametersPath, L"\\Parameters");
    if (!NT_SUCCESS(Status))
        return;

    RtlZeroMemory(QueryTable, sizeof(QueryTable));
    QueryTable[0].Flags = RTL_QUERY_REGISTRY_DIRECT;
    QueryTable[0].Name = L"EnableMSI";
    QueryTable[0].EntryContext = &EnableMsi;
    QueryTable[1].Flags = RTL_QUERY_REGISTRY_DIRECT;
    QueryTable[1].Name = L"EnableMSIX";
    QueryTable[1].EntryContext = &EnableMsix;

    Status = RtlQueryRegistryValues(RTL_REGISTRY_ABSOLUTE,
                                    ParametersPath.Buffer,
                                    QueryTable,
                                    NULL,
                                    NULL);
    if (NT_SUCCESS(Status))
    {
        PciMsiEnabledByPolicy = (EnableMsi != 0);
        PciMsixEnabledByPolicy = (EnableMsix != 0);
    }
}

CODE_SEG("INIT")
NTSTATUS
NTAPI
DriverEntry(
    IN PDRIVER_OBJECT DriverObject,
    IN PUNICODE_STRING RegistryPath)
{
    NTSTATUS Status;

    DPRINT("Peripheral Component Interconnect Bus Driver\n");

    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = PciDispatchDeviceControl;
    DriverObject->MajorFunction[IRP_MJ_PNP] = PciPnpControl;
    DriverObject->MajorFunction[IRP_MJ_POWER] = PciPowerControl;
    DriverObject->MajorFunction[IRP_MJ_SYSTEM_CONTROL] = PciSystemControl;
    DriverObject->DriverExtension->AddDevice = PciAddDevice;
    DriverObject->DriverUnload = PciUnload;

    Status = IoAllocateDriverObjectExtension(DriverObject,
                                             DriverObject,
                                             sizeof(PCI_DRIVER_EXTENSION),
                                             (PVOID*)&DriverExtension);
    if (!NT_SUCCESS(Status))
        return Status;

    RtlZeroMemory(DriverExtension, sizeof(PCI_DRIVER_EXTENSION));

    InitializeListHead(&DriverExtension->BusListHead);
    KeInitializeSpinLock(&DriverExtension->BusListLock);

    /* Initialize global synchronization */
    KeInitializeEvent(&PciGlobalLock, SynchronizationEvent, TRUE);
    InitializeListHead(&PciFdoExtensionListHead);
    KeInitializeSpinLock(&PciGlobalSpinLock);

    /* Copy the registry path for later use */
    PciRegistryPath.MaximumLength = RegistryPath->Length + sizeof(UNICODE_NULL);
    PciRegistryPath.Buffer = ExAllocatePoolWithTag(NonPagedPool,
                                                    PciRegistryPath.MaximumLength,
                                                    TAG_PCI);
    if (PciRegistryPath.Buffer)
    {
        RtlCopyUnicodeString(&PciRegistryPath, RegistryPath);
    }

    /* Check boot options for PCILOCK */
    {
        RTL_QUERY_REGISTRY_TABLE QueryTable[2];
        UNICODE_STRING BootOptions = {0, 0, NULL};

        RtlZeroMemory(QueryTable, sizeof(QueryTable));
        QueryTable[0].Flags = RTL_QUERY_REGISTRY_DIRECT;
        QueryTable[0].Name = L"SystemStartOptions";
        QueryTable[0].EntryContext = &BootOptions;

        Status = RtlQueryRegistryValues(RTL_REGISTRY_CONTROL,
                                        NULL,
                                        QueryTable,
                                        NULL,
                                        NULL);
        if (NT_SUCCESS(Status) && BootOptions.Buffer)
        {
            if (wcsstr(BootOptions.Buffer, L"PCILOCK"))
            {
                PciLockDeviceResources = TRUE;
                DPRINT1("PCI: PCILOCK enabled from boot options\n");
            }
            RtlFreeUnicodeString(&BootOptions);
        }
    }

    /* Initialize the ACPI interface mutex for lazy initialization synchronization */
    ExInitializeFastMutex(&AcpiInterfaceMutex);
    AcpiInterfaceMutexInitialized = TRUE;

    /* Hook HAL dispatch table for PCI bus address translation and slot resources */
    PciHookHal();

    PciReadInterruptPolicy(RegistryPath);
    PciLocateKdDevices();

    return STATUS_SUCCESS;
}


NTSTATUS
PciCreateDeviceIDString(PUNICODE_STRING DeviceID,
                        PPCI_DEVICE Device)
{
    WCHAR Buffer[256];

    _swprintf(Buffer,
              L"PCI\\VEN_%04X&DEV_%04X&SUBSYS_%08X&REV_%02X",
              Device->PciConfig.VendorID,
              Device->PciConfig.DeviceID,
              (Device->PciConfig.u.type0.SubSystemID << 16) +
              Device->PciConfig.u.type0.SubVendorID,
              Device->PciConfig.RevisionID);

    return RtlCreateUnicodeString(DeviceID, Buffer) ? STATUS_SUCCESS : STATUS_INSUFFICIENT_RESOURCES;
}


NTSTATUS
PciCreateInstanceIDString(PUNICODE_STRING InstanceID,
                          PPCI_DEVICE Device)
{
    WCHAR Buffer[3];

    _swprintf(Buffer, L"%02X", Device->SlotNumber.u.AsULONG & 0xff);

    return RtlCreateUnicodeString(InstanceID, Buffer) ? STATUS_SUCCESS : STATUS_INSUFFICIENT_RESOURCES;
}


NTSTATUS
PciCreateHardwareIDsString(PUNICODE_STRING HardwareIDs,
                           PPCI_DEVICE Device)
{
    WCHAR Buffer[256];
    UNICODE_STRING BufferU;
    ULONG Index;

    Index = 0;
    Index += _swprintf(&Buffer[Index],
                       L"PCI\\VEN_%04X&DEV_%04X&SUBSYS_%08X&REV_%02X",
                       Device->PciConfig.VendorID,
                       Device->PciConfig.DeviceID,
                       (Device->PciConfig.u.type0.SubSystemID << 16) +
                       Device->PciConfig.u.type0.SubVendorID,
                       Device->PciConfig.RevisionID);
    Index++;

    Index += _swprintf(&Buffer[Index],
                       L"PCI\\VEN_%04X&DEV_%04X&SUBSYS_%08X",
                       Device->PciConfig.VendorID,
                       Device->PciConfig.DeviceID,
                       (Device->PciConfig.u.type0.SubSystemID << 16) +
                       Device->PciConfig.u.type0.SubVendorID);
    Index++;

    Index += _swprintf(&Buffer[Index],
                       L"PCI\\VEN_%04X&DEV_%04X&CC_%02X%02X%02X",
                       Device->PciConfig.VendorID,
                       Device->PciConfig.DeviceID,
                       Device->PciConfig.BaseClass,
                       Device->PciConfig.SubClass,
                       Device->PciConfig.ProgIf);
    Index++;

    Index += _swprintf(&Buffer[Index],
                       L"PCI\\VEN_%04X&DEV_%04X&CC_%02X%02X",
                       Device->PciConfig.VendorID,
                       Device->PciConfig.DeviceID,
                       Device->PciConfig.BaseClass,
                       Device->PciConfig.SubClass);
    Index++;

    Buffer[Index] = UNICODE_NULL;

    BufferU.Length = BufferU.MaximumLength = (USHORT) Index * sizeof(WCHAR);
    BufferU.Buffer = Buffer;

    return PciDuplicateUnicodeString(0, &BufferU, HardwareIDs);
}


NTSTATUS
PciCreateCompatibleIDsString(PUNICODE_STRING CompatibleIDs,
                             PPCI_DEVICE Device)
{
    WCHAR Buffer[256];
    UNICODE_STRING BufferU;
    ULONG Index;

    Index = 0;
    Index += _swprintf(&Buffer[Index],
                       L"PCI\\VEN_%04X&DEV_%04X&REV_%02X",
                       Device->PciConfig.VendorID,
                       Device->PciConfig.DeviceID,
                       Device->PciConfig.RevisionID);
    Index++;

    Index += _swprintf(&Buffer[Index],
                       L"PCI\\VEN_%04X&DEV_%04X",
                       Device->PciConfig.VendorID,
                       Device->PciConfig.DeviceID);
    Index++;

    Index += _swprintf(&Buffer[Index],
                       L"PCI\\VEN_%04X&CC_%02X%02X%02X",
                       Device->PciConfig.VendorID,
                       Device->PciConfig.BaseClass,
                       Device->PciConfig.SubClass,
                       Device->PciConfig.ProgIf);
    Index++;

    Index += _swprintf(&Buffer[Index],
                       L"PCI\\VEN_%04X&CC_%02X%02X",
                       Device->PciConfig.VendorID,
                       Device->PciConfig.BaseClass,
                       Device->PciConfig.SubClass);
    Index++;

    Index += _swprintf(&Buffer[Index],
                       L"PCI\\VEN_%04X",
                       Device->PciConfig.VendorID);
    Index++;

    Index += _swprintf(&Buffer[Index],
                       L"PCI\\CC_%02X%02X%02X",
                       Device->PciConfig.BaseClass,
                       Device->PciConfig.SubClass,
                       Device->PciConfig.ProgIf);
    Index++;

    Index += _swprintf(&Buffer[Index],
                       L"PCI\\CC_%02X%02X",
                       Device->PciConfig.BaseClass,
                       Device->PciConfig.SubClass);
    Index++;

    Buffer[Index] = UNICODE_NULL;

    BufferU.Length = BufferU.MaximumLength = (USHORT)Index * sizeof(WCHAR);
    BufferU.Buffer = Buffer;

    return PciDuplicateUnicodeString(0, &BufferU, CompatibleIDs);
}


NTSTATUS
PciCreateDeviceDescriptionString(PUNICODE_STRING DeviceDescription,
                                 PPCI_DEVICE Device)
{
    PCWSTR Description;

    switch (Device->PciConfig.BaseClass)
    {
        case PCI_CLASS_PRE_20:
            switch (Device->PciConfig.SubClass)
            {
                case PCI_SUBCLASS_PRE_20_VGA:
                    Description = L"VGA device";
                    break;

                default:
                case PCI_SUBCLASS_PRE_20_NON_VGA:
                    Description = L"PCI device";
                    break;
            }
            break;

        case PCI_CLASS_MASS_STORAGE_CTLR:
            switch (Device->PciConfig.SubClass)
            {
                case PCI_SUBCLASS_MSC_SCSI_BUS_CTLR:
                    Description = L"SCSI controller";
                    break;

                case PCI_SUBCLASS_MSC_IDE_CTLR:
                    Description = L"IDE controller";
                    break;

                case PCI_SUBCLASS_MSC_FLOPPY_CTLR:
                    Description = L"Floppy disk controller";
                    break;

                case PCI_SUBCLASS_MSC_IPI_CTLR:
                    Description = L"IPI controller";
                    break;

                case PCI_SUBCLASS_MSC_RAID_CTLR:
                    Description = L"RAID controller";
                    break;

                default:
                    Description = L"Mass storage controller";
                    break;
            }
            break;

        case PCI_CLASS_NETWORK_CTLR:
            switch (Device->PciConfig.SubClass)
            {
                case PCI_SUBCLASS_NET_ETHERNET_CTLR:
                    Description = L"Ethernet controller";
                    break;

                case PCI_SUBCLASS_NET_TOKEN_RING_CTLR:
                    Description = L"Token-Ring controller";
                    break;

                case PCI_SUBCLASS_NET_FDDI_CTLR:
                    Description = L"FDDI controller";
                    break;

                case PCI_SUBCLASS_NET_ATM_CTLR:
                    Description = L"ATM controller";
                    break;

                default:
                    Description = L"Network controller";
                    break;
            }
            break;

        case PCI_CLASS_DISPLAY_CTLR:
            switch (Device->PciConfig.SubClass)
            {
                case PCI_SUBCLASS_VID_VGA_CTLR:
                    Description = L"VGA display controller";
                    break;

                case PCI_SUBCLASS_VID_XGA_CTLR:
                    Description = L"XGA display controller";
                    break;

                case PCI_SUBCLASS_VID_3D_CTLR:
                    Description = L"Multimedia display controller";
                    break;

                default:
                    Description = L"Other display controller";
                    break;
            }
            break;

        case PCI_CLASS_MULTIMEDIA_DEV:
            switch (Device->PciConfig.SubClass)
            {
                case PCI_SUBCLASS_MM_VIDEO_DEV:
                    Description = L"Multimedia video device";
                    break;

                case PCI_SUBCLASS_MM_AUDIO_DEV:
                    Description = L"Multimedia audio device";
                    break;

                case PCI_SUBCLASS_MM_TELEPHONY_DEV:
                    Description = L"Multimedia telephony device";
                    break;

                default:
                    Description = L"Other multimedia device";
                    break;
            }
            break;

        case PCI_CLASS_MEMORY_CTLR:
            switch (Device->PciConfig.SubClass)
            {
                case PCI_SUBCLASS_MEM_RAM:
                    Description = L"PCI Memory";
                    break;

                case PCI_SUBCLASS_MEM_FLASH:
                    Description = L"PCI Flash Memory";
                    break;

                default:
                    Description = L"Other memory controller";
                    break;
            }
            break;

        case PCI_CLASS_BRIDGE_DEV:
            switch (Device->PciConfig.SubClass)
            {
                case PCI_SUBCLASS_BR_HOST:
                    Description = L"PCI-Host bridge";
                    break;

                case PCI_SUBCLASS_BR_ISA:
                    Description = L"PCI-ISA bridge";
                    break;

                case PCI_SUBCLASS_BR_EISA:
                    Description = L"PCI-EISA bridge";
                    break;

                case PCI_SUBCLASS_BR_MCA:
                    Description = L"PCI-Micro Channel bridge";
                    break;

                case PCI_SUBCLASS_BR_PCI_TO_PCI:
                    Description = L"PCI-PCI bridge";
                    break;

                case PCI_SUBCLASS_BR_PCMCIA:
                    Description = L"PCI-PCMCIA bridge";
                    break;

                case PCI_SUBCLASS_BR_NUBUS:
                    Description = L"PCI-NUBUS bridge";
                    break;

                case PCI_SUBCLASS_BR_CARDBUS:
                    Description = L"PCI-CARDBUS bridge";
                    break;

                default:
                    Description = L"Other bridge device";
                    break;
            }
            break;

        case PCI_CLASS_SIMPLE_COMMS_CTLR:
            switch (Device->PciConfig.SubClass)
            {

                default:
                    Description = L"Communication device";
                    break;
            }
            break;

        case PCI_CLASS_BASE_SYSTEM_DEV:
            switch (Device->PciConfig.SubClass)
            {

                default:
                    Description = L"System device";
                    break;
            }
            break;

        case PCI_CLASS_INPUT_DEV:
            switch (Device->PciConfig.SubClass)
            {

                default:
                    Description = L"Input device";
                    break;
            }
            break;

        case PCI_CLASS_DOCKING_STATION:
            switch (Device->PciConfig.SubClass)
            {

                default:
                    Description = L"Docking station";
                    break;
            }
            break;

        case PCI_CLASS_PROCESSOR:
            switch (Device->PciConfig.SubClass)
            {

                default:
                    Description = L"Processor";
                    break;
            }
            break;

        case PCI_CLASS_SERIAL_BUS_CTLR:
            switch (Device->PciConfig.SubClass)
            {
                case PCI_SUBCLASS_SB_IEEE1394:
                    Description = L"FireWire controller";
                    break;

                case PCI_SUBCLASS_SB_ACCESS:
                    Description = L"ACCESS bus controller";
                    break;

                case PCI_SUBCLASS_SB_SSA:
                    Description = L"SSA controller";
                    break;

                case PCI_SUBCLASS_SB_USB:
                    Description = L"USB controller";
                    break;

                case PCI_SUBCLASS_SB_FIBRE_CHANNEL:
                    Description = L"Fibre Channel controller";
                    break;

                case PCI_SUBCLASS_SB_SMBUS:
                    Description = L"SMBus controller";
                    break;

                default:
                    Description = L"Other serial bus controller";
                    break;
            }
            break;

        default:
            Description = L"Other PCI Device";
            break;
    }

    return RtlCreateUnicodeString(DeviceDescription, Description) ? STATUS_SUCCESS : STATUS_INSUFFICIENT_RESOURCES;
}


NTSTATUS
PciCreateDeviceLocationString(PUNICODE_STRING DeviceLocation,
                              PPCI_DEVICE Device)
{
    WCHAR Buffer[256];

    _swprintf(Buffer,
              L"PCI-Bus %lu, Device %u, Function %u",
              Device->BusNumber,
              Device->SlotNumber.u.bits.DeviceNumber,
              Device->SlotNumber.u.bits.FunctionNumber);

    return RtlCreateUnicodeString(DeviceLocation, Buffer) ? STATUS_SUCCESS : STATUS_INSUFFICIENT_RESOURCES;
}

NTSTATUS
PciDuplicateUnicodeString(
    IN ULONG Flags,
    IN PCUNICODE_STRING SourceString,
    OUT PUNICODE_STRING DestinationString)
{
    if (SourceString == NULL ||
        DestinationString == NULL ||
        SourceString->Length > SourceString->MaximumLength ||
        (SourceString->Length == 0 && SourceString->MaximumLength > 0 && SourceString->Buffer == NULL) ||
        Flags == RTL_DUPLICATE_UNICODE_STRING_ALLOCATE_NULL_STRING ||
        Flags >= 4)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if ((SourceString->Length == 0) &&
        (Flags != (RTL_DUPLICATE_UNICODE_STRING_NULL_TERMINATE |
                   RTL_DUPLICATE_UNICODE_STRING_ALLOCATE_NULL_STRING)))
    {
        DestinationString->Length = 0;
        DestinationString->MaximumLength = 0;
        DestinationString->Buffer = NULL;
    }
    else
    {
        USHORT DestMaxLength = SourceString->Length;

        if (Flags & RTL_DUPLICATE_UNICODE_STRING_NULL_TERMINATE)
            DestMaxLength += sizeof(UNICODE_NULL);

        DestinationString->Buffer = ExAllocatePoolWithTag(PagedPool, DestMaxLength, TAG_PCI);
        if (DestinationString->Buffer == NULL)
            return STATUS_NO_MEMORY;

        RtlCopyMemory(DestinationString->Buffer, SourceString->Buffer, SourceString->Length);
        DestinationString->Length = SourceString->Length;
        DestinationString->MaximumLength = DestMaxLength;

        if (Flags & RTL_DUPLICATE_UNICODE_STRING_NULL_TERMINATE)
            DestinationString->Buffer[DestinationString->Length / sizeof(WCHAR)] = 0;
    }

    return STATUS_SUCCESS;
}

/* EOF */
