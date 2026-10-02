/*
 * PROJECT:     LiberNT DDK
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Smart card driver library, Windows NT definitions
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _SMCNT_
#define _SMCNT_

#ifdef __cplusplus
extern "C" {
#endif

#define SMCLIB_NT 1

typedef struct _OS_DEP_DATA {
    PDEVICE_OBJECT DeviceObject;
    PIRP CurrentIrp;
    PIRP NotificationIrp;
    KMUTANT Mutex;
    KSPIN_LOCK SpinLock;
    struct {
        BOOLEAN Removed;
        LONG RefCount;
        KEVENT RemoveEvent;
        LIST_ENTRY TagList;
    } RemoveLock;
#ifdef DEBUG_INTERFACE
    PDEVICE_OBJECT DebugDeviceObject;
#endif
} OS_DEP_DATA, *POS_DEP_DATA;

#ifdef POOL_TAGGING
#ifndef ExAllocatePool
#define ExAllocatePool(a,b) ExAllocatePoolWithTag(a,b, SMARTCARD_POOL_TAG)
#else
#undef ExAllocatePool
#define ExAllocatePool(a,b) ExAllocatePoolWithTag(a,b, SMARTCARD_POOL_TAG)
#endif
#endif

#ifdef __cplusplus
}
#endif

#endif
