/*
 * PROJECT:     ReactOS Raspberry Pi 3 VC4 WDDM miniport
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     VideoCore IV V3D engine ownership and initialization
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#include "rpi3vc4.h"
#include "ordered_nop.h"

#define RPI3VC4_RPIQ_PATH                  L"\\DosDevices\\RPIQ"
#define RPI3VC4_FILE_DEVICE_RPIQ           2836UL
#define RPI3VC4_RPIQ_PROPERTY_FUNCTION     2008UL
#define RPI3VC4_IOCTL_MAILBOX_PROPERTY     \
    CTL_CODE(RPI3VC4_FILE_DEVICE_RPIQ,     \
             RPI3VC4_RPIQ_PROPERTY_FUNCTION, \
             METHOD_BUFFERED,              \
             FILE_ANY_ACCESS)
#define RPI3VC4_MAILBOX_TAG_REQUEST        0x00000000UL
#define RPI3VC4_MAILBOX_RESPONSE_SUCCESS   0x80000000UL
#define RPI3VC4_MAILBOX_TAG_RESPONSE       0x80000000UL
#define RPI3VC4_MAILBOX_GET_DOMAIN_STATE   0x00030030UL
#define RPI3VC4_MAILBOX_SET_DOMAIN_STATE   0x00038030UL
#define RPI3VC4_MAILBOX_SET_POWER_STATE    0x00028001UL

/* The power-domain binding numbers are firmware domain numbers minus one. */
#define RPI3VC4_POWER_DOMAIN_V3D           11UL
#define RPI3VC4_POWER_DOMAIN_ARM           23UL
#define RPI3VC4_OLD_POWER_DOMAIN_V3D       10UL

#define RPI3VC4_RENDER_TEST_WIDTH           64UL
#define RPI3VC4_RENDER_TEST_HEIGHT          64UL
#define RPI3VC4_RENDER_TEST_TARGET_SIZE     \
    (RPI3VC4_RENDER_TEST_WIDTH * RPI3VC4_RENDER_TEST_HEIGHT * sizeof(ULONG))
#define RPI3VC4_RENDER_TEST_CL_OFFSET       RPI3VC4_RENDER_TEST_TARGET_SIZE
#define RPI3VC4_RENDER_TEST_ALLOCATION_SIZE \
    (RPI3VC4_RENDER_TEST_CL_OFFSET + PAGE_SIZE)
#define RPI3VC4_RENDER_TEST_COLOR           0xff2080e0UL

#define RPI3VC4_PACKET_STORE_MS_TILE_BUFFER_AND_EOF 25U
#define RPI3VC4_PACKET_STORE_TILE_BUFFER_GENERAL    28U
#define RPI3VC4_PACKET_TILE_RENDERING_MODE_CONFIG   113U
#define RPI3VC4_PACKET_CLEAR_COLORS                 114U
#define RPI3VC4_PACKET_TILE_COORDINATES              115U
#define RPI3VC4_RENDER_CONFIG_FORMAT_RGBA8888        (1U << 2)

#include <pshpack1.h>
typedef struct _RPI3VC4_MAILBOX_POWER_DOMAIN
{
    ULONG TotalBuffer;
    ULONG RequestResponse;
    ULONG TagId;
    ULONG ResponseLength;
    ULONG Request;
    ULONG Domain;
    ULONG State;
    ULONG EndTag;
} RPI3VC4_MAILBOX_POWER_DOMAIN, *PRPI3VC4_MAILBOX_POWER_DOMAIN;
#include <poppack.h>

NTSYSAPI
NTSTATUS
NTAPI
ZwWaitForSingleObject(
    _In_ HANDLE Handle,
    _In_ BOOLEAN Alertable,
    _In_opt_ PLARGE_INTEGER Timeout);

static VOID
NTAPI
Rpi3Vc4V3dPollDpcRoutine(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2);

static PULONG
Rpi3Vc4V3dRegister(
    _In_ PRPI3VC4_CONTEXT Context,
    _In_ ULONG Offset)
{
    return (PULONG)((PUCHAR)Context->V3dBase + Offset);
}

static VOID
Rpi3Vc4ClWriteU8(
    _Inout_ PUCHAR *Cursor,
    _In_ UCHAR Value)
{
    *(*Cursor)++ = Value;
}

static VOID
Rpi3Vc4ClWriteU16(
    _Inout_ PUCHAR *Cursor,
    _In_ USHORT Value)
{
    RtlCopyMemory(*Cursor, &Value, sizeof(Value));
    *Cursor += sizeof(Value);
}

static VOID
Rpi3Vc4ClWriteU32(
    _Inout_ PUCHAR *Cursor,
    _In_ ULONG Value)
{
    RtlCopyMemory(*Cursor, &Value, sizeof(Value));
    *Cursor += sizeof(Value);
}

static ULONG
Rpi3Vc4GpuAddress(
    _In_ PRPI3VC4_CONTEXT Context,
    _In_ PHYSICAL_ADDRESS PhysicalAddress)
{
    return Context->BusAlias |
           ((ULONG)PhysicalAddress.QuadPart & RPI3VC4_GPU_ADDRESS_MASK);
}

NTSTATUS
Rpi3Vc4ReserveV3dMemory(
    _Inout_ PRPI3VC4_CONTEXT Context)
{
    PHYSICAL_ADDRESS LowAddress;
    PHYSICAL_ADDRESS HighAddress;
    PHYSICAL_ADDRESS BoundaryAddressMultiple;
    PHYSICAL_ADDRESS PhysicalAddress;
    PMDL Mdl;

    if (Context->V3dBinOverflow != NULL)
        return STATUS_SUCCESS;

    /* Tile-state pointers carry only 28 address bits. Reserve overflow and
     * DMA workspaces together so all tile scratch shares one high nibble.
     * Ordinary textures keep their independent general allocation segment. */
    LowAddress.QuadPart = 0;
    HighAddress.QuadPart = RPI3VC4_HIGHEST_SCANOUT_ADDRESS;
    BoundaryAddressMultiple.QuadPart = 0x10000000ULL;
    Context->V3dBinOverflow = MmAllocateContiguousMemorySpecifyCache(
        RPI3VC4_V3D_WORKING_SIZE, LowAddress, HighAddress,
        BoundaryAddressMultiple, MmCached);
    if (Context->V3dBinOverflow == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    PhysicalAddress = MmGetPhysicalAddress(Context->V3dBinOverflow);
    if (PhysicalAddress.QuadPart <= 0 ||
        (ULONGLONG)PhysicalAddress.QuadPart >
            RPI3VC4_HIGHEST_SCANOUT_ADDRESS -
                (RPI3VC4_V3D_WORKING_SIZE - 1) ||
        ((ULONG)PhysicalAddress.QuadPart & 0xf0000000UL) !=
            ((ULONG)(PhysicalAddress.QuadPart +
                     RPI3VC4_V3D_WORKING_SIZE - 1) & 0xf0000000UL))
    {
        MmFreeContiguousMemorySpecifyCache(Context->V3dBinOverflow,
            RPI3VC4_V3D_WORKING_SIZE, MmCached);
        Context->V3dBinOverflow = NULL;
        return STATUS_CONFLICTING_ADDRESSES;
    }

    /* Remove dirty CPU cache lines before any portion becomes GPU output. */
    Mdl = IoAllocateMdl(Context->V3dBinOverflow,
                       RPI3VC4_V3D_WORKING_SIZE, FALSE, FALSE, NULL);
    if (Mdl == NULL)
    {
        MmFreeContiguousMemorySpecifyCache(Context->V3dBinOverflow,
            RPI3VC4_V3D_WORKING_SIZE, MmCached);
        Context->V3dBinOverflow = NULL;
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(Context->V3dBinOverflow, RPI3VC4_V3D_WORKING_SIZE);
    MmBuildMdlForNonPagedPool(Mdl);
    KeFlushIoBuffers(Mdl, FALSE, TRUE);
    IoFreeMdl(Mdl);

    Context->V3dBinOverflowPhysical = PhysicalAddress;
    Context->Device->DmaWorkspacePhysical.QuadPart =
        PhysicalAddress.QuadPart + RPI3VC4_V3D_BIN_OVERFLOW_SIZE;
    Context->Device->DmaWorkspaceSize = RPI3VC4_V3D_DMA_WORKSPACE_SIZE;
    Context->V3dBinOverflowUsed = 0;
    Context->V3dBinOverflowCurrent = 0;
    return STATUS_SUCCESS;
}

/*
 * Exercise the render control-list thread without accepting any user-mode
 * command bytes.  The full VC4 path must still validate and relocate Mesa's
 * binning lists and shaders; this gate only proves power, bus addressing,
 * command-thread execution, and render-target visibility.
 */
static NTSTATUS
Rpi3Vc4RunRenderThreadTest(
    _Inout_ PRPI3VC4_CONTEXT Context)
{
    PHYSICAL_ADDRESS LowAddress;
    PHYSICAL_ADDRESS HighAddress;
    PHYSICAL_ADDRESS SkipBytes;
    PHYSICAL_ADDRESS AllocationPhysical;
    PHYSICAL_ADDRESS CommandPhysical;
    PVOID Allocation;
    PUCHAR Command;
    PUCHAR Cursor;
    ULONG CommandBytes;
    ULONG CommandStart;
    ULONG CommandEnd;
    ULONG Iteration;
    ULONG InterruptStatus = 0;
    ULONG ControlStatus = 0;
    ULONG Pixel;
    NTSTATUS Status;

    if (Context->BusAlias == 0)
        return STATUS_DEVICE_CONFIGURATION_ERROR;

    LowAddress.QuadPart = 0;
    HighAddress.QuadPart = RPI3VC4_HIGHEST_SCANOUT_ADDRESS;
    SkipBytes.QuadPart = 0;
    Allocation = MmAllocateContiguousMemorySpecifyCache(
                     RPI3VC4_RENDER_TEST_ALLOCATION_SIZE,
                     LowAddress,
                     HighAddress,
                     SkipBytes,
                     MmNonCached);
    if (Allocation == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlFillMemory(Allocation, RPI3VC4_RENDER_TEST_TARGET_SIZE, 0xa5);
    RtlZeroMemory((PUCHAR)Allocation + RPI3VC4_RENDER_TEST_CL_OFFSET,
                  PAGE_SIZE);
    AllocationPhysical = MmGetPhysicalAddress(Allocation);
    Command = (PUCHAR)Allocation + RPI3VC4_RENDER_TEST_CL_OFFSET;
    CommandPhysical = MmGetPhysicalAddress(Command);
    Cursor = Command;

    Rpi3Vc4ClWriteU8(&Cursor, RPI3VC4_PACKET_CLEAR_COLORS);
    Rpi3Vc4ClWriteU32(&Cursor, RPI3VC4_RENDER_TEST_COLOR);
    Rpi3Vc4ClWriteU32(&Cursor, RPI3VC4_RENDER_TEST_COLOR);
    Rpi3Vc4ClWriteU32(&Cursor, 0x00ffffffUL);
    Rpi3Vc4ClWriteU8(&Cursor, 0);

    /* Prime the tile-buffer clear values before the first real store. */
    Rpi3Vc4ClWriteU8(&Cursor, RPI3VC4_PACKET_TILE_COORDINATES);
    Rpi3Vc4ClWriteU8(&Cursor, 0);
    Rpi3Vc4ClWriteU8(&Cursor, 0);
    Rpi3Vc4ClWriteU8(&Cursor, RPI3VC4_PACKET_STORE_TILE_BUFFER_GENERAL);
    Rpi3Vc4ClWriteU16(&Cursor, 0);
    Rpi3Vc4ClWriteU32(&Cursor, 0);

    /*
     * The None-mode store above latches the new clear values into the tile
     * buffer.  Program the destination only afterwards, matching the VC4
     * kernel render-list builder; the rendering-mode packet itself resets
     * tile state on this generation.
     */
    Rpi3Vc4ClWriteU8(&Cursor,
                     RPI3VC4_PACKET_TILE_RENDERING_MODE_CONFIG);
    Rpi3Vc4ClWriteU32(&Cursor,
                      Rpi3Vc4GpuAddress(Context, AllocationPhysical));
    Rpi3Vc4ClWriteU16(&Cursor, RPI3VC4_RENDER_TEST_WIDTH);
    Rpi3Vc4ClWriteU16(&Cursor, RPI3VC4_RENDER_TEST_HEIGHT);
    Rpi3Vc4ClWriteU16(&Cursor,
                      RPI3VC4_RENDER_CONFIG_FORMAT_RGBA8888);

    Rpi3Vc4ClWriteU8(&Cursor, RPI3VC4_PACKET_TILE_COORDINATES);
    Rpi3Vc4ClWriteU8(&Cursor, 0);
    Rpi3Vc4ClWriteU8(&Cursor, 0);
    Rpi3Vc4ClWriteU8(&Cursor,
                     RPI3VC4_PACKET_STORE_MS_TILE_BUFFER_AND_EOF);

    CommandBytes = (ULONG)(Cursor - Command);
    CommandStart = Rpi3Vc4GpuAddress(Context, CommandPhysical);
    if (CommandStart > MAXULONG - CommandBytes)
    {
        Status = STATUS_INTEGER_OVERFLOW;
        goto Done;
    }
    CommandEnd = CommandStart + CommandBytes;
#if defined(_M_ARM64)
    __dsb(_ARM64_BARRIER_SY);
#endif
    KeMemoryBarrier();
    WRITE_REGISTER_ULONG(Rpi3Vc4V3dRegister(Context,
                                             RPI3VC4_V3D_CT1CS),
                         RPI3VC4_V3D_CTRSTA);
    WRITE_REGISTER_ULONG(Rpi3Vc4V3dRegister(Context,
                                             RPI3VC4_V3D_INTDIS),
                         RPI3VC4_V3D_INTERRUPT_MASK);
    WRITE_REGISTER_ULONG(Rpi3Vc4V3dRegister(Context,
                                             RPI3VC4_V3D_INTCTL),
                         RPI3VC4_V3D_INTERRUPT_MASK);
    WRITE_REGISTER_ULONG(Rpi3Vc4V3dRegister(Context,
                                             RPI3VC4_V3D_L2CACTL),
                         RPI3VC4_V3D_L2CCLR);
    WRITE_REGISTER_ULONG(Rpi3Vc4V3dRegister(Context,
                                             RPI3VC4_V3D_SLCACTL),
                         RPI3VC4_V3D_SLCACTL_ALL);
    KeMemoryBarrier();

    WRITE_REGISTER_ULONG(Rpi3Vc4V3dRegister(Context,
                                             RPI3VC4_V3D_CT1CA),
                         CommandStart);
    WRITE_REGISTER_ULONG(Rpi3Vc4V3dRegister(Context,
                                             RPI3VC4_V3D_CT1EA),
                         CommandEnd);

    Status = STATUS_IO_TIMEOUT;
    for (Iteration = 0; Iteration != 50000; ++Iteration)
    {
        InterruptStatus = READ_REGISTER_ULONG(
                              Rpi3Vc4V3dRegister(Context,
                                                 RPI3VC4_V3D_INTCTL));
        ControlStatus = READ_REGISTER_ULONG(
                            Rpi3Vc4V3dRegister(Context,
                                               RPI3VC4_V3D_CT1CS));
        if ((ControlStatus & RPI3VC4_V3D_CTERR) != 0)
        {
            Status = STATUS_DEVICE_HARDWARE_ERROR;
            break;
        }
        if ((InterruptStatus & RPI3VC4_V3D_INT_FRDONE) != 0)
        {
            Status = STATUS_SUCCESS;
            break;
        }
        KeStallExecutionProcessor(10);
    }

    WRITE_REGISTER_ULONG(Rpi3Vc4V3dRegister(Context,
                                             RPI3VC4_V3D_INTCTL),
                         InterruptStatus & RPI3VC4_V3D_INTERRUPT_MASK);
    if (!NT_SUCCESS(Status))
        goto Done;

#if defined(_M_ARM64)
    __dsb(_ARM64_BARRIER_SY);
#endif
    WRITE_REGISTER_ULONG(Rpi3Vc4V3dRegister(Context,
                                             RPI3VC4_V3D_L2CACTL),
                         RPI3VC4_V3D_L2CCLR);
    KeMemoryBarrier();
    Pixel = *(volatile ULONG *)Allocation;
    Context->V3dRenderTestPixel = Pixel;
    if (Pixel != RPI3VC4_RENDER_TEST_COLOR)
        Status = STATUS_DATA_ERROR;

Done:
    WRITE_REGISTER_ULONG(Rpi3Vc4V3dRegister(Context,
                                             RPI3VC4_V3D_CT1CS),
                         RPI3VC4_V3D_CTRSTA);
    MmFreeContiguousMemorySpecifyCache(
        Allocation,
        RPI3VC4_RENDER_TEST_ALLOCATION_SIZE,
        MmNonCached);
    return Status;
}

static NTSTATUS
Rpi3Vc4OpenRpiq(
    _Out_ PHANDLE Handle)
{
    UNICODE_STRING Name;
    OBJECT_ATTRIBUTES Attributes;
    IO_STATUS_BLOCK IoStatus;

    RtlInitUnicodeString(&Name, RPI3VC4_RPIQ_PATH);
    InitializeObjectAttributes(&Attributes,
                               &Name,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);
    return ZwCreateFile(Handle,
                        GENERIC_READ | SYNCHRONIZE,
                        &Attributes,
                        &IoStatus,
                        NULL,
                        FILE_ATTRIBUTE_NORMAL,
                        FILE_SHARE_READ | FILE_SHARE_WRITE,
                        FILE_OPEN,
                        FILE_NON_DIRECTORY_FILE |
                            FILE_SYNCHRONOUS_IO_NONALERT,
                        NULL,
                        0);
}

static NTSTATUS
Rpi3Vc4SendRpiqProperty(
    _In_ HANDLE Handle,
    _Inout_updates_bytes_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize)
{
    IO_STATUS_BLOCK IoStatus;
    NTSTATUS Status;

    Status = ZwDeviceIoControlFile(Handle,
                                   NULL,
                                   NULL,
                                   NULL,
                                   &IoStatus,
                                   RPI3VC4_IOCTL_MAILBOX_PROPERTY,
                                   Buffer,
                                   BufferSize,
                                   Buffer,
                                   BufferSize);
    if (Status == STATUS_PENDING)
    {
        Status = ZwWaitForSingleObject(Handle, FALSE, NULL);
        if (NT_SUCCESS(Status))
            Status = IoStatus.Status;
    }

    if (NT_SUCCESS(Status) && IoStatus.Information != BufferSize)
        Status = STATUS_INFO_LENGTH_MISMATCH;
    return Status;
}

static NTSTATUS
Rpi3Vc4SendPowerDomainProperty(
    _In_ HANDLE RpiqHandle,
    _In_ ULONG Tag,
    _In_ ULONG Domain,
    _In_ ULONG State,
    _Out_opt_ PULONG ReturnedState)
{
    RPI3VC4_MAILBOX_POWER_DOMAIN Property;
    NTSTATUS Status;

    RtlZeroMemory(&Property, sizeof(Property));
    Property.TotalBuffer = sizeof(Property);
    Property.RequestResponse = RPI3VC4_MAILBOX_TAG_REQUEST;
    Property.TagId = Tag;
    Property.ResponseLength = sizeof(Property.Domain) + sizeof(Property.State);
    Property.Request = RPI3VC4_MAILBOX_TAG_REQUEST;
    Property.Domain = Domain;
    Property.State = State;

    Status = Rpi3Vc4SendRpiqProperty(RpiqHandle,
                                     &Property,
                                     sizeof(Property));
    if (!NT_SUCCESS(Status))
        return Status;
    if (Property.RequestResponse != RPI3VC4_MAILBOX_RESPONSE_SUCCESS)
        return STATUS_DEVICE_PROTOCOL_ERROR;
    if (!(Property.Request & RPI3VC4_MAILBOX_TAG_RESPONSE))
        return STATUS_NOT_SUPPORTED;

    if (ReturnedState != NULL)
        *ReturnedState = Property.State;

    return STATUS_SUCCESS;
}

static NTSTATUS
Rpi3Vc4SetV3dPower(
    _In_ BOOLEAN PowerOn)
{
    HANDLE RpiqHandle;
    ULONG State;
    NTSTATUS Status;

    Status = Rpi3Vc4OpenRpiq(&RpiqHandle);
    if (!NT_SUCCESS(Status))
        return Status;

    /*
     * Unknown property tags are silently skipped by the firmware.  Match the
     * Linux firmware ABI probe by using an impossible return value and only
     * selecting the domain interface when the firmware replaces it.
     */
    State = ~0UL;
    Status = Rpi3Vc4SendPowerDomainProperty(
                 RpiqHandle,
                 RPI3VC4_MAILBOX_GET_DOMAIN_STATE,
                 RPI3VC4_POWER_DOMAIN_ARM,
                 State,
                 &State);
    if (NT_SUCCESS(Status) && State != ~0UL)
    {
        Status = Rpi3Vc4SendPowerDomainProperty(
                     RpiqHandle,
                     RPI3VC4_MAILBOX_SET_DOMAIN_STATE,
                     RPI3VC4_POWER_DOMAIN_V3D,
                     PowerOn ? 1UL : 0UL,
                     &State);
    }
    else if (Status == STATUS_NOT_SUPPORTED ||
             (NT_SUCCESS(Status) && State == ~0UL))
    {
        Status = Rpi3Vc4SendPowerDomainProperty(
                     RpiqHandle,
                     RPI3VC4_MAILBOX_SET_POWER_STATE,
                     RPI3VC4_OLD_POWER_DOMAIN_V3D,
                     PowerOn ? 1UL : 0UL,
                     &State);
    }

    ZwClose(RpiqHandle);
    if (!NT_SUCCESS(Status))
        return Status;
    if ((State & 1UL) != (PowerOn ? 1UL : 0UL))
        return STATUS_DEVICE_PROTOCOL_ERROR;

    return STATUS_SUCCESS;
}

NTSTATUS
Rpi3Vc4InitializeV3d(
    _Inout_ PRPI3VC4_CONTEXT Context)
{
    KIRQL OldIrql;
    NTSTATUS Status;

    if (Context == NULL || Context->V3dBase == NULL)
        return STATUS_DEVICE_CONFIGURATION_ERROR;

    Context->V3dReady = FALSE;

    KeAcquireSpinLock(&Context->V3dQueueLock, &OldIrql);
    Context->V3dSubmitHead = 0;
    Context->V3dSubmitTail = 0;
    Context->V3dBinNext = 0;
    Context->V3dBinActiveIndex = 0;
    Context->V3dRenderActiveIndex = 0;
    Context->V3dInterruptPending = 0;
    Context->V3dBinOverflowUsed = 0;
    Context->V3dBinOverflowCurrent = 0;
    Context->V3dBinActive = FALSE;
    Context->V3dRenderActive = FALSE;
    Context->V3dRecovering = FALSE;
    KeReleaseSpinLock(&Context->V3dQueueLock, OldIrql);

    if (!Context->V3dPollInitialized)
    {
        KeInitializeTimerEx(&Context->V3dPollTimer, NotificationTimer);
        KeInitializeDpc(&Context->V3dPollDpc,
                        Rpi3Vc4V3dPollDpcRoutine,
                        Context);
        Context->V3dPollInitialized = TRUE;
    }

    Context->V3dIdent0 = READ_REGISTER_ULONG(
                            Rpi3Vc4V3dRegister(Context,
                                               RPI3VC4_V3D_IDENT0));
    Context->V3dIdent1 = READ_REGISTER_ULONG(
                            Rpi3Vc4V3dRegister(Context,
                                               RPI3VC4_V3D_IDENT1));
    Context->V3dIdent2 = READ_REGISTER_ULONG(
                            Rpi3Vc4V3dRegister(Context,
                                               RPI3VC4_V3D_IDENT2));
    if (Context->V3dIdent0 != RPI3VC4_V3D_EXPECTED_IDENT0)
    {
        Context->V3dStatus = STATUS_DEVICE_HARDWARE_ERROR;
        return STATUS_DEVICE_HARDWARE_ERROR;
    }

    Status = Rpi3Vc4ReserveV3dMemory(Context);
    if (!NT_SUCCESS(Status))
    {
        Context->V3dStatus = Status;
        return Status;
    }

    /*
     * Adopt a deterministic idle engine. Firmware or an earlier boot may
     * leave command threads, binner overflow state, or pending interrupts
     * behind. No userspace submission is admitted until these registers
     * have been reset and the VC4 submit validator is installed.
     */
    WRITE_REGISTER_ULONG(Rpi3Vc4V3dRegister(Context,
                                             RPI3VC4_V3D_INTDIS),
                         RPI3VC4_V3D_INTERRUPT_MASK);
    WRITE_REGISTER_ULONG(Rpi3Vc4V3dRegister(Context,
                                             RPI3VC4_V3D_INTCTL),
                         RPI3VC4_V3D_INTERRUPT_MASK);
    WRITE_REGISTER_ULONG(Rpi3Vc4V3dRegister(Context,
                                             RPI3VC4_V3D_CT0CS),
                         RPI3VC4_V3D_CTRSTA);
    WRITE_REGISTER_ULONG(Rpi3Vc4V3dRegister(Context,
                                             RPI3VC4_V3D_CT1CS),
                         RPI3VC4_V3D_CTRSTA);
    WRITE_REGISTER_ULONG(Rpi3Vc4V3dRegister(Context,
                                             RPI3VC4_V3D_BPOA),
                         0);
    WRITE_REGISTER_ULONG(Rpi3Vc4V3dRegister(Context,
                                             RPI3VC4_V3D_BPOS),
                         0);
    WRITE_REGISTER_ULONG(Rpi3Vc4V3dRegister(Context,
                                             RPI3VC4_V3D_VPMBASE),
                         0);
    KeMemoryBarrier();

    Context->V3dReady = TRUE;
    Context->V3dStatus = STATUS_SUCCESS;
    return STATUS_SUCCESS;
}

NTSTATUS
Rpi3Vc4EnsureV3dReady(
    _Inout_ PRPI3VC4_CONTEXT Context)
{
    NTSTATUS Status;

    if (Context == NULL || Context->V3dBase == NULL)
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return STATUS_INVALID_DEVICE_STATE;

    Status = KeWaitForSingleObject(&Context->V3dPowerMutex,
                                   Executive,
                                   KernelMode,
                                   FALSE,
                                   NULL);
    if (!NT_SUCCESS(Status))
        return Status;

    if (Context->V3dReady)
    {
        Status = Context->V3dRenderTestRan ?
                     Context->V3dRenderTestStatus :
                     STATUS_SUCCESS;
        goto Done;
    }

    Status = Rpi3Vc4SetV3dPower(TRUE);
    if (!NT_SUCCESS(Status))
    {
        Context->V3dStatus = Status;
        goto Done;
    }

    Context->V3dPowerOwned = TRUE;
    KeStallExecutionProcessor(100);
    Status = Rpi3Vc4InitializeV3d(Context);
    if (NT_SUCCESS(Status))
    {
        Context->V3dRenderTestStatus =
            Rpi3Vc4RunRenderThreadTest(Context);
        Context->V3dRenderTestRan = TRUE;
        Status = Context->V3dRenderTestStatus;
        Context->V3dStatus = Status;
    }
    else
    {
        (VOID)Rpi3Vc4SetV3dPower(FALSE);
        Context->V3dPowerOwned = FALSE;
        Context->V3dStatus = Status;
    }

Done:
    KeReleaseMutex(&Context->V3dPowerMutex, FALSE);
    return Status;
}

VOID
Rpi3Vc4StopV3d(
    _Inout_ PRPI3VC4_CONTEXT Context)
{
    NTSTATUS Status;
    KIRQL OldIrql;

    if (Context == NULL || Context->V3dBase == NULL)
        return;

    if (Context->V3dPollInitialized)
    {
        KeCancelTimer(&Context->V3dPollTimer);
        KeRemoveQueueDpc(&Context->V3dPollDpc);
    }

    KeAcquireSpinLock(&Context->V3dQueueLock, &OldIrql);
    Context->V3dReady = FALSE;
    Context->V3dSubmitHead = Context->V3dSubmitTail;
    Context->V3dBinNext = Context->V3dSubmitTail;
    Context->V3dInterruptPending = 0;
    Context->V3dBinOverflowUsed = 0;
    Context->V3dBinOverflowCurrent = 0;
    Context->V3dBinActive = FALSE;
    Context->V3dRenderActive = FALSE;
    Context->V3dRecovering = FALSE;
    KeReleaseSpinLock(&Context->V3dQueueLock, OldIrql);

    Status = KeWaitForSingleObject(&Context->V3dPowerMutex,
                                   Executive,
                                   KernelMode,
                                   FALSE,
                                   NULL);
    if (!NT_SUCCESS(Status))
        return;

    if (Context->V3dBase != NULL)
    {
        WRITE_REGISTER_ULONG(Rpi3Vc4V3dRegister(Context,
                                                 RPI3VC4_V3D_INTDIS),
                             RPI3VC4_V3D_INTERRUPT_MASK);
        WRITE_REGISTER_ULONG(Rpi3Vc4V3dRegister(Context,
                                                 RPI3VC4_V3D_CT0CS),
                             RPI3VC4_V3D_CTRSTA);
        WRITE_REGISTER_ULONG(Rpi3Vc4V3dRegister(Context,
                                                 RPI3VC4_V3D_CT1CS),
                             RPI3VC4_V3D_CTRSTA);
        WRITE_REGISTER_ULONG(Rpi3Vc4V3dRegister(Context,
                                                 RPI3VC4_V3D_BPOA),
                             0);
        WRITE_REGISTER_ULONG(Rpi3Vc4V3dRegister(Context,
                                                 RPI3VC4_V3D_BPOS),
                             0);
        KeMemoryBarrier();
    }
    if (Context->V3dPowerOwned)
    {
        (VOID)Rpi3Vc4SetV3dPower(FALSE);
        Context->V3dPowerOwned = FALSE;
    }
    Context->V3dStatus = STATUS_DEVICE_NOT_READY;
    Context->V3dRenderTestStatus = STATUS_DEVICE_NOT_READY;
    Context->V3dRenderTestRan = FALSE;
    Context->V3dRenderTestPixel = 0;
    if (Context->V3dBinOverflow != NULL)
    {
        MmFreeContiguousMemorySpecifyCache(
            Context->V3dBinOverflow,
            RPI3VC4_V3D_WORKING_SIZE,
            MmCached);
        Context->V3dBinOverflow = NULL;
        Context->Device->DmaWorkspacePhysical.QuadPart = 0;
        Context->Device->DmaWorkspaceSize = 0;
        Context->V3dBinOverflowPhysical.QuadPart = 0;
        Context->V3dBinOverflowUsed = 0;
        Context->V3dBinOverflowCurrent = 0;
    }
    KeReleaseMutex(&Context->V3dPowerMutex, FALSE);
}

static VOID
Rpi3Vc4ArmPollTimer(
    _Inout_ PRPI3VC4_CONTEXT Context,
    _In_ ULONG Milliseconds)
{
    LARGE_INTEGER DueTime;

    DueTime.QuadPart = -(LONGLONG)Milliseconds * 10000;
    KeSetTimer(&Context->V3dPollTimer, DueTime, &Context->V3dPollDpc);
}

static BOOLEAN
Rpi3Vc4PacketAddressValid(
    _In_ PRPI3VC4_CONTEXT Context,
    _In_ ULONG Start,
    _In_ ULONG End)
{
    if (Start == 0 && End == 0)
        return TRUE;
    return Start != 0 && End > Start &&
           (Start & RPI3VC4_GPU_ALIAS_MASK) == Context->BusAlias &&
           (End - 1) >= Start &&
           ((End - 1) & RPI3VC4_GPU_ALIAS_MASK) == Context->BusAlias;
}

static VOID
Rpi3Vc4ResetCommandThreads(
    _Inout_ PRPI3VC4_CONTEXT Context)
{
    WRITE_REGISTER_ULONG(Rpi3Vc4V3dRegister(Context, RPI3VC4_V3D_INTDIS),
                         RPI3VC4_V3D_INTERRUPT_MASK);
    WRITE_REGISTER_ULONG(Rpi3Vc4V3dRegister(Context, RPI3VC4_V3D_INTCTL),
                         RPI3VC4_V3D_INTERRUPT_MASK);
    WRITE_REGISTER_ULONG(Rpi3Vc4V3dRegister(Context, RPI3VC4_V3D_CT0CS),
                         RPI3VC4_V3D_CTRSTA);
    WRITE_REGISTER_ULONG(Rpi3Vc4V3dRegister(Context, RPI3VC4_V3D_CT1CS),
                         RPI3VC4_V3D_CTRSTA);
    WRITE_REGISTER_ULONG(Rpi3Vc4V3dRegister(Context, RPI3VC4_V3D_BPOA), 0);
    WRITE_REGISTER_ULONG(Rpi3Vc4V3dRegister(Context, RPI3VC4_V3D_BPOS), 0);
    WRITE_REGISTER_ULONG(Rpi3Vc4V3dRegister(Context, RPI3VC4_V3D_L2CACTL),
                         RPI3VC4_V3D_L2CCLR);
    WRITE_REGISTER_ULONG(Rpi3Vc4V3dRegister(Context, RPI3VC4_V3D_SLCACTL),
                         RPI3VC4_V3D_SLCACTL_ALL);
    KeMemoryBarrier();
}

/* V3D queue lock held. */
static BOOLEAN
Rpi3Vc4ReserveOverflowSlotLocked(
    _Inout_ PRPI3VC4_CONTEXT Context,
    _Out_ PULONG SlotMask,
    _Out_ PULONG Address)
{
    ULONG Slot;
    ULONG Mask;

    for (Slot = 0;
         Slot < RPI3VC4_V3D_BIN_OVERFLOW_SIZE /
                    RPI3VC4_V3D_BIN_OVERFLOW_SLOT_SIZE;
         ++Slot)
    {
        Mask = 1UL << Slot;
        if ((Context->V3dBinOverflowUsed & Mask) != 0)
            continue;

        Context->V3dBinOverflowUsed |= Mask;
        *SlotMask = Mask;
        *Address = Rpi3Vc4GpuAddress(
                       Context, Context->V3dBinOverflowPhysical) +
                   Slot * RPI3VC4_V3D_BIN_OVERFLOW_SLOT_SIZE;
        return TRUE;
    }

    return FALSE;
}

/* V3D queue lock held. */
static VOID
Rpi3Vc4StartRenderLocked(
    _Inout_ PRPI3VC4_CONTEXT Context,
    _In_ ULONG SubmitIndex)
{
    PRPI3VC4_V3D_SUBMIT Submit;

    if (Context->V3dRecovering || Context->V3dRenderActive ||
        SubmitIndex != Context->V3dSubmitHead ||
        SubmitIndex == Context->V3dSubmitTail)
    {
        return;
    }

    Submit = &Context->V3dSubmitRing[
        SubmitIndex % RPI3VC4_V3D_SUBMIT_RING_SIZE];
    if (!Submit->BinComplete || Submit->RenderStarted)
        return;

    /* VideoCore IV table 51 permits CTnCA writes only with CTRUN clear.
     * A pipeline completion interrupt does not replace that condition. */
    if (Submit->Packet.Ct1Start != 0 &&
        (READ_REGISTER_ULONG(Rpi3Vc4V3dRegister(
            Context, RPI3VC4_V3D_CT1CS)) & RPI3VC4_V3D_CTRUN) != 0)
    {
        return;
    }

    Submit->RenderStarted = TRUE;
    Submit->RenderStartTime100ns = KeQueryInterruptTime();
    Submit->RenderLastProgressTime100ns = Submit->RenderStartTime100ns;
    Submit->RenderLastAddress = Submit->Packet.Ct1Start;
    if (Submit->Packet.Ct1Start == 0)
    {
        Submit->RenderComplete = TRUE;
        return;
    }

    WRITE_REGISTER_ULONG(Rpi3Vc4V3dRegister(Context,
                                             RPI3VC4_V3D_L2CACTL),
                         RPI3VC4_V3D_L2CCLR);
    WRITE_REGISTER_ULONG(Rpi3Vc4V3dRegister(Context,
                                             RPI3VC4_V3D_SLCACTL),
                         RPI3VC4_V3D_SLCACTL_TEXTURE);
    KeMemoryBarrier();
    WRITE_REGISTER_ULONG(Rpi3Vc4V3dRegister(Context, RPI3VC4_V3D_INTENA),
                         RPI3VC4_V3D_INTERRUPT_MASK);
    WRITE_REGISTER_ULONG(Rpi3Vc4V3dRegister(Context, RPI3VC4_V3D_CT1CA),
                         Submit->Packet.Ct1Start);
    WRITE_REGISTER_ULONG(Rpi3Vc4V3dRegister(Context, RPI3VC4_V3D_CT1EA),
                         Submit->Packet.Ct1End);
    Context->V3dRenderActive = TRUE;
    Context->V3dRenderActiveIndex = SubmitIndex;
}

/* V3D queue lock held. */
static VOID
Rpi3Vc4StartBinLocked(
    _Inout_ PRPI3VC4_CONTEXT Context)
{
    PRPI3VC4_V3D_SUBMIT Submit;
    ULONG SubmitIndex;

    if (Context->V3dRecovering || Context->V3dBinActive)
        return;

    while (Context->V3dBinNext != Context->V3dSubmitTail)
    {
        SubmitIndex = Context->V3dBinNext;
        Submit = &Context->V3dSubmitRing[
            SubmitIndex % RPI3VC4_V3D_SUBMIT_RING_SIZE];
        if (Submit->Packet.Ct1Start == 0 ||
            Submit->Packet.Ct0Start == 0)
        {
            Submit->BinComplete = TRUE;
            ++Context->V3dBinNext;
            continue;
        }

        /* FLDONE reports tile-list memory completion. The control thread
         * can still be executing the trailing semaphore record. Do not
         * overwrite CT0CA until the executor has stopped (table 51). */
        if ((READ_REGISTER_ULONG(Rpi3Vc4V3dRegister(
                Context, RPI3VC4_V3D_CT0CS)) & RPI3VC4_V3D_CTRUN) != 0)
        {
            return;
        }

        WRITE_REGISTER_ULONG(Rpi3Vc4V3dRegister(Context,
                                                 RPI3VC4_V3D_L2CACTL),
                             RPI3VC4_V3D_L2CCLR);
        WRITE_REGISTER_ULONG(Rpi3Vc4V3dRegister(Context,
                                                 RPI3VC4_V3D_SLCACTL),
                             RPI3VC4_V3D_SLCACTL_ALL);
#if defined(_M_ARM64)
        __dsb(_ARM64_BARRIER_SY);
#endif
        KeMemoryBarrier();
        WRITE_REGISTER_ULONG(Rpi3Vc4V3dRegister(Context,
                                                 RPI3VC4_V3D_INTENA),
                             RPI3VC4_V3D_INTERRUPT_MASK);
        WRITE_REGISTER_ULONG(Rpi3Vc4V3dRegister(Context,
                                                 RPI3VC4_V3D_CT0CA),
                             Submit->Packet.Ct0Start);
        WRITE_REGISTER_ULONG(Rpi3Vc4V3dRegister(Context,
                                                 RPI3VC4_V3D_CT0EA),
                             Submit->Packet.Ct0End);
        Submit->BinStartTime100ns = KeQueryInterruptTime();
        Submit->BinLastProgressTime100ns = Submit->BinStartTime100ns;
        Submit->BinLastAddress = Submit->Packet.Ct0Start;
        Context->V3dBinActive = TRUE;
        Context->V3dBinActiveIndex = SubmitIndex;
        ++Context->V3dBinNext;
        return;
    }
}

/* V3D queue lock held. */
static VOID
Rpi3Vc4StartPipelinesLocked(
    _Inout_ PRPI3VC4_CONTEXT Context)
{
    PRPI3VC4_V3D_SUBMIT Head;
    BOOLEAN RetryStart;

    if (Context->V3dRecovering)
        return;

    Rpi3Vc4StartRenderLocked(Context, Context->V3dSubmitHead);
    Rpi3Vc4StartBinLocked(Context);
    Rpi3Vc4StartRenderLocked(Context, Context->V3dSubmitHead);
    if (Context->V3dSubmitHead == Context->V3dSubmitTail)
    {
        KeCancelTimer(&Context->V3dPollTimer);
        return;
    }

    Head = &Context->V3dSubmitRing[
        Context->V3dSubmitHead % RPI3VC4_V3D_SUBMIT_RING_SIZE];
    RetryStart = (!Context->V3dBinActive &&
                  Context->V3dBinNext != Context->V3dSubmitTail) ||
                 (!Context->V3dRenderActive && Head->BinComplete);

    /* Active pipelines signal completion through interrupts. Poll them only
     * for the progress watchdog. Keep the short retry when CTRUN prevented
     * a start, or when an ordered no-op still needs retirement. */
    Rpi3Vc4ArmPollTimer(Context, RetryStart ? 1 : 100);
}

typedef struct _RPI3VC4_RETIREMENT_NOTIFICATION
{
    PSOFTGPU_DEVICE Device;
    ULONG Fence;
    NTSTATUS Status;
} RPI3VC4_RETIREMENT_NOTIFICATION;

static BOOLEAN NTAPI
Rpi3Vc4NotifyRetirementSynchronized(PVOID Parameter)
{
    RPI3VC4_RETIREMENT_NOTIFICATION *Notification = Parameter;
    PSOFTGPU_DEVICE Device = Notification->Device;
    PRPI3VC4_CONTEXT Context = Device->PlatformContext;
    PSOFTGPU_ENGINE Engine = &Device->Engines[SOFTGPU_NODE_3D];
    DXGKARGCB_NOTIFY_INTERRUPT_DATA NotifyData;
    ULONG Fence = Notification->Fence;

    /* FenceLock is held by the outer DPC. The interrupt synchronization
     * boundary serializes this callback with real ISRs too. Never acquire
     * FenceLock here: submission holds it while synchronizing with the ISR.
     * Check, watermark update and actual publication are one operation. */
    ASSERT(KeGetCurrentIrql() > DISPATCH_LEVEL);
    RtlZeroMemory(&NotifyData, sizeof(NotifyData));
    if (NT_SUCCESS(Notification->Status))
    {
        if (Engine->NotifiedFence != 0 &&
            (LONG)(Fence - Engine->NotifiedFence) <= 0)
            return TRUE;
        if (Engine->CompletedFence == 0 ||
            (LONG)(Fence - Engine->CompletedFence) > 0)
            Engine->CompletedFence = Fence;
        Engine->NotifiedFence = Fence;
        NotifyData.InterruptType = DXGK_INTERRUPT_DMA_COMPLETED;
        NotifyData.DmaCompleted.SubmissionFenceId = Fence;
        NotifyData.DmaCompleted.NodeOrdinal = 0;
        NotifyData.DmaCompleted.EngineOrdinal = 0;
    }
    else
    {
        if (Context->V3dNotifiedFaultFence != 0 &&
            (LONG)(Fence - Context->V3dNotifiedFaultFence) <= 0)
            return TRUE;
        Context->V3dNotifiedFaultFence = Fence;
        NotifyData.InterruptType = DXGK_INTERRUPT_DMA_FAULTED;
        NotifyData.DmaFaulted.FaultedFenceId = Fence;
        NotifyData.DmaFaulted.Status = Notification->Status;
        NotifyData.DmaFaulted.NodeOrdinal = 0;
        NotifyData.DmaFaulted.EngineOrdinal = 0;
    }
    Device->DxgkInterface.DxgkCbNotifyInterrupt(
        Device->DxgkInterface.DeviceHandle, &NotifyData);
    Device->DxgkInterface.DxgkCbQueueDpc(
        Device->DxgkInterface.DeviceHandle);
    return TRUE;
}

/* Called with FenceLock held. A failed synchronization must leave the ring
 * entry owned by the miniport, so a later DPC can retry without losing its
 * completion. NotifyDpc is deliberately outside both miniport locks. */
static BOOLEAN
Rpi3Vc4NotifyRetirement(
    _Inout_ PSOFTGPU_DEVICE Device,
    _In_ ULONG Fence,
    _In_ NTSTATUS CompletionStatus)
{
    RPI3VC4_RETIREMENT_NOTIFICATION Notification;
    BOOLEAN Published = FALSE;
    NTSTATUS Status;

    if (Device->DxgkInterface.DxgkCbSynchronizeExecution == NULL ||
        Device->DxgkInterface.DxgkCbNotifyInterrupt == NULL ||
        Device->DxgkInterface.DxgkCbQueueDpc == NULL)
        return FALSE;
    Notification.Device = Device;
    Notification.Fence = Fence;
    Notification.Status = CompletionStatus;
    Status = Device->DxgkInterface.DxgkCbSynchronizeExecution(
        Device->DxgkInterface.DeviceHandle,
        Rpi3Vc4NotifyRetirementSynchronized, &Notification, 0, &Published);
    return NT_SUCCESS(Status) && Published;
}

NTSTATUS
Rpi3Vc4SubmitCommand(
    _Inout_ PSOFTGPU_DEVICE Device,
    _In_ const DXGKARG_SUBMITCOMMAND *SubmitCommand)
{
    PRPI3VC4_CONTEXT Context;
    RPI3VC4_DMA_PACKET Packet;
    PHYSICAL_ADDRESS DmaPhysical;
    PUCHAR Mapping = NULL;
    BOOLEAN PhysicalMapping = FALSE;
    BOOLEAN PrivatePacket = FALSE;
    BOOLEAN OrderedNop = FALSE;
    ULONG BinnerOverflowEnd;
    KIRQL OldIrql;

    if (Device == NULL || SubmitCommand == NULL)
        return STATUS_INVALID_PARAMETER;
    Context = (PRPI3VC4_CONTEXT)Device->PlatformContext;
    if (Context == NULL || !Context->V3dReady ||
        SubmitCommand->NodeOrdinal != 0 ||
        SubmitCommand->EngineOrdinal != 0)
    {
        return STATUS_NOT_SUPPORTED;
    }

    RtlZeroMemory(&Packet, sizeof(Packet));
    if (!SubmitCommand->Flags.NullRendering)
    {
        if (SubmitCommand->DmaBufferSubmissionEndOffset <
                SubmitCommand->DmaBufferSubmissionStartOffset ||
            SubmitCommand->DmaBufferSubmissionEndOffset > SubmitCommand->DmaBufferSize ||
            sizeof(Packet) >
                SubmitCommand->DmaBufferSubmissionEndOffset -
                    SubmitCommand->DmaBufferSubmissionStartOffset)
        {
            return STATUS_INVALID_PARAMETER;
        }
        if (SubmitCommand->pDmaBufferPrivateData != NULL &&
            SubmitCommand->DmaBufferPrivateDataSubmissionStartOffset <=
                SubmitCommand->DmaBufferPrivateDataSubmissionEndOffset &&
            SubmitCommand->DmaBufferPrivateDataSubmissionEndOffset <=
                SubmitCommand->DmaBufferPrivateDataSize &&
            sizeof(Packet) <=
                SubmitCommand->DmaBufferPrivateDataSubmissionEndOffset -
                    SubmitCommand->DmaBufferPrivateDataSubmissionStartOffset)
        {
            RtlCopyMemory(
                &Packet,
                (const UCHAR *)SubmitCommand->pDmaBufferPrivateData +
                    SubmitCommand->DmaBufferPrivateDataSubmissionStartOffset,
                sizeof(Packet));
            /* Paging buffers can have reserved private storage without a
             * VC4 packet in it. Inspect their actual DMA stream below. */
            PrivatePacket = Packet.Magic == RPI3VC4_DMA_PACKET_MAGIC;
        }
        if (!PrivatePacket)
        {
            DmaPhysical = SubmitCommand->DmaBufferPhysicalAddress;
            Mapping = MmMapIoSpace(
                          DmaPhysical,
                          SubmitCommand->DmaBufferSubmissionEndOffset,
                          MmCached);
            if (Mapping == NULL)
                return STATUS_INSUFFICIENT_RESOURCES;
            PhysicalMapping = TRUE;
        }
        if (!PrivatePacket)
        {
            RtlCopyMemory(&Packet,
                          Mapping +
                              SubmitCommand->DmaBufferSubmissionStartOffset,
                          sizeof(Packet));
        }
        /* Paging/residency NOPs share node zero's cumulative fence stream.
         * Completing them in the software DPC could retire earlier live V3D
         * buffers. Put them in the same ordered queue as rendering instead. */
        if (Packet.Magic == SOFTGPU_CMD_MAGIC && Mapping != NULL)
        {
            OrderedNop = Rpi3Vc4IsOrderedNopStream(
                Mapping + SubmitCommand->DmaBufferSubmissionStartOffset,
                SubmitCommand->DmaBufferSubmissionEndOffset -
                    SubmitCommand->DmaBufferSubmissionStartOffset);
        }
        if (PhysicalMapping)
        {
            MmUnmapIoSpace(Mapping,
                           SubmitCommand->DmaBufferSubmissionEndOffset);
        }
        if (OrderedNop)
        {
            RtlZeroMemory(&Packet, sizeof(Packet));
            goto QueueSubmit;
        }
        if (Packet.Magic != RPI3VC4_DMA_PACKET_MAGIC)
            return STATUS_NOT_SUPPORTED;
        if (Packet.Op != RPI3VC4_DMA_OP_VALIDATED_CL ||
            Packet.Size != sizeof(Packet) ||
            !Rpi3Vc4PacketAddressValid(Context,
                                       Packet.Ct0Start,
                                       Packet.Ct0End) ||
            !Rpi3Vc4PacketAddressValid(Context,
                                       Packet.Ct1Start,
                                       Packet.Ct1End) ||
            Packet.Ct1Start == 0 ||
            (Packet.Ct0Start != 0 &&
             (Packet.BinnerOverflowAddress != Rpi3Vc4GpuAddress(
                  Context, Context->V3dBinOverflowPhysical) ||
              Packet.BinnerOverflowSize != RPI3VC4_V3D_BIN_OVERFLOW_SIZE ||
              Packet.BinnerOverflowAddress >
                  MAXULONG - Packet.BinnerOverflowSize)))
        {
            return STATUS_ILLEGAL_INSTRUCTION;
        }
        BinnerOverflowEnd = Packet.BinnerOverflowAddress +
                            Packet.BinnerOverflowSize;
        if (Packet.Ct0Start != 0 &&
            !Rpi3Vc4PacketAddressValid(Context,
                                       Packet.BinnerOverflowAddress,
                                       BinnerOverflowEnd))
        {
            return STATUS_ILLEGAL_INSTRUCTION;
        }
    }

QueueSubmit:
    KeAcquireSpinLock(&Context->V3dQueueLock, &OldIrql);
    if (!Context->V3dReady ||
        Context->V3dSubmitTail - Context->V3dSubmitHead >=
            RPI3VC4_V3D_SUBMIT_RING_SIZE)
    {
        KeReleaseSpinLock(&Context->V3dQueueLock, OldIrql);
        return Context->V3dReady ? STATUS_DEVICE_BUSY :
                                   STATUS_DEVICE_NOT_READY;
    }
    RtlZeroMemory(
        &Context->V3dSubmitRing[
            Context->V3dSubmitTail % RPI3VC4_V3D_SUBMIT_RING_SIZE],
        sizeof(Context->V3dSubmitRing[0]));
    Context->V3dSubmitRing[
        Context->V3dSubmitTail % RPI3VC4_V3D_SUBMIT_RING_SIZE].Packet = Packet;
    Context->V3dSubmitRing[
        Context->V3dSubmitTail % RPI3VC4_V3D_SUBMIT_RING_SIZE].Fence =
        SubmitCommand->SubmissionFenceId;
    Context->V3dSubmitTail++;
    Device->Engines[SOFTGPU_NODE_3D].CurrentFence =
        SubmitCommand->SubmissionFenceId;
    Rpi3Vc4StartPipelinesLocked(Context);
    if (Packet.Ct1Start == 0)
        KeInsertQueueDpc(&Context->V3dPollDpc, NULL, NULL);
    KeReleaseSpinLock(&Context->V3dQueueLock, OldIrql);
    return STATUS_SUCCESS;
}

BOOLEAN
Rpi3Vc4Interrupt(
    _Inout_ PSOFTGPU_DEVICE Device)
{
    PRPI3VC4_CONTEXT Context;
    ULONG InterruptStatus;

    if (Device == NULL)
        return FALSE;
    Context = (PRPI3VC4_CONTEXT)Device->PlatformContext;
    if (Context == NULL || !Context->V3dReady || Context->V3dBase == NULL)
        return FALSE;
    InterruptStatus = READ_REGISTER_ULONG(
        Rpi3Vc4V3dRegister(Context, RPI3VC4_V3D_INTCTL)) &
        RPI3VC4_V3D_INTERRUPT_MASK;
    if (InterruptStatus == 0)
        return FALSE;
    if ((InterruptStatus & RPI3VC4_V3D_INT_OUTOMEM) != 0)
    {
        WRITE_REGISTER_ULONG(
            Rpi3Vc4V3dRegister(Context, RPI3VC4_V3D_INTDIS),
            RPI3VC4_V3D_INT_OUTOMEM);
    }
    if ((InterruptStatus & ~RPI3VC4_V3D_INT_OUTOMEM) != 0)
    {
        WRITE_REGISTER_ULONG(
            Rpi3Vc4V3dRegister(Context, RPI3VC4_V3D_INTCTL),
            InterruptStatus & ~RPI3VC4_V3D_INT_OUTOMEM);
    }
    InterlockedOr((volatile LONG *)&Context->V3dInterruptPending,
                  InterruptStatus);
    /* The ISR may interrupt a submitter holding FenceLock/V3dQueueLock.
     * Even try-acquiring an already-owned executive lock is invalid in a
     * checked kernel. Keep bin/render transitions in ProcessDpcLocked, whose
     * interrupt-synchronized capture consumes this latch exactly once. */
    if (Device->DxgkInterface.DxgkCbQueueDpc != NULL)
    {
        Device->DxgkInterface.DxgkCbQueueDpc(
            Device->DxgkInterface.DeviceHandle);
    }
    else
    {
        KeInsertQueueDpc(&Context->V3dPollDpc, NULL, NULL);
    }
    return TRUE;
}

typedef struct _RPI3VC4_INTERRUPT_CAPTURE
{
    PRPI3VC4_CONTEXT Context;
    ULONG Status;
} RPI3VC4_INTERRUPT_CAPTURE;

static BOOLEAN NTAPI
Rpi3Vc4CaptureInterruptSynchronized(PVOID Parameter)
{
    RPI3VC4_INTERRUPT_CAPTURE *Capture = Parameter;
    PRPI3VC4_CONTEXT Context = Capture->Context;
    ULONG Status;

    Status = (ULONG)InterlockedExchange(
        (volatile LONG *)&Context->V3dInterruptPending, 0);
    Status |= READ_REGISTER_ULONG(
        Rpi3Vc4V3dRegister(Context, RPI3VC4_V3D_INTCTL)) &
        RPI3VC4_V3D_INTERRUPT_MASK;
    if (Status & RPI3VC4_V3D_INT_OUTOMEM)
        WRITE_REGISTER_ULONG(
            Rpi3Vc4V3dRegister(Context, RPI3VC4_V3D_INTDIS),
            RPI3VC4_V3D_INT_OUTOMEM);
    if (Status & ~RPI3VC4_V3D_INT_OUTOMEM)
        WRITE_REGISTER_ULONG(
            Rpi3Vc4V3dRegister(Context, RPI3VC4_V3D_INTCTL),
            Status & ~RPI3VC4_V3D_INT_OUTOMEM);
    Capture->Status = Status;
    return TRUE;
}

static VOID
Rpi3Vc4ProcessDpcLocked(
    _Inout_ PSOFTGPU_DEVICE Device)
{
    PRPI3VC4_CONTEXT Context;
    ULONG InterruptStatus;
    ULONG Ct0Status;
    ULONG Ct1Status;
    ULONG Ct0Address;
    ULONG Ct1Address;
    ULONG OverflowAddress;
    ULONG OverflowSlot;
    ULONG Index;
    ULONGLONG Now;
    BOOLEAN Faulted;
    KIRQL OldIrql;
    PRPI3VC4_V3D_SUBMIT Submit;
    PRPI3VC4_V3D_SUBMIT OverflowOwner;
    RPI3VC4_INTERRUPT_CAPTURE Capture;
    BOOLEAN Captured = FALSE;
    NTSTATUS CaptureStatus;

    if (Device == NULL)
        return;
    Context = (PRPI3VC4_CONTEXT)Device->PlatformContext;
    if (Context == NULL || !Context->V3dReady || Context->V3dBase == NULL)
        return;
    KeAcquireSpinLock(&Context->V3dQueueLock, &OldIrql);
    if (!Context->V3dReady || Context->V3dBase == NULL)
    {
        KeReleaseSpinLock(&Context->V3dQueueLock, OldIrql);
        return;
    }

    /*
     * The queue lock serializes the polling and interrupt DPCs, but not the
     * ISR. Consume the software latch and acknowledge hardware status under
     * the interrupt lock too. Otherwise the ISR can latch a completion that
     * this DPC already read from hardware; the next DPC would incorrectly
     * apply that duplicate completion to the next bin/render job.
     */
    Capture.Context = Context;
    Capture.Status = 0;
    CaptureStatus = Device->DxgkInterface.DxgkCbSynchronizeExecution != NULL ?
        Device->DxgkInterface.DxgkCbSynchronizeExecution(
            Device->DxgkInterface.DeviceHandle,
            Rpi3Vc4CaptureInterruptSynchronized, &Capture, 0, &Captured) :
        STATUS_NOT_SUPPORTED;
    if (!NT_SUCCESS(CaptureStatus) || !Captured)
    {
        Rpi3Vc4ArmPollTimer(Context, 1);
        KeReleaseSpinLock(&Context->V3dQueueLock, OldIrql);
        return;
    }
    InterruptStatus = Capture.Status;
    Ct0Status = READ_REGISTER_ULONG(
        Rpi3Vc4V3dRegister(Context, RPI3VC4_V3D_CT0CS));
    Ct1Status = READ_REGISTER_ULONG(
        Rpi3Vc4V3dRegister(Context, RPI3VC4_V3D_CT1CS));
    Ct0Address = READ_REGISTER_ULONG(
        Rpi3Vc4V3dRegister(Context, RPI3VC4_V3D_CT0CA));
    Ct1Address = READ_REGISTER_ULONG(
        Rpi3Vc4V3dRegister(Context, RPI3VC4_V3D_CT1CA));

    if (Context->V3dRecovering ||
        Context->V3dSubmitHead == Context->V3dSubmitTail)
    {
        KeReleaseSpinLock(&Context->V3dQueueLock, OldIrql);
        return;
    }

    Faulted = (Context->V3dBinActive &&
               (Ct0Status & RPI3VC4_V3D_CTERR) != 0) ||
              (Context->V3dRenderActive &&
               (Ct1Status & RPI3VC4_V3D_CTERR) != 0);
    if (!Faulted && Context->V3dBinActive &&
        (InterruptStatus & RPI3VC4_V3D_INT_FLDONE) != 0)
    {
        Submit = &Context->V3dSubmitRing[
            Context->V3dBinActiveIndex % RPI3VC4_V3D_SUBMIT_RING_SIZE];
        Submit->BinComplete = TRUE;
        Context->V3dBinActive = FALSE;
    }
    if (!Faulted && Context->V3dRenderActive &&
        (InterruptStatus & RPI3VC4_V3D_INT_FRDONE) != 0)
    {
        Submit = &Context->V3dSubmitRing[
            Context->V3dRenderActiveIndex % RPI3VC4_V3D_SUBMIT_RING_SIZE];
        Submit->RenderComplete = TRUE;
        Context->V3dRenderActive = FALSE;
    }
    if (!Faulted &&
        (InterruptStatus & RPI3VC4_V3D_INT_OUTOMEM) != 0)
    {
        OverflowOwner = NULL;
        if (Context->V3dBinOverflowCurrent != 0)
        {
            if (Context->V3dBinActive)
            {
                OverflowOwner = &Context->V3dSubmitRing[
                    Context->V3dBinActiveIndex %
                        RPI3VC4_V3D_SUBMIT_RING_SIZE];
            }
            else if (Context->V3dBinNext != Context->V3dSubmitTail)
            {
                OverflowOwner = &Context->V3dSubmitRing[
                    Context->V3dBinNext %
                        RPI3VC4_V3D_SUBMIT_RING_SIZE];
            }
            else if (Context->V3dBinNext != Context->V3dSubmitHead)
            {
                OverflowOwner = &Context->V3dSubmitRing[
                    (Context->V3dBinNext - 1) %
                        RPI3VC4_V3D_SUBMIT_RING_SIZE];
            }

            if (OverflowOwner != NULL)
                OverflowOwner->BinnerOverflowSlots |=
                    Context->V3dBinOverflowCurrent;
            else
                Context->V3dBinOverflowUsed &=
                    ~Context->V3dBinOverflowCurrent;
            Context->V3dBinOverflowCurrent = 0;
        }

        if (!Rpi3Vc4ReserveOverflowSlotLocked(Context,
                                              &OverflowSlot,
                                              &OverflowAddress))
        {
            Faulted = TRUE;
        }
        else
        {
            Context->V3dBinOverflowCurrent = OverflowSlot;
            WRITE_REGISTER_ULONG(
                Rpi3Vc4V3dRegister(Context, RPI3VC4_V3D_BPOA),
                OverflowAddress);
            WRITE_REGISTER_ULONG(
                Rpi3Vc4V3dRegister(Context, RPI3VC4_V3D_BPOS),
                RPI3VC4_V3D_BIN_OVERFLOW_SLOT_SIZE);
            KeMemoryBarrier();
            WRITE_REGISTER_ULONG(
                Rpi3Vc4V3dRegister(Context, RPI3VC4_V3D_INTCTL),
                RPI3VC4_V3D_INT_OUTOMEM);
            WRITE_REGISTER_ULONG(
                Rpi3Vc4V3dRegister(Context, RPI3VC4_V3D_INTENA),
                RPI3VC4_V3D_INT_OUTOMEM);
        }
    }

    Now = KeQueryInterruptTime();
    if (!Faulted && Context->V3dBinActive)
    {
        Submit = &Context->V3dSubmitRing[
            Context->V3dBinActiveIndex % RPI3VC4_V3D_SUBMIT_RING_SIZE];
        if (Ct0Address != Submit->BinLastAddress)
        {
            Submit->BinLastAddress = Ct0Address;
            Submit->BinLastProgressTime100ns = Now;
        }
        if (Now - Submit->BinLastProgressTime100ns >=
            2ULL * 1000 * 1000 * 10)
        {
            Faulted = TRUE;
        }
    }
    if (!Faulted && Context->V3dRenderActive)
    {
        Submit = &Context->V3dSubmitRing[
            Context->V3dRenderActiveIndex % RPI3VC4_V3D_SUBMIT_RING_SIZE];
        if (Ct1Address != Submit->RenderLastAddress)
        {
            Submit->RenderLastAddress = Ct1Address;
            Submit->RenderLastProgressTime100ns = Now;
        }
        if (Now - Submit->RenderLastProgressTime100ns >=
            2ULL * 1000 * 1000 * 10)
        {
            Faulted = TRUE;
        }
    }

    if (Faulted)
    {
        for (Index = Context->V3dSubmitHead;
             Index != Context->V3dSubmitTail;
             ++Index)
        {
            if (!Rpi3Vc4NotifyRetirement(
                    Device, Context->V3dSubmitRing[
                        Index % RPI3VC4_V3D_SUBMIT_RING_SIZE].Fence,
                    STATUS_DEVICE_HARDWARE_ERROR))
            {
                Rpi3Vc4ArmPollTimer(Context, 1);
                KeReleaseSpinLock(&Context->V3dQueueLock, OldIrql);
                return;
            }
        }
        Context->V3dRecovering = TRUE;
        Context->V3dSubmitHead = Context->V3dSubmitTail;
        Context->V3dBinNext = Context->V3dSubmitTail;
        Context->V3dBinOverflowUsed = 0;
        Context->V3dBinOverflowCurrent = 0;
        Context->V3dBinActive = FALSE;
        Context->V3dRenderActive = FALSE;
        Rpi3Vc4ResetCommandThreads(Context);
        Context->V3dRecovering = FALSE;
        Rpi3Vc4StartPipelinesLocked(Context);
        KeReleaseSpinLock(&Context->V3dQueueLock, OldIrql);
        return;
    }

    for (;;)
    {
        while (Context->V3dSubmitHead != Context->V3dSubmitTail)
        {
            Submit = &Context->V3dSubmitRing[
                Context->V3dSubmitHead % RPI3VC4_V3D_SUBMIT_RING_SIZE];
            if (!Submit->RenderComplete)
                break;

            if (!Rpi3Vc4NotifyRetirement(Device, Submit->Fence, STATUS_SUCCESS))
            {
                Rpi3Vc4ArmPollTimer(Context, 1);
                KeReleaseSpinLock(&Context->V3dQueueLock, OldIrql);
                return;
            }
            Context->V3dBinOverflowUsed &= ~Submit->BinnerOverflowSlots;
            ++Context->V3dSubmitHead;
        }

        Rpi3Vc4StartPipelinesLocked(Context);
        if (Context->V3dSubmitHead == Context->V3dSubmitTail ||
            !Context->V3dSubmitRing[
                Context->V3dSubmitHead %
                    RPI3VC4_V3D_SUBMIT_RING_SIZE].RenderComplete)
        {
            break;
        }
    }
    KeReleaseSpinLock(&Context->V3dQueueLock, OldIrql);

}

VOID
Rpi3Vc4Dpc(
    _Inout_ PSOFTGPU_DEVICE Device)
{
    KIRQL OldIrql;

    if (Device == NULL)
        return;

    /* Match the submission order: FenceLock -> V3dQueueLock -> interrupt
     * synchronization. This also prevents a second DPC from overtaking a
     * retired batch before its fence notification reaches dxgkrnl. */
    KeAcquireSpinLock(&Device->FenceLock, &OldIrql);
    if (!Device->Stopped)
        Rpi3Vc4ProcessDpcLocked(Device);
    KeReleaseSpinLock(&Device->FenceLock, OldIrql);

    /* Scheduler retirement can submit more work. It must run after releasing
     * the miniport locks, at DPC level, rather than inside the ISR callback. */
    if (Device->DxgkInterface.DxgkCbNotifyDpc != NULL)
        Device->DxgkInterface.DxgkCbNotifyDpc(
            Device->DxgkInterface.DeviceHandle);
}

static VOID
NTAPI
Rpi3Vc4V3dPollDpcRoutine(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    PRPI3VC4_CONTEXT Context = DeferredContext;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);
    if (Context != NULL && Context->Device != NULL)
        Rpi3Vc4Dpc(Context->Device);
}
