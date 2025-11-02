/*
 * PROJECT:     PCI IDE bus driver extension
 * LICENSE:     See COPYING in the top level directory
 * PURPOSE:     IRP_MJ_PNP operations for PDOs
 * COPYRIGHT:   Copyright 2005 Hervé Poussineau <hpoussin@reactos.org>
 *              Copyright 2023 Dmitry Borisov <di.sean@protonmail.com>
 */

#include "pciidex.h"

#define NDEBUG
#include <debug.h>

static const WCHAR PciIdeChannelHardwareId[] = L"PCIIDE\\IDEChannel";
static const WCHAR PciIdeGenericChannelId[] = L"PCIIDE\\GenericChannel";
static const WCHAR GenIdeChannelHardwareId[] = L"GenIDE\\IDEChannel";
static const WCHAR PnpIdeChannelId[] = L"*PNP0600";
static const WCHAR PrimaryChannelDescriptor[] = L"Primary_IDE_Channel";
static const WCHAR SecondaryChannelDescriptor[] = L"Secondary_IDE_Channel";

static
CODE_SEG("PAGE")
NTSTATUS
PciIdeXPdoStartDevice(
    _Inout_ PPDO_DEVICE_EXTENSION PdoExtension,
    _Inout_ PIO_STACK_LOCATION IoStack)
{
    PUCHAR IoBase;
    PCM_RESOURCE_LIST ResourceList;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor;
    ULONG i;

    PAGED_CODE();

    DbgPrintEx(DPFLTR_DEFAULT_ID,
               DPFLTR_ERROR_LEVEL,
               "[PCIIDEX] Channel %lu PdoStartDevice\n",
               PdoExtension->Channel);

    ResourceList = IoStack->Parameters.StartDevice.AllocatedResourcesTranslated;
    if (!ResourceList ||
        ResourceList->Count == 0 ||
        ResourceList->List[0].PartialResourceList.Count == 0)
    {
        DbgPrintEx(DPFLTR_DEFAULT_ID,
                   DPFLTR_ERROR_LEVEL,
                   "[PCIIDEX] Channel %lu missing translated resources\n",
                   PdoExtension->Channel);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    {
        PCM_RESOURCE_LIST RawResources = IoStack->Parameters.StartDevice.AllocatedResources;

        if (!RawResources ||
            RawResources->Count == 0 ||
            RawResources->List[0].PartialResourceList.Count == 0)
        {
            DbgPrintEx(DPFLTR_DEFAULT_ID,
                       DPFLTR_ERROR_LEVEL,
                       "[PCIIDEX] Channel %lu missing raw resources\n",
                       PdoExtension->Channel);
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        }

        DbgPrintEx(DPFLTR_DEFAULT_ID,
                   DPFLTR_ERROR_LEVEL,
                   "[PCIIDEX] Channel %lu raw resource count: %lu\n",
                   PdoExtension->Channel,
                   RawResources->List[0].PartialResourceList.Count);

        Descriptor = RawResources->List[0].PartialResourceList.PartialDescriptors;
        for (i = 0; i < RawResources->List[0].PartialResourceList.Count; ++i, ++Descriptor)
        {
            if (Descriptor->Type == CmResourceTypePort)
            {
                DbgPrintEx(DPFLTR_DEFAULT_ID,
                           DPFLTR_ERROR_LEVEL,
                           "[PCIIDEX] Channel %lu raw port: Start=0x%llx Length=%lu\n",
                           PdoExtension->Channel,
                           Descriptor->u.Port.Start.QuadPart,
                           Descriptor->u.Port.Length);
            }
            else if (Descriptor->Type == CmResourceTypeInterrupt)
            {
                DbgPrintEx(DPFLTR_DEFAULT_ID,
                           DPFLTR_ERROR_LEVEL,
                           "[PCIIDEX] Channel %lu raw interrupt: Vector=%lu Flags=0x%lx\n",
                           PdoExtension->Channel,
                           Descriptor->u.Interrupt.Vector,
                           Descriptor->Flags);
            }
        }
    }

    {
        PCM_PARTIAL_RESOURCE_LIST Partial = &ResourceList->List[0].PartialResourceList;

        DbgPrintEx(DPFLTR_DEFAULT_ID,
                   DPFLTR_ERROR_LEVEL,
                   "[PCIIDEX] Channel %lu translated resource count: %lu\n",
                   PdoExtension->Channel,
                   Partial->Count);
    }

    if (ResourceList && ResourceList->Count)
    {
        PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor;
        ULONG Count = ResourceList->List[0].PartialResourceList.Count;
        BOOLEAN HasCommandPort = FALSE;
        BOOLEAN HasControlPort = FALSE;
        BOOLEAN HasInterrupt = FALSE;

        Descriptor = ResourceList->List[0].PartialResourceList.PartialDescriptors;
        for (i = 0; i < Count; ++i, ++Descriptor)
        {
            if (Descriptor->Type == CmResourceTypePort)
            {
                ULONG length = Descriptor->u.Port.Length;

                DbgPrintEx(DPFLTR_DEFAULT_ID,
                           DPFLTR_ERROR_LEVEL,
                           "[PCIIDEX] Channel %lu port resource: Start=0x%llx Length=%lu\n",
                           PdoExtension->Channel,
                           Descriptor->u.Port.Start.QuadPart,
                           length);

                if (!HasCommandPort && length >= PCIIDE_LEGACY_COMMAND_IO_RANGE_LENGTH)
                {
                    HasCommandPort = TRUE;
                }

                if (!HasControlPort && length >= PCIIDE_LEGACY_CONTROL_IO_RANGE_LENGTH)
                {
                    HasControlPort = TRUE;
                }
            }
            else if (Descriptor->Type == CmResourceTypeMemory)
            {
                ULONG length = Descriptor->u.Memory.Length;

                DbgPrintEx(DPFLTR_DEFAULT_ID,
                           DPFLTR_ERROR_LEVEL,
                           "[PCIIDEX] Channel %lu memory resource: Start=0x%llx Length=%lu\n",
                           PdoExtension->Channel,
                           Descriptor->u.Memory.Start.QuadPart,
                           length);

                if (!HasCommandPort && length >= PCIIDE_LEGACY_COMMAND_IO_RANGE_LENGTH)
                {
                    HasCommandPort = TRUE;
                }

                if (!HasControlPort && length >= PCIIDE_LEGACY_CONTROL_IO_RANGE_LENGTH)
                {
                    HasControlPort = TRUE;
                }
            }
            else if (Descriptor->Type == CmResourceTypeInterrupt)
            {
                DbgPrintEx(DPFLTR_DEFAULT_ID,
                           DPFLTR_ERROR_LEVEL,
                           "[PCIIDEX] Channel %lu interrupt resource: Vector=%lu Level=%lu\n",
                           PdoExtension->Channel,
                           Descriptor->u.Interrupt.Vector,
                           Descriptor->u.Interrupt.Level);
                HasInterrupt = TRUE;
            }
        }

        if (!HasCommandPort || !HasControlPort)
        {
            DbgPrintEx(DPFLTR_DEFAULT_ID,
                       DPFLTR_ERROR_LEVEL,
                       "[PCIIDEX] Channel %lu legacy resource check: command=%d control=%d interrupt=%d\n",
                       PdoExtension->Channel,
                       HasCommandPort,
                       HasControlPort,
                       HasInterrupt);
        }

        if (!HasInterrupt)
        {
            DbgPrintEx(DPFLTR_DEFAULT_ID,
                       DPFLTR_ERROR_LEVEL,
                       "[PCIIDEX] Channel %lu continuing without legacy interrupt resource\n",
                       PdoExtension->Channel);
        }
    }

    IoBase = PdoExtension->ParentController->BusMasterPortBase;
    if (!IS_PRIMARY_CHANNEL(PdoExtension))
    {
        IoBase += BM_SECONDARY_CHANNEL_OFFSET;
    }
    DPRINT("Bus Master Base %p\n", IoBase);

    PdoExtension->IoBase = IoBase;

    DbgPrintEx(DPFLTR_DEFAULT_ID,
               DPFLTR_ERROR_LEVEL,
               "[PCIIDEX] Channel %lu start completed (IoBase=%p)\n",
               PdoExtension->Channel,
               PdoExtension->IoBase);

    return STATUS_SUCCESS;
}

static
CODE_SEG("PAGE")
NTSTATUS
PciIdeXPdoStopDevice(
    _In_ PPDO_DEVICE_EXTENSION PdoExtension)
{
    PAGED_CODE();

    PdoExtension->IoBase = NULL;

    return STATUS_SUCCESS;
}

static
CODE_SEG("PAGE")
NTSTATUS
PciIdeXPdoRemoveDevice(
    _In_ PPDO_DEVICE_EXTENSION PdoExtension,
    _In_ BOOLEAN FinalRemove)
{
    PFDO_DEVICE_EXTENSION FdoExtension = PdoExtension->ParentController;
    ULONG i;
    
    PAGED_CODE();

    if (FinalRemove && PdoExtension->ReportedMissing)
    {
        ExAcquireFastMutex(&FdoExtension->DeviceSyncMutex);

        for (i = 0; i < MAX_IDE_CHANNEL; ++i)
        {
            if (FdoExtension->Channels[i] == PdoExtension)
            {
                FdoExtension->Channels[i] = NULL;
                break;
            }
        }

        ExReleaseFastMutex(&FdoExtension->DeviceSyncMutex);

        IoDeleteDevice(PdoExtension->Common.Self);
    }

    return STATUS_SUCCESS;
}

static
CODE_SEG("PAGE")
NTSTATUS
PciIdeXPdoQueryStopRemoveDevice(
    _In_ PPDO_DEVICE_EXTENSION PdoExtension)
{
    PAGED_CODE();

    if (PdoExtension->Common.PageFiles ||
        PdoExtension->Common.HibernateFiles ||
        PdoExtension->Common.DumpFiles)
    {
        return STATUS_DEVICE_BUSY;
    }

    return STATUS_SUCCESS;
}

static
CODE_SEG("PAGE")
NTSTATUS
PciIdeXPdoQueryTargetDeviceRelations(
    _In_ PPDO_DEVICE_EXTENSION PdoExtension,
    _In_ PIRP Irp)
{
    PDEVICE_RELATIONS DeviceRelations;

    PAGED_CODE();

    DeviceRelations = ExAllocatePoolWithTag(PagedPool,
                                            sizeof(DEVICE_RELATIONS),
                                            TAG_PCIIDEX);
    if (!DeviceRelations)
        return STATUS_INSUFFICIENT_RESOURCES;

    DeviceRelations->Count = 1;
    DeviceRelations->Objects[0] = PdoExtension->Common.Self;
    ObReferenceObject(PdoExtension->Common.Self);

    Irp->IoStatus.Information = (ULONG_PTR)DeviceRelations;
    return STATUS_SUCCESS;
}

static IO_COMPLETION_ROUTINE PciIdeXOnRepeaterCompletion;

static
NTSTATUS
NTAPI
PciIdeXOnRepeaterCompletion(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_reads_opt_(_Inexpressible_("varies")) PVOID Context)
{
    UNREFERENCED_PARAMETER(DeviceObject);

    if (Irp->PendingReturned)
        KeSetEvent(Context, IO_NO_INCREMENT, FALSE);

    return STATUS_MORE_PROCESSING_REQUIRED;
}

static
CODE_SEG("PAGE")
NTSTATUS
PciIdeXPdoRepeatRequest(
    _In_ PPDO_DEVICE_EXTENSION PdoExtension,
    _In_ PIRP Irp,
    _In_opt_ PDEVICE_CAPABILITIES DeviceCapabilities)
{
    PDEVICE_OBJECT Fdo, TopDeviceObject;
    PIO_STACK_LOCATION IoStack, SubStack;
    PIRP SubIrp;
    KEVENT Event;
    NTSTATUS Status;

    PAGED_CODE();

    Fdo = PdoExtension->ParentController->Common.Self;
    TopDeviceObject = IoGetAttachedDeviceReference(Fdo);

    SubIrp = IoAllocateIrp(TopDeviceObject->StackSize, FALSE);
    if (!SubIrp)
    {
        ObDereferenceObject(TopDeviceObject);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    KeInitializeEvent(&Event, NotificationEvent, FALSE);

    IoStack = IoGetCurrentIrpStackLocation(Irp);
    SubStack = IoGetNextIrpStackLocation(SubIrp);
    RtlCopyMemory(SubStack, IoStack, sizeof(IO_STACK_LOCATION));

    if (DeviceCapabilities)
        SubStack->Parameters.DeviceCapabilities.Capabilities = DeviceCapabilities;

    IoSetCompletionRoutine(SubIrp,
                           PciIdeXOnRepeaterCompletion,
                           &Event,
                           TRUE,
                           TRUE,
                           TRUE);

    SubIrp->IoStatus.Status = STATUS_NOT_SUPPORTED;

    Status = IoCallDriver(TopDeviceObject, SubIrp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
    }

    ObDereferenceObject(TopDeviceObject);

    Status = SubIrp->IoStatus.Status;
    IoFreeIrp(SubIrp);

    return Status;
}

static
CODE_SEG("PAGE")
NTSTATUS
PciIdeXPdoQueryCapabilities(
    _In_ PPDO_DEVICE_EXTENSION PdoExtension,
    _In_ PIRP Irp)
{
    DEVICE_CAPABILITIES ParentCapabilities;
    PDEVICE_CAPABILITIES DeviceCapabilities;
    PIO_STACK_LOCATION IoStack;
    NTSTATUS Status;

    PAGED_CODE();

    /* Get the capabilities of the parent device */
    RtlZeroMemory(&ParentCapabilities, sizeof(ParentCapabilities));
    ParentCapabilities.Size = sizeof(ParentCapabilities);
    ParentCapabilities.Version = 1;
    ParentCapabilities.Address = MAXULONG;
    ParentCapabilities.UINumber = MAXULONG;
    Status = PciIdeXPdoRepeatRequest(PdoExtension, Irp, &ParentCapabilities);
    if (!NT_SUCCESS(Status))
        return Status;

    IoStack = IoGetCurrentIrpStackLocation(Irp);
    DeviceCapabilities = IoStack->Parameters.DeviceCapabilities.Capabilities;
    *DeviceCapabilities = ParentCapabilities;

    /* Override some fields */
    DeviceCapabilities->UniqueID = FALSE;
    DeviceCapabilities->Address = PdoExtension->Channel;
    DeviceCapabilities->Removable = FALSE;
    DeviceCapabilities->SurpriseRemovalOK = TRUE;
    DeviceCapabilities->SilentInstall = TRUE;
    DeviceCapabilities->RawDeviceOK = TRUE;

    return STATUS_SUCCESS;
}

static
CODE_SEG("PAGE")
NTSTATUS
PciIdeXPdoQueryPnpDeviceState(
    _In_ PPDO_DEVICE_EXTENSION PdoExtension,
    _In_ PIRP Irp)
{
    PAGED_CODE();

    if (PdoExtension->Common.PageFiles ||
        PdoExtension->Common.HibernateFiles ||
        PdoExtension->Common.DumpFiles)
    {
        Irp->IoStatus.Information |= PNP_DEVICE_NOT_DISABLEABLE;
    }

    return STATUS_SUCCESS;
}

static
CODE_SEG("PAGE")
NTSTATUS
PciIdeXPdoQueryResources(
    _In_ PPDO_DEVICE_EXTENSION PdoExtension,
    _In_ PIRP Irp)
{
    PAGED_CODE();

    UNREFERENCED_PARAMETER(PdoExtension);
    UNREFERENCED_PARAMETER(Irp);

    /* The arbiter will synthesize raw resources from our requirements. */
    Irp->IoStatus.Information = 0;
    return STATUS_SUCCESS;
}

static
CODE_SEG("PAGE")
NTSTATUS
PciIdeXPdoQueryResourceRequirements(
    _In_ PPDO_DEVICE_EXTENSION PdoExtension,
    _In_ PIRP Irp)
{
    PFDO_DEVICE_EXTENSION FdoExtension;
    PIO_RESOURCE_REQUIREMENTS_LIST RequirementsList;
    PIO_RESOURCE_DESCRIPTOR Descriptor;
    IDE_CHANNEL_STATE ChannelState;
    ULONG CommandPortBase, ControlPortBase, InterruptVector;
    ULONG DescriptorCount;
    ULONG ListSize;

    PAGED_CODE();

    FdoExtension = PdoExtension->ParentController;
    if (FdoExtension->InNativeMode)
        return Irp->IoStatus.Status;

    ChannelState = PciIdeXChannelState(FdoExtension, PdoExtension->Channel);
    if (ChannelState == ChannelDisabled)
        return Irp->IoStatus.Status;

    DescriptorCount = PCIIDE_LEGACY_RESOURCE_COUNT;

    ListSize = sizeof(IO_RESOURCE_REQUIREMENTS_LIST) +
               sizeof(IO_RESOURCE_DESCRIPTOR) * (DescriptorCount - 1);
    RequirementsList = ExAllocatePoolZero(PagedPool, ListSize, TAG_PCIIDEX);
    if (!RequirementsList)
        return STATUS_INSUFFICIENT_RESOURCES;

    /* Legacy mode resources */
    RequirementsList->InterfaceType = Isa;
    RequirementsList->BusNumber = 0;
    RequirementsList->ListSize = ListSize;
    RequirementsList->AlternativeLists = 1;
    RequirementsList->List[0].Version = 1;
    RequirementsList->List[0].Revision = 1;
    RequirementsList->List[0].Count = DescriptorCount;

    if (IS_PRIMARY_CHANNEL(PdoExtension))
    {
        CommandPortBase = PCIIDE_LEGACY_PRIMARY_COMMAND_BASE;
        ControlPortBase = PCIIDE_LEGACY_PRIMARY_CONTROL_BASE;
        InterruptVector = PCIIDE_LEGACY_PRIMARY_IRQ;
    }
    else
    {
        CommandPortBase = PCIIDE_LEGACY_SECONDARY_COMMAND_BASE;
        ControlPortBase = PCIIDE_LEGACY_SECONDARY_CONTROL_BASE;
        InterruptVector = PCIIDE_LEGACY_SECONDARY_IRQ;
    }

    Descriptor = &RequirementsList->List[0].Descriptors[0];

    /* Command port base */
    Descriptor->Type = CmResourceTypePort;
    Descriptor->ShareDisposition = CmResourceShareDeviceExclusive;
    Descriptor->Flags = CM_RESOURCE_PORT_IO | CM_RESOURCE_PORT_16_BIT_DECODE;
    Descriptor->u.Port.Length = PCIIDE_LEGACY_COMMAND_IO_RANGE_LENGTH;
    Descriptor->u.Port.Alignment = 1;
    Descriptor->u.Port.MinimumAddress.QuadPart = CommandPortBase;
    Descriptor->u.Port.MaximumAddress.QuadPart = CommandPortBase +
                                                 PCIIDE_LEGACY_COMMAND_IO_RANGE_LENGTH - 1;
    ++Descriptor;

    /* Control port base */
    Descriptor->Type = CmResourceTypePort;
    Descriptor->ShareDisposition = CmResourceShareDeviceExclusive;
    Descriptor->Flags = CM_RESOURCE_PORT_IO | CM_RESOURCE_PORT_16_BIT_DECODE;
    Descriptor->u.Port.Length = PCIIDE_LEGACY_CONTROL_IO_RANGE_LENGTH;
    Descriptor->u.Port.Alignment = 1;
    Descriptor->u.Port.MinimumAddress.QuadPart = ControlPortBase;
    Descriptor->u.Port.MaximumAddress.QuadPart = ControlPortBase +
                                                 PCIIDE_LEGACY_CONTROL_IO_RANGE_LENGTH - 1;
    ++Descriptor;

    /* Interrupt */
    Descriptor->Type = CmResourceTypeInterrupt;
    Descriptor->ShareDisposition = CmResourceShareShared;
    Descriptor->Flags = CM_RESOURCE_INTERRUPT_LATCHED;
    Descriptor->u.Interrupt.MinimumVector = InterruptVector;
    Descriptor->u.Interrupt.MaximumVector = InterruptVector;
    ++Descriptor;

    Irp->IoStatus.Information = (ULONG_PTR)RequirementsList;
    return STATUS_SUCCESS;
}

static
CODE_SEG("PAGE")
NTSTATUS
PciIdeXPdoQueryId(
    _In_ PPDO_DEVICE_EXTENSION PdoExtension,
    _In_ PIRP Irp)
{
    PIO_STACK_LOCATION IoStack;
    NTSTATUS Status;
    PWCHAR Buffer;
    size_t CharCount;

    PAGED_CODE();

    IoStack = IoGetCurrentIrpStackLocation(Irp);
    switch (IoStack->Parameters.QueryId.IdType)
    {
      case BusQueryDeviceID:
      {
          static const WCHAR PciIdeDeviceId[] = L"PCIIDE\\IDEChannel";

          Buffer = ExAllocatePoolWithTag(PagedPool, sizeof(PciIdeDeviceId), TAG_PCIIDEX);
          if (!Buffer)
              return STATUS_INSUFFICIENT_RESOURCES;

          RtlCopyMemory(Buffer, PciIdeDeviceId, sizeof(PciIdeDeviceId));

          DPRINT("Device ID: '%S'\n", Buffer);
          break;
      }

      case BusQueryHardwareIDs:
      {
          PCWSTR IdTable[5];
          size_t TotalChars = 1; /* final multi-sz terminator */
          ULONG Index;
          PWCHAR Current;

          IdTable[0] = PciIdeChannelHardwareId;
          IdTable[1] = IS_PRIMARY_CHANNEL(PdoExtension) ?
                       PrimaryChannelDescriptor : SecondaryChannelDescriptor;
          IdTable[2] = PciIdeGenericChannelId;
          IdTable[3] = GenIdeChannelHardwareId;
          IdTable[4] = PnpIdeChannelId;

          for (Index = 0; Index < ARRAYSIZE(IdTable); ++Index)
          {
              TotalChars += wcslen(IdTable[Index]) + 1;
          }

          Buffer = ExAllocatePoolWithTag(PagedPool,
                                         TotalChars * sizeof(WCHAR),
                                         TAG_PCIIDEX);
          if (!Buffer)
              return STATUS_INSUFFICIENT_RESOURCES;

          Current = Buffer;
          for (Index = 0; Index < ARRAYSIZE(IdTable); ++Index)
          {
              size_t Length = wcslen(IdTable[Index]);
              RtlCopyMemory(Current, IdTable[Index], (Length + 1) * sizeof(WCHAR));
              Current += Length + 1;
          }
          *Current = UNICODE_NULL;

          Current = Buffer;
          while (*Current)
          {
              DPRINT("  HardwareID: '%S'\n", Current);
              Current += wcslen(Current) + 1;
          }

          break;
      }

      case BusQueryCompatibleIDs:
      {
          PCWSTR IdTable[] =
          {
              PciIdeGenericChannelId,
              GenIdeChannelHardwareId,
              PnpIdeChannelId
          };
          size_t TotalChars = 1;
          PWCHAR Current;
          ULONG Index;

          for (Index = 0; Index < ARRAYSIZE(IdTable); ++Index)
          {
              TotalChars += wcslen(IdTable[Index]) + 1;
          }

          Buffer = ExAllocatePoolWithTag(PagedPool,
                                         TotalChars * sizeof(WCHAR),
                                         TAG_PCIIDEX);
          if (!Buffer)
              return STATUS_INSUFFICIENT_RESOURCES;

          Current = Buffer;
          for (Index = 0; Index < ARRAYSIZE(IdTable); ++Index)
          {
              size_t Length = wcslen(IdTable[Index]);
              RtlCopyMemory(Current, IdTable[Index], (Length + 1) * sizeof(WCHAR));
              Current += Length + 1;
          }
          *Current = UNICODE_NULL;

          Current = Buffer;
          while (*Current)
          {
              DPRINT("  CompatibleID: '%S'\n", Current);
              Current += wcslen(Current) + 1;
          }

          break;
      }

      case BusQueryInstanceID:
      {
          CharCount = sizeof("0");

          Buffer = ExAllocatePoolWithTag(PagedPool,
                                         CharCount * sizeof(WCHAR),
                                         TAG_PCIIDEX);
          if (!Buffer)
              return STATUS_INSUFFICIENT_RESOURCES;

          Status = RtlStringCchPrintfExW(Buffer,
                                         CharCount,
                                         NULL,
                                         NULL,
                                         0,
                                         L"%lu",
                                         PdoExtension->Channel);
          ASSERT(NT_SUCCESS(Status));

          DPRINT("Instance ID: '%S'\n", Buffer);
          break;
      }

      default:
          return Irp->IoStatus.Status;
    }

    Irp->IoStatus.Information = (ULONG_PTR)Buffer;
    return STATUS_SUCCESS;
}

static
CODE_SEG("PAGE")
NTSTATUS
PciIdeXPdoQueryDeviceText(
    _In_ PPDO_DEVICE_EXTENSION PdoExtension,
    _In_ PIRP Irp)
{
    PIO_STACK_LOCATION IoStack;
    PWCHAR Buffer;
    ULONG Size;

    PAGED_CODE();

    IoStack = IoGetCurrentIrpStackLocation(Irp);
    switch (IoStack->Parameters.QueryDeviceText.DeviceTextType)
    {
        case DeviceTextLocationInformation:
        {
            static const WCHAR PrimaryChannelText[] = L"Primary channel";
            static const WCHAR SecondaryChannelText[] = L"Secondary channel";

            if (IS_PRIMARY_CHANNEL(PdoExtension))
                Size = sizeof(PrimaryChannelText);
            else
                Size = sizeof(SecondaryChannelText);

            Buffer = ExAllocatePoolWithTag(PagedPool, Size, TAG_PCIIDEX);
            if (!Buffer)
                return STATUS_INSUFFICIENT_RESOURCES;

            RtlCopyMemory(Buffer,
                          IS_PRIMARY_CHANNEL(PdoExtension) ?
                          PrimaryChannelText : SecondaryChannelText,
                          Size);

            DPRINT("Device ID: '%S'\n", Buffer);
            break;
        }

        default:
            return Irp->IoStatus.Status;
    }

    Irp->IoStatus.Information = (ULONG_PTR)Buffer;
    return STATUS_SUCCESS;
}

static
CODE_SEG("PAGE")
NTSTATUS
PciIdeXPdoQueryDeviceUsageNotification(
    _In_ PPDO_DEVICE_EXTENSION PdoExtension,
    _In_ PIRP Irp)
{
    PIO_STACK_LOCATION IoStack;
    NTSTATUS Status;
    volatile LONG* Counter;

    PAGED_CODE();

    Status = PciIdeXPdoRepeatRequest(PdoExtension, Irp, NULL);
    if (!NT_SUCCESS(Status))
        return Status;

    IoStack = IoGetCurrentIrpStackLocation(Irp);
    switch (IoStack->Parameters.UsageNotification.Type)
    {
        case DeviceUsageTypePaging:
            Counter = &PdoExtension->Common.PageFiles;
            break;

        case DeviceUsageTypeHibernation:
            Counter = &PdoExtension->Common.HibernateFiles;
            break;

        case DeviceUsageTypeDumpFile:
            Counter = &PdoExtension->Common.DumpFiles;
            break;

        default:
            return Status;
    }

    IoAdjustPagingPathCount(Counter, IoStack->Parameters.UsageNotification.InPath);
    IoInvalidateDeviceState(PdoExtension->Common.Self);

    return STATUS_SUCCESS;
}

static
CODE_SEG("PAGE")
NTSTATUS
PciIdeXPdoDispatchPnp(
    _In_ PPDO_DEVICE_EXTENSION PdoExtension,
    _Inout_ PIRP Irp)
{
    NTSTATUS Status;
    PIO_STACK_LOCATION IoStack;

    PAGED_CODE();

    IoStack = IoGetCurrentIrpStackLocation(Irp);
    switch (IoStack->MinorFunction)
    {
        case IRP_MN_START_DEVICE:
            Status = PciIdeXPdoStartDevice(PdoExtension, IoStack);
            break;

        case IRP_MN_STOP_DEVICE:
            Status = PciIdeXPdoStopDevice(PdoExtension);
            break;

        case IRP_MN_QUERY_STOP_DEVICE:
        case IRP_MN_QUERY_REMOVE_DEVICE:
            Status = PciIdeXPdoQueryStopRemoveDevice(PdoExtension);
            break;

        case IRP_MN_CANCEL_REMOVE_DEVICE:
        case IRP_MN_CANCEL_STOP_DEVICE:
            Status = STATUS_SUCCESS;
            break;

        case IRP_MN_SURPRISE_REMOVAL:
        case IRP_MN_REMOVE_DEVICE:
            Status = PciIdeXPdoRemoveDevice(PdoExtension,
                                            IoStack->MinorFunction == IRP_MN_REMOVE_DEVICE);
            break;

        case IRP_MN_QUERY_DEVICE_RELATIONS:
            if (IoStack->Parameters.QueryDeviceRelations.Type == TargetDeviceRelation)
                Status = PciIdeXPdoQueryTargetDeviceRelations(PdoExtension, Irp);
            else
                Status = Irp->IoStatus.Status;
            break;

        case IRP_MN_QUERY_CAPABILITIES:
            Status = PciIdeXPdoQueryCapabilities(PdoExtension, Irp);
            break;

        case IRP_MN_QUERY_PNP_DEVICE_STATE:
            Status = PciIdeXPdoQueryPnpDeviceState(PdoExtension, Irp);
            break;

        case IRP_MN_QUERY_RESOURCES:
            Status = PciIdeXPdoQueryResources(PdoExtension, Irp);
            break;

        case IRP_MN_QUERY_RESOURCE_REQUIREMENTS:
            Status = PciIdeXPdoQueryResourceRequirements(PdoExtension, Irp);
            break;

        case IRP_MN_QUERY_ID:
            Status = PciIdeXPdoQueryId(PdoExtension, Irp);
            break;

        case IRP_MN_QUERY_DEVICE_TEXT:
            Status = PciIdeXPdoQueryDeviceText(PdoExtension, Irp);
            break;

        case IRP_MN_DEVICE_USAGE_NOTIFICATION:
            Status = PciIdeXPdoQueryDeviceUsageNotification(PdoExtension, Irp);
            break;

        default:
            Status = Irp->IoStatus.Status;
            break;
    }

    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return Status;
}

CODE_SEG("PAGE")
NTSTATUS
NTAPI
PciIdeXDispatchPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PAGED_CODE();

    if (IS_FDO(DeviceObject->DeviceExtension))
        return PciIdeXFdoDispatchPnp(DeviceObject->DeviceExtension, Irp);
    else
        return PciIdeXPdoDispatchPnp(DeviceObject->DeviceExtension, Irp);
}
