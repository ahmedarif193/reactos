/*
 * PROJECT:         ReactOS ACPI Subsystem
 * LICENSE:         GPL-2.0-or-later - See COPYING in the top level directory
 * FILE:            drivers/bus/acpi/acpica/include/rosmem.h
 * PURPOSE:         Temporary ACPICA memory instrumentation wrappers
 * COPYRIGHT:       Copyright (c) Ahmed ARIF (arif.ing@outlook.com)
 */
#pragma once

/* FIXME: remove once the offending memset/memcpy caller that corrupts the
 *        second root peer namespace node has been identified.
 */
#include <stddef.h>

extern void *AcpiRos_SuspectNode;
extern size_t AcpiRos_SuspectNodeSize;

void *AcpiRosMemcpy(void *dst, const void *src, size_t n, void *retaddr);
void *AcpiRosMemmove(void *dst, const void *src, size_t n, void *retaddr);
void *AcpiRosMemset(void *dst, int c, size_t n, void *retaddr);

#ifndef ACPI_ROS_MEMWRAP_DISABLED
#define memcpy(d,s,n)   AcpiRosMemcpy((d),(s),(n), __builtin_return_address(0))
#define memmove(d,s,n)  AcpiRosMemmove((d),(s),(n), __builtin_return_address(0))
#define memset(d,c,n)   AcpiRosMemset((d),(c),(n), __builtin_return_address(0))
#endif
