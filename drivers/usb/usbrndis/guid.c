/*
 * PROJECT:     ReactOS USB RNDIS Network Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     GUID definitions
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

/* Need to include NT types before USB headers */
#ifndef NDIS_MINIPORT_DRIVER
#define NDIS_MINIPORT_DRIVER
#endif
#ifndef NDIS51_MINIPORT
#define NDIS51_MINIPORT 1
#endif

#include <ndis.h>
#include <initguid.h>
#include <usbdi.h>
#include <usbbusif.h>
