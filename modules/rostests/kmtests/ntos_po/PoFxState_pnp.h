/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */
#pragma once

#define IOCTL_KMTEST_PNP_VETO CTL_CODE(FILE_DEVICE_UNKNOWN, 0x900, METHOD_BUFFERED, FILE_READ_DATA | FILE_WRITE_DATA)

typedef struct _KMT_PNP_VETO_STATE
{
    ULONG Veto;
    ULONG Queries;
    ULONG Cancels;
    ULONG Removes;
    ULONG Started;
} KMT_PNP_VETO_STATE;

#ifdef KMT_KERNEL_MODE
VOID KmtPoFxInitializePnp(PDRIVER_OBJECT DriverObject, PDEVICE_OBJECT ControlDevice);
PDEVICE_OBJECT KmtPoFxAcquireDevice(PDEVICE_OBJECT *Pdo);
VOID KmtPoFxReleaseDevice(PDEVICE_OBJECT Fdo);
#endif
