/*
 * PROJECT:         ReactOS ACPI/PCI Interface
 * LICENSE:         GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * FILE:            include/reactos/drivers/acpi/acpipci.h
 * PURPOSE:         ACPI-PCI device interface definitions
 * PROGRAMMERS:     ReactOS Development Team
 *
 * This header defines the device interface that allows the PCI driver to
 * request ACPI method evaluation for PCI devices without directly importing
 * acpi.sys exports. This follows the Windows driver model where drivers
 * communicate via device interfaces and IOCTLs rather than direct linking.
 */

#pragma once

/*
 * GUID_ACPI_PCI_INTERFACE
 *
 * Device interface GUID registered by the ACPI driver to provide
 * ACPI method evaluation services to the PCI driver.
 *
 * {A7E9DB84-DB4C-4289-876B-E2C3E63B5F4F}
 *
 * NOTE: This is only a declaration. To instantiate (allocate storage),
 * include <initguid.h> before including this header in exactly one .c file.
 */
DEFINE_GUID(GUID_ACPI_PCI_INTERFACE,
    0xA7E9DB84, 0xDB4C, 0x4289,
    0x87, 0x6B, 0xE2, 0xC3, 0xE6, 0x3B, 0x5F, 0x4F);

/*
 * IOCTL_ACPI_EVAL_METHOD_FOR_PCI
 *
 * Custom IOCTL for evaluating ACPI methods on PCI devices.
 * Unlike the standard IOCTL_ACPI_EVAL_METHOD which requires sending
 * the IRP to the specific ACPI device node, this IOCTL includes the
 * PCI device location (Segment:Bus:Device:Function) and can be sent
 * to the ACPI driver's device interface.
 *
 * Input buffer: ACPI_PCI_EVAL_INPUT_BUFFER (followed by method arguments)
 * Output buffer: ACPI_EVAL_OUTPUT_BUFFER (same as standard ACPI IOCTLs)
 */
#define IOCTL_ACPI_EVAL_METHOD_FOR_PCI \
    CTL_CODE(FILE_DEVICE_ACPI, 0x10, METHOD_BUFFERED, FILE_READ_ACCESS | FILE_WRITE_ACCESS)

/*
 * Signature for ACPI_PCI_EVAL_INPUT_BUFFER
 * 'ApcE' = ACPI PCI Eval
 */
#define ACPI_PCI_EVAL_INPUT_BUFFER_SIGNATURE    'EcpA'

/*
 * ACPI_PCI_EVAL_INPUT_BUFFER
 *
 * Input buffer structure for IOCTL_ACPI_EVAL_METHOD_FOR_PCI.
 * This structure specifies the PCI device location and the ACPI method
 * to evaluate, followed by the standard ACPI evaluation input buffer.
 */
typedef struct _ACPI_PCI_EVAL_INPUT_BUFFER {
    /*
     * Signature identifying this structure.
     * Must be ACPI_PCI_EVAL_INPUT_BUFFER_SIGNATURE.
     */
    ULONG Signature;

    /*
     * PCI device location - Segment:Bus:Device:Function
     */
    ULONG Segment;
    ULONG Bus;
    ULONG Device;
    ULONG Function;

    /*
     * Size of the entire input buffer including this header and
     * the embedded ACPI evaluation input buffer.
     */
    ULONG TotalSize;

    /*
     * Offset from the start of this structure to the embedded
     * ACPI_EVAL_INPUT_BUFFER (or variant like ACPI_EVAL_INPUT_BUFFER_COMPLEX).
     * The embedded buffer starts at:
     *   (PUCHAR)this + InputBufferOffset
     */
    ULONG InputBufferOffset;

    /*
     * Size of the embedded ACPI evaluation input buffer.
     */
    ULONG InputBufferSize;

} ACPI_PCI_EVAL_INPUT_BUFFER, *PACPI_PCI_EVAL_INPUT_BUFFER;

/*
 * Minimum size validation for ACPI_PCI_EVAL_INPUT_BUFFER
 */
#define ACPI_PCI_EVAL_INPUT_BUFFER_MIN_SIZE \
    sizeof(ACPI_PCI_EVAL_INPUT_BUFFER)

/*
 * Helper macro to calculate the total size of ACPI_PCI_EVAL_INPUT_BUFFER
 * given the size of the embedded ACPI evaluation input buffer.
 */
#define ACPI_PCI_EVAL_INPUT_BUFFER_TOTAL_SIZE(EmbeddedSize) \
    (sizeof(ACPI_PCI_EVAL_INPUT_BUFFER) + (EmbeddedSize))

/*
 * Helper macro to get a pointer to the embedded ACPI evaluation input buffer.
 */
#define ACPI_PCI_EVAL_GET_INPUT_BUFFER(Buffer) \
    ((PVOID)((PUCHAR)(Buffer) + (Buffer)->InputBufferOffset))

/* EOF */
