/*
 * COPYRIGHT:       2026 Ahmed ARIF (arif.ing@outlook.com)
 * PROJECT:     ReactOS NDIS library
 * FILE:        ndis/miniport6_state.c
 * PURPOSE:     NDIS 6.x Miniport Adapter State Machine
 * PROGRAMMERS: ReactOS Development Team
 * NOTES:       This file implements the NDIS 6.x Pause/Restart state machine
 *              which is critical for PnP (Plug and Play) operations.
 */

#include "ndissys.h"

#if NDIS_SUPPORT_NDIS6

/*
 * Internal structure to track NDIS 6.x adapter state
 * This extends the adapter information stored by the miniport driver block
 */
typedef struct _NDIS6_ADAPTER_STATE_BLOCK {
    LIST_ENTRY ListEntry;
    NDIS_HANDLE MiniportAdapterHandle;
    NDIS_HANDLE MiniportAdapterContext;
    NDIS_MINIPORT_ADAPTER_STATE CurrentState;
    NDIS_MINIPORT_ADAPTER_STATE PreviousState;
    ULONG PauseReason;
    KEVENT PauseCompleteEvent;
    KEVENT RestartCompleteEvent;
    NDIS_STATUS LastRestartStatus;
    KSPIN_LOCK StateLock;
    /* Registration attributes */
    ULONG AttributeFlags;
    UINT CheckForHangTimeInSeconds;
    NDIS_INTERFACE_TYPE InterfaceType;
    /* General attributes (stored on first set) */
    BOOLEAN GeneralAttributesSet;
    NDIS_MEDIUM MediaType;
    NDIS_PHYSICAL_MEDIUM PhysicalMediumType;
    ULONG MtuSize;
    NDIS_MEDIA_CONNECT_STATE MediaConnectState;
    NDIS_MEDIA_DUPLEX_STATE MediaDuplexState;
} NDIS6_ADAPTER_STATE_BLOCK, *PNDIS6_ADAPTER_STATE_BLOCK;

/* Global list of NDIS 6.x adapter state blocks */
static LIST_ENTRY Ndis6AdapterStateList;
static KSPIN_LOCK Ndis6AdapterStateListLock;
static BOOLEAN Ndis6AdapterStateInitialized = FALSE;

/*
 * State transition table
 * Defines valid state transitions for the adapter state machine
 */
typedef struct _NDIS6_STATE_TRANSITION {
    NDIS_MINIPORT_ADAPTER_STATE FromState;
    NDIS_MINIPORT_ADAPTER_STATE ToState;
    BOOLEAN Valid;
} NDIS6_STATE_TRANSITION;

static const NDIS6_STATE_TRANSITION StateTransitionTable[] = {
    /* From Unknown */
    { NdisMiniportAdapterStateUnknown, NdisMiniportAdapterStateInitializing, TRUE },
    /* From Initializing */
    { NdisMiniportAdapterStateInitializing, NdisMiniportAdapterStateRunning, TRUE },
    { NdisMiniportAdapterStateInitializing, NdisMiniportAdapterStateHalted, TRUE },
    /* From Running */
    { NdisMiniportAdapterStateRunning, NdisMiniportAdapterStatePausing, TRUE },
    { NdisMiniportAdapterStateRunning, NdisMiniportAdapterStateHalting, TRUE },
    /* From Pausing */
    { NdisMiniportAdapterStatePausing, NdisMiniportAdapterStatePaused, TRUE },
    /* From Paused */
    { NdisMiniportAdapterStatePaused, NdisMiniportAdapterStateRestarting, TRUE },
    { NdisMiniportAdapterStatePaused, NdisMiniportAdapterStateHalting, TRUE },
    /* From Restarting */
    { NdisMiniportAdapterStateRestarting, NdisMiniportAdapterStateRunning, TRUE },
    { NdisMiniportAdapterStateRestarting, NdisMiniportAdapterStatePaused, TRUE },
    /* From Halting */
    { NdisMiniportAdapterStateHalting, NdisMiniportAdapterStateHalted, TRUE },
    /* Terminator */
    { NdisMiniportAdapterStateUnknown, NdisMiniportAdapterStateUnknown, FALSE }
};

/*
 * State name strings for debug logging
 */
static const PCSTR StateNames[] = {
    "Unknown",
    "Initializing",
    "Running",
    "Pausing",
    "Paused",
    "Restarting",
    "Halting",
    "Halted"
};

/*
 * InitializeNdis6AdapterStateSupport
 * Internal function to initialize NDIS 6.x adapter state tracking structures
 */
static VOID
InitializeNdis6AdapterStateSupport(VOID)
{
    if (!Ndis6AdapterStateInitialized)
    {
        InitializeListHead(&Ndis6AdapterStateList);
        KeInitializeSpinLock(&Ndis6AdapterStateListLock);
        Ndis6AdapterStateInitialized = TRUE;
    }
}

/*
 * Ndis6iGetStateName
 * Returns the name string for a given adapter state
 */
static PCSTR
Ndis6iGetStateName(
    _In_ NDIS_MINIPORT_ADAPTER_STATE State)
{
    if (State >= NdisMiniportAdapterStateUnknown &&
        State <= NdisMiniportAdapterStateHalted)
    {
        return StateNames[State];
    }
    return "Invalid";
}

/*
 * Ndis6iValidateStateTransition
 * Validates that a state transition is allowed
 *
 * Parameters:
 *   FromState - Current state
 *   ToState - Target state
 *
 * Returns:
 *   TRUE if the transition is valid, FALSE otherwise
 */
static BOOLEAN
Ndis6iValidateStateTransition(
    _In_ NDIS_MINIPORT_ADAPTER_STATE FromState,
    _In_ NDIS_MINIPORT_ADAPTER_STATE ToState)
{
    ULONG i;

    for (i = 0; StateTransitionTable[i].Valid ||
                StateTransitionTable[i].FromState != NdisMiniportAdapterStateUnknown ||
                StateTransitionTable[i].ToState != NdisMiniportAdapterStateUnknown; i++)
    {
        if (StateTransitionTable[i].FromState == FromState &&
            StateTransitionTable[i].ToState == ToState)
        {
            return StateTransitionTable[i].Valid;
        }
    }

    return FALSE;
}

/*
 * Ndis6iSetAdapterState
 * Sets the adapter state with logging
 *
 * Parameters:
 *   StateBlock - Pointer to the adapter state block
 *   NewState - The new state to set
 *
 * Returns:
 *   NDIS_STATUS_SUCCESS if the state was set
 *   NDIS_STATUS_INVALID_STATE if the transition is invalid
 */
static NDIS_STATUS
Ndis6iSetAdapterState(
    _Inout_ PNDIS6_ADAPTER_STATE_BLOCK StateBlock,
    _In_ NDIS_MINIPORT_ADAPTER_STATE NewState)
{
    KIRQL OldIrql;
    NDIS_MINIPORT_ADAPTER_STATE OldState;

    KeAcquireSpinLock(&StateBlock->StateLock, &OldIrql);

    OldState = StateBlock->CurrentState;

    if (!Ndis6iValidateStateTransition(OldState, NewState))
    {
        DPRINT1("Invalid state transition from %s to %s\n",
            Ndis6iGetStateName(OldState), Ndis6iGetStateName(NewState));
        KeReleaseSpinLock(&StateBlock->StateLock, OldIrql);
        return NDIS_STATUS_INVALID_STATE;
    }

    StateBlock->PreviousState = OldState;
    StateBlock->CurrentState = NewState;

    DPRINT1("Adapter state transition: %s -> %s\n",
        Ndis6iGetStateName(OldState), Ndis6iGetStateName(NewState));

    KeReleaseSpinLock(&StateBlock->StateLock, OldIrql);

    return NDIS_STATUS_SUCCESS;
}

/*
 * Ndis6iFindAdapterStateBlock
 * Finds the state block for a given adapter handle
 *
 * Parameters:
 *   MiniportAdapterHandle - Handle to the miniport adapter
 *
 * Returns:
 *   Pointer to the state block, or NULL if not found
 */
static PNDIS6_ADAPTER_STATE_BLOCK
Ndis6iFindAdapterStateBlock(
    _In_ NDIS_HANDLE MiniportAdapterHandle)
{
    PLIST_ENTRY Entry;
    PNDIS6_ADAPTER_STATE_BLOCK StateBlock;
    KIRQL OldIrql;

    InitializeNdis6AdapterStateSupport();

    KeAcquireSpinLock(&Ndis6AdapterStateListLock, &OldIrql);

    for (Entry = Ndis6AdapterStateList.Flink;
         Entry != &Ndis6AdapterStateList;
         Entry = Entry->Flink)
    {
        StateBlock = CONTAINING_RECORD(Entry, NDIS6_ADAPTER_STATE_BLOCK, ListEntry);
        if (StateBlock->MiniportAdapterHandle == MiniportAdapterHandle)
        {
            KeReleaseSpinLock(&Ndis6AdapterStateListLock, OldIrql);
            return StateBlock;
        }
    }

    KeReleaseSpinLock(&Ndis6AdapterStateListLock, OldIrql);
    return NULL;
}

/*
 * Ndis6iCreateAdapterStateBlock
 * Creates a new state block for an adapter
 *
 * Parameters:
 *   MiniportAdapterHandle - Handle to the miniport adapter
 *
 * Returns:
 *   Pointer to the new state block, or NULL on failure
 */
static PNDIS6_ADAPTER_STATE_BLOCK
Ndis6iCreateAdapterStateBlock(
    _In_ NDIS_HANDLE MiniportAdapterHandle)
{
    PNDIS6_ADAPTER_STATE_BLOCK StateBlock;
    KIRQL OldIrql;

    InitializeNdis6AdapterStateSupport();

    /* Check if one already exists */
    StateBlock = Ndis6iFindAdapterStateBlock(MiniportAdapterHandle);
    if (StateBlock != NULL)
    {
        return StateBlock;
    }

    /* Allocate new state block */
    StateBlock = ExAllocatePoolWithTag(NonPagedPool,
                                       sizeof(NDIS6_ADAPTER_STATE_BLOCK),
                                       NDIS_TAG);
    if (StateBlock == NULL)
    {
        DPRINT1("Failed to allocate adapter state block\n");
        return NULL;
    }

    RtlZeroMemory(StateBlock, sizeof(NDIS6_ADAPTER_STATE_BLOCK));

    StateBlock->MiniportAdapterHandle = MiniportAdapterHandle;
    StateBlock->CurrentState = NdisMiniportAdapterStateUnknown;
    StateBlock->PreviousState = NdisMiniportAdapterStateUnknown;

    KeInitializeSpinLock(&StateBlock->StateLock);
    KeInitializeEvent(&StateBlock->PauseCompleteEvent, NotificationEvent, FALSE);
    KeInitializeEvent(&StateBlock->RestartCompleteEvent, NotificationEvent, FALSE);

    /* Add to global list */
    KeAcquireSpinLock(&Ndis6AdapterStateListLock, &OldIrql);
    InsertTailList(&Ndis6AdapterStateList, &StateBlock->ListEntry);
    KeReleaseSpinLock(&Ndis6AdapterStateListLock, OldIrql);

    DPRINT1("Created adapter state block for handle %p\n",
        MiniportAdapterHandle);

    return StateBlock;
}

/*
 * Ndis6iRemoveAdapterStateBlock
 * Removes and frees a state block
 *
 * Parameters:
 *   MiniportAdapterHandle - Handle to the miniport adapter
 */
static VOID
Ndis6iRemoveAdapterStateBlock(
    _In_ NDIS_HANDLE MiniportAdapterHandle)
{
    PNDIS6_ADAPTER_STATE_BLOCK StateBlock;
    KIRQL OldIrql;

    StateBlock = Ndis6iFindAdapterStateBlock(MiniportAdapterHandle);
    if (StateBlock == NULL)
    {
        return;
    }

    KeAcquireSpinLock(&Ndis6AdapterStateListLock, &OldIrql);
    RemoveEntryList(&StateBlock->ListEntry);
    KeReleaseSpinLock(&Ndis6AdapterStateListLock, OldIrql);

    ExFreePoolWithTag(StateBlock, NDIS_TAG);

    DPRINT1("Removed adapter state block for handle %p\n",
        MiniportAdapterHandle);
}

/*
 * @implemented
 */
VOID
EXPORT
NdisMPauseComplete(
    _In_ NDIS_HANDLE MiniportAdapterHandle)
{
    PNDIS6_ADAPTER_STATE_BLOCK StateBlock;
    NDIS_STATUS Status;

    DPRINT("NdisMPauseComplete called for handle %p\n",
        MiniportAdapterHandle);

    /* Validate handle */
    if (MiniportAdapterHandle == NULL)
    {
        DPRINT1("Invalid adapter handle\n");
        return;
    }

    /* Find the adapter state block */
    StateBlock = Ndis6iFindAdapterStateBlock(MiniportAdapterHandle);
    if (StateBlock == NULL)
    {
        DPRINT1("Adapter state block not found for handle %p\n",
            MiniportAdapterHandle);
        return;
    }

    /* Verify we're in Pausing state */
    if (StateBlock->CurrentState != NdisMiniportAdapterStatePausing)
    {
        DPRINT("NdisMPauseComplete called but adapter is not in Pausing state (current: %s)\n",
            Ndis6iGetStateName(StateBlock->CurrentState));
        return;
    }

    /* Transition to Paused state */
    Status = Ndis6iSetAdapterState(StateBlock, NdisMiniportAdapterStatePaused);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        DPRINT1("Failed to transition to Paused state: 0x%x\n", Status);
        return;
    }

    /* Signal pause complete event */
    KeSetEvent(&StateBlock->PauseCompleteEvent, IO_NO_INCREMENT, FALSE);

    DPRINT1("NdisMPauseComplete: Adapter %p is now Paused\n",
        MiniportAdapterHandle);

    /*
     * TODO: Complete any pending pause IRP
     * In a full implementation, we would:
     * 1. Find the pending pause IRP associated with this adapter
     * 2. Complete the IRP with STATUS_SUCCESS
     * 3. Continue with any follow-up PnP operations (e.g., power state change)
     */
}

/*
 * @implemented
 */
VOID
EXPORT
NdisMRestartComplete(
    _In_ NDIS_HANDLE MiniportAdapterHandle,
    _In_ NDIS_STATUS Status)
{
    PNDIS6_ADAPTER_STATE_BLOCK StateBlock;
    NDIS_MINIPORT_ADAPTER_STATE TargetState;
    NDIS_STATUS TransitionStatus;

    DPRINT("NdisMRestartComplete called for handle %p, Status 0x%x\n",
        MiniportAdapterHandle, Status);

    /* Validate handle */
    if (MiniportAdapterHandle == NULL)
    {
        DPRINT1("Invalid adapter handle\n");
        return;
    }

    /* Find the adapter state block */
    StateBlock = Ndis6iFindAdapterStateBlock(MiniportAdapterHandle);
    if (StateBlock == NULL)
    {
        DPRINT1("Adapter state block not found for handle %p\n",
            MiniportAdapterHandle);
        return;
    }

    /* Verify we're in Restarting state */
    if (StateBlock->CurrentState != NdisMiniportAdapterStateRestarting)
    {
        DPRINT("NdisMRestartComplete called but adapter is not in Restarting state (current: %s)\n",
            Ndis6iGetStateName(StateBlock->CurrentState));
        return;
    }

    /* Store the restart status */
    StateBlock->LastRestartStatus = Status;

    /* Determine target state based on restart status */
    if (Status == NDIS_STATUS_SUCCESS)
    {
        TargetState = NdisMiniportAdapterStateRunning;
        DPRINT1("Restart succeeded, transitioning to Running\n");
    }
    else
    {
        TargetState = NdisMiniportAdapterStatePaused;
        DPRINT1("Restart failed (0x%x), transitioning back to Paused\n", Status);
    }

    /* Transition to target state */
    TransitionStatus = Ndis6iSetAdapterState(StateBlock, TargetState);
    if (TransitionStatus != NDIS_STATUS_SUCCESS)
    {
        DPRINT1("Failed to transition to %s state: 0x%x\n",
            Ndis6iGetStateName(TargetState), TransitionStatus);
        return;
    }

    /* Signal restart complete event */
    KeSetEvent(&StateBlock->RestartCompleteEvent, IO_NO_INCREMENT, FALSE);

    DPRINT1("NdisMRestartComplete: Adapter %p is now %s\n",
        MiniportAdapterHandle, Ndis6iGetStateName(TargetState));

    /*
     * TODO: Complete any pending restart IRP
     * In a full implementation, we would:
     * 1. Find the pending restart IRP associated with this adapter
     * 2. Complete the IRP with the appropriate status
     * 3. If restart failed, potentially trigger device removal
     */
}

/*
 * @implemented
 */
NDIS_STATUS
EXPORT
NdisMSetMiniportAttributes(
    _In_ NDIS_HANDLE NdisMiniportHandle,
    _In_ PNDIS_MINIPORT_ADAPTER_ATTRIBUTES MiniportAttributes)
{
    PNDIS6_ADAPTER_STATE_BLOCK StateBlock;
    PNDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES RegAttrs;
    PNDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES GenAttrs;

    DPRINT("NdisMSetMiniportAttributes called for handle %p\n",
        NdisMiniportHandle);

    /* Validate parameters */
    if (NdisMiniportHandle == NULL || MiniportAttributes == NULL)
    {
        DPRINT1("Invalid parameter\n");
        return NDIS_STATUS_INVALID_PARAMETER;
    }

    /* Get or create state block */
    StateBlock = Ndis6iCreateAdapterStateBlock(NdisMiniportHandle);
    if (StateBlock == NULL)
    {
        DPRINT1("Failed to create/find adapter state block\n");
        return NDIS_STATUS_RESOURCES;
    }

    /* Process based on attribute type (determined by header type) */
    switch (MiniportAttributes->RegistrationAttributes.Header.Type)
    {
        case NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES:
            RegAttrs = &MiniportAttributes->RegistrationAttributes;

            /* Validate revision */
            if (RegAttrs->Header.Revision < NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES_REVISION_1)
            {
                DPRINT1("Invalid registration attributes revision\n");
                return NDIS_STATUS_INVALID_PARAMETER;
            }

            /* Store registration attributes */
            StateBlock->MiniportAdapterContext = RegAttrs->MiniportAdapterContext;
            StateBlock->AttributeFlags = RegAttrs->AttributeFlags;
            StateBlock->CheckForHangTimeInSeconds = RegAttrs->CheckForHangTimeInSeconds;
            StateBlock->InterfaceType = RegAttrs->InterfaceType;

            /*
             * Also store the adapter context in the device extension for use by
             * NdisMIndicateStatusEx, NdisMIndicateReceiveNetBufferLists, etc.
             * Device extension layout for NDIS 6.x:
             *   [0] = DriverBlock
             *   [1] = PDO
             *   [2] = AdapterContext (miniport's context)
             *   [3] = NextDevice
             *   [4] = DmaAdapter
             *   [5] = Resources
             *   [6] = LOGICAL_ADAPTER pointer
             */
            {
                PDEVICE_OBJECT DeviceObject = (PDEVICE_OBJECT)NdisMiniportHandle;
                if (DeviceObject && DeviceObject->DeviceExtension)
                {
                    ((PVOID*)DeviceObject->DeviceExtension)[2] = RegAttrs->MiniportAdapterContext;
                    DPRINT1("NDIS6: NdisMSetMiniportAttributes - stored AdapterContext=%p in DeviceExtension[2]\n",
                             RegAttrs->MiniportAdapterContext);
                }
            }

            /* Transition to Initializing state if in Unknown state */
            if (StateBlock->CurrentState == NdisMiniportAdapterStateUnknown)
            {
                Ndis6iSetAdapterState(StateBlock, NdisMiniportAdapterStateInitializing);
            }

            DPRINT1("Stored registration attributes: Context=%p, Flags=0x%x\n",
                RegAttrs->MiniportAdapterContext, RegAttrs->AttributeFlags);
            break;

        case NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES:
            GenAttrs = &MiniportAttributes->GeneralAttributes;

            /* Validate revision */
            if (GenAttrs->Header.Revision < NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES_REVISION_1)
            {
                DPRINT1("Invalid general attributes revision\n");
                return NDIS_STATUS_INVALID_PARAMETER;
            }

            /* Store general attributes */
            StateBlock->GeneralAttributesSet = TRUE;
            StateBlock->MediaType = GenAttrs->MediaType;
            StateBlock->PhysicalMediumType = GenAttrs->PhysicalMediumType;
            StateBlock->MtuSize = GenAttrs->MtuSize;
            StateBlock->MediaConnectState = GenAttrs->MediaConnectState;
            StateBlock->MediaDuplexState = GenAttrs->MediaDuplexState;

            DPRINT1("Stored general attributes: MediaType=%d, MTU=%lu\n",
                GenAttrs->MediaType, GenAttrs->MtuSize);
            break;

        default:
            DPRINT1("Unsupported attribute type: 0x%x (stub)\n",
                MiniportAttributes->RegistrationAttributes.Header.Type);
            /* For unsupported types, we accept but don't process */
            break;
    }

    return NDIS_STATUS_SUCCESS;
}

/*
 * Ndis6iInitiateAdapterPause
 * Internal function to initiate adapter pause (called by NDIS)
 *
 * Parameters:
 *   MiniportAdapterHandle - Handle to the miniport adapter
 *   PauseReason - Reason for the pause
 *
 * Returns:
 *   NDIS_STATUS_SUCCESS if pause completed synchronously
 *   NDIS_STATUS_PENDING if pause will complete asynchronously
 *   Error status on failure
 */
NDIS_STATUS
Ndis6iInitiateAdapterPause(
    _In_ NDIS_HANDLE MiniportAdapterHandle,
    _In_ ULONG PauseReason)
{
    PNDIS6_ADAPTER_STATE_BLOCK StateBlock;
    NDIS_STATUS Status;

    DPRINT1("Initiating pause for adapter %p, reason 0x%x\n",
        MiniportAdapterHandle, PauseReason);

    StateBlock = Ndis6iFindAdapterStateBlock(MiniportAdapterHandle);
    if (StateBlock == NULL)
    {
        return NDIS_STATUS_INVALID_PARAMETER;
    }

    /* Verify we're in Running state */
    if (StateBlock->CurrentState != NdisMiniportAdapterStateRunning)
    {
        DPRINT1("Cannot pause adapter - not in Running state (current: %s)\n",
            Ndis6iGetStateName(StateBlock->CurrentState));
        return NDIS_STATUS_INVALID_STATE;
    }

    /* Record pause reason */
    StateBlock->PauseReason = PauseReason;

    /* Reset pause complete event */
    KeClearEvent(&StateBlock->PauseCompleteEvent);

    /* Transition to Pausing state */
    Status = Ndis6iSetAdapterState(StateBlock, NdisMiniportAdapterStatePausing);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        return Status;
    }

    /*
     * TODO: Call the miniport's PauseHandler
     * In a full implementation:
     * 1. Look up the miniport driver block
     * 2. Get the PauseHandler from the characteristics
     * 3. Call the handler with appropriate parameters
     * 4. Return the handler's status
     */

    return NDIS_STATUS_PENDING;
}

/*
 * Ndis6iInitiateAdapterRestart
 * Internal function to initiate adapter restart (called by NDIS)
 *
 * Parameters:
 *   MiniportAdapterHandle - Handle to the miniport adapter
 *
 * Returns:
 *   NDIS_STATUS_SUCCESS if restart completed synchronously
 *   NDIS_STATUS_PENDING if restart will complete asynchronously
 *   Error status on failure
 */
NDIS_STATUS
Ndis6iInitiateAdapterRestart(
    _In_ NDIS_HANDLE MiniportAdapterHandle)
{
    PNDIS6_ADAPTER_STATE_BLOCK StateBlock;
    NDIS_STATUS Status;

    DPRINT1("Initiating restart for adapter %p\n",
        MiniportAdapterHandle);

    StateBlock = Ndis6iFindAdapterStateBlock(MiniportAdapterHandle);
    if (StateBlock == NULL)
    {
        return NDIS_STATUS_INVALID_PARAMETER;
    }

    /* Verify we're in Paused state */
    if (StateBlock->CurrentState != NdisMiniportAdapterStatePaused)
    {
        DPRINT1("Cannot restart adapter - not in Paused state (current: %s)\n",
            Ndis6iGetStateName(StateBlock->CurrentState));
        return NDIS_STATUS_INVALID_STATE;
    }

    /* Reset restart complete event */
    KeClearEvent(&StateBlock->RestartCompleteEvent);

    /* Transition to Restarting state */
    Status = Ndis6iSetAdapterState(StateBlock, NdisMiniportAdapterStateRestarting);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        return Status;
    }

    /*
     * TODO: Call the miniport's RestartHandler
     * In a full implementation:
     * 1. Look up the miniport driver block
     * 2. Get the RestartHandler from the characteristics
     * 3. Prepare restart parameters
     * 4. Call the handler
     * 5. Return the handler's status
     */

    return NDIS_STATUS_PENDING;
}

/*
 * Ndis6iGetAdapterState
 * Internal function to get the current adapter state
 *
 * Parameters:
 *   MiniportAdapterHandle - Handle to the miniport adapter
 *
 * Returns:
 *   Current adapter state, or NdisMiniportAdapterStateUnknown if not found
 */
NDIS_MINIPORT_ADAPTER_STATE
Ndis6iGetAdapterState(
    _In_ NDIS_HANDLE MiniportAdapterHandle)
{
    PNDIS6_ADAPTER_STATE_BLOCK StateBlock;
    NDIS_MINIPORT_ADAPTER_STATE State;
    KIRQL OldIrql;

    StateBlock = Ndis6iFindAdapterStateBlock(MiniportAdapterHandle);
    if (StateBlock == NULL)
    {
        return NdisMiniportAdapterStateUnknown;
    }

    KeAcquireSpinLock(&StateBlock->StateLock, &OldIrql);
    State = StateBlock->CurrentState;
    KeReleaseSpinLock(&StateBlock->StateLock, OldIrql);

    return State;
}

/*
 * Ndis6iSetAdapterRunning
 * Internal function to transition adapter to Running state after initialization
 *
 * Parameters:
 *   MiniportAdapterHandle - Handle to the miniport adapter
 *
 * Returns:
 *   NDIS_STATUS_SUCCESS on success
 */
NDIS_STATUS
Ndis6iSetAdapterRunning(
    _In_ NDIS_HANDLE MiniportAdapterHandle)
{
    PNDIS6_ADAPTER_STATE_BLOCK StateBlock;

    StateBlock = Ndis6iFindAdapterStateBlock(MiniportAdapterHandle);
    if (StateBlock == NULL)
    {
        return NDIS_STATUS_INVALID_PARAMETER;
    }

    return Ndis6iSetAdapterState(StateBlock, NdisMiniportAdapterStateRunning);
}

/*
 * Ndis6iSetAdapterHalting
 * Internal function to transition adapter to Halting state
 *
 * Parameters:
 *   MiniportAdapterHandle - Handle to the miniport adapter
 *
 * Returns:
 *   NDIS_STATUS_SUCCESS on success
 */
NDIS_STATUS
Ndis6iSetAdapterHalting(
    _In_ NDIS_HANDLE MiniportAdapterHandle)
{
    PNDIS6_ADAPTER_STATE_BLOCK StateBlock;

    StateBlock = Ndis6iFindAdapterStateBlock(MiniportAdapterHandle);
    if (StateBlock == NULL)
    {
        return NDIS_STATUS_INVALID_PARAMETER;
    }

    return Ndis6iSetAdapterState(StateBlock, NdisMiniportAdapterStateHalting);
}

/*
 * Ndis6iSetAdapterHalted
 * Internal function to transition adapter to Halted state and clean up
 *
 * Parameters:
 *   MiniportAdapterHandle - Handle to the miniport adapter
 */
VOID
Ndis6iSetAdapterHalted(
    _In_ NDIS_HANDLE MiniportAdapterHandle)
{
    PNDIS6_ADAPTER_STATE_BLOCK StateBlock;

    StateBlock = Ndis6iFindAdapterStateBlock(MiniportAdapterHandle);
    if (StateBlock != NULL)
    {
        Ndis6iSetAdapterState(StateBlock, NdisMiniportAdapterStateHalted);
        Ndis6iRemoveAdapterStateBlock(MiniportAdapterHandle);
    }
}

#endif /* NDIS_SUPPORT_NDIS6 */

/* EOF */
