
#include <stdarg.h>

#define COM_NO_WINDOWS_H
#include "initguid.h"

#include <wdmguid.h>
#include <umpnpmgr/sysguid.h>

/* FIXME: shouldn't go there! */
DEFINE_GUID(GUID_DEVICE_SYS_BUTTON,
  0x4AFA3D53L, 0x74A7, 0x11d0, 0xbe, 0x5e, 0x00, 0xA0, 0xC9, 0x06, 0x28, 0x57);
DEFINE_GUID(GUID_DEVINTERFACE_DISK,
  0x53f56307L, 0xb6bf, 0x11d0, 0x94, 0xf2, 0x00, 0xa0, 0xc9, 0x1e, 0xfb, 0x8b);
DEFINE_GUID(GUID_DEVINTERFACE_CDROM,
  0x53f56308L, 0xb6bf, 0x11d0, 0x94, 0xf2, 0x00, 0xa0, 0xc9, 0x1e, 0xfb, 0x8b);
DEFINE_GUID(GUID_DEVINTERFACE_PARTITION,
  0x53f5630aL, 0xb6bf, 0x11d0, 0x94, 0xf2, 0x00, 0xa0, 0xc9, 0x1e, 0xfb, 0x8b);

/*
 * Display device interfaces, declared by <ntddvdeo.h>.  Both are needed to
 * find a display adapter's devnode from user mode: the arrival interface is
 * the one a WDDM display port registers, and the adapter interface is the
 * older name a legacy miniport registers.
 */
DEFINE_GUID(GUID_DISPLAY_DEVICE_ARRIVAL,
  0x1ca05180L, 0xa699, 0x450a, 0x9a, 0x0c, 0xde, 0x4f, 0xbe, 0x3d, 0xdd, 0x89);
DEFINE_GUID(GUID_DEVINTERFACE_DISPLAY_ADAPTER,
  0x5b45201dL, 0xf2f2, 0x4f3b, 0x85, 0xbb, 0x30, 0xff, 0x1f, 0x95, 0x35, 0x99);

/*
 * ACPI/PCI device interface. Mirrored from
 * <reactos/drivers/acpi/acpipci.h> here to give every driver that links
 * wdmguid a real definition of GUID_ACPI_PCI_INTERFACE. Without this,
 * release builds (-O2/-O3) strip the DECLSPEC_SELECTANY storage emitted
 * by DEFINE_GUID in each consumer and the link fails with
 * "undefined reference to GUID_ACPI_PCI_INTERFACE".
 */
DEFINE_GUID(GUID_ACPI_PCI_INTERFACE,
  0xA7E9DB84L, 0xDB4C, 0x4289, 0x87, 0x6B, 0xE2, 0xC3, 0xE6, 0x3B, 0x5F, 0x4F);

/* ACPI system table device interface from acpisystem.h. */
DEFINE_GUID(GUID_ACPI_SYSTEM_INTERFACE,
  0x4B4B1E0FL, 0x4EBF, 0x46A7, 0x92, 0xA5, 0xDB, 0x27, 0xD3, 0xB9, 0xB6, 0xA1);

/* ReactOS TPM 2.0 device interface from tpm2.h. */
DEFINE_GUID(GUID_DEVINTERFACE_REACTOS_TPM2,
  0xC43A14B0L, 0x914F, 0x4D3C, 0x9F, 0x1A, 0x16, 0xB9, 0xE6, 0x0D, 0x79, 0xBC);

/* EOF */
