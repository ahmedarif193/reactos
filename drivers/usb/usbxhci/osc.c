/*
 * PROJECT:     ReactOS USB xHCI Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     ACPI _OSC (Operating System Capabilities) handling
 * COPYRIGHT:   Copyright 2025 Ahmed ARIF <arif.ing@outlook.com>
 *
 * This file implements the two-phase _OSC negotiation protocol per ACPI 6.5
 * Section 6.2.11 to properly negotiate USB host controller capabilities
 * with platform firmware.
 *
 * Implementation Approach:
 * We use IOCTL_ACPI_EVAL_METHOD sent to the lower device object (PDO),
 * which is the Windows-compatible way to evaluate ACPI methods. The IRP
 * travels up the stack to the ACPI driver which handles the evaluation.
 *
 * This is the proper Windows driver approach - no direct ACPI exports needed.
 */

#include "usbxhci.h"
#include <acpiioct.h>

#define NDEBUG
#include <debug.h>

/*
 * USB _OSC UUID in ACPI wire format (LE/BE converted)
 * Original UUID: CE2EE385-00E6-48CB-9F05-2EDB927C4899
 *
 * Per ACPI 6.5 Section 6.2.11:
 * - Data1 (4 bytes): Little-Endian
 * - Data2 (2 bytes): Little-Endian
 * - Data3 (2 bytes): Little-Endian
 * - Data4 (8 bytes): Big-Endian (as-is)
 */
static const UCHAR UsbOscUuid[16] = {
    0x85, 0xE3, 0x2E, 0xCE,  /* Data1: CE2EE385 -> LE */
    0xE6, 0x00,              /* Data2: 00E6 -> LE */
    0xCB, 0x48,              /* Data3: 48CB -> LE */
    0x9F, 0x05,              /* Data4[0-1]: BE (no swap) */
    0x2E, 0xDB, 0x92, 0x7C, 0x48, 0x99  /* Data4[2-7]: BE (no swap) */
};

/*
 * _OSC method takes 4 arguments:
 *   Arg0: UUID (Buffer, 16 bytes)
 *   Arg1: Revision (Integer)
 *   Arg2: Count of DWORDs in Capabilities buffer (Integer)
 *   Arg3: Capabilities buffer (Buffer, 3 DWORDs = 12 bytes)
 *
 * Buffer layout for ACPI_EVAL_INPUT_BUFFER_COMPLEX:
 *   - ACPI_EVAL_INPUT_BUFFER_COMPLEX header
 *   - Argument[0]: Buffer (UUID, 16 bytes)
 *   - Argument[1]: Integer (Revision)
 *   - Argument[2]: Integer (Count = 3)
 *   - Argument[3]: Buffer (Capabilities, 12 bytes)
 */
#define OSC_INPUT_BUFFER_SIZE ( \
    FIELD_OFFSET(ACPI_EVAL_INPUT_BUFFER_COMPLEX, Argument) + \
    ACPI_METHOD_ARGUMENT_LENGTH(16) + \
    ACPI_METHOD_ARGUMENT_LENGTH(sizeof(ULONG)) + \
    ACPI_METHOD_ARGUMENT_LENGTH(sizeof(ULONG)) + \
    ACPI_METHOD_ARGUMENT_LENGTH(12) \
)

/*
 * Output buffer needs to hold:
 *   - ACPI_EVAL_OUTPUT_BUFFER header
 *   - Return value: Buffer (Capabilities, 12 bytes)
 */
#define OSC_OUTPUT_BUFFER_SIZE ( \
    FIELD_OFFSET(ACPI_EVAL_OUTPUT_BUFFER, Argument) + \
    ACPI_METHOD_ARGUMENT_LENGTH(12) \
)

/**
 * @brief Sends an IOCTL_ACPI_EVAL_METHOD IRP synchronously.
 *
 * This function builds and sends an internal device control IRP to
 * evaluate an ACPI method on the device's ACPI node.
 *
 * @param DeviceObject Target device object (lower device in stack)
 * @param InputBuffer ACPI method input buffer
 * @param InputSize Size of input buffer
 * @param OutputBuffer Buffer to receive output (can be NULL)
 * @param OutputSize Size of output buffer
 *
 * @return NTSTATUS
 */
static
NTSTATUS
XHCI_SendAcpiEvalMethodIoctl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_reads_bytes_(InputSize) PVOID InputBuffer,
    _In_ ULONG InputSize,
    _Out_writes_bytes_opt_(OutputSize) PVOID OutputBuffer,
    _In_ ULONG OutputSize)
{
    NTSTATUS Status;
    KEVENT Event;
    IO_STATUS_BLOCK IoStatusBlock;
    PIRP Irp;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);

    Irp = IoBuildDeviceIoControlRequest(
        IOCTL_ACPI_EVAL_METHOD,
        DeviceObject,
        InputBuffer,
        InputSize,
        OutputBuffer,
        OutputSize,
        FALSE, /* Not internal - this goes to ACPI driver */
        &Event,
        &IoStatusBlock);

    if (!Irp)
    {
        DPRINT1("usbxhci: Failed to allocate IRP for ACPI method evaluation\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = IoCallDriver(DeviceObject, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatusBlock.Status;
    }

    return Status;
}

/**
 * @brief Evaluates _OSC method via IOCTL.
 *
 * This function builds the ACPI_EVAL_INPUT_BUFFER_COMPLEX for _OSC
 * and sends it to the lower device object.
 *
 * @param Extension XHCI extension
 * @param QueryMode TRUE for Query phase, FALSE for Commit
 * @param SupportCaps Support capabilities DWORD
 * @param ControlCaps Control capabilities DWORD
 * @param ReturnStatus Output: Status DWORD from firmware
 * @param ReturnControl Output: Control DWORD from firmware
 *
 * @return NTSTATUS
 */
static
NTSTATUS
XHCI_EvaluateOscMethod(
    _In_ PXHCI_EXTENSION Extension,
    _In_ BOOLEAN QueryMode,
    _In_ ULONG SupportCaps,
    _In_ ULONG ControlCaps,
    _Out_ PULONG ReturnStatus,
    _Out_ PULONG ReturnControl)
{
    NTSTATUS Status;
    PACPI_EVAL_INPUT_BUFFER_COMPLEX InputBuffer;
    PACPI_EVAL_OUTPUT_BUFFER OutputBuffer;
    PACPI_METHOD_ARGUMENT Argument;
    PULONG CapBuffer;
    ULONG CapDwords[3];

    /* Initialize outputs */
    *ReturnStatus = 0;
    *ReturnControl = 0;

    /* Check if we have a lower device object to send IOCTLs to */
    if (!Extension->Resources || !Extension->Resources->LowerDeviceObject)
    {
        DPRINT1("usbxhci: No lower device object available for ACPI IOCTL\n");
        return STATUS_NOT_FOUND;
    }

    /* Allocate input buffer */
    InputBuffer = ExAllocatePoolWithTag(NonPagedPool,
                                        OSC_INPUT_BUFFER_SIZE,
                                        XHCI_TAG);
    if (!InputBuffer)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Allocate output buffer */
    OutputBuffer = ExAllocatePoolWithTag(NonPagedPool,
                                         OSC_OUTPUT_BUFFER_SIZE,
                                         XHCI_TAG);
    if (!OutputBuffer)
    {
        ExFreePoolWithTag(InputBuffer, XHCI_TAG);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(InputBuffer, OSC_INPUT_BUFFER_SIZE);
    RtlZeroMemory(OutputBuffer, OSC_OUTPUT_BUFFER_SIZE);

    /* Build input buffer header */
    InputBuffer->Signature = ACPI_EVAL_INPUT_BUFFER_COMPLEX_SIGNATURE;
    InputBuffer->MethodNameAsUlong = 'CSO_'; /* "_OSC" in reverse */
    InputBuffer->Size = OSC_INPUT_BUFFER_SIZE;
    InputBuffer->ArgumentCount = 4;

    /* Build the 3-DWORD capabilities buffer */
    CapDwords[0] = QueryMode ? USB_OSC_STATUS_QUERY : 0; /* Status/Query DWORD */
    CapDwords[1] = SupportCaps;                          /* Support DWORD */
    CapDwords[2] = ControlCaps;                          /* Control DWORD */

    /* Argument 0: UUID buffer (16 bytes) */
    Argument = InputBuffer->Argument;
    ACPI_METHOD_SET_ARGUMENT_BUFFER(Argument, UsbOscUuid, sizeof(UsbOscUuid));

    /* Argument 1: Revision (Integer = 1) */
    Argument = ACPI_METHOD_NEXT_ARGUMENT(Argument);
    ACPI_METHOD_SET_ARGUMENT_INTEGER(Argument, 1);

    /* Argument 2: Count (Integer = 3 DWORDs) */
    Argument = ACPI_METHOD_NEXT_ARGUMENT(Argument);
    ACPI_METHOD_SET_ARGUMENT_INTEGER(Argument, 3);

    /* Argument 3: Capabilities buffer (12 bytes) */
    Argument = ACPI_METHOD_NEXT_ARGUMENT(Argument);
    ACPI_METHOD_SET_ARGUMENT_BUFFER(Argument, CapDwords, sizeof(CapDwords));

    /* Send the IOCTL */
    Status = XHCI_SendAcpiEvalMethodIoctl(
        Extension->Resources->LowerDeviceObject,
        InputBuffer,
        OSC_INPUT_BUFFER_SIZE,
        OutputBuffer,
        OSC_OUTPUT_BUFFER_SIZE);

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("usbxhci: ACPI _OSC IOCTL failed: 0x%lX\n", Status);
        goto Cleanup;
    }

    /* Validate output */
    if (OutputBuffer->Signature != ACPI_EVAL_OUTPUT_BUFFER_SIGNATURE)
    {
        DPRINT1("usbxhci: Invalid _OSC output signature\n");
        Status = STATUS_UNSUCCESSFUL;
        goto Cleanup;
    }

    if (OutputBuffer->Count < 1)
    {
        DPRINT1("usbxhci: _OSC returned no arguments\n");
        Status = STATUS_UNSUCCESSFUL;
        goto Cleanup;
    }

    /* Parse the return buffer */
    Argument = OutputBuffer->Argument;
    if (Argument->Type != ACPI_METHOD_ARGUMENT_BUFFER ||
        Argument->DataLength < 4)
    {
        DPRINT1("usbxhci: _OSC returned unexpected type %u length %u\n",
                Argument->Type, Argument->DataLength);
        Status = STATUS_UNSUCCESSFUL;
        goto Cleanup;
    }

    /* Extract status and control from returned buffer */
    CapBuffer = (PULONG)Argument->Data;
    *ReturnStatus = CapBuffer[0];

    if (Argument->DataLength >= 12)
    {
        *ReturnControl = CapBuffer[2]; /* Control is DWORD 2 */
    }
    else if (Argument->DataLength >= 8)
    {
        *ReturnControl = CapBuffer[1]; /* Fallback */
    }

    /* Check for error flags in status */
    if (*ReturnStatus & USB_OSC_STATUS_UUID_UNKNOWN)
    {
        DPRINT1("usbxhci: _OSC: UUID not recognized\n");
        Status = STATUS_NOT_SUPPORTED;
        goto Cleanup;
    }

    if (*ReturnStatus & USB_OSC_STATUS_REV_UNKNOWN)
    {
        DPRINT1("usbxhci: _OSC: Revision not recognized\n");
        Status = STATUS_NOT_SUPPORTED;
        goto Cleanup;
    }

    if (*ReturnStatus & USB_OSC_STATUS_FAILURE)
    {
        DPRINT1("usbxhci: _OSC: General failure\n");
        Status = STATUS_UNSUCCESSFUL;
        goto Cleanup;
    }

    Status = STATUS_SUCCESS;

Cleanup:
    ExFreePoolWithTag(OutputBuffer, XHCI_TAG);
    ExFreePoolWithTag(InputBuffer, XHCI_TAG);
    return Status;
}

/**
 * @brief Applies the _OSC policy based on granted controls.
 *
 * This function updates the OscContext flags based on what the
 * firmware granted. These flags are then used by other parts of
 * the driver to determine what operations are allowed.
 *
 * @param Extension Pointer to the XHCI extension
 * @param OscContext Pointer to the OSC context to update
 */
VOID
XHCI_ApplyOscPolicy(
    _In_ PXHCI_EXTENSION Extension,
    _Inout_ PXHCI_OSC_CONTEXT OscContext)
{
    ULONG Granted = OscContext->ControlGranted;
    ULONG Support = OscContext->SupportCapabilities;

    UNREFERENCED_PARAMETER(Extension);

    /* Control flags - set based on granted controls */
    OscContext->OsControlsPortPower =
        (Granted & USB_OSC_CTRL_PORT_POWER) != 0;

    OscContext->OsControlsLinkState =
        (Granted & USB_OSC_CTRL_LINK_STATE) != 0;

    OscContext->OsControlsUsbcMux =
        (Granted & USB_OSC_CTRL_USBC_MUX) != 0;

    OscContext->OsManagesPowerStates =
        (Granted & USB_OSC_CTRL_POWER_STATE) != 0;

    OscContext->OsControlsCompliance =
        (Granted & USB_OSC_CTRL_COMPLIANCE) != 0;

    OscContext->OsControlsU1U2 =
        (Granted & USB_OSC_CTRL_U1U2_ENTRY) != 0;

    /* Capability flags - based on what we advertised + controller features */
    OscContext->LpmSupported =
        (Support & USB_OSC_SUPPORT_LPM) != 0;

    OscContext->Rtd3Supported =
        (Support & USB_OSC_SUPPORT_RTD3) != 0 &&
        OscContext->OsManagesPowerStates;

    OscContext->Usb4Supported =
        (Support & USB_OSC_SUPPORT_USB4) != 0;

    /* Log summary */
    DPRINT1("usbxhci: _OSC policy applied:\n");
    DPRINT1("  Port Power: %s\n",
           OscContext->OsControlsPortPower ? "OS" : "Firmware");
    DPRINT1("  Link State: %s\n",
           OscContext->OsControlsLinkState ? "OS" : "Firmware");
    DPRINT1("  USB-C Mux: %s\n",
           OscContext->OsControlsUsbcMux ? "OS" : "Firmware");
    DPRINT1("  Power State: %s\n",
           OscContext->OsManagesPowerStates ? "OS" : "Firmware");
    DPRINT1("  U1/U2 Entry: %s\n",
           OscContext->OsControlsU1U2 ? "OS" : "Firmware");
}

/**
 * @brief Sets default OSC policy for legacy platforms.
 *
 * When _OSC is not available (legacy platform), this function sets
 * sensible defaults that assume full OS control, matching the
 * behavior of systems without ACPI _OSC support.
 *
 * @param OscContext Pointer to the OSC context to initialize
 */
static
VOID
XHCI_SetLegacyOscDefaults(
    _Inout_ PXHCI_OSC_CONTEXT OscContext)
{
    OscContext->Evaluated = FALSE;
    OscContext->FirmwareFirst = FALSE;

    /* Legacy platforms: assume full OS control */
    OscContext->OsControlsPortPower = TRUE;
    OscContext->OsControlsLinkState = TRUE;
    OscContext->OsControlsUsbcMux = FALSE;  /* USB-C mux requires _OSC */
    OscContext->OsManagesPowerStates = TRUE;
    OscContext->OsControlsCompliance = TRUE;
    OscContext->OsControlsU1U2 = TRUE;

    /* Capability flags for legacy */
    OscContext->LpmSupported = TRUE;
    OscContext->Rtd3Supported = FALSE;  /* RTD3 requires _OSC */
    OscContext->Usb4Supported = FALSE;  /* USB4 requires _OSC */

    DPRINT1("usbxhci: _OSC: Set legacy defaults (full OS control)\n");
}

/**
 * @brief Performs full two-phase _OSC negotiation.
 *
 * This function implements the mandatory Query-then-Commit protocol
 * per ACPI 6.5 Section 6.2.11. It:
 *   1. Sends a Query (_OSC with Bit 0 = 1) to determine available controls
 *   2. Sends a Commit (_OSC with Bit 0 = 0) requesting only available controls
 *   3. Updates the OSC context with the granted controls
 *
 * IRQL: Must be called at PASSIVE_LEVEL (ACPI interpreter requirement)
 *
 * @param Extension Pointer to the XHCI extension
 *
 * @return NTSTATUS
 */
NTSTATUS
XHCI_EvaluateOsc(
    _In_ PXHCI_EXTENSION Extension)
{
    NTSTATUS Status;
    ULONG SupportCaps;
    ULONG ControlCaps;
    ULONG AvailableControl;
    ULONG GrantedControl;
    ULONG QueryStatus;
    ULONG CommitStatus;

    XHCI_ASSERT_PASSIVE("XHCI_EvaluateOsc");

    /* Initialize OSC context */
    RtlZeroMemory(&Extension->OscContext, sizeof(XHCI_OSC_CONTEXT));

    /*
     * Check if we have resources and a lower device object.
     * The lower device object is needed to send ACPI IOCTLs.
     */
    if (!Extension->Resources || !Extension->Resources->LowerDeviceObject)
    {
        DPRINT1("usbxhci: _OSC: No lower device object available\n");
        XHCI_SetLegacyOscDefaults(&Extension->OscContext);
        return STATUS_SUCCESS;
    }

    DPRINT1("usbxhci: _OSC: Using IOCTL to evaluate _OSC method\n");

    /*
     * Build our capability advertisement.
     *
     * Advertise all capabilities the driver supports.
     * This includes USB4 to ensure USB4/Thunderbolt controllers
     * don't remain in "Connection Manager" mode.
     */
    SupportCaps = USB_OSC_SUPPORT_USB20 |
                  USB_OSC_SUPPORT_USB30 |
                  USB_OSC_SUPPORT_USB31_GEN2 |
                  USB_OSC_SUPPORT_USB4 |        /* Include USB4! */
                  USB_OSC_SUPPORT_XHCI_PM |
                  USB_OSC_SUPPORT_RTD3 |
                  USB_OSC_SUPPORT_LPM;

    /* Request all controls we can handle */
    ControlCaps = USB_OSC_CTRL_PORT_POWER |
                  USB_OSC_CTRL_LINK_STATE |
                  USB_OSC_CTRL_USBC_MUX |
                  USB_OSC_CTRL_POWER_STATE |
                  USB_OSC_CTRL_COMPLIANCE |
                  USB_OSC_CTRL_U1U2_ENTRY;

    Extension->OscContext.SupportCapabilities = SupportCaps;

    /*
     * Query Phase (ACPI 6.5 MANDATORY)
     *
     * First _OSC call MUST have Bit 0 = 1 to query what
     * firmware would grant without committing.
     */
    DPRINT1("usbxhci: _OSC Query phase - asking what controls are available\n");

    Status = XHCI_EvaluateOscMethod(Extension,
                                    TRUE,           /* QueryMode = TRUE */
                                    SupportCaps,
                                    ControlCaps,
                                    &QueryStatus,
                                    &AvailableControl);

    if (!NT_SUCCESS(Status))
    {
        /*
         * _OSC not available - legacy platform, assume full OS control.
         * This can happen if: _OSC method doesn't exist, ACPI device not found,
         * or the IOCTL failed.
         */
        DPRINT1("usbxhci: _OSC not available (Status=0x%lX) - using legacy defaults\n",
                Status);
        XHCI_SetLegacyOscDefaults(&Extension->OscContext);
        return STATUS_SUCCESS;
    }

    Extension->OscContext.QueryCompleted = TRUE;
    Extension->OscContext.ControlAvailable = AvailableControl;

    DPRINT1("usbxhci: _OSC Query returned: status=0x%lX, available=0x%lX\n",
           QueryStatus, AvailableControl);

    /* Check if firmware would mask our request */
    if (QueryStatus & USB_OSC_STATUS_MASKED)
    {
        DPRINT1("usbxhci: Query indicates firmware will mask some controls\n");
    }

    /*
     * Commit Phase
     *
     * Only request controls that Query indicated would be granted.
     * This avoids unnecessary "masked" warnings.
     */
    ControlCaps = ControlCaps & AvailableControl;
    Extension->OscContext.ControlRequested = ControlCaps;

    if (ControlCaps == 0)
    {
        DPRINT1("usbxhci: Firmware refuses all OS control (Firmware First mode)\n");
        Extension->OscContext.Evaluated = TRUE;
        Extension->OscContext.FirmwareFirst = TRUE;
        Extension->OscContext.ControlGranted = 0;
        XHCI_ApplyOscPolicy(Extension, &Extension->OscContext);
        return STATUS_SUCCESS;
    }

    DPRINT1("usbxhci: _OSC Commit phase - requesting controls 0x%lX\n", ControlCaps);

    Status = XHCI_EvaluateOscMethod(Extension,
                                    FALSE,          /* QueryMode = FALSE (Commit) */
                                    SupportCaps,
                                    ControlCaps,
                                    &CommitStatus,
                                    &GrantedControl);

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("usbxhci: _OSC Commit phase failed: 0x%lX\n", Status);
        /* Query succeeded but Commit failed - unusual but handle it */
        Extension->OscContext.Evaluated = TRUE;
        Extension->OscContext.FirmwareFirst = TRUE;
        XHCI_ApplyOscPolicy(Extension, &Extension->OscContext);
        return Status;
    }

    Extension->OscContext.CommitCompleted = TRUE;
    Extension->OscContext.Evaluated = TRUE;
    Extension->OscContext.ControlGranted = GrantedControl;

    DPRINT1("usbxhci: _OSC Commit returned: status=0x%lX, granted=0x%lX\n",
           CommitStatus, GrantedControl);

    /* Final check: did we get anything? */
    if (GrantedControl == 0)
    {
        DPRINT1("usbxhci: Firmware denied all control requests\n");
        Extension->OscContext.FirmwareFirst = TRUE;
    }

    /* Derive feature flags from granted controls */
    XHCI_ApplyOscPolicy(Extension, &Extension->OscContext);

    return STATUS_SUCCESS;
}

/**
 * @brief Returns TRUE if OS has been granted port power control.
 *
 * This function checks the OSC context to determine if the OS is
 * allowed to control port power. Use this before modifying port
 * power registers.
 *
 * @param Extension Pointer to the XHCI extension
 *
 * @return TRUE if OS controls port power, FALSE if firmware does
 */
BOOLEAN
XHCI_ShouldControlPortPower(
    _In_ PXHCI_EXTENSION Extension)
{
    /*
     * Return TRUE if:
     * - _OSC was not evaluated (legacy, assume OS control)
     * - _OSC granted port power control to OS
     */
    if (!Extension->OscContext.Evaluated)
    {
        return TRUE;  /* Legacy default */
    }

    return Extension->OscContext.OsControlsPortPower;
}

/**
 * @brief Returns TRUE if OS has been granted U1/U2 LPM control.
 *
 * This function checks the OSC context to determine if the OS is
 * allowed to manage U1/U2 link power management. Use this before
 * configuring LPM in endpoint contexts.
 *
 * @param Extension Pointer to the XHCI extension
 *
 * @return TRUE if OS controls U1/U2, FALSE if firmware does
 */
BOOLEAN
XHCI_ShouldManageU1U2(
    _In_ PXHCI_EXTENSION Extension)
{
    if (!Extension->OscContext.Evaluated)
    {
        return TRUE;  /* Legacy default */
    }

    return Extension->OscContext.OsControlsU1U2;
}

/**
 * @brief Returns TRUE if OS is allowed to manage power states.
 *
 * This function checks the OSC context to determine if the OS is
 * allowed to manage xHCI power states (D0/D3). Use this during
 * suspend/resume operations.
 *
 * @param Extension Pointer to the XHCI extension
 *
 * @return TRUE if OS manages power states, FALSE if firmware does
 */
BOOLEAN
XHCI_ShouldManagePowerStates(
    _In_ PXHCI_EXTENSION Extension)
{
    if (!Extension->OscContext.Evaluated)
    {
        return TRUE;  /* Legacy default */
    }

    return Extension->OscContext.OsManagesPowerStates;
}
