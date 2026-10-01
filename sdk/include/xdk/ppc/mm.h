$if (_NTDDK_)
/*
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Windows NT PowerPC memory manager boundaries
 */

extern NTKERNELAPI PVOID MmHighestUserAddress;
extern NTKERNELAPI PVOID MmSystemRangeStart;
extern NTKERNELAPI ULONG_PTR MmUserProbeAddress;

#define MM_HIGHEST_USER_ADDRESS MmHighestUserAddress
#define MM_SYSTEM_RANGE_START   MmSystemRangeStart
#if defined(_LOCAL_COPY_USER_PROBE_ADDRESS_)
#define MM_USER_PROBE_ADDRESS _LOCAL_COPY_USER_PROBE_ADDRESS_
extern ULONG_PTR _LOCAL_COPY_USER_PROBE_ADDRESS_;
#else
#define MM_USER_PROBE_ADDRESS MmUserProbeAddress
#endif
#define MM_LOWEST_USER_ADDRESS   ((PVOID)0x10000)
#define MM_KSEG0_BASE            MM_SYSTEM_RANGE_START
#define MM_SYSTEM_SPACE_END      0xFFFFFFFF
#define MM_LOWEST_SYSTEM_ADDRESS ((PVOID)0xC0800000)

$endif /* _NTDDK_ */
