/*
 * PROJECT:        ReactOS Kernel
 * LICENSE:        GNU GPLv2 only as published by the Free Software Foundation
 * PURPOSE:        To Implement AHCI Miniport driver targeting storport NT 5.2
 * PROGRAMMERS:    Aman Priyadarshi (aman.eureka@gmail.com)
 */

#include "storahci.h"

#define AHCI_MAX_SRBS_TO_DRAIN  (MAXIMUM_QUEUE_BUFFER_SIZE * 2 + MAXIMUM_AHCI_PORT_NCS)
#define AHCI_COMPLETION_WORK_BURST 16

static
BOOLEAN
AhciStopPort(
    __in PAHCI_PORT_EXTENSION PortExtension,
    __in ULONG TimeoutUsec
    );

static
BOOLEAN
AhciResetPortAndFailRequests(
    __in PAHCI_ADAPTER_EXTENSION AdapterExtension,
    __in PAHCI_PORT_EXTENSION PortExtension,
    __in UCHAR SrbStatus,
    __in UCHAR ScsiStatus,
    __in BOOLEAN RequestHardReset
    );

static
VOID
AhciDrainPendingRequests(
    __in PAHCI_PORT_EXTENSION PortExtension,
    __in UCHAR SrbStatus,
    __in UCHAR ScsiStatus,
    __inout PSCSI_REQUEST_BLOCK *CompletionList,
    __in ULONG CompletionListCapacity,
    __inout PULONG CompletionCount
    );

static
VOID
AhciCompleteRequest(
    __in PAHCI_ADAPTER_EXTENSION AdapterExtension,
    __in_opt PAHCI_PORT_EXTENSION PortExtension,
    __in PSCSI_REQUEST_BLOCK Srb
    );

static
VOID
AhciSchedulePortCompletionWorker(
    __in PAHCI_PORT_EXTENSION PortExtension
    );

static
VOID
AhciQueueAdapterCompletion(
    __in PAHCI_ADAPTER_EXTENSION AdapterExtension,
    __in PSCSI_REQUEST_BLOCK Srb
    );

static
VOID
AhciStopCompletionThread(
    __in PAHCI_ADAPTER_EXTENSION AdapterExtension
    );

static VOID NTAPI AhciCompletionWorkerThread(
    __in PVOID Context
    );

static
BOOLEAN
AhciQueuePortCompletion(
    __in PAHCI_PORT_EXTENSION PortExtension,
    __in PSCSI_REQUEST_BLOCK Srb,
    __in BOOLEAN ForceSchedule
    )
{
    PAHCI_ADAPTER_EXTENSION adapterExtension;
    KIRQL oldIrql;
    BOOLEAN queued;

    if ((PortExtension == NULL) || (Srb == NULL))
    {
        return FALSE;
    }

    adapterExtension = PortExtension->AdapterExtension;
    if (adapterExtension == NULL)
    {
        return FALSE;
    }

    if (adapterExtension->CompletionThreadObject == NULL)
    {
        AhciCompleteRequest(adapterExtension, PortExtension, Srb);
        return TRUE;
    }

    KeAcquireSpinLock(&adapterExtension->CompletionListLock, &oldIrql);
    queued = AddQueue(&PortExtension->CompletionQueue, Srb);
    KeReleaseSpinLock(&adapterExtension->CompletionListLock, oldIrql);

    if (!queued)
    {
        AhciDebugPrint("\tCompletion queue full for port %u\n", PortExtension->PortNumber);
        AhciCompleteRequest(adapterExtension, PortExtension, Srb);
        return FALSE;
    }

    if (ForceSchedule)
    {
        AhciSchedulePortCompletionWorker(PortExtension);
    }

    return TRUE;
}

static
VOID
AhciSchedulePortCompletionWorker(
    __in PAHCI_PORT_EXTENSION PortExtension
    )
{
    PAHCI_ADAPTER_EXTENSION adapterExtension;
    KIRQL oldIrql;
    BOOLEAN inserted;

    if (PortExtension == NULL)
    {
        return;
    }

    adapterExtension = PortExtension->AdapterExtension;
    if (adapterExtension == NULL)
    {
        return;
    }

    if (adapterExtension->CompletionThreadObject == NULL)
    {
        return;
    }

    inserted = FALSE;

    KeAcquireSpinLock(&adapterExtension->CompletionListLock, &oldIrql);
    if (PortExtension->CompletionPending == 0)
    {
        PortExtension->CompletionPending = 1;
        InsertTailList(&adapterExtension->PendingPortList, &PortExtension->CompletionListEntry);
        inserted = TRUE;
    }
    KeReleaseSpinLock(&adapterExtension->CompletionListLock, oldIrql);

    if (inserted)
    {
        KeSetEvent(&adapterExtension->CompletionEvent, IO_NO_INCREMENT, FALSE);
    }
}

static
VOID
AhciQueueAdapterCompletion(
    __in PAHCI_ADAPTER_EXTENSION AdapterExtension,
    __in PSCSI_REQUEST_BLOCK Srb
    )
{
    KIRQL oldIrql;
    BOOLEAN queued;

    if ((AdapterExtension == NULL) || (Srb == NULL))
    {
        return;
    }

    if (AdapterExtension->CompletionThreadObject == NULL)
    {
        AhciCompleteRequest(AdapterExtension, NULL, Srb);
        return;
    }

    KeAcquireSpinLock(&AdapterExtension->CompletionListLock, &oldIrql);
    queued = AddQueue(&AdapterExtension->AdapterCompletionQueue, Srb);
    KeReleaseSpinLock(&AdapterExtension->CompletionListLock, oldIrql);

    if (!queued)
    {
        AhciDebugPrint("\tAdapter completion queue full, completing inline\n");
        AhciCompleteRequest(AdapterExtension, NULL, Srb);
        return;
    }

    KeSetEvent(&AdapterExtension->CompletionEvent, IO_NO_INCREMENT, FALSE);
}

static
VOID
AhciStopCompletionThread(
    __in PAHCI_ADAPTER_EXTENSION AdapterExtension
    )
{
    PETHREAD threadObject;
    KIRQL currentIrql;

    if (AdapterExtension == NULL)
    {
        return;
    }

    threadObject = AdapterExtension->CompletionThreadObject;
    if (threadObject == NULL)
    {
        return;
    }

    AdapterExtension->AdapterCompletionThreadStop = 1;
    KeSetEvent(&AdapterExtension->CompletionEvent, IO_NO_INCREMENT, FALSE);

    /* Only wait for thread termination if we're at PASSIVE_LEVEL */
    currentIrql = KeGetCurrentIrql();
    if (currentIrql < DISPATCH_LEVEL)
    {
        KeWaitForSingleObject(threadObject, Executive, KernelMode, FALSE, NULL);
        ObDereferenceObject(threadObject);
        AdapterExtension->CompletionThreadObject = NULL;
    }
    else
    {
        /* At DISPATCH_LEVEL, we can't wait. Just signal the thread to stop.
         * The thread will clean itself up when it exits. */
        AhciDebugPrint("AhciStopCompletionThread called at DISPATCH_LEVEL, thread will self-terminate\n");
    }
}

static VOID NTAPI AhciCompletionWorkerThread(
    __in PVOID Context
    )
{
    PAHCI_ADAPTER_EXTENSION adapterExtension;
    KIRQL oldIrql;

    adapterExtension = (PAHCI_ADAPTER_EXTENSION)Context;
    if (adapterExtension == NULL)
    {
        PsTerminateSystemThread(STATUS_INVALID_PARAMETER);
    }

    for (;;)
    {
        KeWaitForSingleObject(&adapterExtension->CompletionEvent,
                              Executive,
                              KernelMode,
                              FALSE,
                              NULL);

        for (;;)
        {
            PSCSI_REQUEST_BLOCK srb = NULL;
            PAHCI_PORT_EXTENSION port = NULL;

            KeAcquireSpinLock(&adapterExtension->CompletionListLock, &oldIrql);

            srb = RemoveQueue(&adapterExtension->AdapterCompletionQueue);
            if (srb != NULL)
            {
                KeReleaseSpinLock(&adapterExtension->CompletionListLock, oldIrql);
                AhciCompleteRequest(adapterExtension, NULL, srb);
                continue;
            }

            if (!IsListEmpty(&adapterExtension->PendingPortList))
            {
                PLIST_ENTRY entry;

                entry = RemoveHeadList(&adapterExtension->PendingPortList);
                port = CONTAINING_RECORD(entry, AHCI_PORT_EXTENSION, CompletionListEntry);
                port->CompletionPending = 0;
                KeReleaseSpinLock(&adapterExtension->CompletionListLock, oldIrql);

                for (;;)
                {
                    KeAcquireSpinLock(&adapterExtension->CompletionListLock, &oldIrql);
                    srb = RemoveQueue(&port->CompletionQueue);
                    KeReleaseSpinLock(&adapterExtension->CompletionListLock, oldIrql);

                    if (srb == NULL)
                    {
                        break;
                    }

                    AhciCompleteRequest(adapterExtension, port, srb);
                }

                continue;
            }

            KeClearEvent(&adapterExtension->CompletionEvent);
            KeReleaseSpinLock(&adapterExtension->CompletionListLock, oldIrql);
            break;
        }

        if (adapterExtension->AdapterCompletionThreadStop != 0)
        {
            BOOLEAN remaining;

            KeAcquireSpinLock(&adapterExtension->CompletionListLock, &oldIrql);
            remaining = !IsListEmpty(&adapterExtension->PendingPortList) ||
                        (adapterExtension->AdapterCompletionQueue.Head != adapterExtension->AdapterCompletionQueue.Tail);
            KeReleaseSpinLock(&adapterExtension->CompletionListLock, oldIrql);

            if (!remaining)
            {
                break;
            }

            KeSetEvent(&adapterExtension->CompletionEvent, IO_NO_INCREMENT, FALSE);
        }
    }

    /* Clean up thread reference if we're self-terminating */
    if (adapterExtension->CompletionThreadObject != NULL)
    {
        ObDereferenceObject(adapterExtension->CompletionThreadObject);
        InterlockedExchangePointer((PVOID *)&adapterExtension->CompletionThreadObject, NULL);
    }

    PsTerminateSystemThread(STATUS_SUCCESS);
}

static
PAHCI_PORT_EXTENSION
AhciTryGetPortExtension(
    __in PAHCI_ADAPTER_EXTENSION AdapterExtension,
    __in ULONG PathId
    );

static
VOID
AhciCompleteRequest(
    __in PAHCI_ADAPTER_EXTENSION AdapterExtension,
    __in_opt PAHCI_PORT_EXTENSION PortExtension,
    __in PSCSI_REQUEST_BLOCK Srb
    );

static
VOID
AhciCopyIdentifyString (
    __out_bcount(BufferLength) UCHAR *Destination,
    __in_bcount(SourceLength) const UCHAR *Source,
    __in SIZE_T BufferLength,
    __in SIZE_T SourceLength
    )
{
    SIZE_T index;
    SIZE_T maxWords;

    if ((Destination == NULL) || (Source == NULL) || (BufferLength == 0))
    {
        return;
    }

    maxWords = (BufferLength - 1) / 2;
    if (SourceLength / 2 < maxWords)
    {
        maxWords = SourceLength / 2;
    }

    for (index = 0; index < maxWords; index++)
    {
        SIZE_T sourceIndex = index * 2;
        UCHAR high = Source[sourceIndex];
        UCHAR low = (sourceIndex + 1 < SourceLength) ? Source[sourceIndex + 1] : ' ';
        Destination[2 * index] = low;
        Destination[2 * index + 1] = high;
    }

    Destination[BufferLength - 1] = '\0';

    for (index = 0; index < BufferLength - 1; index++)
    {
        if (Destination[index] == '\0')
        {
            break;
        }
        if (Destination[index] < ' ')
        {
            Destination[index] = ' ';
        }
    }
}

static
VOID
AhciDefaultCompletionRoutine(
    __in PVOID PortExtension,
    __in PVOID SrbPointer
    )
{
    PSCSI_REQUEST_BLOCK Srb;
    UNREFERENCED_PARAMETER(PortExtension);

    Srb = (PSCSI_REQUEST_BLOCK)SrbPointer;
    if (Srb == NULL)
    {
        return;
    }

    if (Srb->SrbStatus == SRB_STATUS_PENDING)
    {
        Srb->SrbStatus = SRB_STATUS_SUCCESS;
    }
}

static
PAHCI_PORT_EXTENSION
AhciTryGetPortExtension(
    __in PAHCI_ADAPTER_EXTENSION AdapterExtension,
    __in ULONG PathId
    )
{
    if ((AdapterExtension == NULL) || (PathId >= MAXIMUM_AHCI_PORT_COUNT))
    {
        return NULL;
    }

    if (PathId >= AdapterExtension->PortCount)
    {
        return NULL;
    }

    return &AdapterExtension->PortExtension[PathId];
}

static
VOID
AhciCompleteRequest(
    __in PAHCI_ADAPTER_EXTENSION AdapterExtension,
    __in_opt PAHCI_PORT_EXTENSION PortExtension,
    __in PSCSI_REQUEST_BLOCK Srb
    )
{
    PAHCI_SRB_EXTENSION SrbExtension;
    PAHCI_COMPLETION_ROUTINE CompletionRoutine;

    if ((AdapterExtension == NULL) || (Srb == NULL))
    {
        return;
    }

    if (Srb->SrbStatus == SRB_STATUS_PENDING)
    {
        Srb->SrbStatus = SRB_STATUS_SUCCESS;
    }

    SrbExtension = GetSrbExtension(Srb);
    CompletionRoutine = AhciDefaultCompletionRoutine;

    if (SrbExtension != NULL)
    {
        if (SrbExtension->CompletionRoutine != NULL)
        {
            CompletionRoutine = SrbExtension->CompletionRoutine;
        }

        if ((PortExtension == NULL) &&
            (CompletionRoutine != AhciDefaultCompletionRoutine) &&
            (SrbExtension->OwningPort != NULL))
        {
            PortExtension = SrbExtension->OwningPort;
        }

        SrbExtension->CompletionRoutine = NULL;
        SrbExtension->OwningPort = NULL;
    }

    if ((CompletionRoutine != NULL) &&
        ((PortExtension != NULL) || (CompletionRoutine == AhciDefaultCompletionRoutine)))
    {
        CompletionRoutine(PortExtension, Srb);
    }
    else if (CompletionRoutine != NULL)
    {
        AhciDebugPrint("\tCompletion routine lacks port context for SRB %p\n", Srb);
        CompletionRoutine(NULL, Srb);
    }

    StorPortNotification(RequestComplete, AdapterExtension, Srb);
}

static
VOID
AhciPortErrorRecoveryDpc(
    __in PSTOR_DPC Dpc,
    __in PVOID HwDeviceExtension,
    __in PVOID SystemArgument1,
    __in PVOID SystemArgument2
    );

static
SIZE_T
AhciGetTrimmedStringSegment(
    __in_ecount(BufferLength) const UCHAR *Buffer,
    __in SIZE_T BufferLength,
    __out_opt SIZE_T *StartIndex
    );

static
SIZE_T
AhciAppendIdentifierComponent(
    __in_ecount(SourceLength) const UCHAR *Source,
    __in SIZE_T SourceLength,
    __out_bcount_opt(BufferLength) UCHAR *Buffer,
    __in SIZE_T BufferLength,
    __in SIZE_T CurrentLength
    );

static
SIZE_T
AhciBuildDeviceIdentifierString(
    __in PAHCI_PORT_EXTENSION PortExtension,
    __out_bcount_opt(BufferLength) UCHAR *Buffer,
    __in SIZE_T BufferLength
    );

/**
 * @name AhciPortInitialize
 * @implemented
 *
 * Initialize port by setting up PxCLB & PxFB Registers
 *
 * @param PortExtension
 *
 * @return
 * Return true if intialization was successful
 */
BOOLEAN
NTAPI
AhciPortInitialize (
    __in PVOID DeviceExtension
    )
{
    PAHCI_PORT_EXTENSION PortExtension;
    AHCI_PORT_CMD cmd;
    PAHCI_MEMORY_REGISTERS abar;
    ULONG mappedLength, portNumber, ticks;
    PAHCI_ADAPTER_EXTENSION adapterExtension;
    STOR_PHYSICAL_ADDRESS commandListPhysical, receivedFISPhysical;

    AhciDebugPrint("AhciPortInitialize()\n");

    PortExtension = (PAHCI_PORT_EXTENSION)DeviceExtension;
    adapterExtension = PortExtension->AdapterExtension;
    abar = adapterExtension->ABAR_Address;
    portNumber = PortExtension->PortNumber;

    NT_ASSERT(abar != NULL);
    NT_ASSERT(portNumber < adapterExtension->PortCount);

    PortExtension->Port = &abar->PortList[portNumber];
    AhciInitializeQueue(&PortExtension->SrbQueue);
    AhciInitializeQueue(&PortExtension->CompletionQueue);
    PortExtension->CompletionPending = 0;
    InitializeListHead(&PortExtension->CompletionListEntry);

    commandListPhysical = StorPortGetPhysicalAddress(adapterExtension,
                                                     NULL,
                                                     PortExtension->CommandList,
                                                     &mappedLength);

    if ((mappedLength == 0) || ((commandListPhysical.LowPart % 1024) != 0))
    {
        AhciDebugPrint("\tcommandListPhysical mappedLength:%d\n", mappedLength);
        return FALSE;
    }

    receivedFISPhysical = StorPortGetPhysicalAddress(adapterExtension,
                                                     NULL,
                                                     PortExtension->ReceivedFIS,
                                                     &mappedLength);

    if ((mappedLength == 0) || ((receivedFISPhysical.LowPart % 256) != 0))
    {
        AhciDebugPrint("\treceivedFISPhysical mappedLength:%d\n", mappedLength);
        return FALSE;
    }

    // Ensure that the controller is not in the running state by reading and examining each
    // implemented port’s PxCMD register. If PxCMD.ST, PxCMD.CR, PxCMD.FRE and
    // PxCMD.FR are all cleared, the port is in an idle state. Otherwise, the port is not idle and
    // should be placed in the idle state prior to manipulating HBA and port specific registers.
    // System software places a port into the idle state by clearing PxCMD.ST and waiting for
    // PxCMD.CR to return ‘0’ when read. Software should wait at least 500 milliseconds for
    // this to occur. If PxCMD.FRE is set to ‘1’, software should clear it to ‘0’ and wait at least
    // 500 milliseconds for PxCMD.FR to return ‘0’ when read. If PxCMD.CR or PxCMD.FR do
    // not clear to ‘0’ correctly, then software may attempt a port reset or a full HBA reset to recove

    // TODO: Check if port is in idle state or not, if not then restart port
    cmd.Status = StorPortReadRegisterUlong(adapterExtension, &PortExtension->Port->CMD);
    if ((cmd.FR != 0) || (cmd.CR != 0) || (cmd.FRE != 0) || (cmd.ST != 0))
    {
        cmd.ST = 0;
        cmd.FRE = 0;

        ticks = 3;
        do
        {
            StorPortStallExecution(50000);
            cmd.Status = StorPortReadRegisterUlong(adapterExtension, &PortExtension->Port->CMD);
            if (ticks == 0)
            {
                AhciDebugPrint("\tAttempt to reset port failed: %x\n", cmd);
                return FALSE;
            }
            ticks--;
        }
        while(cmd.CR != 0 || cmd.FR != 0);
    }

    // 10.1.2 For each implemented port, system software shall allocate memory for and program:
    // ? PxCLB and PxCLBU (if CAP.S64A is set to ‘1’)
    // ? PxFB and PxFBU (if CAP.S64A is set to ‘1’)
    // Note: Assuming 32bit support only
    StorPortWriteRegisterUlong(adapterExtension, &PortExtension->Port->CLB, commandListPhysical.LowPart);
    if (IsAdapterCAPS64(adapterExtension->CAP))
    {
        StorPortWriteRegisterUlong(adapterExtension, &PortExtension->Port->CLBU, commandListPhysical.HighPart);
    }

    StorPortWriteRegisterUlong(adapterExtension, &PortExtension->Port->FB, receivedFISPhysical.LowPart);
    if (IsAdapterCAPS64(adapterExtension->CAP))
    {
        StorPortWriteRegisterUlong(adapterExtension, &PortExtension->Port->FBU, receivedFISPhysical.HighPart);
    }

    PortExtension->IdentifyDeviceDataPhysicalAddress = StorPortGetPhysicalAddress(adapterExtension,
                                                                                  NULL,
                                                                                  PortExtension->IdentifyDeviceData,
                                                                                  &mappedLength);

    // set device power state flag to D0
    PortExtension->DevicePowerState = StorPowerDeviceD0;

    // clear pending interrupts
    StorPortWriteRegisterUlong(adapterExtension, &PortExtension->Port->SERR, (ULONG)~0);
    StorPortWriteRegisterUlong(adapterExtension, &PortExtension->Port->IS, (ULONG)~0);
    StorPortWriteRegisterUlong(adapterExtension, adapterExtension->IS, (1 << PortExtension->PortNumber));

    return TRUE;
}// -- AhciPortInitialize();

/**
 * @name AhciAllocateResourceForAdapter
 * @implemented
 *
 * Allocate memory from poll for required pointers
 *
 * @param AdapterExtension
 * @param ConfigInfo
 *
 * @return
 * return TRUE if allocation was successful
 */
BOOLEAN
AhciAllocateResourceForAdapter (
    __in PAHCI_ADAPTER_EXTENSION AdapterExtension,
    __in PPORT_CONFIGURATION_INFORMATION ConfigInfo
    )
{
    PCHAR nonCachedExtension, tmp;
    ULONG index, NCS, AlignedNCS;
    ULONG portCount, portImplemented, nonCachedExtensionSize;
    PAHCI_PORT_EXTENSION PortExtension;

    AhciDebugPrint("AhciAllocateResourceForAdapter()\n");

    NCS = AHCI_Global_Port_CAP_NCS(AdapterExtension->CAP);
    AlignedNCS = ROUND_UP(NCS, 8);

    // get port count -- Number of set bits in `AdapterExtension->PortImplemented`
    portCount = 0;
    portImplemented = AdapterExtension->PortImplemented;

    NT_ASSERT(portImplemented != 0);
    for (index = MAXIMUM_AHCI_PORT_COUNT - 1; index > 0; index--)
        if ((portImplemented & (1 << index)) != 0)
            break;

    portCount = index + 1;
    AhciDebugPrint("\tPort Count: %d\n", portCount);

    AdapterExtension->PortCount = portCount;
    AhciInitializeQueue(&AdapterExtension->AdapterCompletionQueue);
    InitializeListHead(&AdapterExtension->PendingPortList);
    KeInitializeSpinLock(&AdapterExtension->CompletionListLock);
    KeInitializeEvent(&AdapterExtension->CompletionEvent, NotificationEvent, FALSE);
    AdapterExtension->AdapterCompletionThreadStop = 0;
    AdapterExtension->CompletionThreadObject = NULL;

    nonCachedExtensionSize =    sizeof(AHCI_COMMAND_HEADER) * AlignedNCS + //should be 1K aligned
                                sizeof(AHCI_RECEIVED_FIS) +
                                sizeof(IDENTIFY_DEVICE_DATA);

    // align nonCachedExtensionSize to 1024
    nonCachedExtensionSize = ROUND_UP(nonCachedExtensionSize, 1024);

    AdapterExtension->NonCachedExtension = StorPortGetUncachedExtension(AdapterExtension,
                                                                        ConfigInfo,
                                                                        nonCachedExtensionSize * portCount);

    if (AdapterExtension->NonCachedExtension == NULL)
    {
        AhciDebugPrint("\tadapterExtension->NonCachedExtension == NULL\n");
        return FALSE;
    }

    nonCachedExtension = AdapterExtension->NonCachedExtension;
    AhciZeroMemory(nonCachedExtension, nonCachedExtensionSize * portCount);

    for (index = 0; index < portCount; index++)
    {
        PortExtension = &AdapterExtension->PortExtension[index];

        PortExtension->DeviceParams.IsActive = FALSE;
        if ((AdapterExtension->PortImplemented & (1 << index)) != 0)
        {
            PortExtension->PortNumber = index;
            PortExtension->DeviceParams.IsActive = TRUE;
            PortExtension->AdapterExtension = AdapterExtension;
            PortExtension->CommandList = (PAHCI_COMMAND_HEADER)nonCachedExtension;
            PortExtension->ErrorRecoveryScheduled = 0;

            tmp = (PCHAR)(nonCachedExtension + sizeof(AHCI_COMMAND_HEADER) * AlignedNCS);

            PortExtension->ReceivedFIS = (PAHCI_RECEIVED_FIS)tmp;
            PortExtension->IdentifyDeviceData = (PIDENTIFY_DEVICE_DATA)(tmp + sizeof(AHCI_RECEIVED_FIS));
            PortExtension->MaxPortQueueDepth = NCS;
            nonCachedExtension += nonCachedExtensionSize;

            StorPortInitializeDpc(AdapterExtension,
                                  &PortExtension->ErrorRecoveryDpc,
                                  AhciPortErrorRecoveryDpc);
        }
    }
    {
        NTSTATUS status;
        HANDLE threadHandle = NULL;

        status = PsCreateSystemThread(&threadHandle,
                                      THREAD_ALL_ACCESS,
                                      NULL,
                                      NULL,
                                      NULL,
                                      AhciCompletionWorkerThread,
                                      AdapterExtension);
        if (!NT_SUCCESS(status))
        {
            AhciDebugPrint("\tFailed to create completion worker thread, status 0x%08lx\n", status);
            return FALSE;
        }

        status = ObReferenceObjectByHandle(threadHandle,
                                           THREAD_ALL_ACCESS,
                                           NULL,
                                           KernelMode,
                                           (PVOID *)&AdapterExtension->CompletionThreadObject,
                                           NULL);
        ZwClose(threadHandle);
        if (!NT_SUCCESS(status))
        {
            AhciDebugPrint("\tFailed to reference completion worker thread, status 0x%08lx\n", status);
            AdapterExtension->CompletionThreadObject = NULL;
            AdapterExtension->AdapterCompletionThreadStop = 1;
            return FALSE;
        }
    }

    return TRUE;
}// -- AhciAllocateResourceForAdapter();

/**
 * @name AhciStartPort
 * @implemented
 *
 * Try to start the port device
 *
 * @param AdapterExtension
 * @param PortExtension
 *
 */
BOOLEAN
AhciStartPort (
    __in PAHCI_PORT_EXTENSION PortExtension
    )
{
    ULONG index;
    AHCI_PORT_CMD cmd;
    AHCI_TASK_FILE_DATA tfd;
    AHCI_INTERRUPT_ENABLE ie;
    AHCI_SERIAL_ATA_STATUS ssts;
    AHCI_SERIAL_ATA_CONTROL sctl;
    PAHCI_ADAPTER_EXTENSION AdapterExtension;

    AhciDebugPrint("AhciStartPort()\n");
    AhciDebugPrint("\tPort Number: %u\n", PortExtension->PortNumber);

    AdapterExtension = PortExtension->AdapterExtension;
    cmd.Status = StorPortReadRegisterUlong(AdapterExtension, &PortExtension->Port->CMD);

    if ((cmd.FR == 1) && (cmd.CR == 1) && (cmd.FRE == 1) && (cmd.ST == 1))
    {
        // Already Running
        return TRUE;
    }

    cmd.SUD = 1;
    cmd.POD = 1;
    cmd.ICC = 1; /* request active power state */
    cmd.FRE = 0;
    cmd.ST = 0;
    StorPortWriteRegisterUlong(AdapterExtension, &PortExtension->Port->CMD, cmd.Status);

    /* Allow the link some time to settle before issuing COMRESET */
    StorPortStallExecution(500);

    /* Always issue a COMRESET so the link state is refreshed */
    AhciDebugPrint("\tIssuing COMRESET\n");

    sctl.Status = StorPortReadRegisterUlong(AdapterExtension, &PortExtension->Port->SCTL);
    sctl.DET = 1;
    sctl.IPM = 0; /* allow any power management transitions */
    sctl.SPD = 0; /* no speed restriction */
    StorPortWriteRegisterUlong(AdapterExtension, &PortExtension->Port->SCTL, sctl.Status);

    /* Keep COMRESET asserted for at least 1ms */
    StorPortStallExecution(1000);

    sctl.Status = StorPortReadRegisterUlong(AdapterExtension, &PortExtension->Port->SCTL);
    sctl.DET = 0;
    sctl.IPM = 0;
    sctl.SPD = 0;
    StorPortWriteRegisterUlong(AdapterExtension, &PortExtension->Port->SCTL, sctl.Status);

    /* Poll DET to verify if a device is attached to the port */
    ssts.Status = 0;
    for (index = 0; index < 100; index++)
    {
        StorPortStallExecution(1000);
        ssts.Status = StorPortReadRegisterUlong(AdapterExtension, &PortExtension->Port->SSTS);

        if (ssts.DET == 0)
        {
            continue;
        }

        if (ssts.DET == 0x3)
        {
            break;
        }

        /* DET == 1 means device present with no communication yet, keep waiting */
        if (ssts.DET == 0x1)
        {
            continue;
        }

        /* Any other DET value (device absent / phy offline) we bail out */
        break;
    }

    /* If we never saw DET==3 try a final poke by toggling DET once more */
    if (ssts.DET != 0x3)
    {
        sctl.Status = StorPortReadRegisterUlong(AdapterExtension, &PortExtension->Port->SCTL);
        sctl.DET = 1;
        StorPortWriteRegisterUlong(AdapterExtension, &PortExtension->Port->SCTL, sctl.Status);
        StorPortStallExecution(1000);

        sctl.Status = StorPortReadRegisterUlong(AdapterExtension, &PortExtension->Port->SCTL);
        sctl.DET = 0;
        StorPortWriteRegisterUlong(AdapterExtension, &PortExtension->Port->SCTL, sctl.Status);

        for (index = 0; index < 30; index++)
        {
            StorPortStallExecution(1000);
            ssts.Status = StorPortReadRegisterUlong(AdapterExtension, &PortExtension->Port->SSTS);
            if (ssts.DET == 0x3)
            {
                break;
            }
            if (ssts.DET != 0x1)
            {
                break;
            }
        }
    }

    ssts.Status = StorPortReadRegisterUlong(AdapterExtension, &PortExtension->Port->SSTS);
    AhciDebugPrint("\tPxSSTS DET=%x SPD=%x IPM=%x\n", ssts.DET, ssts.SPD, ssts.IPM);

    if (ssts.DET == 0x3)
    {
        /* ensure the controller stays powered/active */
        cmd.Status = StorPortReadRegisterUlong(AdapterExtension, &PortExtension->Port->CMD);
        cmd.SUD = 1;
        cmd.POD = 1;
        cmd.ICC = 1;
        StorPortWriteRegisterUlong(AdapterExtension, &PortExtension->Port->CMD, cmd.Status);
    }
    else
    {
        AhciDebugPrint("\tPort %u failed to detect device, PxCMD=%08lx PxSCTL=%08lx\n",
                       PortExtension->PortNumber,
                       StorPortReadRegisterUlong(AdapterExtension, &PortExtension->Port->CMD),
                       StorPortReadRegisterUlong(AdapterExtension, &PortExtension->Port->SCTL));
    }
    switch (ssts.DET)
    {
        case 0x3:
            {
                NT_ASSERT(cmd.ST == 0);

                // make sure FIS Recieve is enabled (cmd.FRE)
                index = 0;
                do
                {
                    StorPortStallExecution(10000);
                    cmd.Status = StorPortReadRegisterUlong(AdapterExtension, &PortExtension->Port->CMD);
                    cmd.FRE = 1;
                    StorPortWriteRegisterUlong(AdapterExtension, &PortExtension->Port->CMD, cmd.Status);
                    index++;
                }
                while((cmd.FR != 1) && (index < 3));

                if (cmd.FR != 1)
                {
                    // failed to start FIS DMA engine
                    // it can crash the driver later
                    // so better to turn this port off
                    return FALSE;
                }

                // start port channel
                // set cmd.ST

                NT_ASSERT(cmd.FRE == 1);
                NT_ASSERT(cmd.CR == 0);

                // why assert? well If we face such condition on DET = 0x3
                // then we don't have port in idle state and hence before executing this part of code
                // we must have restarted it.
                tfd.Status = StorPortReadRegisterUlong(AdapterExtension, &PortExtension->Port->TFD);

                if ((tfd.STS.BSY) || (tfd.STS.DRQ))
                {
                    AhciDebugPrint("\tUnhandled Case BSY-DRQ\n");
                }

                // clear pending interrupts
                StorPortWriteRegisterUlong(AdapterExtension, &PortExtension->Port->SERR, (ULONG)~0);
                StorPortWriteRegisterUlong(AdapterExtension, &PortExtension->Port->IS, (ULONG)~0);
                StorPortWriteRegisterUlong(AdapterExtension, AdapterExtension->IS, (1 << PortExtension->PortNumber));

                // set IE
                ie.Status = StorPortReadRegisterUlong(AdapterExtension, &PortExtension->Port->IE);
                /* Device to Host Register FIS Interrupt Enable */
                ie.DHRE = 1;
                /* PIO Setup FIS Interrupt Enable */
                ie.PSE = 1;
                /* DMA Setup FIS Interrupt Enable  */
                ie.DSE = 1;
                /* Set Device Bits FIS Interrupt Enable */
                ie.SDBE = 1;
                /* Unknown FIS Interrupt Enable */
                ie.UFE = 0;
                /* Descriptor Processed Interrupt Enable */
                ie.DPE = 0;
                /* Port Change Interrupt Enable */
                ie.PCE = 1;
                /* Device Mechanical Presence Enable */
                ie.DMPE = 0;
                /* PhyRdy Change Interrupt Enable */
                ie.PRCE = 1;
                /* Incorrect Port Multiplier Enable */
                ie.IPME = 0;
                /* Overflow Enable */
                ie.OFE = 1;
                /* Interface Non-fatal Error Enable */
                ie.INFE = 1;
                /* Interface Fatal Error Enable */
                ie.IFE = 1;
                /* Host Bus Data Error Enable */
                ie.HBDE = 1;
                /* Host Bus Fatal Error Enable */
                ie.HBFE = 1;
                /* Task File Error Enable */
                ie.TFEE = 1;

                cmd.Status = StorPortReadRegisterUlong(AdapterExtension, &PortExtension->Port->CMD);
                /* Cold Presence Detect Enable */
                if (cmd.CPD) // does it support CPD?
                {
                    // disable it for now
                    ie.CPDE = 0;
                }

                // should I replace this to single line?
                // by directly setting ie.Status?

                StorPortWriteRegisterUlong(AdapterExtension, &PortExtension->Port->IE, ie.Status);

                cmd.ST = 1;
                StorPortWriteRegisterUlong(AdapterExtension, &PortExtension->Port->CMD, cmd.Status);
                cmd.Status = StorPortReadRegisterUlong(AdapterExtension, &PortExtension->Port->CMD);

                if (cmd.ST != 1)
                {
                    AhciDebugPrint("\tFailed to start Port\n");
                    return FALSE;
                }

                return TRUE;
            }
        default:
            // unhandled case
            AhciDebugPrint("\tDET == %x Unsupported\n", ssts.DET);
            return FALSE;
    }
}// -- AhciStartPort();

/**
 * @name AhciCommandCompletionDpcRoutine
 * @implemented
 *
 * Handles Completed Commands
 *
 * @param Dpc
 * @param AdapterExtension
 * @param SystemArgument1
 * @param SystemArgument2
 */
VOID
AhciCommandCompletionDpcRoutine (
    __in PSTOR_DPC Dpc,
    __in PVOID HwDeviceExtension,
    __in PVOID SystemArgument1,
    __in PVOID SystemArgument2
  )
{
    PAHCI_ADAPTER_EXTENSION AdapterExtension;
    PAHCI_PORT_EXTENSION PortExtension;
    STOR_LOCK_HANDLE lockhandle = {0};
    BOOLEAN queueEmpty;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument2);

    AdapterExtension = (PAHCI_ADAPTER_EXTENSION)HwDeviceExtension;
    PortExtension = (PAHCI_PORT_EXTENSION)SystemArgument1;

    if ((AdapterExtension == NULL) || (PortExtension == NULL))
    {
        AhciDebugPrint("AhciCommandCompletionDpcRoutine() invalid context\n");
    }

    StorPortAcquireSpinLock(AdapterExtension, InterruptLock, NULL, &lockhandle);
    queueEmpty = (PortExtension->CompletionQueue.Head == PortExtension->CompletionQueue.Tail);
    StorPortReleaseSpinLock(AdapterExtension, &lockhandle);

    if (!queueEmpty)
    {
        AhciSchedulePortCompletionWorker(PortExtension);
    }
}// -- AhciCommandCompletionDpcRoutine();

/**
 * @name AhciPortErrorRecoveryDpc
 * @implemented
 *
 * Handle deferred fatal error recovery for a port.
 *
 * @param Dpc
 * @param HwDeviceExtension
 * @param SystemArgument1
 * @param SystemArgument2
 */
static
VOID
AhciPortErrorRecoveryDpc (
    __in PSTOR_DPC Dpc,
    __in PVOID HwDeviceExtension,
    __in PVOID SystemArgument1,
    __in PVOID SystemArgument2
    )
{
    PAHCI_ADAPTER_EXTENSION AdapterExtension;
    PAHCI_PORT_EXTENSION PortExtension;

    UNREFERENCED_PARAMETER(Dpc);

    AdapterExtension = (PAHCI_ADAPTER_EXTENSION)HwDeviceExtension;
    PortExtension = (PAHCI_PORT_EXTENSION)SystemArgument1;

    if ((AdapterExtension == NULL) || (PortExtension == NULL))
    {
        AhciDebugPrint("AhciPortErrorRecoveryDpc() invalid context\n");
        return;
    }

    if (!AhciResetPortAndFailRequests(AdapterExtension,
                                      PortExtension,
                                      SRB_STATUS_ERROR,
                                      SCSISTAT_CHECK_CONDITION,
                                      TRUE))
    {
        AhciDebugPrint("\tPort %u recovery failed\n", PortExtension->PortNumber);
    }

    InterlockedExchange(&PortExtension->ErrorRecoveryScheduled, 0);
}// -- AhciPortErrorRecoveryDpc();

/**
 * @name AhciHwPassiveInitialize
 * @implemented
 *
 * initializes the HBA and finds all devices that are of interest to the miniport driver. (at PASSIVE LEVEL)
 *
 * @param adapterExtension
 *
 * @return
 * return TRUE if intialization was successful
 */
BOOLEAN
AhciHwPassiveInitialize (
    __in PVOID DeviceExtension
    )
{
    ULONG index;
    PAHCI_ADAPTER_EXTENSION AdapterExtension;
    PAHCI_PORT_EXTENSION PortExtension;

    AhciDebugPrint("AhciHwPassiveInitialize()\n");

    AdapterExtension = (PAHCI_ADAPTER_EXTENSION)DeviceExtension;

    for (index = 0; index < AdapterExtension->PortCount; index++)
    {
        if ((AdapterExtension->PortImplemented & (0x1 << index)) != 0)
        {
            PortExtension = &AdapterExtension->PortExtension[index];
            PortExtension->DeviceParams.IsActive = AhciStartPort(PortExtension);
            PortExtension->ErrorRecoveryScheduled = 0;

            if (PortExtension->DeviceParams.IsActive)
            {
                StorPortInitializeDpc(AdapterExtension, &PortExtension->CommandCompletion, AhciCommandCompletionDpcRoutine);
            }
            else
            {
                AhciDebugPrint("\tPort %u inactive after start\n", index);
            }
        }
    }

    return TRUE;
}// -- AhciHwPassiveInitialize();

/**
 * @name AhciHwInitialize
 * @implemented
 *
 * initializes the HBA and finds all devices that are of interest to the miniport driver.
 *
 * @param adapterExtension
 *
 * @return
 * return TRUE if intialization was successful
 */
BOOLEAN
NTAPI
AhciHwInitialize (
    __in PVOID DeviceExtension
    )
{
    PAHCI_ADAPTER_EXTENSION AdapterExtension;
    AHCI_GHC ghc;

    AhciDebugPrint("AhciHwInitialize()\n");

    AdapterExtension = (PAHCI_ADAPTER_EXTENSION)DeviceExtension;
    AdapterExtension->StateFlags.MessagePerPort = FALSE;

    // First check what type of interrupt/synchronization device is using
    ghc.Status = StorPortReadRegisterUlong(AdapterExtension, &AdapterExtension->ABAR_Address->GHC);

    // When set to ‘1’ by hardware, indicates that the HBA requested more than one MSI vector
    // but has reverted to using the first vector only.  When this bit is cleared to ‘0’,
    // the HBA has not reverted to single MSI mode (i.e. hardware is already in single MSI mode,
    // software has allocated the number of messages requested
    if (ghc.MRSM == 0)
    {
        AdapterExtension->StateFlags.MessagePerPort = TRUE;
        AhciDebugPrint("\tMultiple MSI based message not supported\n");
    }

    StorPortEnablePassiveInitialization(AdapterExtension, AhciHwPassiveInitialize);

    return TRUE;
}// -- AhciHwInitialize();

/**
 * @name AhciCompleteIssuedSrb
 * @implemented
 *
 * Complete issued Srbs
 *
 * @param PortExtension
 *
 */
VOID
AhciCompleteIssuedSrb (
    __in PAHCI_PORT_EXTENSION PortExtension,
    __in ULONG CommandsToComplete
    )
{
    ULONG NCS, i;
    PSCSI_REQUEST_BLOCK Srb;
    PAHCI_SRB_EXTENSION SrbExtension;
    PAHCI_ADAPTER_EXTENSION AdapterExtension;
    BOOLEAN queuedAny = FALSE;

    AhciDebugPrint("AhciCompleteIssuedSrb() mask=0x%lx\n", CommandsToComplete);

    NT_ASSERT(CommandsToComplete != 0);

    AdapterExtension = PortExtension->AdapterExtension;
    NCS = AHCI_Global_Port_CAP_NCS(AdapterExtension->CAP);

    for (i = 0; i < NCS; i++)
    {
        if (((1 << i) & CommandsToComplete) == 0)
        {
            continue;
        }

        Srb = PortExtension->Slot[i];
        if (Srb == NULL)
        {
            continue;
        }

        SrbExtension = GetSrbExtension(Srb);
        if (SrbExtension == NULL)
        {
            AhciDebugPrint("\tMissing SRB extension for slot %u\n", i);
            continue;
        }

        if (SrbExtension->CompletionRoutine == NULL)
        {
            if (Srb->SrbStatus == SRB_STATUS_PENDING)
            {
                Srb->SrbStatus = SRB_STATUS_SUCCESS;
            }

            SrbExtension->CompletionRoutine = AhciDefaultCompletionRoutine;
        }

        if (AhciQueuePortCompletion(PortExtension, Srb, FALSE))
        {
            queuedAny = TRUE;
        }
    }

    if (queuedAny)
    {
        if (!StorPortIssueDpc(AdapterExtension, &PortExtension->CommandCompletion, PortExtension, NULL))
        {
            AhciDebugPrint("\tFailed to queue completion DPC for port %u\n", PortExtension->PortNumber);
        }
    }

    return;
}// -- AhciCompleteIssuedSrb();

/**
 * @name AhciInterruptHandler
 * @not_implemented
 *
 * Interrupt Handler for PortExtension
 *
 * @param PortExtension
 *
 */
VOID
AhciInterruptHandler (
    __in PAHCI_PORT_EXTENSION PortExtension
    )
{
    ULONG is, ci, sact, outstanding;
    AHCI_INTERRUPT_STATUS PxIS;
    AHCI_INTERRUPT_STATUS PxISMasked;
    PAHCI_ADAPTER_EXTENSION AdapterExtension;

    AhciDebugPrint("AhciInterruptHandler() Port=%u\n", PortExtension->PortNumber);

    AdapterExtension = PortExtension->AdapterExtension;
    NT_ASSERT(IsPortValid(AdapterExtension, PortExtension->PortNumber));

    // 5.5.3
    // 1. Software determines the cause of the interrupt by reading the PxIS register.
    //    It is possible for multiple bits to be set
    // 2. Software clears appropriate bits in the PxIS register corresponding to the cause of the interrupt.
    // 3. Software clears the interrupt bit in IS.IPS corresponding to the port.
    // 4. If executing non-queued commands, software reads the PxCI register, and compares the current value to
    //    the list of commands previously issued by software that are still outstanding.
    //    If executing native queued commands, software reads the PxSACT register and compares the current
    //    value to the list of commands previously issued by software.
    //    Software completes with success any outstanding command whose corresponding bit has been cleared in
    //    the respective register. PxCI and PxSACT are volatile registers; software should only use their values
    //    to determine commands that have completed, not to determine which commands have previously been issued.
    // 5. If there were errors, noted in the PxIS register, software performs error recovery actions (see section 6.2.2).
    PxISMasked.Status = 0;
    PxIS.Status = StorPortReadRegisterUlong(AdapterExtension, &PortExtension->Port->IS);
    AhciDebugPrint("\tPxIS=0x%08lx\n", PxIS.Status);

    // 6.2.2
    // Fatal Error
    // signified by the setting of PxIS.HBFS, PxIS.HBDS, PxIS.IFS, or PxIS.TFES
    if (PxIS.HBFS || PxIS.HBDS || PxIS.IFS || PxIS.TFES)
    {
        // In this state, the HBA shall not issue any new commands nor acknowledge DMA Setup FISes to process
        // any native command queuing commands. To recover, the port must be restarted.
        AhciDebugPrint("\tFatal Error: %x\n", PxIS.Status);

        PxISMasked.HBFS = PxIS.HBFS;
        PxISMasked.HBDS = PxIS.HBDS;
        PxISMasked.IFS = PxIS.IFS;
        PxISMasked.TFES = PxIS.TFES;

        if (InterlockedCompareExchange(&PortExtension->ErrorRecoveryScheduled, 1, 0) == 0)
        {
            StorPortIssueDpc(AdapterExtension,
                              &PortExtension->ErrorRecoveryDpc,
                              PortExtension,
                              (PVOID)(ULONG_PTR)PxIS.Status);
        }
    }

    // Normal Command Completion
    // 3.3.5
    // A D2H Register FIS has been received with the ‘I’ bit set, and has been copied into system memory.
    PxISMasked.DHRS = PxIS.DHRS;
    // A PIO Setup FIS has been received with the ‘I’ bit set, it has been copied into system memory.
    PxISMasked.PSS = PxIS.PSS;
    // A DMA Setup FIS has been received with the ‘I’ bit set and has been copied into system memory.
    PxISMasked.DSS = PxIS.DSS;
    // A Set Device Bits FIS has been received with the ‘I’ bit set and has been copied into system memory/
    PxISMasked.SDBS = PxIS.SDBS;
    // A PRD with the ‘I’ bit set has transferred all of its data.
    PxISMasked.DPS = PxIS.DPS;

    if (PxISMasked.Status != 0)
    {
        StorPortWriteRegisterUlong(AdapterExtension, &PortExtension->Port->IS, PxISMasked.Status);
    }

    // 10.7.1.1
    // Clear port interrupt
    // It is set by the level of the virtual interrupt line being a set, and cleared by a write of ‘1’ from the software.
    is = (1 << PortExtension->PortNumber);
    StorPortWriteRegisterUlong(AdapterExtension, AdapterExtension->IS, is);
    AhciDebugPrint("\tCleared adapter IS bit 0x%lx\n", is);

    ci = StorPortReadRegisterUlong(AdapterExtension, &PortExtension->Port->CI);
    sact = StorPortReadRegisterUlong(AdapterExtension, &PortExtension->Port->SACT);

    outstanding = ci | sact; // NOTE: Including both non-NCQ and NCQ based commands
    if ((PortExtension->CommandIssuedSlots & (~outstanding)) != 0)
    {
        ULONG completed = (PortExtension->CommandIssuedSlots & (~outstanding));
        AhciDebugPrint("\tCompleting slots mask 0x%lx\n", completed);
        AhciCompleteIssuedSrb(PortExtension, completed);
        PortExtension->CommandIssuedSlots &= outstanding;
        AhciDebugPrint("\tOutstanding mask now 0x%lx\n", PortExtension->CommandIssuedSlots);
    }

    return;
}// -- AhciInterruptHandler();

/**
 * @name AhciHwInterrupt
 * @implemented
 *
 * The Storport driver calls the HwStorInterrupt routine after the HBA generates an interrupt request.
 *
 * @param AdapterExtension
 *
 * @return
 * return TRUE Indicates that an interrupt was pending on adapter.
 * return FALSE Indicates the interrupt was not ours.
 */
BOOLEAN
NTAPI
AhciHwInterrupt (
    __in PVOID DeviceExtension
    )
{
    PAHCI_ADAPTER_EXTENSION AdapterExtension;
    ULONG portPending, nextPort, i, portCount;

    AdapterExtension = (PAHCI_ADAPTER_EXTENSION)DeviceExtension;

    if (AdapterExtension->StateFlags.Removed)
    {
        return FALSE;
    }

    portPending = StorPortReadRegisterUlong(AdapterExtension, AdapterExtension->IS);

    // we process interrupt for implemented ports only
    portCount = AdapterExtension->PortCount;
    portPending = portPending & AdapterExtension->PortImplemented;

    if (portPending == 0)
    {
        return FALSE;
    }

    for (i = 1; i <= portCount; i++)
    {
        nextPort = (AdapterExtension->LastInterruptPort + i) % portCount;
        if ((portPending & (0x1 << nextPort)) == 0)
            continue;

        NT_ASSERT(IsPortValid(AdapterExtension, nextPort));

        if (AdapterExtension->PortExtension[nextPort].DeviceParams.IsActive == FALSE)
        {
            continue;
        }

        // we can assign this interrupt to this port
        AdapterExtension->LastInterruptPort = nextPort;
        AhciInterruptHandler(&AdapterExtension->PortExtension[nextPort]);

        portPending &= ~(1 << nextPort);

        // interrupt belongs to this device
        // should always return TRUE
        return TRUE;
    }

    AhciDebugPrint("\tSomething went wrong");
    return FALSE;
}// -- AhciHwInterrupt();

/**
 * @name AhciHwStartIo
 * @not_implemented
 *
 * The Storport driver calls the HwStorStartIo routine one time for each incoming I/O request.
 *
 * @param adapterExtension
 * @param Srb
 *
 * @return
 * return TRUE if the request was accepted
 * return FALSE if the request must be submitted later
 */
BOOLEAN
NTAPI
AhciHwStartIo (
    __in PVOID DeviceExtension,
    __in PSCSI_REQUEST_BLOCK Srb
    )
{
    PAHCI_ADAPTER_EXTENSION AdapterExtension;

    AhciDebugPrint("AhciHwStartIo() Path=%u Target=%u LUN=%u Function=%u\n",
                   Srb->PathId,
                   Srb->TargetId,
                   Srb->Lun,
                   Srb->Function);

    AdapterExtension = (PAHCI_ADAPTER_EXTENSION)DeviceExtension;

    if (!IsPortValid(AdapterExtension, Srb->PathId))
    {
        Srb->SrbStatus = SRB_STATUS_NO_DEVICE;
        AhciDebugPrint("\tPort invalid, completing with NO_DEVICE\n");
        AhciQueueAdapterCompletion(AdapterExtension, Srb);
        return TRUE;
    }

    switch(Srb->Function)
    {
        case SRB_FUNCTION_PNP:
            {
                // https://learn.microsoft.com/en-us/previous-versions/windows/drivers/storage/handling-srb-function-pnp
                // If the function member of an SRB is set to SRB_FUNCTION_PNP,
                // the SRB is a structure of type SCSI_PNP_REQUEST_BLOCK.

                PSCSI_PNP_REQUEST_BLOCK pnpRequest;
                pnpRequest = (PSCSI_PNP_REQUEST_BLOCK)Srb;
                if ((pnpRequest->SrbPnPFlags & SRB_PNP_FLAGS_ADAPTER_REQUEST) != 0)
                {
                    switch(pnpRequest->PnPAction)
                    {
                        case StorRemoveDevice:
                        case StorSurpriseRemoval:
                            {
                                Srb->SrbStatus = SRB_STATUS_SUCCESS;
                                AdapterExtension->StateFlags.Removed = 1;
                                AhciDebugPrint("\tAdapter removed\n");
                                AhciStopCompletionThread(AdapterExtension);
                            }
                            break;
                        case StorStopDevice:
                            {
                                Srb->SrbStatus = SRB_STATUS_SUCCESS;
                                AhciDebugPrint("\tRequested to Stop the adapter\n");
                                AhciStopCompletionThread(AdapterExtension);
                            }
                            break;
                        default:
                            Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
                            break;
                    }
                }
                else
                {
                    Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
                }
            }
            break;
        case SRB_FUNCTION_EXECUTE_SCSI:
            {
                // https://learn.microsoft.com/en-us/previous-versions/windows/drivers/storage/handling-srb-function-execute-scsi
                // On receipt of an SRB_FUNCTION_EXECUTE_SCSI request, a miniport driver's HwScsiStartIo
                // routine does the following:
                //
                // - Gets and/or sets up whatever context the miniport driver maintains in its device,
                //   logical unit, and/or SRB extensions
                //   For example, a miniport driver might set up a logical unit extension with pointers
                //   to the SRB itself and the SRB DataBuffer pointer, the SRB DataTransferLength value,
                //   and a driver-defined value (or CDB SCSIOP_XXX value) indicating the operation to be
                //   carried out on the HBA.
                //
                // - Calls an internal routine to program the HBA, as partially directed by the SrbFlags,
                //   for the requested operation
                //   For a device I/O operation, such an internal routine generally selects the target device
                //   and sends the CDB over the bus to the target logical unit.
                PCDB cdb = (PCDB)&Srb->Cdb;
                if (Srb->CdbLength == 0)
                {
                    AhciDebugPrint("\tOperationCode: %d\n", cdb->CDB10.OperationCode);
                    Srb->SrbStatus = SRB_STATUS_BAD_FUNCTION;
                    break;
                }

                NT_ASSERT(cdb != NULL);

                switch(cdb->CDB10.OperationCode)
                {
                    case SCSIOP_INQUIRY:
                        Srb->SrbStatus = DeviceInquiryRequest(AdapterExtension, Srb, cdb);
                        break;
                    case SCSIOP_REPORT_LUNS:
                        Srb->SrbStatus = DeviceReportLuns(AdapterExtension, Srb, cdb);
                        break;
                    case SCSIOP_READ_CAPACITY:
                        Srb->SrbStatus = DeviceRequestCapacity(AdapterExtension, Srb, cdb);
                        break;
                    case SCSIOP_TEST_UNIT_READY:
                        Srb->SrbStatus = DeviceRequestComplete(AdapterExtension, Srb, cdb);
                        break;
                    case SCSIOP_MODE_SENSE:
                        Srb->SrbStatus = DeviceRequestSense(AdapterExtension, Srb, cdb);
                        break;
                    case SCSIOP_READ:
                    case SCSIOP_WRITE:
                        Srb->SrbStatus = DeviceRequestReadWrite(AdapterExtension, Srb, cdb);
                        break;
                    case SCSIOP_READ16:
                    case SCSIOP_WRITE16:
                        Srb->SrbStatus = DeviceRequestReadWrite16(AdapterExtension, Srb, cdb);
                        break;
                    case SCSIOP_SYNCHRONIZE_CACHE:
                    case SCSIOP_SYNCHRONIZE_CACHE16:
                        Srb->SrbStatus = DeviceRequestFlush(AdapterExtension, Srb, cdb);
                        break;
                    case SCSIOP_START_STOP_UNIT:
                        Srb->SrbStatus = DeviceRequestComplete(AdapterExtension, Srb, cdb);
                        break;
                    default:
                        AhciDebugPrint("\tOperationCode: %d\n", cdb->CDB10.OperationCode);
                        Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
                        break;
                }
            }
            break;
        default:
            AhciDebugPrint("\tUnknown function code recieved: %x\n", Srb->Function);
            Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
            break;
    }

    if (Srb->SrbStatus != SRB_STATUS_PENDING)
    {
        PAHCI_PORT_EXTENSION completionPort;

        AhciDebugPrint("\tSRB completed immediately, status=0x%x\n", Srb->SrbStatus);

        completionPort = AhciTryGetPortExtension(AdapterExtension, Srb->PathId);
        if (completionPort != NULL)
        {
            AhciQueuePortCompletion(completionPort, Srb, TRUE);
        }
        else
        {
            AhciQueueAdapterCompletion(AdapterExtension, Srb);
        }
    }
    else
    {
        AhciDebugPrint("\tSRB pending, queuing for execution\n");
        AhciProcessIO(AdapterExtension, Srb->PathId, Srb);
    }
    return TRUE;
}// -- AhciHwStartIo();

/**
 * @name AhciHwResetBus
 * @not_implemented
 *
 * The HwStorResetBus routine is called by the port driver to clear error conditions.
 *
 * @param adapterExtension
 * @param PathId
 *
 * @return
 * return TRUE if bus was successfully reset
 */
BOOLEAN
NTAPI
AhciHwResetBus (
    __in PVOID AdapterExtension,
    __in ULONG PathId
    )
{
    PAHCI_ADAPTER_EXTENSION adapterExtension;

    AhciDebugPrint("AhciHwResetBus()\n");

    if (AdapterExtension == NULL)
    {
        AhciDebugPrint("\tAdapterExtension is NULL\n");
        return FALSE;
    }

    adapterExtension = (PAHCI_ADAPTER_EXTENSION)AdapterExtension;

    if (!IsPortValid(adapterExtension, PathId))
    {
        AhciDebugPrint("\tInvalid PathId %lu for reset\n", PathId);
        return FALSE;
    }

    return AhciResetPortAndFailRequests(adapterExtension,
                                        &adapterExtension->PortExtension[PathId],
                                        SRB_STATUS_BUS_RESET,
                                        SCSISTAT_BUSY,
                                        TRUE);
}// -- AhciHwResetBus();

/**
 * @name AhciHwFindAdapter
 * @implemented
 *
 * The HwStorFindAdapter routine uses the supplied configuration to determine whether a specific
 * HBA is supported and, if it is, to return configuration information about that adapter.
 *
 *  10.1 Platform Communication
 *  http://www.intel.in/content/dam/www/public/us/en/documents/technical-specifications/serial-ata-ahci-spec-rev1_2.pdf

 * @param DeviceExtension
 * @param HwContext
 * @param BusInformation
 * @param ArgumentString
 * @param ConfigInfo
 * @param Reserved3
 *
 * @return
 *      SP_RETURN_FOUND
 *          Indicates that a supported HBA was found and that the HBA-relevant configuration information was successfully determined and set in the PORT_CONFIGURATION_INFORMATION structure.
 *
 *      SP_RETURN_ERROR
 *          Indicates that an HBA was found but there was an error obtaining the configuration information. If possible, such an error should be logged with StorPortLogError.
 *
 *      SP_RETURN_BAD_CONFIG
 *          Indicates that the supplied configuration information was invalid for the adapter.
 *
 *      SP_RETURN_NOT_FOUND
 *          Indicates that no supported HBA was found for the supplied configuration information.
 *
 * @remarks Called by Storport.
 */
ULONG
NTAPI
AhciHwFindAdapter (
    __in PVOID DeviceExtension,
    __in PVOID HwContext,
    __in PVOID BusInformation,
    __in PCHAR ArgumentString,
    __inout PPORT_CONFIGURATION_INFORMATION ConfigInfo,
    __in PBOOLEAN Reserved3
    )
{
    AHCI_GHC ghc;
    ULONG index, pci_cfg_len;
    PACCESS_RANGE accessRange;
    UCHAR pci_cfg_buf[sizeof(PCI_COMMON_CONFIG)];

    PAHCI_MEMORY_REGISTERS abar;
    PPCI_COMMON_CONFIG pciConfigData;
    PAHCI_ADAPTER_EXTENSION adapterExtension;

    AhciDebugPrint("AhciHwFindAdapter()\n");

    UNREFERENCED_PARAMETER(HwContext);
    UNREFERENCED_PARAMETER(BusInformation);
    UNREFERENCED_PARAMETER(ArgumentString);
    UNREFERENCED_PARAMETER(Reserved3);

    adapterExtension = DeviceExtension;
    adapterExtension->SlotNumber = ConfigInfo->SlotNumber;
    adapterExtension->SystemIoBusNumber = ConfigInfo->SystemIoBusNumber;

    // get PCI configuration header
    pci_cfg_len = StorPortGetBusData(
                        adapterExtension,
                        PCIConfiguration,
                        adapterExtension->SystemIoBusNumber,
                        adapterExtension->SlotNumber,
                        pci_cfg_buf,
                        sizeof(PCI_COMMON_CONFIG));

    if (pci_cfg_len != sizeof(PCI_COMMON_CONFIG))
    {
        AhciDebugPrint("\tpci_cfg_len != %d :: %d", sizeof(PCI_COMMON_CONFIG), pci_cfg_len);
        return SP_RETURN_ERROR;//Not a valid device at the given bus number
    }

    pciConfigData = (PPCI_COMMON_CONFIG)pci_cfg_buf;
    adapterExtension->VendorID = pciConfigData->VendorID;
    adapterExtension->DeviceID = pciConfigData->DeviceID;
    adapterExtension->RevisionID = pciConfigData->RevisionID;
    // The last PCI base address register (BAR[5], header offset 0x24) points to the AHCI base memory, it’s called ABAR (AHCI Base Memory Register).
    adapterExtension->AhciBaseAddress = pciConfigData->u.type0.BaseAddresses[5] & (0xFFFFFFF0);

    AhciDebugPrint("\tVendorID: %04x  DeviceID: %04x  RevisionID: %02x\n",
                   adapterExtension->VendorID,
                   adapterExtension->DeviceID,
                   adapterExtension->RevisionID);

    // 2.1.11
    abar = NULL;
    if (ConfigInfo->NumberOfAccessRanges > 0)
    {
        accessRange = *(ConfigInfo->AccessRanges);
        for (index = 0; index < ConfigInfo->NumberOfAccessRanges; index++)
        {
            if (accessRange[index].RangeStart.QuadPart == adapterExtension->AhciBaseAddress)
            {
                abar = StorPortGetDeviceBase(adapterExtension,
                                             ConfigInfo->AdapterInterfaceType,
                                             ConfigInfo->SystemIoBusNumber,
                                             accessRange[index].RangeStart,
                                             accessRange[index].RangeLength,
                                             !accessRange[index].RangeInMemory);
                break;
            }
        }
    }

    if (abar == NULL)
    {
        AhciDebugPrint("\tabar == NULL\n");
        return SP_RETURN_ERROR; // corrupted information supplied
    }

    adapterExtension->ABAR_Address = abar;
    adapterExtension->CAP = StorPortReadRegisterUlong(adapterExtension, &abar->CAP);
    adapterExtension->CAP2 = StorPortReadRegisterUlong(adapterExtension, &abar->CAP2);
    adapterExtension->Version = StorPortReadRegisterUlong(adapterExtension, &abar->VS);
    adapterExtension->LastInterruptPort = (ULONG)-1;

    // 10.1.2
    // 1. Indicate that system software is AHCI aware by setting GHC.AE to ‘1’.
    // 3.1.2 -- AE bit is read-write only if CAP.SAM is '0'
    ghc.Status = StorPortReadRegisterUlong(adapterExtension, &abar->GHC);
    // AE := Highest Significant bit of GHC
    if (ghc.AE != 0)// Hmm, controller was already in power state
    {
        // reset controller to have it in known state
        AhciDebugPrint("\tAE Already set, Reset()\n");
        if (!AhciAdapterReset(adapterExtension))
        {
            AhciDebugPrint("\tReset Failed!\n");
            return SP_RETURN_ERROR;// reset failed
        }
    }

    ghc.Status = 0;
    ghc.AE = 1;// only AE=1
    // tell the controller that we know about AHCI
    StorPortWriteRegisterUlong(adapterExtension, &abar->GHC, ghc.Status);

    adapterExtension->IS = &abar->IS;
    adapterExtension->PortImplemented = StorPortReadRegisterUlong(adapterExtension, &abar->PI);

    if (adapterExtension->PortImplemented == 0)
    {
        AhciDebugPrint("\tadapterExtension->PortImplemented == 0\n");
        return SP_RETURN_ERROR;
    }

    ConfigInfo->Master = TRUE;
    ConfigInfo->AlignmentMask = 0x3;
    ConfigInfo->ScatterGather = TRUE;
    ConfigInfo->DmaWidth = Width32Bits;
    ConfigInfo->WmiDataProvider = FALSE;
    ConfigInfo->Dma32BitAddresses = TRUE;

    if (IsAdapterCAPS64(adapterExtension->CAP))
    {
        ConfigInfo->Dma64BitAddresses = TRUE;
    }

    ConfigInfo->MaximumNumberOfTargets = 1;
    ConfigInfo->ResetTargetSupported = TRUE;
    ConfigInfo->NumberOfPhysicalBreaks = 0x21;
    ConfigInfo->MaximumNumberOfLogicalUnits = 1;
    ConfigInfo->NumberOfBuses = MAXIMUM_AHCI_PORT_COUNT;
    ConfigInfo->MaximumTransferLength = MAXIMUM_TRANSFER_LENGTH;
    ConfigInfo->SynchronizationModel = StorSynchronizeFullDuplex;

    // Turn IE -- Interrupt Enabled
    ghc.Status = StorPortReadRegisterUlong(adapterExtension, &abar->GHC);
    ghc.IE = 1;
    StorPortWriteRegisterUlong(adapterExtension, &abar->GHC, ghc.Status);

    // allocate necessary resource for each port
    if (!AhciAllocateResourceForAdapter(adapterExtension, ConfigInfo))
    {
        NT_ASSERT(FALSE);
        return SP_RETURN_ERROR;
    }

    for (index = 0; index < adapterExtension->PortCount; index++)
    {
        if ((adapterExtension->PortImplemented & (0x1 << index)) != 0)
        {
            if (!AhciPortInitialize(&adapterExtension->PortExtension[index]))
            {
                AhciDebugPrint("\tPort %u initialization failed\n", index);
                adapterExtension->PortExtension[index].DeviceParams.IsActive = FALSE;
            }
        }
    }

    return SP_RETURN_FOUND;
}// -- AhciHwFindAdapter();

/**
 * @name DriverEntry
 * @implemented
 *
 * Initial Entrypoint for storahci miniport driver
 *
 * @param DriverObject
 * @param RegistryPath
 *
 * @return
 * NT_STATUS in case of driver loaded successfully.
 */
ULONG
NTAPI
DriverEntry (
    __in PVOID DriverObject,
    __in PVOID RegistryPath
    )
{
    ULONG status;
    // initialize the hardware data structure
    HW_INITIALIZATION_DATA hwInitializationData = {0};

    KdPrintEx((DPFLTR_IHVDRIVER_ID,
               DPFLTR_INFO_LEVEL,
               "storahci DriverEntry: DriverObject %p RegistryPath %p\n",
               DriverObject,
               RegistryPath));
    // set size of hardware initialization structure
    hwInitializationData.HwInitializationDataSize = sizeof(HW_INITIALIZATION_DATA);

    // identity required miniport entry point routines
    hwInitializationData.HwStartIo = AhciHwStartIo;
    hwInitializationData.HwResetBus = AhciHwResetBus;
    hwInitializationData.HwInterrupt = AhciHwInterrupt;
    hwInitializationData.HwInitialize = AhciHwInitialize;
    hwInitializationData.HwFindAdapter = AhciHwFindAdapter;

    // adapter specific information
    hwInitializationData.TaggedQueuing = TRUE;
    hwInitializationData.AutoRequestSense = TRUE;
    hwInitializationData.MultipleRequestPerLu = TRUE;
    hwInitializationData.NeedPhysicalAddresses = TRUE;

    hwInitializationData.NumberOfAccessRanges = 6;
    hwInitializationData.AdapterInterfaceType = PCIBus;
    hwInitializationData.MapBuffers = STOR_MAP_NON_READ_WRITE_BUFFERS;

    // set required extension sizes
    hwInitializationData.SrbExtensionSize = sizeof(AHCI_SRB_EXTENSION);
    hwInitializationData.DeviceExtensionSize = sizeof(AHCI_ADAPTER_EXTENSION);

    // register our hw init data
    status = StorPortInitialize(DriverObject,
                                RegistryPath,
                                &hwInitializationData,
                                NULL);

    NT_ASSERT(status == STATUS_SUCCESS);
    return status;
}// -- DriverEntry();

/**
 * @name AhciATA_CFIS
 * @implemented
 *
 * create ATA CFIS from Srb
 *
 * @param PortExtension
 * @param Srb
 *
 * @return
 * Number of CFIS fields used in DWORD
 */
ULONG
AhciATA_CFIS (
    __in PAHCI_PORT_EXTENSION PortExtension,
    __in PAHCI_SRB_EXTENSION SrbExtension
    )
{
    PAHCI_COMMAND_TABLE cmdTable;

    UNREFERENCED_PARAMETER(PortExtension);

    AhciDebugPrint("AhciATA_CFIS()\n");

    cmdTable = (PAHCI_COMMAND_TABLE)SrbExtension;

    AhciZeroMemory((PCHAR)cmdTable->CFIS, sizeof(cmdTable->CFIS));

    cmdTable->CFIS[AHCI_ATA_CFIS_FisType] = FIS_TYPE_REG_H2D;       // FIS Type
    cmdTable->CFIS[AHCI_ATA_CFIS_PMPort_C] = (1 << 7);              // PM Port & C
    cmdTable->CFIS[AHCI_ATA_CFIS_CommandReg] = SrbExtension->CommandReg;

    cmdTable->CFIS[AHCI_ATA_CFIS_FeaturesLow] = SrbExtension->FeaturesLow;
    cmdTable->CFIS[AHCI_ATA_CFIS_LBA0] = SrbExtension->LBA0;
    cmdTable->CFIS[AHCI_ATA_CFIS_LBA1] = SrbExtension->LBA1;
    cmdTable->CFIS[AHCI_ATA_CFIS_LBA2] = SrbExtension->LBA2;
    cmdTable->CFIS[AHCI_ATA_CFIS_Device] = SrbExtension->Device;
    cmdTable->CFIS[AHCI_ATA_CFIS_LBA3] = SrbExtension->LBA3;
    cmdTable->CFIS[AHCI_ATA_CFIS_LBA4] = SrbExtension->LBA4;
    cmdTable->CFIS[AHCI_ATA_CFIS_LBA5] = SrbExtension->LBA5;
    cmdTable->CFIS[AHCI_ATA_CFIS_FeaturesHigh] = SrbExtension->FeaturesHigh;
    cmdTable->CFIS[AHCI_ATA_CFIS_SectorCountLow] = SrbExtension->SectorCountLow;
    cmdTable->CFIS[AHCI_ATA_CFIS_SectorCountHigh] = SrbExtension->SectorCountHigh;

    return 5;
}// -- AhciATA_CFIS();

/**
 * @name AhciATAPI_CFIS
 * @not_implemented
 *
 * create ATAPI CFIS from Srb
 *
 * @param PortExtension
 * @param Srb
 *
 * @return
 * Number of CFIS fields used in DWORD
 */
ULONG
AhciATAPI_CFIS (
    __in PAHCI_PORT_EXTENSION PortExtension,
    __in PAHCI_SRB_EXTENSION SrbExtension
    )
{
    PAHCI_COMMAND_TABLE cmdTable;
    UNREFERENCED_PARAMETER(PortExtension);

    AhciDebugPrint("AhciATAPI_CFIS()\n");

    cmdTable = (PAHCI_COMMAND_TABLE)SrbExtension;

    NT_ASSERT(SrbExtension->CommandReg == IDE_COMMAND_ATAPI_PACKET);

    AhciZeroMemory((PCHAR)cmdTable->CFIS, sizeof(cmdTable->CFIS));

    cmdTable->CFIS[AHCI_ATA_CFIS_FisType] = FIS_TYPE_REG_H2D;       // FIS Type
    cmdTable->CFIS[AHCI_ATA_CFIS_PMPort_C] = (1 << 7);              // PM Port & C
    cmdTable->CFIS[AHCI_ATA_CFIS_CommandReg] = SrbExtension->CommandReg;

    cmdTable->CFIS[AHCI_ATA_CFIS_FeaturesLow] = SrbExtension->FeaturesLow;
    cmdTable->CFIS[AHCI_ATA_CFIS_LBA0] = SrbExtension->LBA0;
    cmdTable->CFIS[AHCI_ATA_CFIS_LBA1] = SrbExtension->LBA1;
    cmdTable->CFIS[AHCI_ATA_CFIS_LBA2] = SrbExtension->LBA2;
    cmdTable->CFIS[AHCI_ATA_CFIS_Device] = SrbExtension->Device;
    cmdTable->CFIS[AHCI_ATA_CFIS_LBA3] = SrbExtension->LBA3;
    cmdTable->CFIS[AHCI_ATA_CFIS_LBA4] = SrbExtension->LBA4;
    cmdTable->CFIS[AHCI_ATA_CFIS_LBA5] = SrbExtension->LBA5;
    cmdTable->CFIS[AHCI_ATA_CFIS_FeaturesHigh] = SrbExtension->FeaturesHigh;
    cmdTable->CFIS[AHCI_ATA_CFIS_SectorCountLow] = SrbExtension->SectorCountLow;
    cmdTable->CFIS[AHCI_ATA_CFIS_SectorCountHigh] = SrbExtension->SectorCountHigh;

    return 5;
}// -- AhciATAPI_CFIS();

/**
 * @name AhciBuild_PRDT
 * @implemented
 *
 * Build PRDT for data transfer
 *
 * @param PortExtension
 * @param Srb
 *
 * @return
 * Return number of entries in PRDT.
 */
ULONG
AhciBuild_PRDT (
    __in PAHCI_PORT_EXTENSION PortExtension,
    __in PAHCI_SRB_EXTENSION SrbExtension
    )
{
    ULONG index;
    PAHCI_COMMAND_TABLE cmdTable;
    PLOCAL_SCATTER_GATHER_LIST sgl;
    PAHCI_ADAPTER_EXTENSION AdapterExtension;

    AhciDebugPrint("AhciBuild_PRDT()\n");

    sgl = SrbExtension->pSgl;
    cmdTable = (PAHCI_COMMAND_TABLE)SrbExtension;
    AdapterExtension = PortExtension->AdapterExtension;

    NT_ASSERT(sgl != NULL);
    NT_ASSERT(sgl->NumberOfElements < MAXIMUM_AHCI_PRDT_ENTRIES);

    if ((sgl == NULL) || (sgl->NumberOfElements == 0))
    {
        AhciDebugPrint("\tInvalid scatter/gather list\n");
        return 0;
    }

    if (sgl->NumberOfElements >= MAXIMUM_AHCI_PRDT_ENTRIES)
    {
        AhciDebugPrint("\tPRDT element count %lu exceeds maximum %u\n",
                       sgl->NumberOfElements,
                       MAXIMUM_AHCI_PRDT_ENTRIES);
        return 0;
    }

    for (index = 0; index < sgl->NumberOfElements; index++)
    {
        if (sgl->List[index].Length == 0)
        {
            AhciDebugPrint("\tScatter/gather element %lu has zero length\n", index);
            return 0;
        }

        if (sgl->List[index].Length > MAXIMUM_TRANSFER_LENGTH)
        {
            AhciDebugPrint("\tScatter/gather length %lu exceeds maximum transfer length\n",
                           sgl->List[index].Length);
            return 0;
        }

        cmdTable->PRDT[index].DBA = sgl->List[index].PhysicalAddress.LowPart;
        if (IsAdapterCAPS64(AdapterExtension->CAP))
        {
            cmdTable->PRDT[index].DBAU = sgl->List[index].PhysicalAddress.HighPart;
        }

        // Data Byte Count (DBC): A ‘0’ based value that Indicates the length, in bytes, of the data block.
        // A maximum of length of 4MB may exist for any entry. Bit ‘0’ of this field must always be ‘1’ to
        // indicate an even byte count. A value of ‘1’ indicates 2 bytes, ‘3’ indicates 4 bytes, etc.
        cmdTable->PRDT[index].DBC = sgl->List[index].Length - 1;
    }

    return sgl->NumberOfElements;
}// -- AhciBuild_PRDT();

/**
 * @name AhciProcessSrb
 * @implemented
 *
 * Prepare Srb for IO processing
 *
 * @param PortExtension
 * @param Srb
 * @param SlotIndex
 *
 */
VOID
AhciProcessSrb (
    __in PAHCI_PORT_EXTENSION PortExtension,
    __in PSCSI_REQUEST_BLOCK Srb,
    __in ULONG SlotIndex
    )
{
    ULONG prdtlen, sig, length, cfl;
    PAHCI_SRB_EXTENSION SrbExtension;
    PAHCI_COMMAND_HEADER CommandHeader;
    PAHCI_ADAPTER_EXTENSION AdapterExtension;
    STOR_PHYSICAL_ADDRESS CommandTablePhysicalAddress;

    NT_ASSERT(Srb->PathId == PortExtension->PortNumber);

    SrbExtension = GetSrbExtension(Srb);
    AdapterExtension = PortExtension->AdapterExtension;

    NT_ASSERT(SrbExtension != NULL);
    NT_ASSERT(SrbExtension->AtaFunction != 0);

    AhciDebugPrint("AhciProcessSrb() Slot=%u SRB=%p Function=%u AtaFunc=0x%x\n",
                   SlotIndex,
                   Srb,
                   Srb->Function,
                   SrbExtension->AtaFunction);

    if ((SrbExtension->AtaFunction == ATA_FUNCTION_ATA_IDENTIFY) &&
        (SrbExtension->CommandReg == IDE_COMMAND_NOT_VALID))
    {
        // Here we are safe to check SIG register
        sig = StorPortReadRegisterUlong(AdapterExtension, &PortExtension->Port->SIG);
        if (sig == 0x101)
        {
            AhciDebugPrint("\tATA Device Found!\n");
            SrbExtension->CommandReg = IDE_COMMAND_IDENTIFY;
        }
        else
        {
            AhciDebugPrint("\tATAPI Device Found!\n");
            SrbExtension->CommandReg = IDE_COMMAND_ATAPI_IDENTIFY;
        }
    }

    NT_ASSERT(SlotIndex < AHCI_Global_Port_CAP_NCS(AdapterExtension->CAP));
    SrbExtension->SlotIndex = SlotIndex;

    // program the CFIS in the CommandTable
    CommandHeader = &PortExtension->CommandList[SlotIndex];

    cfl = 0;
    if (IsAtapiCommand(SrbExtension->AtaFunction))
    {
        cfl = AhciATAPI_CFIS(PortExtension, SrbExtension);
    }
    else if (IsAtaCommand(SrbExtension->AtaFunction))
    {
        cfl = AhciATA_CFIS(PortExtension, SrbExtension);
    }
    else
    {
        NT_ASSERT(FALSE);
    }

    prdtlen = 0;
    if (IsDataTransferNeeded(SrbExtension))
    {
        prdtlen = AhciBuild_PRDT(PortExtension, SrbExtension);
        if (prdtlen == 0)
        {
            AhciDebugPrint("\tFailed to build PRDT for SRB %p\n", Srb);
            PortExtension->Slot[SlotIndex] = NULL;
            PortExtension->QueueSlots &= ~(1 << SlotIndex);
            Srb->SrbStatus = SRB_STATUS_ERROR;
            if (!AddQueue(&PortExtension->CompletionQueue, Srb))
            {
                AhciDebugPrint("\tCompletion queue full while reporting PRDT failure\n");
            }
            StorPortIssueDpc(AdapterExtension,
                              &PortExtension->CommandCompletion,
                              PortExtension,
                              Srb);
            return;
        }
    }

    // Program the command header
    CommandHeader->DI.PRDTL = prdtlen; // number of entries in PRD table
    CommandHeader->DI.CFL = cfl;
    CommandHeader->DI.A = (SrbExtension->AtaFunction & ATA_FUNCTION_ATAPI_COMMAND) ? 1 : 0;
    CommandHeader->DI.W = (SrbExtension->Flags & ATA_FLAGS_DATA_OUT) ? 1 : 0;
    CommandHeader->DI.P = 0;    // ATA Specifications says so
    CommandHeader->DI.PMP = 0;  // Port Multiplier

    // Reset -- Manual Configuation
    CommandHeader->DI.R = 0;
    CommandHeader->DI.B = 0;
    CommandHeader->DI.C = 0;

    CommandHeader->PRDBC = 0;

    CommandHeader->Reserved[0] = 0;
    CommandHeader->Reserved[1] = 0;
    CommandHeader->Reserved[2] = 0;
    CommandHeader->Reserved[3] = 0;

    // set CommandHeader CTBA
    CommandTablePhysicalAddress = StorPortGetPhysicalAddress(AdapterExtension,
                                                             NULL,
                                                             SrbExtension,
                                                             &length);

    NT_ASSERT(length != 0);

    // command table alignment
    NT_ASSERT((CommandTablePhysicalAddress.LowPart % 128) == 0);

    CommandHeader->CTBA = CommandTablePhysicalAddress.LowPart;

    if (IsAdapterCAPS64(AdapterExtension->CAP))
    {
        CommandHeader->CTBA_U = CommandTablePhysicalAddress.HighPart;
    }

    // mark this slot
    PortExtension->Slot[SlotIndex] = Srb;
    PortExtension->QueueSlots |= 1 << SlotIndex;
    return;
}// -- AhciProcessSrb();

/**
 * @name AhciActivatePort
 * @implemented
 *
 * Program Port and populate command list
 *
 * @param PortExtension
 *
 */

#ifdef _MSC_VER     // avoid MSVC C4700
    #pragma warning(push)
    #pragma warning(disable: 4700)
#endif

VOID
AhciActivatePort (
    __in PAHCI_PORT_EXTENSION PortExtension
    )
{
    AHCI_PORT_CMD cmd;
    ULONG QueueSlots, slotToActivate, tmp;
    PAHCI_ADAPTER_EXTENSION AdapterExtension;

    AhciDebugPrint("AhciActivatePort() Port=%u QueueSlots=0x%lx Issued=0x%lx\n",
                   PortExtension->PortNumber,
                   PortExtension->QueueSlots,
                   PortExtension->CommandIssuedSlots);

    AdapterExtension = PortExtension->AdapterExtension;
    QueueSlots = PortExtension->QueueSlots;

    if (QueueSlots == 0)
    {
        return;
    }

    // section 3.3.14
    // Bits in this field shall only be set to ‘1’ by software when PxCMD.ST is set to ‘1’
    cmd.Status = StorPortReadRegisterUlong(AdapterExtension, &PortExtension->Port->CMD);

    if (cmd.ST == 0) // PxCMD.ST == 0
    {
        return;
    }

    // get the lowest set bit
    tmp = QueueSlots & (QueueSlots - 1);

    if (tmp == 0)
        slotToActivate = QueueSlots;
    else
        slotToActivate = (QueueSlots & (~tmp));

    // mark that bit off in QueueSlots
    // so we can know we it is really needed to activate port or not
    PortExtension->QueueSlots &= ~slotToActivate;
    // mark this CommandIssuedSlots
    // to validate in completeIssuedCommand
    PortExtension->CommandIssuedSlots |= slotToActivate;

    // tell the HBA to issue this Command Slot to the given port
    StorPortWriteRegisterUlong(AdapterExtension, &PortExtension->Port->CI, slotToActivate);
    AhciDebugPrint("\tActivated slot mask 0x%lx\n", slotToActivate);

    return;
}// -- AhciActivatePort();

#ifdef _MSC_VER     // avoid MSVC C4700
    #pragma warning(pop)
#endif

/**
 * @name AhciProcessIO
 * @implemented
 *
 * Acquire Exclusive lock to port, populate pending commands to command List
 * program controller's port to process new commands in command list.
 *
 * @param AdapterExtension
 * @param PathId
 * @param Srb
 *
 */
VOID
AhciProcessIO (
    __in PAHCI_ADAPTER_EXTENSION AdapterExtension,
    __in UCHAR PathId,
    __in PSCSI_REQUEST_BLOCK Srb
    )
{
    PSCSI_REQUEST_BLOCK tmpSrb;
    STOR_LOCK_HANDLE lockhandle = {0};
    PAHCI_PORT_EXTENSION PortExtension;
    ULONG commandSlotMask, occupiedSlots, slotIndex, NCS;

    AhciDebugPrint("AhciProcessIO() Path=%u\n", PathId);

    PortExtension = &AdapterExtension->PortExtension[PathId];

    NT_ASSERT(PathId < AdapterExtension->PortCount);

    // Acquire Lock
    StorPortAcquireSpinLock(AdapterExtension, InterruptLock, NULL, &lockhandle);

    // add Srb to queue
    AddQueue(&PortExtension->SrbQueue, Srb);
    AhciDebugPrint("\tQueued SRB %p, DeviceActive=%d\n", Srb, PortExtension->DeviceParams.IsActive);

    if (PortExtension->DeviceParams.IsActive == FALSE)
    {
        // Release Lock
        StorPortReleaseSpinLock(AdapterExtension, &lockhandle);
        return; // we should wait for device to get active
    }

    occupiedSlots = (PortExtension->QueueSlots | PortExtension->CommandIssuedSlots); // Busy command slots for given port
    NCS = AHCI_Global_Port_CAP_NCS(AdapterExtension->CAP);
    commandSlotMask = (1 << NCS) - 1; // available slots mask

    commandSlotMask = (commandSlotMask & ~occupiedSlots);
    if(commandSlotMask != 0)
    {
        // iterate over HBA port slots
        for (slotIndex = 0; slotIndex < NCS; slotIndex++)
        {
            // find first free slot
            if ((commandSlotMask & (1 << slotIndex)) != 0)
            {
                tmpSrb = RemoveQueue(&PortExtension->SrbQueue);
                if (tmpSrb != NULL)
                {
                    NT_ASSERT(tmpSrb->PathId == PathId);
                    AhciProcessSrb(PortExtension, tmpSrb, slotIndex);
                }
                else
                {
                    break;
                }
            }
            else
            {
                break;
            }
        }
    }

    // program HBA port
    AhciActivatePort(PortExtension);

    // Release Lock
    StorPortReleaseSpinLock(AdapterExtension, &lockhandle);

    return;
}// -- AhciProcessIO();

/**
 * @name AtapiInquiryCompletion
 * @implemented
 *
 * AtapiInquiryCompletion routine should be called after device signals
 * for device inquiry request is completed (through interrupt) -- ATAPI Device only
 *
 * @param PortExtension
 * @param Srb
 *
 */
VOID
AtapiInquiryCompletion (
    __in PVOID _Extension,
    __in PVOID _Srb
    )
{
    PAHCI_PORT_EXTENSION PortExtension;
    PAHCI_ADAPTER_EXTENSION AdapterExtension;
    PSCSI_REQUEST_BLOCK Srb;
    BOOLEAN status;

    AhciDebugPrint("AtapiInquiryCompletion()\n");

    PortExtension = (PAHCI_PORT_EXTENSION)_Extension;
    Srb = (PSCSI_REQUEST_BLOCK)_Srb;

    NT_ASSERT(Srb != NULL);
    NT_ASSERT(PortExtension != NULL);

    AdapterExtension = PortExtension->AdapterExtension;

    // send queue depth
    status = StorPortSetDeviceQueueDepth(PortExtension->AdapterExtension,
                                         Srb->PathId,
                                         Srb->TargetId,
                                         Srb->Lun,
                                         AHCI_Global_Port_CAP_NCS(AdapterExtension->CAP));

    NT_ASSERT(status == TRUE);
    return;
}// -- AtapiInquiryCompletion();

/**
 * @name InquiryCompletion
 * @implemented
 *
 * InquiryCompletion routine should be called after device signals
 * for device inquiry request is completed (through interrupt)
 *
 * @param PortExtension
 * @param Srb
 *
 */
VOID
InquiryCompletion (
    __in PVOID _Extension,
    __in PVOID _Srb
    )
{
    PAHCI_PORT_EXTENSION PortExtension;
    PSCSI_REQUEST_BLOCK Srb;

//    PCDB cdb;
    BOOLEAN status;
    PINQUIRYDATA InquiryData;
    PAHCI_SRB_EXTENSION SrbExtension;
    PAHCI_ADAPTER_EXTENSION AdapterExtension;
    PIDENTIFY_DEVICE_DATA IdentifyDeviceData;

    AhciDebugPrint("InquiryCompletion()\n");

    PortExtension = (PAHCI_PORT_EXTENSION)_Extension;
    Srb = (PSCSI_REQUEST_BLOCK)_Srb;

    NT_ASSERT(Srb != NULL);
    NT_ASSERT(PortExtension != NULL);

//    cdb = (PCDB)&Srb->Cdb;
    InquiryData = Srb->DataBuffer;
    SrbExtension = GetSrbExtension(Srb);
    AdapterExtension = PortExtension->AdapterExtension;
    IdentifyDeviceData = PortExtension->IdentifyDeviceData;

    if (Srb->SrbStatus != SRB_STATUS_SUCCESS)
    {
        if (Srb->SrbStatus == SRB_STATUS_NO_DEVICE)
        {
            PortExtension->DeviceParams.DeviceType = AHCI_DEVICE_TYPE_NODEVICE;
        }
        return;
    }

    NT_ASSERT(InquiryData != NULL);
    NT_ASSERT(Srb->SrbStatus == SRB_STATUS_SUCCESS);

    // Device specific data
    PortExtension->DeviceParams.MaxLba.QuadPart = 0;

    if (SrbExtension->CommandReg == IDE_COMMAND_IDENTIFY)
    {
        PortExtension->DeviceParams.DeviceType = AHCI_DEVICE_TYPE_ATA;
        if (IdentifyDeviceData->GeneralConfiguration.RemovableMedia)
        {
            PortExtension->DeviceParams.RemovableDevice = 1;
        }

        if ((IdentifyDeviceData->CommandSetSupport.BigLba) && (IdentifyDeviceData->CommandSetActive.BigLba))
        {
            PortExtension->DeviceParams.Lba48BitMode = 1;
        }

        PortExtension->DeviceParams.AccessType = DIRECT_ACCESS_DEVICE;

        /* Device max address lba */
        if (PortExtension->DeviceParams.Lba48BitMode)
        {
            PortExtension->DeviceParams.MaxLba.LowPart = IdentifyDeviceData->Max48BitLBA[0];
            PortExtension->DeviceParams.MaxLba.HighPart = IdentifyDeviceData->Max48BitLBA[1];
        }
        else
        {
            PortExtension->DeviceParams.MaxLba.LowPart = IdentifyDeviceData->UserAddressableSectors;
        }

        /* Bytes Per Logical Sector */
        if (IdentifyDeviceData->PhysicalLogicalSectorSize.LogicalSectorLongerThan256Words)
        {
            AhciDebugPrint("\tBytesPerLogicalSector != DEVICE_ATA_BLOCK_SIZE\n");
            NT_ASSERT(FALSE);
        }

        PortExtension->DeviceParams.BytesPerLogicalSector = DEVICE_ATA_BLOCK_SIZE;

        /* Bytes Per Physical Sector */
        if (IdentifyDeviceData->PhysicalLogicalSectorSize.MultipleLogicalSectorsPerPhysicalSector)
        {
            AhciDebugPrint("\tBytesPerPhysicalSector != DEVICE_ATA_BLOCK_SIZE\n");
            NT_ASSERT(FALSE);
        }

    PortExtension->DeviceParams.BytesPerPhysicalSector = DEVICE_ATA_BLOCK_SIZE;

    // Convert IDENTIFY strings from word-swapped ASCII to C strings (non-paged cache)
    AhciCopyIdentifyString(PortExtension->DeviceParams.VendorId,
                           IdentifyDeviceData->ModelNumber,
                           sizeof(PortExtension->DeviceParams.VendorId),
                           sizeof(IdentifyDeviceData->ModelNumber));
    AhciCopyIdentifyString(PortExtension->DeviceParams.RevisionID,
                           IdentifyDeviceData->FirmwareRevision,
                           sizeof(PortExtension->DeviceParams.RevisionID),
                           sizeof(IdentifyDeviceData->FirmwareRevision));
    AhciCopyIdentifyString(PortExtension->DeviceParams.SerialNumber,
                           IdentifyDeviceData->SerialNumber,
                           sizeof(PortExtension->DeviceParams.SerialNumber),
                           sizeof(IdentifyDeviceData->SerialNumber));

    AhciCopyIdentifyString(PortExtension->DeviceParams.SerialNumberAscii,
                           IdentifyDeviceData->SerialNumber,
                           sizeof(PortExtension->DeviceParams.SerialNumberAscii),
                           sizeof(IdentifyDeviceData->SerialNumber));
    PortExtension->DeviceParams.SerialNumberAsciiLength =
        (USHORT)AhciGetTrimmedStringSegment(PortExtension->DeviceParams.SerialNumberAscii,
                                            sizeof(PortExtension->DeviceParams.SerialNumberAscii),
                                            NULL);

    PortExtension->DeviceParams.DeviceIdentifierLength =
        (USHORT)AhciBuildDeviceIdentifierString(PortExtension,
                                                PortExtension->DeviceParams.DeviceIdentifier,
                                                sizeof(PortExtension->DeviceParams.DeviceIdentifier));

    // TODO: Add other device params
    AhciDebugPrint("\tATA Device\n");
    }
    else
    {
        AhciDebugPrint("\tATAPI Device\n");
        PortExtension->DeviceParams.DeviceType = AHCI_DEVICE_TYPE_ATAPI;
        PortExtension->DeviceParams.AccessType = READ_ONLY_DIRECT_ACCESS_DEVICE;
    }

    // INQUIRYDATABUFFERSIZE = 36 ; Defined in storport.h
    if (Srb->DataTransferLength < INQUIRYDATABUFFERSIZE)
    {
        AhciDebugPrint("\tDataBufferLength < sizeof(INQUIRYDATA), Could crash the driver.\n");
        NT_ASSERT(FALSE);
    }

    // update data transfer length
    Srb->DataTransferLength = INQUIRYDATABUFFERSIZE;

    // prepare data to send
    InquiryData->Versions = 2;
    InquiryData->Wide32Bit = 1;
    InquiryData->CommandQueue = (PortExtension->MaxPortQueueDepth > 1) ? 1 : 0;
    InquiryData->ResponseDataFormat = 0x2;
    InquiryData->DeviceTypeModifier = 0;
    InquiryData->DeviceTypeQualifier = DEVICE_CONNECTED;
    InquiryData->AdditionalLength = INQUIRYDATABUFFERSIZE - 5;
    InquiryData->DeviceType = PortExtension->DeviceParams.AccessType;
    InquiryData->RemovableMedia = PortExtension->DeviceParams.RemovableDevice;

    // Fill VendorID, Product Revision Level and other string fields
    StorPortCopyMemory(InquiryData->VendorId, PortExtension->DeviceParams.VendorId, sizeof(InquiryData->VendorId) - 1);
    StorPortCopyMemory(InquiryData->ProductId, PortExtension->DeviceParams.RevisionID, sizeof(PortExtension->DeviceParams.RevisionID));
    StorPortCopyMemory(InquiryData->ProductRevisionLevel, PortExtension->DeviceParams.SerialNumber, sizeof(InquiryData->ProductRevisionLevel) - 1);

    InquiryData->VendorId[sizeof(InquiryData->VendorId) - 1] = '\0';
    InquiryData->ProductId[sizeof(InquiryData->ProductId) - 1] = '\0';
    InquiryData->ProductRevisionLevel[sizeof(InquiryData->ProductRevisionLevel) - 1] = '\0';

    // send queue depth
    status = StorPortSetDeviceQueueDepth(PortExtension->AdapterExtension,
                                         Srb->PathId,
                                         Srb->TargetId,
                                         Srb->Lun,
                                         AHCI_Global_Port_CAP_NCS(AdapterExtension->CAP));

    NT_ASSERT(status == TRUE);
    return;
}// -- InquiryCompletion();

 /**
 * @name AhciATAPICommand
 * @implemented
 *
 * Handles ATAPI Requests commands
 *
 * @param AdapterExtension
 * @param Srb
 * @param Cdb
 *
 * @return
 * return STOR status for AhciATAPICommand
 */
UCHAR
AhciATAPICommand (
    __in PAHCI_ADAPTER_EXTENSION AdapterExtension,
    __in PSCSI_REQUEST_BLOCK Srb,
    __in PCDB Cdb
    )
{
    ULONG SrbFlags, DataBufferLength;
    PAHCI_SRB_EXTENSION SrbExtension;
    PAHCI_PORT_EXTENSION PortExtension;

    AhciDebugPrint("AhciATAPICommand()\n");

    SrbFlags = Srb->SrbFlags;
    SrbExtension = GetSrbExtension(Srb);
    DataBufferLength = Srb->DataTransferLength;
    PortExtension = &AdapterExtension->PortExtension[Srb->PathId];

    NT_ASSERT(PortExtension->DeviceParams.DeviceType == AHCI_DEVICE_TYPE_ATAPI);

    NT_ASSERT(SrbExtension != NULL);

    SrbExtension->AtaFunction = ATA_FUNCTION_ATAPI_COMMAND;
    SrbExtension->Flags = 0;

    if (SrbFlags & SRB_FLAGS_DATA_IN)
    {
        SrbExtension->Flags |= ATA_FLAGS_DATA_IN;
    }

    if (SrbFlags & SRB_FLAGS_DATA_OUT)
    {
        SrbExtension->Flags |= ATA_FLAGS_DATA_OUT;
    }

    SrbExtension->FeaturesLow = 0;

    SrbExtension->CompletionRoutine = NULL;

    NT_ASSERT(Cdb != NULL);
    switch(Cdb->CDB10.OperationCode)
    {
        case SCSIOP_INQUIRY:
            SrbExtension->Flags |= ATA_FLAGS_DATA_IN;
            SrbExtension->CompletionRoutine = AtapiInquiryCompletion;
            break;
        case SCSIOP_READ:
            SrbExtension->Flags |= ATA_FLAGS_USE_DMA;
            SrbExtension->FeaturesLow = 0x5;
            break;
        case SCSIOP_WRITE:
            SrbExtension->Flags |= ATA_FLAGS_USE_DMA;
            SrbExtension->FeaturesLow = 0x1;
            break;
    }

    if (Srb->CdbLength > sizeof(SrbExtension->CommandTable.ACMD))
    {
        AhciDebugPrint("\tATAPI command too large: %u\n", Srb->CdbLength);
        return SRB_STATUS_INVALID_REQUEST;
    }

    AhciZeroMemory((PCHAR)SrbExtension->CommandTable.ACMD,
                   sizeof(SrbExtension->CommandTable.ACMD));

    if (Srb->CdbLength != 0)
    {
        StorPortCopyMemory(SrbExtension->CommandTable.ACMD,
                           Srb->Cdb,
                           Srb->CdbLength);
    }

    SrbExtension->CommandReg = IDE_COMMAND_ATAPI_PACKET;

    SrbExtension->LBA0 = 0;
    SrbExtension->LBA1 = (UCHAR)(DataBufferLength >> 0);
    SrbExtension->LBA2 = (UCHAR)(DataBufferLength >> 8);
    SrbExtension->Device = 0;
    SrbExtension->LBA3 = 0;
    SrbExtension->LBA4 = 0;
    SrbExtension->LBA5 = 0;
    SrbExtension->FeaturesHigh = 0;
    SrbExtension->SectorCountLow = 0;
    SrbExtension->SectorCountHigh = 0;

    if ((SrbExtension->Flags & ATA_FLAGS_DATA_IN) || (SrbExtension->Flags & ATA_FLAGS_DATA_OUT))
    {
        SrbExtension->pSgl = (PLOCAL_SCATTER_GATHER_LIST)StorPortGetScatterGatherList(AdapterExtension, Srb);
        if (SrbExtension->pSgl == NULL)
        {
            AhciDebugPrint("\tFailed to acquire scatter/gather list for ATAPI command\n");
            return SRB_STATUS_ERROR;
        }
    }

    return SRB_STATUS_PENDING;
}// -- AhciATAPICommand();

/**
 * @name DeviceRequestSense
 * @implemented
 *
 * Handle SCSIOP_MODE_SENSE OperationCode
 *
 * @param AdapterExtension
 * @param Srb
 * @param Cdb
 *
 * @return
 * return STOR status for DeviceRequestSense
 */
UCHAR
DeviceRequestSense (
    __in PAHCI_ADAPTER_EXTENSION AdapterExtension,
    __in PSCSI_REQUEST_BLOCK Srb,
    __in PCDB Cdb
    )
{
    PMODE_PARAMETER_HEADER ModeHeader;
    PAHCI_PORT_EXTENSION PortExtension;

    AhciDebugPrint("DeviceRequestSense()\n");

    NT_ASSERT(IsPortValid(AdapterExtension, Srb->PathId));
    NT_ASSERT(Cdb->CDB10.OperationCode == SCSIOP_MODE_SENSE);

    PortExtension = &AdapterExtension->PortExtension[Srb->PathId];

    if (PortExtension->DeviceParams.DeviceType == AHCI_DEVICE_TYPE_ATAPI)
    {
        return AhciATAPICommand(AdapterExtension, Srb, Cdb);
    }

    ModeHeader = (PMODE_PARAMETER_HEADER)Srb->DataBuffer;

    NT_ASSERT(ModeHeader != NULL);

    AhciZeroMemory((PCHAR)ModeHeader, Srb->DataTransferLength);

    ModeHeader->ModeDataLength = sizeof(MODE_PARAMETER_HEADER);
    ModeHeader->MediumType = 0;
    ModeHeader->DeviceSpecificParameter = 0;
    ModeHeader->BlockDescriptorLength = 0;

    if (Cdb->MODE_SENSE.PageCode == MODE_SENSE_CURRENT_VALUES)
    {
        ModeHeader->ModeDataLength = sizeof(MODE_PARAMETER_HEADER) + sizeof(MODE_PARAMETER_BLOCK);
        ModeHeader->BlockDescriptorLength = sizeof(MODE_PARAMETER_BLOCK);
    }

    return SRB_STATUS_SUCCESS;
}// -- DeviceRequestSense();

/**
 * @name DeviceRequestReadWrite
 * @implemented
 *
 * Handle SCSIOP_READ SCSIOP_WRITE OperationCode
 *
 * @param AdapterExtension
 * @param Srb
 * @param Cdb
 *
 * @return
 * return STOR status for DeviceRequestReadWrite
 */
static
UCHAR
AhciPrepareAtaReadWrite (
    __in PAHCI_ADAPTER_EXTENSION AdapterExtension,
    __in PAHCI_PORT_EXTENSION PortExtension,
    __in PSCSI_REQUEST_BLOCK Srb,
    __in ULONGLONG StartOffset,
    __in ULONG SectorCount,
    __in BOOLEAN IsReading
    )
{
    PAHCI_SRB_EXTENSION SrbExtension;

    SrbExtension = GetSrbExtension(Srb);
    if (SrbExtension == NULL)
    {
        AhciDebugPrint("\tSrbExtension is NULL\n");
        return SRB_STATUS_ERROR;
    }

    SrbExtension->AtaFunction = ATA_FUNCTION_ATA_READ;
    SrbExtension->Flags = 0;
    SrbExtension->CompletionRoutine = NULL;

    if (IsReading)
    {
        SrbExtension->Flags |= ATA_FLAGS_DATA_IN;
        SrbExtension->CommandReg = IDE_COMMAND_READ_DMA;
    }
    else
    {
        SrbExtension->Flags |= ATA_FLAGS_DATA_OUT;
        SrbExtension->CommandReg = IDE_COMMAND_WRITE_DMA;
    }

    if (SectorCount != 0)
    {
        SrbExtension->Flags |= ATA_FLAGS_USE_DMA;
    }

    SrbExtension->FeaturesLow = 0;
    SrbExtension->LBA0 = (UCHAR)(StartOffset & 0xFF);
    SrbExtension->LBA1 = (UCHAR)((StartOffset >> 8) & 0xFF);
    SrbExtension->LBA2 = (UCHAR)((StartOffset >> 16) & 0xFF);

    SrbExtension->Device = (UCHAR)(0xA0 | IDE_LBA_MODE | ((StartOffset >> 24) & 0x0F));
    SrbExtension->FeaturesHigh = 0;

    if (PortExtension->DeviceParams.Lba48BitMode)
    {
        ULONG sectorLimit = 0x10000;

        SrbExtension->Flags |= ATA_FLAGS_48BIT_COMMAND;

        if (IsReading)
        {
            SrbExtension->CommandReg = IDE_COMMAND_READ_DMA_EXT;
        }
        else
        {
            SrbExtension->CommandReg = IDE_COMMAND_WRITE_DMA_EXT;
        }

        SrbExtension->LBA3 = (UCHAR)((StartOffset >> 24) & 0xFF);
        SrbExtension->LBA4 = (UCHAR)((StartOffset >> 32) & 0xFF);
        SrbExtension->LBA5 = (UCHAR)((StartOffset >> 40) & 0xFF);

        if (SectorCount > sectorLimit)
        {
            if (SectorCount != sectorLimit)
            {
                AhciDebugPrint("\tSector count %lu exceeds 48-bit command limit\n", SectorCount);
                return SRB_STATUS_INVALID_REQUEST;
            }

            SrbExtension->SectorCountLow = 0;
            SrbExtension->SectorCountHigh = 0;
        }
        else
        {
            SrbExtension->SectorCountLow = (UCHAR)(SectorCount & 0xFF);
            SrbExtension->SectorCountHigh = (UCHAR)((SectorCount >> 8) & 0xFF);
        }
    }
    else
    {
        if (StartOffset > 0x0FFFFFFF)
        {
            AhciDebugPrint("\tLBA 0x%I64x exceeds 28-bit addressing range\n", StartOffset);
            return SRB_STATUS_INVALID_REQUEST;
        }

        if (SectorCount > 0x100)
        {
            AhciDebugPrint("\tSector count %lu exceeds 28-bit capability\n", SectorCount);
            return SRB_STATUS_INVALID_REQUEST;
        }

        SrbExtension->LBA3 = 0;
        SrbExtension->LBA4 = 0;
        SrbExtension->LBA5 = 0;

        if (SectorCount == 0x100)
        {
            SrbExtension->SectorCountLow = 0;
        }
        else
        {
            SrbExtension->SectorCountLow = (UCHAR)(SectorCount & 0xFF);
        }

        SrbExtension->SectorCountHigh = 0;
    }

    SrbExtension->pSgl = NULL;
    if (SectorCount != 0)
    {
        SrbExtension->pSgl = (PLOCAL_SCATTER_GATHER_LIST)StorPortGetScatterGatherList(AdapterExtension, Srb);
        if (SrbExtension->pSgl == NULL)
        {
            AhciDebugPrint("\tFailed to acquire scatter/gather list\n");
            return SRB_STATUS_ERROR;
        }
    }

    return SRB_STATUS_PENDING;
}// -- AhciPrepareAtaReadWrite();

UCHAR
DeviceRequestReadWrite (
    __in PAHCI_ADAPTER_EXTENSION AdapterExtension,
    __in PSCSI_REQUEST_BLOCK Srb,
    __in PCDB Cdb
    )
{
    BOOLEAN IsReading;
    ULONGLONG StartOffset;
    PAHCI_PORT_EXTENSION PortExtension;
    ULONG DataTransferLength, BytesPerSector, SectorCount;

    AhciDebugPrint("DeviceRequestReadWrite()\n");

    NT_ASSERT(IsPortValid(AdapterExtension, Srb->PathId));
    NT_ASSERT((Cdb->CDB10.OperationCode == SCSIOP_READ) || (Cdb->CDB10.OperationCode == SCSIOP_WRITE));

    PortExtension = &AdapterExtension->PortExtension[Srb->PathId];

    if (PortExtension->DeviceParams.DeviceType == AHCI_DEVICE_TYPE_ATAPI)
    {
        return AhciATAPICommand(AdapterExtension, Srb, Cdb);
    }

    DataTransferLength = Srb->DataTransferLength;
    BytesPerSector = PortExtension->DeviceParams.BytesPerLogicalSector;

    if (BytesPerSector == 0)
    {
        AhciDebugPrint("\tInvalid bytes per sector\n");
        return SRB_STATUS_ERROR;
    }

    if ((DataTransferLength % BytesPerSector) != 0)
    {
        AhciDebugPrint("\tTransfer length %lu not aligned to sector size %lu\n",
                       DataTransferLength,
                       BytesPerSector);
        return SRB_STATUS_INVALID_REQUEST;
    }

    SectorCount = DataTransferLength / BytesPerSector;

    if (SectorCount == 0)
    {
        AhciDebugPrint("\tZero-length transfer request\n");
        Srb->DataTransferLength = 0;
        return SRB_STATUS_SUCCESS;
    }

    Srb->DataTransferLength = SectorCount * BytesPerSector;

    StartOffset = AhciGetLba(Cdb, Srb->CdbLength);
    IsReading = (Cdb->CDB10.OperationCode == SCSIOP_READ);

    return AhciPrepareAtaReadWrite(AdapterExtension,
                                   PortExtension,
                                   Srb,
                                   StartOffset,
                                   SectorCount,
                                   IsReading);
}// -- DeviceRequestReadWrite();

UCHAR
DeviceRequestReadWrite16 (
    __in PAHCI_ADAPTER_EXTENSION AdapterExtension,
    __in PSCSI_REQUEST_BLOCK Srb,
    __in PCDB Cdb
    )
{
    BOOLEAN IsReading;
    ULONGLONG StartOffset;
    ULONG BytesPerSector;
    ULONG SectorCount;
    PAHCI_PORT_EXTENSION PortExtension;

    AhciDebugPrint("DeviceRequestReadWrite16()\n");

    NT_ASSERT(IsPortValid(AdapterExtension, Srb->PathId));
    NT_ASSERT((Cdb->CDB16.OperationCode == SCSIOP_READ16) ||
              (Cdb->CDB16.OperationCode == SCSIOP_WRITE16));

    PortExtension = &AdapterExtension->PortExtension[Srb->PathId];

    if (PortExtension->DeviceParams.DeviceType == AHCI_DEVICE_TYPE_ATAPI)
    {
        return AhciATAPICommand(AdapterExtension, Srb, Cdb);
    }

    BytesPerSector = PortExtension->DeviceParams.BytesPerLogicalSector;
    if (BytesPerSector == 0)
    {
        AhciDebugPrint("\tInvalid bytes per sector\n");
        return SRB_STATUS_ERROR;
    }

    SectorCount = 0;
    REVERSE_BYTES(&SectorCount, Cdb->CDB16.TransferLength);

    if (SectorCount == 0)
    {
        AhciDebugPrint("\tZero-length READ/WRITE16 request\n");
        Srb->DataTransferLength = 0;
        return SRB_STATUS_SUCCESS;
    }

    if (SectorCount > (MAXIMUM_TRANSFER_LENGTH / BytesPerSector))
    {
        AhciDebugPrint("\tSector count %lu exceeds maximum transfer capability\n", SectorCount);
        return SRB_STATUS_INVALID_REQUEST;
    }

    Srb->DataTransferLength = SectorCount * BytesPerSector;

    StartOffset = AhciGetLba(Cdb, 0x10);
    IsReading = (Cdb->CDB16.OperationCode == SCSIOP_READ16);

    return AhciPrepareAtaReadWrite(AdapterExtension,
                                   PortExtension,
                                   Srb,
                                   StartOffset,
                                   SectorCount,
                                   IsReading);
}// -- DeviceRequestReadWrite16();

UCHAR
DeviceRequestFlush (
    __in PAHCI_ADAPTER_EXTENSION AdapterExtension,
    __in PSCSI_REQUEST_BLOCK Srb,
    __in PCDB Cdb
    )
{
    PAHCI_SRB_EXTENSION SrbExtension;
    PAHCI_PORT_EXTENSION PortExtension;

    AhciDebugPrint("DeviceRequestFlush()\n");

    NT_ASSERT(IsPortValid(AdapterExtension, Srb->PathId));
    UNREFERENCED_PARAMETER(Cdb);

    PortExtension = &AdapterExtension->PortExtension[Srb->PathId];

    if (PortExtension->DeviceParams.DeviceType == AHCI_DEVICE_TYPE_ATAPI)
    {
        AhciDebugPrint("\tFlush ignored for ATAPI device\n");
        Srb->DataTransferLength = 0;
        return SRB_STATUS_SUCCESS;
    }

    SrbExtension = GetSrbExtension(Srb);
    if (SrbExtension == NULL)
    {
        AhciDebugPrint("\tSrbExtension is NULL\n");
        return SRB_STATUS_ERROR;
    }

    Srb->DataTransferLength = 0;

    SrbExtension->AtaFunction = ATA_FUNCTION_ATA_COMMAND;
    SrbExtension->Flags = 0;
    SrbExtension->CompletionRoutine = NULL;

    SrbExtension->FeaturesLow = 0;
    SrbExtension->FeaturesHigh = 0;
    SrbExtension->SectorCountLow = 0;
    SrbExtension->SectorCountHigh = 0;
    SrbExtension->LBA0 = 0;
    SrbExtension->LBA1 = 0;
    SrbExtension->LBA2 = 0;
    SrbExtension->LBA3 = 0;
    SrbExtension->LBA4 = 0;
    SrbExtension->LBA5 = 0;

    SrbExtension->Device = (UCHAR)(0xA0 | IDE_LBA_MODE);

    if (PortExtension->DeviceParams.Lba48BitMode)
    {
        SrbExtension->Flags |= ATA_FLAGS_48BIT_COMMAND;
        SrbExtension->CommandReg = IDE_COMMAND_FLUSH_CACHE_EXT;
    }
    else
    {
        SrbExtension->CommandReg = IDE_COMMAND_FLUSH_CACHE;
    }

    SrbExtension->pSgl = NULL;

    return SRB_STATUS_PENDING;
}// -- DeviceRequestFlush();

/**
 * @name DeviceRequestCapacity
 * @implemented
 *
 * Handle SCSIOP_READ_CAPACITY OperationCode
 *
 * @param AdapterExtension
 * @param Srb
 * @param Cdb
 *
 * @return
 * return STOR status for DeviceRequestCapacity
 */
UCHAR
DeviceRequestCapacity (
    __in PAHCI_ADAPTER_EXTENSION AdapterExtension,
    __in PSCSI_REQUEST_BLOCK Srb,
    __in PCDB Cdb
    )
{
    ULONG MaxLba, BytesPerLogicalSector;
    PREAD_CAPACITY_DATA ReadCapacity;
    PAHCI_PORT_EXTENSION PortExtension;

    AhciDebugPrint("DeviceRequestCapacity()\n");

    UNREFERENCED_PARAMETER(AdapterExtension);
    UNREFERENCED_PARAMETER(Cdb);

    NT_ASSERT(Srb->DataBuffer != NULL);
    NT_ASSERT(IsPortValid(AdapterExtension, Srb->PathId));


    PortExtension = &AdapterExtension->PortExtension[Srb->PathId];

    if (PortExtension->DeviceParams.DeviceType == AHCI_DEVICE_TYPE_ATAPI)
    {
        return AhciATAPICommand(AdapterExtension, Srb, Cdb);
    }

    if (Cdb->CDB10.OperationCode == SCSIOP_READ_CAPACITY)
    {
        ReadCapacity = (PREAD_CAPACITY_DATA)Srb->DataBuffer;

        BytesPerLogicalSector = PortExtension->DeviceParams.BytesPerLogicalSector;
        MaxLba = (ULONG)PortExtension->DeviceParams.MaxLba.QuadPart - 1;

        // I trust you windows :D
        NT_ASSERT(Srb->DataTransferLength >= sizeof(READ_CAPACITY_DATA));

        // I trust you user :D
        NT_ASSERT(PortExtension->DeviceParams.MaxLba.QuadPart < (ULONG)-1);

        // Actually I don't trust anyone :p
        Srb->DataTransferLength = sizeof(READ_CAPACITY_DATA);

        REVERSE_BYTES(&ReadCapacity->BytesPerBlock, &BytesPerLogicalSector);
        REVERSE_BYTES(&ReadCapacity->LogicalBlockAddress, &MaxLba);
    }
    else if ((Cdb->READ_CAPACITY16.OperationCode == SCSIOP_READ_CAPACITY16) &&
             (Cdb->READ_CAPACITY16.ServiceAction == SERVICE_ACTION_READ_CAPACITY16))
    {
        PREAD_CAPACITY16_DATA ReadCapacity16;
        ULONGLONG MaxLba64;

        ReadCapacity16 = (PREAD_CAPACITY16_DATA)Srb->DataBuffer;
        BytesPerLogicalSector = PortExtension->DeviceParams.BytesPerLogicalSector;
        MaxLba64 = (PortExtension->DeviceParams.MaxLba.QuadPart > 0) ?
                    (PortExtension->DeviceParams.MaxLba.QuadPart - 1) : 0;

        if (Srb->DataTransferLength < sizeof(READ_CAPACITY16_DATA))
        {
            AhciDebugPrint("\tREAD_CAPACITY16 buffer too small\n");
            return SRB_STATUS_INVALID_REQUEST;
        }

        AhciZeroMemory((PCHAR)ReadCapacity16, sizeof(READ_CAPACITY16_DATA));

        REVERSE_BYTES_QUAD(&ReadCapacity16->LogicalBlockAddress, &MaxLba64);
        REVERSE_BYTES(&ReadCapacity16->BytesPerBlock, &BytesPerLogicalSector);

        Srb->DataTransferLength = sizeof(READ_CAPACITY16_DATA);
    }
    else
    {
        AhciDebugPrint("\tSCSIOP_READ_CAPACITY16 not supported\n");
        NT_ASSERT(FALSE);
    }

    return SRB_STATUS_SUCCESS;
}// -- DeviceRequestCapacity();

/**
 * @name DeviceRequestComplete
 * @implemented
 *
 * Handle UnHandled Requests
 *
 * @param AdapterExtension
 * @param Srb
 * @param Cdb
 *
 * @return
 * return STOR status for DeviceRequestComplete
 */
UCHAR
DeviceRequestComplete (
    __in PAHCI_ADAPTER_EXTENSION AdapterExtension,
    __in PSCSI_REQUEST_BLOCK Srb,
    __in PCDB Cdb
    )
{
    AhciDebugPrint("DeviceRequestComplete()\n");

    UNREFERENCED_PARAMETER(AdapterExtension);
    UNREFERENCED_PARAMETER(Cdb);

    Srb->ScsiStatus = SCSISTAT_GOOD;

    return SRB_STATUS_SUCCESS;
}// -- DeviceRequestComplete();

/**
 * @name DeviceReportLuns
 * @implemented
 *
 * Handle SCSIOP_REPORT_LUNS OperationCode
 *
 * @param AdapterExtension
 * @param Srb
 * @param Cdb
 *
 * @return
 * return STOR status for DeviceReportLuns
 */
UCHAR
DeviceReportLuns (
    __in PAHCI_ADAPTER_EXTENSION AdapterExtension,
    __in PSCSI_REQUEST_BLOCK Srb,
    __in PCDB Cdb
    )
{
    PLUN_LIST LunList;
    PAHCI_PORT_EXTENSION PortExtension;

    AhciDebugPrint("DeviceReportLuns()\n");

    UNREFERENCED_PARAMETER(Cdb);

    PortExtension = &AdapterExtension->PortExtension[Srb->PathId];

    NT_ASSERT(Srb->DataTransferLength >= sizeof(LUN_LIST));
    NT_ASSERT(Cdb->CDB10.OperationCode == SCSIOP_REPORT_LUNS);

    if (PortExtension->DeviceParams.DeviceType == AHCI_DEVICE_TYPE_ATAPI)
    {
        return AhciATAPICommand(AdapterExtension, Srb, Cdb);
    }

    LunList = (PLUN_LIST)Srb->DataBuffer;

    NT_ASSERT(LunList != NULL);

    AhciZeroMemory((PCHAR)LunList, sizeof(LUN_LIST));

    LunList->LunListLength[3] = 8;

    Srb->ScsiStatus = SCSISTAT_GOOD;
    Srb->DataTransferLength = sizeof(LUN_LIST);

    return SRB_STATUS_SUCCESS;
}// -- DeviceReportLuns();

/**
 * @name DeviceInquiryRequest
 * @implemented
 *
 * Tells wheather given port is implemented or not
 *
 * @param AdapterExtension
 * @param Srb
 * @param Cdb
 *
 * @return
 * return STOR status for DeviceInquiryRequest
 *
 * @remark
 * http://www.seagate.com/staticfiles/support/disc/manuals/Interface%20manuals/100293068c.pdf
 */
UCHAR
DeviceInquiryRequest (
    __in PAHCI_ADAPTER_EXTENSION AdapterExtension,
    __in PSCSI_REQUEST_BLOCK Srb,
    __in PCDB Cdb
    )
{
    PVOID DataBuffer;
    PAHCI_SRB_EXTENSION SrbExtension;
    PAHCI_PORT_EXTENSION PortExtension;
    PVPD_SUPPORTED_PAGES_PAGE VpdOutputBuffer;
    ULONG DataBufferLength;
    SIZE_T serialStartIndex;
    SIZE_T serialLengthTrimmed;
    SIZE_T identifierLengthRequired;
    SIZE_T identifierOriginalLength;
    BOOLEAN serialPageAvailable;
    BOOLEAN identifierPageAvailable;

    AhciDebugPrint("DeviceInquiryRequest()\n");

    NT_ASSERT(Cdb->CDB10.OperationCode == SCSIOP_INQUIRY);
    NT_ASSERT(IsPortValid(AdapterExtension, Srb->PathId));

    SrbExtension = GetSrbExtension(Srb);
    PortExtension = &AdapterExtension->PortExtension[Srb->PathId];

    if (PortExtension->DeviceParams.DeviceType == AHCI_DEVICE_TYPE_ATAPI)
    {
        return AhciATAPICommand(AdapterExtension, Srb, Cdb);
    }

    if (Srb->Lun != 0)
    {
        return SRB_STATUS_SELECTION_TIMEOUT;
    }
    else if (Cdb->CDB6INQUIRY3.EnableVitalProductData == 0)
    {
        // 3.6.1
        // If the EVPD bit is set to zero, the device server shall return the standard INQUIRY data
        AhciDebugPrint("\tEVPD Inquired\n");
        NT_ASSERT(SrbExtension != NULL);

        SrbExtension->AtaFunction = ATA_FUNCTION_ATA_IDENTIFY;
        SrbExtension->Flags |= ATA_FLAGS_DATA_IN;
        SrbExtension->CompletionRoutine = InquiryCompletion;
        SrbExtension->CommandReg = IDE_COMMAND_NOT_VALID;

        // TODO: Should use AhciZeroMemory
        SrbExtension->FeaturesLow = 0;
        SrbExtension->LBA0 = 0;
        SrbExtension->LBA1 = 0;
        SrbExtension->LBA2 = 0;
        SrbExtension->Device = 0xA0;
        SrbExtension->LBA3 = 0;
        SrbExtension->LBA4 = 0;
        SrbExtension->LBA5 = 0;
        SrbExtension->FeaturesHigh = 0;
        SrbExtension->SectorCountLow = 0;
        SrbExtension->SectorCountHigh = 0;

        SrbExtension->Sgl.NumberOfElements = 1;
        SrbExtension->Sgl.List[0].PhysicalAddress.LowPart = PortExtension->IdentifyDeviceDataPhysicalAddress.LowPart;
        SrbExtension->Sgl.List[0].PhysicalAddress.HighPart = PortExtension->IdentifyDeviceDataPhysicalAddress.HighPart;
        SrbExtension->Sgl.List[0].Length = sizeof(IDENTIFY_DEVICE_DATA);

        SrbExtension->pSgl = &SrbExtension->Sgl;
        return SRB_STATUS_PENDING;
    }
    else
    {
        ULONG requiredLength;

        AhciDebugPrint("\tVPD Inquired\n");

        DataBuffer = Srb->DataBuffer;
        DataBufferLength = Srb->DataTransferLength;
        if (DataBuffer == NULL)
        {
            return SRB_STATUS_INVALID_REQUEST;
        }

        AhciZeroMemory(DataBuffer, DataBufferLength);

        serialStartIndex = 0;
        serialLengthTrimmed = AhciGetTrimmedStringSegment(PortExtension->DeviceParams.SerialNumber,
                                                          sizeof(PortExtension->DeviceParams.SerialNumber),
                                                          &serialStartIndex);
        serialPageAvailable = (serialLengthTrimmed != 0);

        identifierLengthRequired = AhciBuildDeviceIdentifierString(PortExtension, NULL, 0);
        identifierOriginalLength = identifierLengthRequired;
        identifierPageAvailable = (identifierLengthRequired != 0);
        if (identifierLengthRequired > 0xFF)
        {
            AhciDebugPrint("\tIdentifier length %lu exceeds maximum, truncating to 255 bytes\n",
                           (ULONG)identifierLengthRequired);
            identifierLengthRequired = 0xFF;
        }

        switch (Cdb->CDB6INQUIRY3.PageCode)
        {
            case VPD_SUPPORTED_PAGES:
                {
                    UCHAR supportedPages[3] = {0};
                    ULONG supportedCount = 0;
                    ULONG index;

                    AhciDebugPrint("\tVPD_SUPPORTED_PAGES\n");

                    supportedPages[supportedCount++] = VPD_SUPPORTED_PAGES;
                    if (serialPageAvailable)
                    {
                        supportedPages[supportedCount++] = VPD_SERIAL_NUMBER;
                    }
                    if (identifierPageAvailable)
                    {
                        supportedPages[supportedCount++] = VPD_DEVICE_IDENTIFIERS;
                    }

                    requiredLength = sizeof(VPD_SUPPORTED_PAGES_PAGE) + supportedCount;
                    if (DataBufferLength < requiredLength)
                    {
                        AhciDebugPrint("\tDataBufferLength: %lu Required: %lu\n",
                                       DataBufferLength,
                                       requiredLength);
                        return SRB_STATUS_INVALID_REQUEST;
                    }

                    VpdOutputBuffer = (PVPD_SUPPORTED_PAGES_PAGE)DataBuffer;
                    VpdOutputBuffer->DeviceType = PortExtension->DeviceParams.AccessType;
                    VpdOutputBuffer->DeviceTypeQualifier = DEVICE_CONNECTED;
                    VpdOutputBuffer->PageCode = VPD_SUPPORTED_PAGES;
                    VpdOutputBuffer->Reserved = 0;
                    VpdOutputBuffer->PageLength = (UCHAR)supportedCount;

                    for (index = 0; index < supportedCount; index++)
                    {
                        VpdOutputBuffer->SupportedPageList[index] = supportedPages[index];
                    }

                    Srb->DataTransferLength = requiredLength;
                    return SRB_STATUS_SUCCESS;
                }

            case VPD_SERIAL_NUMBER:
                {
                    PVPD_SERIAL_NUMBER_PAGE serialPage;
                    SIZE_T copyLength;

                    AhciDebugPrint("\tVPD_SERIAL_NUMBER\n");

                    copyLength = serialLengthTrimmed;
                    if (copyLength > 0xFF)
                    {
                        AhciDebugPrint("\tSerial number length %lu exceeds maximum, truncating\n",
                                       (ULONG)copyLength);
                        copyLength = 0xFF;
                    }

                    requiredLength = sizeof(VPD_SERIAL_NUMBER_PAGE) + (ULONG)copyLength;
                    if (DataBufferLength < requiredLength)
                    {
                        AhciDebugPrint("\tDataBufferLength: %lu Required: %lu\n",
                                       DataBufferLength,
                                       requiredLength);
                        return SRB_STATUS_INVALID_REQUEST;
                    }

                    if (!serialPageAvailable)
                    {
                        AhciDebugPrint("\tSerial number unavailable for port %u\n",
                                       PortExtension->PortNumber);
                    }

                    serialPage = (PVPD_SERIAL_NUMBER_PAGE)DataBuffer;
                    serialPage->DeviceType = PortExtension->DeviceParams.AccessType;
                    serialPage->DeviceTypeQualifier = DEVICE_CONNECTED;
                    serialPage->PageCode = VPD_SERIAL_NUMBER;
                    serialPage->Reserved = 0;
                    serialPage->PageLength = (UCHAR)copyLength;

                    if (copyLength > 0)
                    {
                        StorPortCopyMemory(serialPage->SerialNumber,
                                           PortExtension->DeviceParams.SerialNumber + serialStartIndex,
                                           copyLength);
                    }

                    Srb->DataTransferLength = requiredLength;
                    return SRB_STATUS_SUCCESS;
                }

            case VPD_DEVICE_IDENTIFIERS:
                {
                    PVPD_IDENTIFICATION_PAGE identificationPage;
                    PVPD_IDENTIFICATION_DESCRIPTOR descriptor;
                    SIZE_T copiedLength;

                    AhciDebugPrint("\tVPD_DEVICE_IDENTIFIERS\n");

                    requiredLength = sizeof(VPD_IDENTIFICATION_PAGE) +
                                     sizeof(VPD_IDENTIFICATION_DESCRIPTOR) +
                                     (ULONG)identifierLengthRequired;
                    if (DataBufferLength < requiredLength)
                    {
                        AhciDebugPrint("\tDataBufferLength: %lu Required: %lu\n",
                                       DataBufferLength,
                                       requiredLength);
                        return SRB_STATUS_INVALID_REQUEST;
                    }

                    identificationPage = (PVPD_IDENTIFICATION_PAGE)DataBuffer;
                    descriptor = (PVPD_IDENTIFICATION_DESCRIPTOR)identificationPage->Descriptors;

                    identificationPage->DeviceType = PortExtension->DeviceParams.AccessType;
                    identificationPage->DeviceTypeQualifier = DEVICE_CONNECTED;
                    identificationPage->PageCode = VPD_DEVICE_IDENTIFIERS;
                    identificationPage->Reserved = 0;
                    identificationPage->PageLength = (UCHAR)(sizeof(VPD_IDENTIFICATION_DESCRIPTOR) + identifierLengthRequired);

                    descriptor->CodeSet = VpdCodeSetAscii;
                    descriptor->Reserved = 0;
                    descriptor->IdentifierType = VpdIdentifierTypeVendorId;
                    descriptor->Association = VpdAssocDevice;
                    descriptor->Reserved2 = 0;
                    descriptor->Reserved3 = 0;
                    descriptor->IdentifierLength = (UCHAR)identifierLengthRequired;

                    copiedLength = AhciBuildDeviceIdentifierString(PortExtension,
                                                                   descriptor->Identifier,
                                                                   identifierLengthRequired);
                    if (copiedLength > identifierLengthRequired)
                    {
                        AhciDebugPrint("\tIdentifier truncated from %lu to %lu bytes\n",
                                       (ULONG)copiedLength,
                                       (ULONG)identifierLengthRequired);
                    }
                    else if (identifierOriginalLength > identifierLengthRequired)
                    {
                        AhciDebugPrint("\tIdentifier truncated to %lu bytes\n",
                                       (ULONG)identifierLengthRequired);
                    }

                    Srb->DataTransferLength = requiredLength;
                    return SRB_STATUS_SUCCESS;
                }

            default:
                AhciDebugPrint("\tPageCode: %x\n", Cdb->CDB6INQUIRY3.PageCode);
                return SRB_STATUS_INVALID_REQUEST;
        }
    }
}// -- DeviceInquiryRequest();

/**
 * @name AhciAdapterReset
 * @implemented
 *
 * 10.4.3 HBA Reset
 * If the HBA becomes unusable for multiple ports, and a software reset or port reset does not correct the
 * problem, software may reset the entire HBA by setting GHC.HR to ‘1’. When software sets the GHC.HR
 * bit to ‘1’, the HBA shall perform an internal reset action. The bit shall be cleared to ‘0’ by the HBA when
 * the reset is complete. A software write of ‘0’ to GHC.HR shall have no effect. To perform the HBA reset,
 * software sets GHC.HR to ‘1’ and may poll until this bit is read to be ‘0’, at which point software knows that
 * the HBA reset has completed.
 * If the HBA has not cleared GHC.HR to ‘0’ within 1 second of software setting GHC.HR to ‘1’, the HBA is in
 * a hung or locked state.
 *
 * @param AdapterExtension
 *
 * @return
 * TRUE in case AHCI Controller RESTARTED successfully. i.e GHC.HR == 0
 */
BOOLEAN
AhciAdapterReset (
    __in PAHCI_ADAPTER_EXTENSION AdapterExtension
    )
{
    ULONG ticks;
    AHCI_GHC ghc;
    PAHCI_MEMORY_REGISTERS abar = NULL;

    AhciDebugPrint("AhciAdapterReset()\n");

    abar = AdapterExtension->ABAR_Address;
    if (abar == NULL) // basic sanity
    {
        return FALSE;
    }

    // HR -- Very first bit (lowest significant)
    ghc.Status = 0;
    ghc.HR = 1;
    StorPortWriteRegisterUlong(AdapterExtension, &abar->GHC, ghc.Status);

    for (ticks = 0; ticks < 50; ++ticks)
    {
        ghc.Status = StorPortReadRegisterUlong(AdapterExtension, &abar->GHC);
        if (ghc.HR == 0)
        {
            break;
        }
        StorPortStallExecution(20000);
    }

    if (ticks == 50)// 1 second
    {
        AhciDebugPrint("\tDevice Timeout\n");
        return FALSE;
    }

    return TRUE;
}// -- AhciAdapterReset();

static
BOOLEAN
AhciStopPort (
    __in PAHCI_PORT_EXTENSION PortExtension,
    __in ULONG TimeoutUsec
    )
{
    AHCI_PORT_CMD cmd;
    PAHCI_ADAPTER_EXTENSION AdapterExtension;
    ULONG elapsed;
    const ULONG pollInterval = 50; // 50 microseconds
    BOOLEAN result = TRUE;

    if ((PortExtension == NULL) || (PortExtension->Port == NULL))
    {
        AhciDebugPrint("AhciStopPort() invalid port extension\n");
        return FALSE;
    }

    AdapterExtension = PortExtension->AdapterExtension;
    if (AdapterExtension == NULL)
    {
        AhciDebugPrint("AhciStopPort() adapter extension missing\n");
        return FALSE;
    }

    cmd.Status = StorPortReadRegisterUlong(AdapterExtension, &PortExtension->Port->CMD);

    if (cmd.ST != 0)
    {
        cmd.ST = 0;
        StorPortWriteRegisterUlong(AdapterExtension, &PortExtension->Port->CMD, cmd.Status);

        elapsed = 0;
        do
        {
            StorPortStallExecution(pollInterval);
            elapsed += pollInterval;
            cmd.Status = StorPortReadRegisterUlong(AdapterExtension, &PortExtension->Port->CMD);
        }
        while ((cmd.CR != 0) && (elapsed < TimeoutUsec));

        if (cmd.CR != 0)
        {
            AhciDebugPrint("\tFailed to clear CR for port %u\n", PortExtension->PortNumber);
            result = FALSE;
        }
    }

    if (cmd.FRE != 0)
    {
        cmd.FRE = 0;
        StorPortWriteRegisterUlong(AdapterExtension, &PortExtension->Port->CMD, cmd.Status);

        elapsed = 0;
        do
        {
            StorPortStallExecution(pollInterval);
            elapsed += pollInterval;
            cmd.Status = StorPortReadRegisterUlong(AdapterExtension, &PortExtension->Port->CMD);
        }
        while ((cmd.FR != 0) && (elapsed < TimeoutUsec));

        if (cmd.FR != 0)
        {
            AhciDebugPrint("\tFailed to clear FR for port %u\n", PortExtension->PortNumber);
            result = FALSE;
        }
    }

    return result;
}// -- AhciStopPort();

static
VOID
AhciDrainPendingRequests (
    __in PAHCI_PORT_EXTENSION PortExtension,
    __in UCHAR SrbStatus,
    __in UCHAR ScsiStatus,
    __inout PSCSI_REQUEST_BLOCK *CompletionList,
    __in ULONG CompletionListCapacity,
    __inout PULONG CompletionCount
    )
{
    ULONG index;
    PSCSI_REQUEST_BLOCK Srb;

    if ((CompletionList == NULL) || (CompletionCount == NULL))
    {
        AhciDebugPrint("AhciDrainPendingRequests() invalid completion storage\n");
        return;
    }

    // Drain command slots
    for (index = 0; index < MAXIMUM_AHCI_PORT_NCS; index++)
    {
        Srb = PortExtension->Slot[index];
        if (Srb != NULL)
        {
            if (*CompletionCount >= CompletionListCapacity)
            {
                AhciDebugPrint("\tCompletion list overflow while draining slots\n");
                break;
            }

            PortExtension->Slot[index] = NULL;
            PortExtension->QueueSlots &= ~(1 << index);
            PortExtension->CommandIssuedSlots &= ~(1 << index);

            Srb->SrbStatus = SrbStatus;
            Srb->ScsiStatus = ScsiStatus;
            CompletionList[*CompletionCount] = Srb;
            (*CompletionCount)++;
        }
    }

    PortExtension->QueueSlots = 0;
    PortExtension->CommandIssuedSlots = 0;

    // Drain completion queue
    while ((Srb = RemoveQueue(&PortExtension->CompletionQueue)) != NULL)
    {
        if (*CompletionCount >= CompletionListCapacity)
        {
            AhciDebugPrint("\tCompletion list overflow while draining completion queue\n");
            break;
        }

        Srb->SrbStatus = SrbStatus;
        Srb->ScsiStatus = ScsiStatus;
        CompletionList[*CompletionCount] = Srb;
        (*CompletionCount)++;
    }

    // Drain pending SRB queue
    while ((Srb = RemoveQueue(&PortExtension->SrbQueue)) != NULL)
    {
        if (*CompletionCount >= CompletionListCapacity)
        {
            AhciDebugPrint("\tCompletion list overflow while draining pending queue\n");
            break;
        }

        Srb->SrbStatus = SrbStatus;
        Srb->ScsiStatus = ScsiStatus;
        CompletionList[*CompletionCount] = Srb;
        (*CompletionCount)++;
    }
}// -- AhciDrainPendingRequests();

static
BOOLEAN
AhciResetPortAndFailRequests (
    __in PAHCI_ADAPTER_EXTENSION AdapterExtension,
    __in PAHCI_PORT_EXTENSION PortExtension,
    __in UCHAR SrbStatus,
    __in UCHAR ScsiStatus,
    __in BOOLEAN RequestHardReset
    )
{
    STOR_LOCK_HANDLE lockhandle = {0};
    PSCSI_REQUEST_BLOCK completionList[AHCI_MAX_SRBS_TO_DRAIN] = {0};
    ULONG completionCount = 0;
    BOOLEAN stopStatus = TRUE;
    BOOLEAN startStatus = TRUE;
    ULONG completionIndex;

    if ((AdapterExtension == NULL) || (PortExtension == NULL))
    {
        AhciDebugPrint("AhciResetPortAndFailRequests() invalid arguments\n");
        return FALSE;
    }

    AhciDebugPrint("AhciResetPortAndFailRequests() Port=%u\n", PortExtension->PortNumber);

    StorPortAcquireSpinLock(AdapterExtension, InterruptLock, NULL, &lockhandle);

    PortExtension->DeviceParams.IsActive = FALSE;

    if (PortExtension->Port != NULL)
    {
        stopStatus = AhciStopPort(PortExtension, 500000);
    }
    else
    {
        AhciDebugPrint("\tPort register base is NULL for port %u\n", PortExtension->PortNumber);
        stopStatus = FALSE;
    }

    AhciDrainPendingRequests(PortExtension,
                             SrbStatus,
                             ScsiStatus,
                             completionList,
                             RTL_NUMBER_OF(completionList),
                             &completionCount);

    if (PortExtension->Port != NULL)
    {
        StorPortWriteRegisterUlong(AdapterExtension, &PortExtension->Port->SACT, 0);
        StorPortWriteRegisterUlong(AdapterExtension, &PortExtension->Port->CI, 0);
        StorPortWriteRegisterUlong(AdapterExtension, &PortExtension->Port->SERR, (ULONG)~0);
        StorPortWriteRegisterUlong(AdapterExtension, &PortExtension->Port->IS, (ULONG)~0);
    }

    if (AdapterExtension->IS != NULL)
    {
        StorPortWriteRegisterUlong(AdapterExtension,
                                   AdapterExtension->IS,
                                   (1 << PortExtension->PortNumber));
    }

    StorPortReleaseSpinLock(AdapterExtension, &lockhandle);

    for (completionIndex = 0; completionIndex < completionCount; completionIndex++)
    {
        PSCSI_REQUEST_BLOCK pendingSrb = completionList[completionIndex];

        if (pendingSrb == NULL)
        {
            continue;
        }

        AhciQueuePortCompletion(PortExtension, pendingSrb, TRUE);
    }

    StorPortAcquireSpinLock(AdapterExtension, InterruptLock, NULL, &lockhandle);

    if (RequestHardReset)
    {
        startStatus = AhciStartPort(PortExtension);
    }

    PortExtension->DeviceParams.IsActive = (BOOLEAN)startStatus;
    PortExtension->ErrorRecoveryScheduled = 0;

    StorPortReleaseSpinLock(AdapterExtension, &lockhandle);

    if (!stopStatus)
    {
        AhciDebugPrint("\tFailed to stop port %u\n", PortExtension->PortNumber);
    }

    if (!startStatus)
    {
        AhciDebugPrint("\tFailed to restart port %u\n", PortExtension->PortNumber);
    }

    return (stopStatus && startStatus);
}// -- AhciResetPortAndFailRequests();

/**
 * @name AhciZeroMemory
 * @implemented
 *
 * Clear buffer by filling zeros
 *
 * @param Buffer
 * @param BufferSize
 */
FORCEINLINE
VOID
AhciZeroMemory (
    __out PCHAR Buffer,
    __in ULONG BufferSize
    )
{
    ULONG i;
    for (i = 0; i < BufferSize; i++)
    {
        Buffer[i] = 0;
    }

    return;
}// -- AhciZeroMemory();

/**
 * @name IsPortValid
 * @implemented
 *
 * Tells wheather given port is implemented or not
 *
 * @param AdapterExtension
 * @param PathId
 *
 * @return
 * return TRUE if provided port is valid (implemented) or not
 */
FORCEINLINE
BOOLEAN
IsPortValid (
    __in PAHCI_ADAPTER_EXTENSION AdapterExtension,
    __in ULONG pathId
    )
{
    NT_ASSERT(pathId < MAXIMUM_AHCI_PORT_COUNT);

    if (pathId >= AdapterExtension->PortCount)
    {
        return FALSE;
    }

    return AdapterExtension->PortExtension[pathId].DeviceParams.IsActive;
}// -- IsPortValid()

/**
 * @name AddQueue
 * @implemented
 *
 * Add Srb to Queue
 *
 * @param Queue
 * @param Srb
 *
 * @return
 * return TRUE if Srb is successfully added to Queue
 *
 */
FORCEINLINE
BOOLEAN
AddQueue (
    __inout PAHCI_QUEUE Queue,
    __in PVOID Srb
    )
{
    NT_ASSERT(Queue->Head < MAXIMUM_QUEUE_BUFFER_SIZE);
    NT_ASSERT(Queue->Tail < MAXIMUM_QUEUE_BUFFER_SIZE);

    if (Queue->Tail == ((Queue->Head + 1) % MAXIMUM_QUEUE_BUFFER_SIZE))
        return FALSE;

    Queue->Buffer[Queue->Head++] = Srb;
    Queue->Head %= MAXIMUM_QUEUE_BUFFER_SIZE;

    return TRUE;
}// -- AddQueue();

/**
 * @name RemoveQueue
 * @implemented
 *
 * Remove and return Srb from Queue
 *
 * @param Queue
 *
 * @return
 * return Srb
 *
 */
FORCEINLINE
PVOID
RemoveQueue (
    __inout PAHCI_QUEUE Queue
    )
{
    PVOID Srb;

    NT_ASSERT(Queue->Head < MAXIMUM_QUEUE_BUFFER_SIZE);
    NT_ASSERT(Queue->Tail < MAXIMUM_QUEUE_BUFFER_SIZE);

    if (Queue->Head == Queue->Tail)
        return NULL;

    Srb = Queue->Buffer[Queue->Tail++];
    Queue->Tail %= MAXIMUM_QUEUE_BUFFER_SIZE;

    return Srb;
}// -- RemoveQueue();

/**
 * @name GetSrbExtension
 * @implemented
 *
 * GetSrbExtension from Srb make sure It is properly aligned
 *
 * @param Srb
 *
 * @return
 * return SrbExtension
 *
 */
FORCEINLINE
PAHCI_SRB_EXTENSION
GetSrbExtension (
    __in PSCSI_REQUEST_BLOCK Srb
    )
{
    ULONG Offset;
    ULONG_PTR SrbExtension;

    SrbExtension = (ULONG_PTR)Srb->SrbExtension;
    Offset = SrbExtension % 128;

    // CommandTable should be 128 byte aligned
    if (Offset != 0)
        Offset = 128 - Offset;

    return (PAHCI_SRB_EXTENSION)(SrbExtension + Offset);
}// -- PAHCI_SRB_EXTENSION();

/**
 * @name AhciGetLba
 * @implemented
 *
 * Find the logical address of demand block from Cdb
 *
 * @param Srb
 *
 * @return
 * return Logical Address of the block
 *
 */
FORCEINLINE
ULONG64
AhciGetLba (
    __in PCDB Cdb,
    __in ULONG CdbLength
    )
{
    ULONG64 lba = 0;

    NT_ASSERT(Cdb != NULL);
    NT_ASSERT(CdbLength != 0);

    if (CdbLength == 0x10)
    {
        REVERSE_BYTES_QUAD(&lba, Cdb->CDB16.LogicalBlock);
    }
    else
    {
        lba |= Cdb->CDB10.LogicalBlockByte3 << 0;
        lba |= Cdb->CDB10.LogicalBlockByte2 << 8;
        lba |= Cdb->CDB10.LogicalBlockByte1 << 16;
        lba |= Cdb->CDB10.LogicalBlockByte0 << 24;
    }

    return lba;
}// -- AhciGetLba();

static
SIZE_T
AhciGetTrimmedStringSegment (
    __in_ecount(BufferLength) const UCHAR *Buffer,
    __in SIZE_T BufferLength,
    __out_opt SIZE_T *StartIndex
    )
{
    SIZE_T start;
    SIZE_T index;

    if (StartIndex != NULL)
    {
        *StartIndex = 0;
    }

    if ((Buffer == NULL) || (BufferLength == 0))
    {
        return 0;
    }

    start = 0;
    while ((start < BufferLength) && (Buffer[start] == ' ' || Buffer[start] == '\0'))
    {
        start++;
    }

    if (start >= BufferLength)
    {
        if (StartIndex != NULL)
        {
            *StartIndex = BufferLength;
        }
        return 0;
    }

    index = start;
    while ((index < BufferLength) && (Buffer[index] != '\0'))
    {
        index++;
    }

    if (index > BufferLength)
    {
        index = BufferLength;
    }

    while ((index > start) && (Buffer[index - 1] == ' ' || Buffer[index - 1] == '\0'))
    {
        index--;
    }

    if (StartIndex != NULL)
    {
        *StartIndex = start;
    }

    if (index <= start)
    {
        return 0;
    }

    return index - start;
}// -- AhciGetTrimmedStringSegment();

static
SIZE_T
AhciAppendIdentifierComponent (
    __in_ecount(SourceLength) const UCHAR *Source,
    __in SIZE_T SourceLength,
    __out_bcount_opt(BufferLength) UCHAR *Buffer,
    __in SIZE_T BufferLength,
    __in SIZE_T CurrentLength
    )
{
    SIZE_T startIndex;
    SIZE_T componentLength;
    SIZE_T requiredLength;

    componentLength = AhciGetTrimmedStringSegment(Source, SourceLength, &startIndex);
    if (componentLength == 0)
    {
        return CurrentLength;
    }

    requiredLength = CurrentLength;

    if (requiredLength != 0)
    {
        if ((Buffer != NULL) && (requiredLength < BufferLength))
        {
            Buffer[requiredLength] = ' ';
        }
        requiredLength++;
    }

    if (Buffer != NULL)
    {
        SIZE_T available;
        SIZE_T copyLength;

        available = (BufferLength > requiredLength) ? (BufferLength - requiredLength) : 0;
        copyLength = (componentLength < available) ? componentLength : available;
        if (copyLength > 0)
        {
            StorPortCopyMemory(Buffer + requiredLength,
                               Source + startIndex,
                               copyLength);
        }
    }

    requiredLength += componentLength;
    return requiredLength;
}// -- AhciAppendIdentifierComponent();

static
SIZE_T
AhciBuildDeviceIdentifierString (
    __in PAHCI_PORT_EXTENSION PortExtension,
    __out_bcount_opt(BufferLength) UCHAR *Buffer,
    __in SIZE_T BufferLength
    )
{
    SIZE_T totalLength;

    totalLength = 0;
    totalLength = AhciAppendIdentifierComponent(PortExtension->DeviceParams.VendorId,
                                                sizeof(PortExtension->DeviceParams.VendorId),
                                                Buffer,
                                                BufferLength,
                                                totalLength);

    totalLength = AhciAppendIdentifierComponent(PortExtension->DeviceParams.RevisionID,
                                                sizeof(PortExtension->DeviceParams.RevisionID),
                                                Buffer,
                                                BufferLength,
                                                totalLength);

    totalLength = AhciAppendIdentifierComponent(PortExtension->DeviceParams.SerialNumber,
                                                sizeof(PortExtension->DeviceParams.SerialNumber),
                                                Buffer,
                                                BufferLength,
                                                totalLength);

    if (totalLength == 0)
    {
        static const CHAR fallbackIdentifier[] = "STORAHCI";
        SIZE_T fallbackLength = sizeof(fallbackIdentifier) - 1;

        if ((Buffer != NULL) && (BufferLength != 0))
        {
            SIZE_T copyLength = (fallbackLength < BufferLength) ? fallbackLength : BufferLength;
            StorPortCopyMemory(Buffer, fallbackIdentifier, copyLength);
        }

        return fallbackLength;
    }

    return totalLength;
}// -- AhciBuildDeviceIdentifierString();
