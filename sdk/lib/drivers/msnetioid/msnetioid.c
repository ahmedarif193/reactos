/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Network identifier constants provided by msnetioid.lib
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntddk.h>
#include <netiodef.h>

CONST DL_EUI48 eui48_broadcast = { EUI48_BROADCAST_INIT };
