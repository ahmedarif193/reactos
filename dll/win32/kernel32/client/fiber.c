/*
 * PROJECT:     ReactOS System Libraries
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Fiber Implementation
 * COPYRIGHT:   Copyright 2005-2011 Alex Ionescu (alex@relsoft.net)
 *              Copyright 2003-2008 KJK::Hyperion (noog@libero.it)
 *              Copyright 2018 Mark Jansen (mark.jansen@reactos.org)
 */

#include <k32.h>
#include <ndk/rtltypes.h>

#define NDEBUG
#include <debug.h>

/* TEB.HasFiberData was renamed to DbgHasFiberData (a bit in SameTebFlags) at
 * Vista. Map both spellings to a single accessor so this code compiles
 * regardless of NTDDI level. */
#if (NTDDI_VERSION >= NTDDI_LONGHORN)
#define TEB_HAS_FIBER_DATA(t)        ((t)->DbgHasFiberData)
#define TEB_SET_FIBER_DATA(t, v)     ((t)->DbgHasFiberData = (v))
#else
#define TEB_HAS_FIBER_DATA(t)        ((t)->HasFiberData)
#define TEB_SET_FIBER_DATA(t, v)     ((t)->HasFiberData = (v))
#endif

#ifdef _M_IX86
C_ASSERT(FIELD_OFFSET(FIBER, ExceptionList) == 0x04);
C_ASSERT(FIELD_OFFSET(FIBER, StackBase) == 0x08);
C_ASSERT(FIELD_OFFSET(FIBER, StackLimit) == 0x0C);
C_ASSERT(FIELD_OFFSET(FIBER, DeallocationStack) == 0x10);
C_ASSERT(FIELD_OFFSET(FIBER, FiberContext) == 0x14);
#if (NTDDI_VERSION >= NTDDI_LONGHORN)
C_ASSERT(FIELD_OFFSET(FIBER, Wx86Tib) == 0x2E0);
C_ASSERT(FIELD_OFFSET(FIBER, ActivationContextStackPointer) == 0x2E4);
C_ASSERT(FIELD_OFFSET(FIBER, FlsData) == 0x2E8);
C_ASSERT(FIELD_OFFSET(FIBER, GuaranteedStackBytes) == 0x2EC);
C_ASSERT(FIELD_OFFSET(FIBER, TebFlags) == 0x2F0);
#else
C_ASSERT(FIELD_OFFSET(FIBER, GuaranteedStackBytes) == 0x2E0);
C_ASSERT(FIELD_OFFSET(FIBER, FlsData) == 0x2E4);
C_ASSERT(FIELD_OFFSET(FIBER, ActivationContextStackPointer) == 0x2E8);
#endif
#endif // _M_IX86

/* PRIVATE FUNCTIONS **********************************************************/

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * @implemented
 */
BOOL
WINAPI
ConvertFiberToThread(VOID)
{
    PTEB Teb;
    PFIBER FiberData;
    DPRINT1("Converting Fiber to Thread\n");

    /* Check if the thread is already not a fiber */
    Teb = NtCurrentTeb();
    if (!TEB_HAS_FIBER_DATA(Teb))
    {
        /* Fail */
        SetLastError(ERROR_ALREADY_THREAD);
        return FALSE;
    }

    /* This thread won't run a fiber anymore */
    TEB_SET_FIBER_DATA(Teb, FALSE);
    FiberData = Teb->NtTib.FiberData;
    Teb->NtTib.FiberData = NULL;

    /* Free the fiber */
    ASSERT(FiberData != NULL);
    RtlFreeHeap(RtlGetProcessHeap(),
                0,
                FiberData);

    /* Success */
    return TRUE;
}

/*
 * @implemented
 */
LPVOID
WINAPI
ConvertThreadToFiberEx(_In_opt_ LPVOID lpParameter,
                       _In_ DWORD dwFlags)
{
    PTEB Teb;
    PFIBER Fiber;
    DPRINT1("Converting Thread to Fiber\n");

    /* Check for invalid flags */
    if (dwFlags & ~FIBER_FLAG_FLOAT_SWITCH)
    {
        /* Fail */
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }

    /* Are we already a fiber? */
    Teb = NtCurrentTeb();
    if (TEB_HAS_FIBER_DATA(Teb))
    {
        /* Fail */
        SetLastError(ERROR_ALREADY_FIBER);
        return NULL;
    }

    /* Allocate the fiber */
    Fiber = RtlAllocateHeap(RtlGetProcessHeap(),
                            0,
                            sizeof(FIBER));
    if (!Fiber)
    {
        /* Fail */
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }

    /* Copy some contextual data from the thread to the fiber */
    Fiber->FiberData = lpParameter;
    Fiber->ExceptionList = Teb->NtTib.ExceptionList;
    Fiber->StackBase = Teb->NtTib.StackBase;
    Fiber->StackLimit = Teb->NtTib.StackLimit;
    Fiber->DeallocationStack = Teb->DeallocationStack;
    Fiber->FlsData = Teb->FlsData;
    Fiber->GuaranteedStackBytes = Teb->GuaranteedStackBytes;
    Fiber->ActivationContextStackPointer = Teb->ActivationContextStackPointer;

    /* Save FPU State if requested, otherwise just the basic registers */
    Fiber->FiberContext.ContextFlags = (dwFlags & FIBER_FLAG_FLOAT_SWITCH) ?
                                       (CONTEXT_FULL | CONTEXT_FLOATING_POINT) :
                                       CONTEXT_FULL;

    /* Associate the fiber to the current thread */
    Teb->NtTib.FiberData = Fiber;
    TEB_SET_FIBER_DATA(Teb, TRUE);

    /* Return opaque fiber data */
    return (LPVOID)Fiber;
}

/*
 * @implemented
 */
LPVOID
WINAPI
ConvertThreadToFiber(_In_opt_ LPVOID lpParameter)
{
    /* Call the newer function */
    return ConvertThreadToFiberEx(lpParameter,
                                  0);
}

/*
 * @implemented
 */
LPVOID
WINAPI
CreateFiber(_In_ SIZE_T dwStackSize,
            _In_ LPFIBER_START_ROUTINE lpStartAddress,
            _In_opt_ LPVOID lpParameter)
{
    /* Call the Newer Function */
    return CreateFiberEx(dwStackSize,
                         0,
                         0,
                         lpStartAddress,
                         lpParameter);
}

/*
 * @implemented
 */
LPVOID
WINAPI
CreateFiberEx(_In_ SIZE_T dwStackCommitSize,
              _In_ SIZE_T dwStackReserveSize,
              _In_ DWORD dwFlags,
              _In_ LPFIBER_START_ROUTINE lpStartAddress,
              _In_opt_ LPVOID lpParameter)
{
    PFIBER Fiber;
    NTSTATUS Status;
    INITIAL_TEB InitialTeb;
    PACTIVATION_CONTEXT_STACK ActivationContextStackPointer;
    DPRINT("Creating Fiber\n");

    /* Check for invalid flags */
    if (dwFlags & ~FIBER_FLAG_FLOAT_SWITCH)
    {
        /* Fail */
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }

    /* Allocate the Activation Context Stack */
    ActivationContextStackPointer = NULL;
    Status = RtlAllocateActivationContextStack(&ActivationContextStackPointer);
    if (!NT_SUCCESS(Status))
    {
        /* Fail */
        BaseSetLastNTError(Status);
        return NULL;
    }

    /* Allocate the fiber */
    Fiber = RtlAllocateHeap(RtlGetProcessHeap(),
                            0,
                            sizeof(FIBER));
    if (!Fiber)
    {
        /* Free the activation context stack */
        RtlFreeActivationContextStack(ActivationContextStackPointer);

        /* Fail */
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }

    /* Create the stack for the fiber */
    Status = BaseCreateStack(NtCurrentProcess(),
                              dwStackCommitSize,
                              dwStackReserveSize,
                              &InitialTeb);
    if (!NT_SUCCESS(Status))
    {
        /* Free the fiber */
        RtlFreeHeap(GetProcessHeap(),
                    0,
                    Fiber);

        /* Free the activation context stack */
        RtlFreeActivationContextStack(ActivationContextStackPointer);

        /* Failure */
        BaseSetLastNTError(Status);
        return NULL;
    }

    /* Clear the context */
    RtlZeroMemory(&Fiber->FiberContext,
                  sizeof(CONTEXT));

    /* Copy the data into the fiber */
    Fiber->StackBase = InitialTeb.StackBase;
    Fiber->StackLimit = InitialTeb.StackLimit;
    Fiber->DeallocationStack = InitialTeb.AllocatedStackBase;
    Fiber->FiberData = lpParameter;
    Fiber->ExceptionList = EXCEPTION_CHAIN_END;
    Fiber->GuaranteedStackBytes = 0;
    Fiber->FlsData = NULL;
    Fiber->ActivationContextStackPointer = ActivationContextStackPointer;

    /* Save FPU State if requested, otherwise just the basic registers */
    Fiber->FiberContext.ContextFlags = (dwFlags & FIBER_FLAG_FLOAT_SWITCH) ?
                                       (CONTEXT_FULL | CONTEXT_FLOATING_POINT) :
                                       CONTEXT_FULL;

    /* Initialize the context for the fiber */
    BaseInitializeContext(&Fiber->FiberContext,
                          lpParameter,
                          lpStartAddress,
                          InitialTeb.StackBase,
                          2);

    /* Return the Fiber */
    return Fiber;
}

/*
 * @implemented
 */
VOID
WINAPI
DeleteFiber(_In_ LPVOID lpFiber)
{
    SIZE_T Size;
    PFIBER Fiber;
    PTEB Teb;

    /* Are we deleting ourselves? */
    Teb = NtCurrentTeb();
    Fiber = (PFIBER)lpFiber;
    if (TEB_HAS_FIBER_DATA(Teb) &&
        (Teb->NtTib.FiberData == Fiber))
    {
        /* Just exit */
        ExitThread(1);
    }

    /* Not ourselves, de-allocate the stack */
    Size = 0 ;
    NtFreeVirtualMemory(NtCurrentProcess(),
                        &Fiber->DeallocationStack,
                        &Size,
                        MEM_RELEASE);

    /* Get rid of FLS */
    RtlProcessFlsData(Fiber->FlsData, 3);

    /* Get rid of the activation context stack */
    RtlFreeActivationContextStack(Fiber->ActivationContextStackPointer);

    /* Free the fiber data */
    RtlFreeHeap(RtlGetProcessHeap(),
                0,
                lpFiber);
}

/*
 * @implemented
 */
BOOL
WINAPI
IsThreadAFiber(VOID)
{
    /* Return flag in the TEB */
    return TEB_HAS_FIBER_DATA(NtCurrentTeb());
}

/*
 * @implemented
 */
DWORD
WINAPI
FlsAlloc(PFLS_CALLBACK_FUNCTION lpCallback)
{
    NTSTATUS Status;
    ULONG Index;

    Status = RtlFlsAlloc(lpCallback, &Index);
    if (!NT_SUCCESS(Status))
    {
        BaseSetLastNTError(Status);
        return FLS_OUT_OF_INDEXES;
    }
    return Index;
}


/*
 * @implemented
 */
BOOL
WINAPI
FlsFree(DWORD dwFlsIndex)
{
    NTSTATUS Status = RtlFlsFree(dwFlsIndex);

    if (!NT_SUCCESS(Status))
        BaseSetLastNTError(Status);
    return NT_SUCCESS(Status);
}


/*
 * @implemented
 */
PVOID
WINAPI
FlsGetValue(DWORD dwFlsIndex)
{
    NTSTATUS Status;
    PVOID Data;

    Status = RtlFlsGetValue(dwFlsIndex, &Data);
    if (!NT_SUCCESS(Status))
    {
        BaseSetLastNTError(Status);
        return NULL;
    }

    SetLastError(ERROR_SUCCESS);
    return Data;
}


/*
 * @implemented
 */
BOOL
WINAPI
FlsSetValue(DWORD dwFlsIndex,
            PVOID lpFlsData)
{
    NTSTATUS Status = RtlFlsSetValue(dwFlsIndex, lpFlsData);

    if (!NT_SUCCESS(Status))
        BaseSetLastNTError(Status);
    return NT_SUCCESS(Status);
}

/* EOF */
