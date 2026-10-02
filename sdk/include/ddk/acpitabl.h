/*
 * PROJECT:     LiberNT DDK
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ACPI table definitions
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _ACPITABL_H
#define _ACPITABL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <pshpack1.h>
typedef struct _ACPI_PLD_BUFFER {
    UINT32 Revision:7;
    UINT32 IgnoreColor:1;
    UINT32 Color:24;
    UINT32 Width:16;
    UINT32 Height:16;
    UINT32 UserVisible:1;
    UINT32 Dock:1;
    UINT32 Lid:1;
    UINT32 Panel:3;
    UINT32 VerticalPosition:2;
    UINT32 HorizontalPosition:2;
    UINT32 Shape:4;
    UINT32 GroupOrientation:1;
    UINT32 GroupToken:8;
    UINT32 GroupPosition:8;
    UINT32 Bay:1;
    UINT32 Ejectable:1;
    UINT32 EjectionRequired:1;
    UINT32 CabinetNumber:8;
    UINT32 CardCageNumber:8;
    UINT32 Reserved:14;
} ACPI_PLD_BUFFER, *PACPI_PLD_BUFFER;

typedef struct _ACPI_PLD_V2_BUFFER {
    UINT32 Revision:7;
    UINT32 IgnoreColor:1;
    UINT32 Color:24;
    UINT32 Width:16;
    UINT32 Height:16;
    UINT32 UserVisible:1;
    UINT32 Dock:1;
    UINT32 Lid:1;
    UINT32 Panel:3;
    UINT32 VerticalPosition:2;
    UINT32 HorizontalPosition:2;
    UINT32 Shape:4;
    UINT32 GroupOrientation:1;
    UINT32 GroupToken:8;
    UINT32 GroupPosition:8;
    UINT32 Bay:1;
    UINT32 Ejectable:1;
    UINT32 EjectionRequired:1;
    UINT32 CabinetNumber:8;
    UINT32 CardCageNumber:8;
    UINT32 Reference:1;
    UINT32 Rotation:4;
    UINT32 Order:5;
    UINT32 Reserved:4;
    UINT32 VerticalOffset:16;
    UINT32 HorizontalOffset:16;
} ACPI_PLD_V2_BUFFER, *PACPI_PLD_V2_BUFFER;
#include <poppack.h>

#include <pshpack1.h>
typedef enum _ACPI_PLD_PANEL {
    AcpiPldPanelTop     = 0,
    AcpiPldPanelBottom  = 1,
    AcpiPldPanelLeft    = 2,
    AcpiPldPanelRight   = 3,
    AcpiPldPanelFront   = 4,
    AcpiPldPanelBack    = 5,
    AcpiPldPanelUnknown = 6,
} ACPI_PLD_PANEL, *PACPI_PLD_PANEL;
#include <poppack.h>

typedef ACPI_PLD_PANEL AcpiPldPanel;

#include <pshpack1.h>
typedef enum _ACPI_PLD_ROTATION {
    AcpiPldRotation0      = 0,
    AcpiPldRotation45     = 1,
    AcpiPldRotation90     = 2,
    AcpiPldRotation135    = 3,
    AcpiPldRotation180    = 4,
    AcpiPldRotation225    = 5,
    AcpiPldRotation270    = 6,
    AcpiPldRotation315    = 7,
} ACPI_PLD_ROTATION, *PACPI_PLD_ROTATION;
#include <poppack.h>

typedef ACPI_PLD_ROTATION AcpiPldRotation;

#ifdef __cplusplus
}
#endif

#endif
