
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
 * ACPI/PCI device interface. Mirrored from
 * <reactos/drivers/acpi/acpipci.h> here to give every driver that links
 * wdmguid a real definition of GUID_ACPI_PCI_INTERFACE. Without this,
 * release builds (-O2/-O3) strip the DECLSPEC_SELECTANY storage emitted
 * by DEFINE_GUID in each consumer and the link fails with
 * "undefined reference to GUID_ACPI_PCI_INTERFACE".
 */
DEFINE_GUID(GUID_ACPI_PCI_INTERFACE,
  0xA7E9DB84L, 0xDB4C, 0x4289, 0x87, 0x6B, 0xE2, 0xC3, 0xE6, 0x3B, 0x5F, 0x4F);

/* EOF */
