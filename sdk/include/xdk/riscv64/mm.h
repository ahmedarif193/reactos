$if (_NTDDK_)
/*
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 */


/* Public kernel MM boundaries for the provisional Sv39 layout (ABI-052). */
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
#define MM_LOWEST_USER_ADDRESS   ((PVOID)0x0000000000010000ULL)
#define MM_LOWEST_SYSTEM_ADDRESS ((PVOID)0xFFFFFFC000000000ULL)

$endif /* _NTDDK_ */
