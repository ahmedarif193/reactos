/*
 * PROJECT:     ReactOS Kernel&Driver SDK
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Hardware Resources Arbiter Library
 * COPYRIGHT:   Copyright 2020 Vadim Galyant <vgal@rambler.ru>
 * COPYRIGHT:   Copyright 2025 Ahmed ARIF <arif.ing@outlook.com>
 *
 * ARM64 COMPATIBILITY NOTES:
 *   This arbiter library is designed to be architecture-agnostic where possible.
 *   ARM64 systems do not use x86-style I/O ports (0xCF8-0xCFF for PCI config).
 *   Architecture-specific reserved ranges are handled conditionally.
 */

#pragma once

#define ARBITER_SIGNATURE  'sbrA'
#define TAG_ARBITER        'MbrA'
#define TAG_ARB_ALLOCATION 'AbrA'
#define TAG_ARB_RANGE      'RbrA'

/* ============================================================================================
   SCORING HEURISTIC CONSTANTS

   These constants control the scoring algorithm used to prioritize resource alternatives.
   Higher scores indicate more preferred alternatives.

   ARB_SCORE_WINDOW_DIVISOR:
     Penalty divisor for large address windows. A request spanning a huge range
     (e.g., 0x0000-0xFFFF) is less constrained than one with a tight window.
     Divide window slack by this value to compute penalty. Larger values mean
     less penalty for wide windows.

   ARB_SCORE_ALIGNMENT_DIVISOR:
     Penalty divisor for coarse alignment requirements. Requests requiring
     high alignment (e.g., 4096-byte aligned) are harder to satisfy.
     Divide alignment by this value to compute penalty.

   Rationale: These values were chosen empirically to balance between
   prioritizing tightly-constrained requests (which have fewer placement options)
   while not over-penalizing reasonable alignment requirements.
   ========================================================================================== */
#ifndef ARB_SCORE_WINDOW_DIVISOR
#define ARB_SCORE_WINDOW_DIVISOR    4096ULL
#endif

#ifndef ARB_SCORE_ALIGNMENT_DIVISOR
#define ARB_SCORE_ALIGNMENT_DIVISOR 16ULL
#endif

/* ============================================================================================
   ARCHITECTURE-SPECIFIC CONSTANTS

   ARM64 does not use x86-style I/O port addressing for PCI configuration.
   Instead, ARM64 systems typically use memory-mapped configuration space (ECAM).
   ========================================================================================== */
#if defined(_M_IX86) || defined(_M_AMD64) || defined(_X86_) || defined(_AMD64_)
#define ARB_ARCH_HAS_IO_PORTS       1
#define ARB_PCI_CONFIG_PORT_START   0x0CF8ULL
#define ARB_PCI_CONFIG_PORT_END     0x0CFFULL
#else
/* ARM64, ARM32, RISC-V, etc. do not have x86-style I/O ports */
#define ARB_ARCH_HAS_IO_PORTS       0
#endif

/* ============================================================================================
   ASSIGNMENT OWNERSHIP FLAGS

   These flags are stored in the WorkSpace field (low bits) of ARBITER_LIST_ENTRY
   to track whether the arbiter allocated the Assignment descriptor.

   IMPORTANT: When the arbiter allocates an Assignment descriptor (because Entry
   did not provide one), it sets ARB_WORKSPACE_OWNS_ASSIGNMENT in WorkSpace.
   This flag MUST be checked before freeing the Assignment to avoid:
     1. Use-after-free: Freeing memory the caller expects to remain valid
     2. Double-free: Freeing memory that was provided by the caller
     3. Memory leak: Forgetting to free arbiter-allocated memory
   ========================================================================================== */
#define ARB_WORKSPACE_OWNS_ASSIGNMENT   0x0001UL
#define ARB_WORKSPACE_FLAG_MASK         0x000FFUL  /* Reserved bits for flags */

typedef struct _ARBITER_ORDERING
{
    UINT64 Start;
    UINT64 End;
} ARBITER_ORDERING, *PARBITER_ORDERING;

typedef struct _ARBITER_ORDERING_LIST
{
    UINT16 Count;
    UINT16 Maximum;
    PARBITER_ORDERING Orderings;
} ARBITER_ORDERING_LIST, *PARBITER_ORDERING_LIST;

/* Internal alternative descriptor used by the arbiter core.
 * Do NOT expose in public headers; public ARBITER_LIST_ENTRY.Alternatives
 * remains PIO_RESOURCE_DESCRIPTOR.
 */
typedef struct _ARBITER_ALTERNATIVE {
    ULONGLONG Minimum;
    ULONGLONG Maximum;
    ULONG Length;
    ULONG Alignment;
    INT32 Priority;
    ULONG Flags;
    PIO_RESOURCE_DESCRIPTOR Descriptor;
} ARBITER_ALTERNATIVE, *PARBITER_ALTERNATIVE;

typedef struct _ARBITER_ALLOCATION_STATE
{
    UINT64 Start;
    UINT64 End;
    UINT64 CurrentMinimum;
    UINT64 CurrentMaximum;
    PARBITER_LIST_ENTRY Entry;
    PARBITER_ALTERNATIVE CurrentAlternative;
    UINT32 AlternativeCount;
    PARBITER_ALTERNATIVE Alternatives;
    UINT16 Flags;
    UCHAR RangeAttributes;
    UCHAR RangeAvailableAttributes;
    ULONG_PTR WorkSpace;
} ARBITER_ALLOCATION_STATE, *PARBITER_ALLOCATION_STATE;

typedef struct _ARBITER_INSTANCE *PARBITER_INSTANCE;

/*
 * Note: ARBITER_CONFLICT_INFO is defined in ntddk.h with the following structure:
 *
 * typedef struct _ARBITER_CONFLICT_INFO {
 *     PDEVICE_OBJECT OwningObject;
 *     ULONGLONG Start;
 *     ULONGLONG End;
 * } ARBITER_CONFLICT_INFO, *PARBITER_CONFLICT_INFO;
 *
 * We do not redefine it here to avoid conflicts.
 */

typedef NTSTATUS
(NTAPI * PARB_UNPACK_REQUIREMENT)(
    _In_ PIO_RESOURCE_DESCRIPTOR IoDescriptor,
    _Out_ PUINT64 OutMinimumAddress,
    _Out_ PUINT64 OutMaximumAddress,
    _Out_ PULONG OutLength,
    _Out_ PULONG OutAlignment
);

typedef NTSTATUS
(NTAPI * PARB_PACK_RESOURCE)(
    _In_ PIO_RESOURCE_DESCRIPTOR IoDescriptor,
    _In_ UINT64 Start,
    _Out_ PCM_PARTIAL_RESOURCE_DESCRIPTOR CmDescriptor
);

typedef NTSTATUS
(NTAPI * PARB_UNPACK_RESOURCE)(
    _In_ PCM_PARTIAL_RESOURCE_DESCRIPTOR CmDescriptor,
    _Out_ PUINT64 Start,
    _Out_ PULONG OutLength
);

typedef INT32
(NTAPI * PARB_SCORE_REQUIREMENT)(
    _In_ PIO_RESOURCE_DESCRIPTOR IoDescriptor
);

typedef NTSTATUS
(NTAPI * PARB_TEST_ALLOCATION)(
    _In_ PARBITER_INSTANCE Arbiter,
    _In_ PLIST_ENTRY ArbitrationList
);

typedef NTSTATUS
(NTAPI * PARB_RETEST_ALLOCATION)(
    _In_ PARBITER_INSTANCE Arbiter,
    _In_ PLIST_ENTRY ArbitrationList
);

typedef NTSTATUS
(NTAPI * PARB_COMMIT_ALLOCATION)(
    _In_ PARBITER_INSTANCE Arbiter
);

typedef NTSTATUS
(NTAPI * PARB_ROLLBACK_ALLOCATION)(
    _In_ PARBITER_INSTANCE Arbiter
);

typedef NTSTATUS
(NTAPI * PARB_BOOT_ALLOCATION)(
    _In_ PARBITER_INSTANCE Arbiter,
    _In_ PLIST_ENTRY ArbitrationList
);

/*
 * PARB_QUERY_ARBITRATE - Query whether arbitration is possible for a device
 *
 * This callback queries whether the arbiter can satisfy an arbitration request.
 *
 * NOTE: The default implementation (ArbQueryArbitrate) uses a simplified signature
 * for backward compatibility. More sophisticated implementations should use
 * TestAllocation with the full ArbitrationList parameter.
 *
 * Parameters:
 *   Arbiter - Pointer to the arbiter instance
 *
 * Returns:
 *   STATUS_SUCCESS if arbitration is possible
 *   STATUS_NOT_SUPPORTED for the default implementation
 */
typedef NTSTATUS
(NTAPI * PARB_QUERY_ARBITRATE)(
    _In_ PARBITER_INSTANCE Arbiter
);

/*
 * PARB_QUERY_CONFLICT - Query conflicts for a specific resource request
 *
 * This callback determines what resources conflict with a proposed allocation.
 * Used by PnP manager to report why a resource request cannot be satisfied.
 *
 * NOTE: The default implementation (ArbQueryConflict) uses a simplified signature
 * for backward compatibility. More sophisticated implementations should provide
 * their own callback with the full parameter set.
 *
 * Parameters:
 *   Arbiter - Pointer to the arbiter instance
 *
 * Returns:
 *   STATUS_SUCCESS if query completed (even if no conflicts)
 *   STATUS_NOT_SUPPORTED for the default implementation
 */
typedef NTSTATUS
(NTAPI * PARB_QUERY_CONFLICT)(
    _In_ PARBITER_INSTANCE Arbiter
);

/*
 * PARB_ADD_RESERVED - Add architecture/bus-specific reserved ranges
 *
 * This callback adds reserved resource ranges that should not be allocated
 * to devices. Examples include:
 *   - x86: PCI Type-1 config ports 0xCF8-0xCFF
 *   - x86: Legacy VGA aperture 0xA0000-0xBFFFF
 *   - ARM64: Platform-specific MMIO regions from ACPI/DT
 *
 * Parameters:
 *   Arbiter - Pointer to the arbiter instance
 *
 * Returns:
 *   STATUS_SUCCESS on success
 *   STATUS_INSUFFICIENT_RESOURCES if range list allocation fails
 */
typedef NTSTATUS
(NTAPI * PARB_ADD_RESERVED)(
    _In_ PARBITER_INSTANCE Arbiter
);

/*
 * PARB_START_ARBITER - Initialize and start the arbiter
 *
 * This callback is called when the arbiter should begin operation.
 * Typically called during device start (IRP_MN_START_DEVICE) processing.
 * Should ensure reserved ranges are added and arbiter is ready to process
 * allocation requests.
 *
 * NOTE: The default implementation (ArbStartArbiter) uses a simplified signature.
 * More sophisticated implementations that need context should provide their own.
 *
 * Parameters:
 *   Arbiter - Pointer to the arbiter instance
 *
 * Returns:
 *   STATUS_SUCCESS if arbiter started successfully
 *   Appropriate error code on failure
 */
typedef NTSTATUS
(NTAPI * PARB_START_ARBITER)(
    _In_ PARBITER_INSTANCE Arbiter
);

NTSTATUS
NTAPI
ArbCommitAllocation(
    _In_ PARBITER_INSTANCE Arbiter);

NTSTATUS
NTAPI
ArbRollbackAllocation(
    _In_ PARBITER_INSTANCE Arbiter);

NTSTATUS
NTAPI
ArbBootAllocation(
    _In_ PARBITER_INSTANCE Arbiter,
    _In_ PLIST_ENTRY ArbitrationList);

typedef NTSTATUS
(NTAPI * PARB_PREPROCESS_ENTRY)(
    _In_ PARBITER_INSTANCE Arbiter,
    _Inout_ PARBITER_ALLOCATION_STATE ArbState
);

typedef NTSTATUS
(NTAPI * PARB_ALLOCATE_ENTRY)(
    _In_ PARBITER_INSTANCE Arbiter,
    _Inout_ PARBITER_ALLOCATION_STATE ArbState
);

typedef BOOLEAN
(NTAPI * PARB_GET_NEXT_ALLOCATION_RANGE)(
    _In_ PARBITER_INSTANCE Arbiter,
    _Inout_ PARBITER_ALLOCATION_STATE ArbState
);

typedef BOOLEAN
(NTAPI * PARB_FIND_SUITABLE_RANGE)(
    _In_ PARBITER_INSTANCE Arbiter,
    _Inout_ PARBITER_ALLOCATION_STATE ArbState
);

typedef VOID
(NTAPI * PARB_ADD_ALLOCATION)(
    _In_ PARBITER_INSTANCE Arbiter,
    _Inout_ PARBITER_ALLOCATION_STATE ArbState
);

typedef VOID
(NTAPI * PARB_BACKTRACK_ALLOCATION)(
    _In_ PARBITER_INSTANCE Arbiter,
    _Inout_ PARBITER_ALLOCATION_STATE ArbState
);

/*
 * PARB_OVERRIDE_CONFLICT - Attempt to resolve resource conflicts
 *
 * This callback is invoked when a conflict is detected during arbitration.
 * It allows the arbiter to attempt resolution strategies such as:
 *   - Relocating existing allocations
 *   - Negotiating with conflicting owners
 *   - Using alternate resource ranges
 *
 * NOTE: The default implementation (ArbOverrideConflict) uses a simplified
 * signature and returns STATUS_ARBITRATION_UNHANDLED. More sophisticated
 * implementations should provide their own callback with full context.
 *
 * Parameters:
 *   Arbiter - Pointer to the arbiter instance
 *
 * Returns:
 *   STATUS_SUCCESS if conflict was resolved
 *   STATUS_ARBITRATION_UNHANDLED if conflict cannot be resolved
 */
typedef NTSTATUS
(NTAPI * PARB_OVERRIDE_CONFLICT)(
    _In_ PARBITER_INSTANCE Arbiter
);

/* Conflict callback function pointer type for type safety */
typedef NTSTATUS
(NTAPI * PARB_CONFLICT_CALLBACK)(
    _In_ PVOID Context,
    _In_opt_ PCM_PARTIAL_RESOURCE_DESCRIPTOR ConflictingResource
);

typedef struct _ARBITER_INSTANCE
{
    UINT32 Signature;
    PKEVENT MutexEvent;
    PCWSTR Name;
    CM_RESOURCE_TYPE ResourceType;
    PRTL_RANGE_LIST Allocation;
    PRTL_RANGE_LIST PossibleAllocation;
    ARBITER_ORDERING_LIST OrderingList;
    ARBITER_ORDERING_LIST ReservedList;
    INT32 ReferenceCount;
    PARBITER_INTERFACE Interface;
    UINT32 AllocationStackMaxSize;
    PARBITER_ALLOCATION_STATE AllocationStack;
    PARB_UNPACK_REQUIREMENT UnpackRequirement;
    PARB_PACK_RESOURCE PackResource;
    PARB_UNPACK_RESOURCE UnpackResource;
    PARB_SCORE_REQUIREMENT ScoreRequirement;
    PARB_TEST_ALLOCATION TestAllocation;
    PARB_RETEST_ALLOCATION RetestAllocation;
    PARB_COMMIT_ALLOCATION CommitAllocation;
    PARB_ROLLBACK_ALLOCATION RollbackAllocation;
    PARB_BOOT_ALLOCATION BootAllocation;
    PARB_QUERY_ARBITRATE QueryArbitrate; // Not used yet
    PARB_QUERY_CONFLICT QueryConflict; // Not used yet
    PARB_ADD_RESERVED AddReserved; // Not used yet
    PARB_START_ARBITER StartArbiter; // Not used yet
    PARB_PREPROCESS_ENTRY PreprocessEntry;
    PARB_ALLOCATE_ENTRY AllocateEntry;
    PARB_GET_NEXT_ALLOCATION_RANGE GetNextAllocationRange;
    PARB_FIND_SUITABLE_RANGE FindSuitableRange;
    PARB_ADD_ALLOCATION AddAllocation;
    PARB_BACKTRACK_ALLOCATION BacktrackAllocation;
    PARB_OVERRIDE_CONFLICT OverrideConflict; // Not used yet
    BOOLEAN TransactionInProgress;
    PVOID Extension;
    PDEVICE_OBJECT BusDeviceObject;
    PVOID ConflictCallbackContext;
    PARB_CONFLICT_CALLBACK ConflictCallback; /* Type-safe callback instead of PVOID */
} ARBITER_INSTANCE, *PARBITER_INSTANCE;

typedef NTSTATUS
(NTAPI * PARB_TRANSLATE_ORDERING)(
    _Out_ PIO_RESOURCE_DESCRIPTOR OutIoDescriptor,
    _In_ PIO_RESOURCE_DESCRIPTOR IoDescriptor
);

/*
 * ArbInitializeArbiterInstance - Initialize an arbiter instance
 *
 * NOTE: This function is NOT in PAGE segment because it is called during
 * system boot initialization which may occur at elevated IRQL (DISPATCH_LEVEL)
 * on ARM64. The function uses NonPagedPool for all allocations.
 */
NTSTATUS
NTAPI
ArbInitializeArbiterInstance(
    _Inout_ PARBITER_INSTANCE Arbiter,
    _In_ PDEVICE_OBJECT BusDeviceObject,
    _In_ CM_RESOURCE_TYPE ResourceType,
    _In_ PCWSTR ArbiterName,
    _In_ PCWSTR OrderName,
    _In_ PARB_TRANSLATE_ORDERING TranslateOrderingFunction
);

/*
 * ArbDestroyArbiterInstance - Clean up and destroy an arbiter instance
 *
 * This function releases all resources allocated by ArbInitializeArbiterInstance:
 *   - MutexEvent (NonPagedPool KEVENT)
 *   - AllocationStack (PagedPool)
 *   - Allocation range list (PagedPool RTL_RANGE_LIST)
 *   - PossibleAllocation range list (PagedPool RTL_RANGE_LIST)
 *   - OrderingList and ReservedList arrays
 *
 * IMPORTANT: Caller must ensure no outstanding allocations reference this arbiter
 * before calling this function. The arbiter lock is NOT held during cleanup
 * as the instance is being destroyed.
 *
 * Parameters:
 *   Arbiter - Pointer to the arbiter instance to destroy
 *
 * Returns:
 *   None. Arbiter structure is zeroed after cleanup.
 */
CODE_SEG("PAGE")
VOID
NTAPI
ArbDestroyArbiterInstance(
    _Inout_ PARBITER_INSTANCE Arbiter
);

/*
 * ArbAcquireArbiterLock - Acquire arbiter instance lock
 *
 * Acquires the mutex event protecting arbiter state.
 * Must be paired with ArbReleaseArbiterLock.
 */
CODE_SEG("PAGE")
VOID
NTAPI
ArbAcquireArbiterLock(
    _In_ PARBITER_INSTANCE Arbiter
);

/*
 * ArbReleaseArbiterLock - Release arbiter instance lock
 */
CODE_SEG("PAGE")
VOID
NTAPI
ArbReleaseArbiterLock(
    _In_ PARBITER_INSTANCE Arbiter
);

/*
 * ArbFreeArbiterAllocations - Free arbiter-allocated Assignment descriptors
 *
 * Iterates through an arbitration list and frees any Assignment descriptors
 * that were allocated by the arbiter. Call this after ArbRollbackAllocation
 * or when abandoning an allocation attempt.
 *
 * IMPORTANT: This only frees assignments allocated by the arbiter (tracked
 * via ARB_WORKSPACE_OWNS_ASSIGNMENT). Caller-provided assignments are NOT freed.
 */
CODE_SEG("PAGE")
VOID
NTAPI
ArbFreeArbiterAllocations(
    _In_ PLIST_ENTRY ArbitrationList
);
