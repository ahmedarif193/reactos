/*
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 */

/* ReactOS RV64 memory types. Hardware fields follow the supervisor ISA;
 * software paging encodings are private to the port. */
#ifndef _RISCV64_MMTYPES_H
#define _RISCV64_MMTYPES_H

#define PAGE_SIZE                       0x1000
#define PAGE_SHIFT                      12L
#define MM_ALLOCATION_GRANULARITY       0x10000
#define MM_ALLOCATION_GRANULARITY_SHIFT 16L
/* Width of a pointer-sized virtual page number, not the hardware PPN. */
#define MM_PAGE_FRAME_NUMBER_SIZE       (64 - PAGE_SHIFT)

/* Sv39 low-half user limit selected by ABI-052. Keep this compile-time value
 * identical to the kernel's initial MmHighestUserAddress value. */
#define MI_HIGHEST_USER_ADDRESS         ((PVOID)0x0000003FFFFEFFFFULL)

#ifdef __cplusplus
extern "C" {
#endif

/* MM initialization supplies these boundaries after choosing the Sv39 map. */
extern NTSYSAPI PVOID MmHighestUserAddress;
extern NTSYSAPI PVOID MmSystemRangeStart;
extern NTSYSAPI ULONG_PTR MmUserProbeAddress;

#ifdef __cplusplus
}
#endif

#define MM_HIGHEST_USER_ADDRESS MmHighestUserAddress
#define MM_SYSTEM_RANGE_START MmSystemRangeStart
#define MM_USER_PROBE_ADDRESS MmUserProbeAddress

typedef struct _HARDWARE_PTE
{
    ULONG64 Valid : 1;
    ULONG64 Read : 1;
    ULONG64 Write : 1;
    ULONG64 Execute : 1;
    ULONG64 Owner : 1;
    ULONG64 Global : 1;
    ULONG64 Accessed : 1;
    ULONG64 Dirty : 1;
    ULONG64 CopyOnWrite : 1; /* RSW[0], ignored by hardware. */
    ULONG64 SoftwareReserved : 1;
    ULONG64 PageFrameNumber : 44;
    ULONG64 Reserved : 10; /* No Svnapot/Svpbmt encodings in this baseline. */
} HARDWARE_PTE, *PHARDWARE_PTE;

/* Invalid entries are ReactOS-private. Test Valid before interpreting these
 * views: bit 8 is CopyOnWrite in a valid entry and Transition otherwise. */
typedef struct _MMPTE_SOFTWARE
{
    ULONG64 Valid : 1;
    ULONG64 Protection : 5;
    ULONG64 Reserved0 : 2;
    ULONG64 Transition : 1;
    ULONG64 Prototype : 1;
    ULONG64 PageFileLow : 4;
    ULONG64 UsedPageTableEntries : 10;
    ULONG64 Reserved1 : 8;
    ULONG64 PageFileHigh : 32;
} MMPTE_SOFTWARE;

typedef struct _MMPTE_TRANSITION
{
    ULONG64 Valid : 1;
    ULONG64 Protection : 5;
    ULONG64 Reserved0 : 2;
    ULONG64 Transition : 1;
    ULONG64 Prototype : 1;
    ULONG64 PageFrameNumber : 44;
    ULONG64 Reserved1 : 10;
} MMPTE_TRANSITION;

typedef struct _MMPTE_PROTOTYPE
{
    ULONG64 Valid : 1;
    ULONG64 Protection : 5;
    ULONG64 ReadOnly : 1;
    ULONG64 Reserved0 : 1;
    ULONG64 Transition : 1;
    ULONG64 Prototype : 1;
    ULONG64 Reserved1 : 6;
    LONG64 ProtoAddress : 48;
} MMPTE_PROTOTYPE;

typedef struct _MMPTE_SUBSECTION
{
    ULONG64 Valid : 1;
    ULONG64 Protection : 5;
    ULONG64 Reserved0 : 2;
    ULONG64 Transition : 1;
    ULONG64 Prototype : 1;
    ULONG64 Reserved1 : 6;
    LONG64 SubsectionAddress : 48;
} MMPTE_SUBSECTION;

typedef struct _MMPTE_LIST
{
    ULONG64 Valid : 1;
    ULONG64 Protection : 5;
    ULONG64 OneEntry : 1;
    ULONG64 Reserved0 : 1;
    ULONG64 Transition : 1;
    ULONG64 Prototype : 1;
    ULONG64 Reserved1 : 22;
    ULONG64 NextEntry : 32;
} MMPTE_LIST;

typedef struct _MMPTE
{
    union
    {
        ULONG64 Long;
        HARDWARE_PTE Hard;
        HARDWARE_PTE Flush;
        MMPTE_SOFTWARE Soft;
        MMPTE_TRANSITION Trans;
        MMPTE_PROTOTYPE Proto;
        MMPTE_SUBSECTION Subsect;
        MMPTE_LIST List;
    } u;
} MMPTE, *PMMPTE;

C_ASSERT(sizeof(HARDWARE_PTE) == sizeof(ULONG64));
C_ASSERT(sizeof(MMPTE_SOFTWARE) == sizeof(ULONG64));
C_ASSERT(sizeof(MMPTE_TRANSITION) == sizeof(ULONG64));
C_ASSERT(sizeof(MMPTE_PROTOTYPE) == sizeof(ULONG64));
C_ASSERT(sizeof(MMPTE_SUBSECTION) == sizeof(ULONG64));
C_ASSERT(sizeof(MMPTE_LIST) == sizeof(ULONG64));
C_ASSERT(sizeof(MMPTE) == sizeof(ULONG64));
C_ASSERT(__alignof(MMPTE) == sizeof(ULONG64));

#endif /* _RISCV64_MMTYPES_H */
