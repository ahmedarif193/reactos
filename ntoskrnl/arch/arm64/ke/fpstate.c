/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            ntoskrnl/arch/arm64/ke/fpstate.c
 * PURPOSE:         ARM64 FP/SVE/SME lazy context switching implementation
 * COPYRIGHT:       Copyright 2025 ReactOS ARM64 Team
 *
 * This file implements lazy floating-point context switching for ARM64.
 * The key insight is that many threads never use floating-point, so we
 * avoid the overhead of saving/restoring FP state by:
 *
 * 1. Initially disabling FP access (trap on first use)
 * 2. On first FP instruction, trap and allocate state buffer
 * 3. Enable FP and mark state as dirty
 * 4. Only save state during context switch if dirty
 * 5. On resume, restore or trap based on existing state
 *
 * SVE/SME follow the same pattern but with larger, scalable state buffers.
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#include "../include/fpstate.h"

/*
 * Global configuration - set during boot.
 */
BOOLEAN KiArm64HasNeon = FALSE;
BOOLEAN KiArm64HasSve = FALSE;
BOOLEAN KiArm64HasSme = FALSE;
ULONG KiArm64MaxSveVectorLength = 0;
ULONG KiArm64SveStateSize = 0;

/*
 * Statistics for debugging.
 */
#if DBG
static volatile LONG KiArm64FpTrapsTotal = 0;
static volatile LONG KiArm64FpAllocations = 0;
static volatile LONG KiArm64FpSaves = 0;
static volatile LONG KiArm64FpRestores = 0;
static volatile LONG KiArm64SveTraps = 0;
#endif

/*
 * Pool tag for FP state allocations.
 */
#define KI_FP_STATE_TAG 'FpSv'

/**
 * @brief Query the current SVE vector length from hardware.
 *
 * Reads ZCR_EL1.LEN to determine the configured SVE vector length.
 * This must be called with SVE enabled.
 *
 * @return SVE vector length in bits.
 */
static
ULONG
KiArm64QuerySveVectorLength(
    VOID)
{
    ULONG64 Zcr;
    ULONG VlBytes;

    /*
     * ZCR_EL1.LEN field (bits [3:0]) encodes (VL/128)-1
     * So VL = (LEN + 1) * 128 bits
     *
     * However, reading ZCR only gives the maximum allowed VL.
     * To get the actual VL, we need to read the SVE VL from
     * a scalable register or use the RDVL instruction.
     *
     * For now, assume ZCR reflects actual VL.
     *
     * Note: ZCR_EL1 is encoded as S3_0_C1_C2_0 (op0=3, op1=0, CRn=1, CRm=2, op2=0)
     * We use the raw encoding because -march=armv8-a doesn't include SVE.
     */
    __asm__ __volatile__("mrs %0, S3_0_C1_C2_0" : "=r"(Zcr));

    VlBytes = ((Zcr & 0xF) + 1) * 16;  /* In bytes */
    return VlBytes * 8;  /* Convert to bits */
}

/**
 * @brief Calculate SVE state buffer size for a given vector length.
 */
ULONG
KiArm64GetSveStateSize(
    _In_ ULONG VectorLengthBits)
{
    ULONG VlBytes = VectorLengthBits / 8;
    ULONG Size;

    /*
     * SVE state consists of:
     * - 32 Z registers: 32 * VL bytes
     * - 16 P registers: 16 * (VL/8) bytes = 2 * VL bytes
     * - FFR register: VL/8 bytes = VL/8 bytes
     * - FPCR/FPSR: 8 bytes
     * - ZCR: 8 bytes (for restore)
     *
     * Total = 32*VL + 2*VL + VL/8 + 16 = 34*VL + VL/8 + 16
     *       = (272*VL + VL + 128) / 8 = (273*VL + 128) / 8
     *
     * Round up to 16-byte alignment.
     */
    Size = (32 * VlBytes) +             /* Z registers */
           (2 * VlBytes) +              /* P registers */
           (VlBytes / 8) +              /* FFR */
           16;                          /* Control registers + padding */

    return (Size + 15) & ~15;           /* Align to 16 bytes */
}

/**
 * @brief Initialize FP/SVE support during system boot.
 */
VOID
KiArm64InitializeFpSupport(
    VOID)
{
    ULONG64 Pfr0, Pfr1;
    ULONG64 Cpacr;

    /*
     * Read processor feature registers to detect capabilities.
     */
    __asm__ __volatile__("mrs %0, id_aa64pfr0_el1" : "=r"(Pfr0));
    __asm__ __volatile__("mrs %0, id_aa64pfr1_el1" : "=r"(Pfr1));

    /*
     * ID_AA64PFR0_EL1.AdvSIMD (bits [23:20]):
     * 0b0000 = NEON implemented
     * 0b0001 = NEON + FP16
     * 0b1111 = Not implemented
     */
    KiArm64HasNeon = (((Pfr0 >> 20) & 0xF) != 0xF);

    /*
     * ID_AA64PFR0_EL1.SVE (bits [35:32]):
     * 0b0000 = Not implemented
     * 0b0001 = SVE implemented
     */
    KiArm64HasSve = (((Pfr0 >> 32) & 0xF) != 0);

    /*
     * ID_AA64PFR1_EL1.SME (bits [27:24]):
     * 0b0000 = Not implemented
     * 0b0001 = SME implemented
     * 0b0010 = SME2 implemented
     */
    KiArm64HasSme = (((Pfr1 >> 24) & 0xF) != 0);

    /*
     * If SVE is available, determine the maximum vector length.
     * We need to temporarily enable SVE to query the VL.
     */
    if (KiArm64HasSve)
    {
        __asm__ __volatile__("mrs %0, cpacr_el1" : "=r"(Cpacr));
        Cpacr |= CPACR_EL1_FPEN_ENABLE | CPACR_EL1_ZEN_ENABLE;
        __asm__ __volatile__("msr cpacr_el1, %0" :: "r"(Cpacr));
        __asm__ __volatile__("isb" ::: "memory");

        KiArm64MaxSveVectorLength = KiArm64QuerySveVectorLength();
        KiArm64SveStateSize = KiArm64GetSveStateSize(KiArm64MaxSveVectorLength);
    }

    /*
     * Log detected capabilities.
     */
    DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_INFO_LEVEL,
               "[arm64] FP Support: NEON=%d SVE=%d (VL=%lu) SME=%d\n",
               KiArm64HasNeon, KiArm64HasSve, KiArm64MaxSveVectorLength, KiArm64HasSme);
}

/**
 * @brief Allocate FP state buffer for a thread.
 */
static
NTSTATUS
KiArm64AllocateFpState(
    _Inout_ PKARM64_FP_STATE FpState,
    _In_ BOOLEAN NeedsSve)
{
    PVOID Buffer;
    ULONG Size;

    if (NeedsSve && KiArm64HasSve)
    {
        Size = KiArm64SveStateSize;
        FpState->SveVectorLength = KiArm64MaxSveVectorLength;
    }
    else
    {
        Size = KI_NEON_STATE_SIZE;
        FpState->SveVectorLength = 0;
    }

    /*
     * Allocate from NonPagedPool since we may save/restore at elevated IRQL.
     */
    Buffer = ExAllocatePoolWithTag(NonPagedPool, Size, KI_FP_STATE_TAG);
    if (Buffer == NULL)
    {
        DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                   "[arm64] Failed to allocate FP state buffer (size=%lu)\n", Size);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /*
     * Zero-initialize the buffer. This is the architecturally defined
     * reset state for FP registers.
     */
    RtlZeroMemory(Buffer, Size);

    FpState->FpBuffer = Buffer;
    FpState->FpBufferSize = Size;
    FpState->Flags = 0;

#if DBG
    InterlockedIncrement(&KiArm64FpAllocations);
#endif

    return STATUS_SUCCESS;
}

/**
 * @brief Handle FP/SVE trap - called when thread first accesses FP.
 */
BOOLEAN
KiArm64HandleFpTrap(
    _Inout_ PKTRAP_FRAME TrapFrame,
    _In_ ULONG Esr)
{
    PKTHREAD Thread;
    PKARM64_FP_STATE FpState;
    ULONG EsrClass;
    BOOLEAN NeedsSve;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(TrapFrame);

#if DBG
    InterlockedIncrement(&KiArm64FpTrapsTotal);
#endif

    /*
     * Extract exception class from ESR.
     */
    EsrClass = (Esr >> 26) & 0x3F;

    /*
     * Determine if this is an SVE trap or regular FP trap.
     */
    NeedsSve = (EsrClass == ESR_EC_SVE_TRAP);

#if DBG
    if (NeedsSve)
    {
        InterlockedIncrement(&KiArm64SveTraps);
    }
#endif

    /*
     * Get current thread and its FP state.
     * Note: We need to access thread-specific FP state. For now, we use
     * a placeholder approach until the KTHREAD extension is properly wired.
     */
    Thread = KeGetCurrentThread();
    if (Thread == NULL)
    {
        /*
         * Very early boot - just enable FP globally.
         */
        if (NeedsSve && KiArm64HasSve)
        {
            KiArm64EnableSve();
        }
        else
        {
            KiArm64EnableFp();
        }
        return TRUE;
    }

    /*
     * Access the thread's FP state.
     * TODO: This needs to be properly wired to the KTHREAD structure.
     * For now, we store FP state in StateSaveArea if available.
     */
    if (Thread->StateSaveArea == NULL)
    {
        /*
         * No StateSaveArea - allocate one that can hold our FP state.
         * This is a temporary solution until proper KTHREAD integration.
         */
        PVOID Area = ExAllocatePoolWithTag(NonPagedPool,
                                           sizeof(KARM64_FP_STATE) + KI_NEON_STATE_SIZE,
                                           KI_FP_STATE_TAG);
        if (Area == NULL)
        {
            DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                       "[arm64] FpTrap: Failed to allocate StateSaveArea\n");
            /* Enable FP anyway to prevent infinite traps */
            KiArm64EnableFp();
            return TRUE;
        }
        RtlZeroMemory(Area, sizeof(KARM64_FP_STATE) + KI_NEON_STATE_SIZE);
        Thread->StateSaveArea = Area;
    }

    FpState = (PKARM64_FP_STATE)Thread->StateSaveArea;

    /*
     * If no FP buffer allocated yet, allocate one now.
     */
    if (FpState->FpBuffer == NULL)
    {
        Status = KiArm64AllocateFpState(FpState, NeedsSve);
        if (!NT_SUCCESS(Status))
        {
            /* Enable FP anyway to prevent infinite traps */
            KiArm64EnableFp();
            return TRUE;
        }

        DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_TRACE_LEVEL,
                   "[arm64] FpTrap: Allocated FP state for thread %p (SVE=%d)\n",
                   Thread, NeedsSve);
    }
    else if (NeedsSve && FpState->SveVectorLength == 0)
    {
        /*
         * Thread previously used NEON but now needs SVE.
         * Upgrade the buffer.
         */
        ExFreePoolWithTag(FpState->FpBuffer, KI_FP_STATE_TAG);
        FpState->FpBuffer = NULL;
        FpState->FpBufferSize = 0;

        Status = KiArm64AllocateFpState(FpState, TRUE);
        if (!NT_SUCCESS(Status))
        {
            KiArm64EnableFp();
            return TRUE;
        }

        DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_TRACE_LEVEL,
                   "[arm64] FpTrap: Upgraded FP state to SVE for thread %p\n", Thread);
    }

    /*
     * Restore any previously saved state.
     * This handles the case where the thread was context-switched out
     * with FP state, and is now resuming.
     */
    if (FpState->Flags & KI_FP_CTX_FLAG_RESTORED)
    {
        /* State already restored, just enable access */
    }
    else if (FpState->FpBufferSize > 0)
    {
        /*
         * Restore FP state from buffer.
         * The actual restore happens via assembly helpers.
         */
        KiArm64RestoreFpState(Thread);
        FpState->Flags |= KI_FP_CTX_FLAG_RESTORED;
    }

    /*
     * Enable FP/SVE access for this thread.
     */
    if (NeedsSve || (FpState->Flags & KI_FP_CTX_FLAG_SVE_ACTIVE))
    {
        FpState->Flags |= KI_FP_CTX_FLAG_SVE_ACTIVE;
        KiArm64EnableSve();
    }
    else
    {
        KiArm64EnableFp();
    }

    /*
     * Mark state as dirty since we're about to execute FP instructions.
     */
    FpState->Flags |= KI_FP_CTX_FLAG_DIRTY;

    return TRUE;
}

/**
 * @brief Save NEON state to buffer.
 */
static
VOID
KiArm64SaveNeonState(
    _Out_writes_bytes_(KI_NEON_STATE_SIZE) PVOID Buffer)
{
    PKARM64_NEON_STATE State = (PKARM64_NEON_STATE)Buffer;

    /*
     * Save FPCR/FPSR control registers.
     * Note: mrs reads into 64-bit register, but FPCR/FPSR are only 32 bits.
     * We use a 64-bit temporary to satisfy the constraint, then truncate.
     */
    {
        ULONG64 FpcrTemp, FpsrTemp;
        __asm__ __volatile__("mrs %0, fpcr" : "=r"(FpcrTemp));
        __asm__ __volatile__("mrs %0, fpsr" : "=r"(FpsrTemp));
        State->Fpcr = (ULONG)FpcrTemp;
        State->Fpsr = (ULONG)FpsrTemp;
    }

    /*
     * Save Q0-Q31 (128-bit NEON registers).
     * Using STP with Q-register pairs for efficiency.
     */
    __asm__ __volatile__(
        "stp q0,  q1,  [%0, #(0 * 32)]      \n"
        "stp q2,  q3,  [%0, #(1 * 32)]      \n"
        "stp q4,  q5,  [%0, #(2 * 32)]      \n"
        "stp q6,  q7,  [%0, #(3 * 32)]      \n"
        "stp q8,  q9,  [%0, #(4 * 32)]      \n"
        "stp q10, q11, [%0, #(5 * 32)]      \n"
        "stp q12, q13, [%0, #(6 * 32)]      \n"
        "stp q14, q15, [%0, #(7 * 32)]      \n"
        "stp q16, q17, [%0, #(8 * 32)]      \n"
        "stp q18, q19, [%0, #(9 * 32)]      \n"
        "stp q20, q21, [%0, #(10 * 32)]     \n"
        "stp q22, q23, [%0, #(11 * 32)]     \n"
        "stp q24, q25, [%0, #(12 * 32)]     \n"
        "stp q26, q27, [%0, #(13 * 32)]     \n"
        "stp q28, q29, [%0, #(14 * 32)]     \n"
        "stp q30, q31, [%0, #(15 * 32)]     \n"
        :
        : "r"(State->Q)
        : "memory"
    );
}

/**
 * @brief Restore NEON state from buffer.
 */
static
VOID
KiArm64RestoreNeonState(
    _In_reads_bytes_(KI_NEON_STATE_SIZE) PVOID Buffer)
{
    PKARM64_NEON_STATE State = (PKARM64_NEON_STATE)Buffer;

    /*
     * Restore Q0-Q31 (128-bit NEON registers).
     */
    __asm__ __volatile__(
        "ldp q0,  q1,  [%0, #(0 * 32)]      \n"
        "ldp q2,  q3,  [%0, #(1 * 32)]      \n"
        "ldp q4,  q5,  [%0, #(2 * 32)]      \n"
        "ldp q6,  q7,  [%0, #(3 * 32)]      \n"
        "ldp q8,  q9,  [%0, #(4 * 32)]      \n"
        "ldp q10, q11, [%0, #(5 * 32)]      \n"
        "ldp q12, q13, [%0, #(6 * 32)]      \n"
        "ldp q14, q15, [%0, #(7 * 32)]      \n"
        "ldp q16, q17, [%0, #(8 * 32)]      \n"
        "ldp q18, q19, [%0, #(9 * 32)]      \n"
        "ldp q20, q21, [%0, #(10 * 32)]     \n"
        "ldp q22, q23, [%0, #(11 * 32)]     \n"
        "ldp q24, q25, [%0, #(12 * 32)]     \n"
        "ldp q26, q27, [%0, #(13 * 32)]     \n"
        "ldp q28, q29, [%0, #(14 * 32)]     \n"
        "ldp q30, q31, [%0, #(15 * 32)]     \n"
        :
        : "r"(State->Q)
        : "memory"
    );

    /*
     * Restore FPCR/FPSR control registers.
     */
    __asm__ __volatile__("msr fpcr, %0" :: "r"((ULONG64)State->Fpcr));
    __asm__ __volatile__("msr fpsr, %0" :: "r"((ULONG64)State->Fpsr));
}

/**
 * @brief Save thread's FP state during context switch.
 */
VOID
KiArm64SaveFpState(
    _Inout_ PKTHREAD Thread)
{
    PKARM64_FP_STATE FpState;

    if (Thread == NULL || Thread->StateSaveArea == NULL)
    {
        return;
    }

    FpState = (PKARM64_FP_STATE)Thread->StateSaveArea;

    /*
     * Only save if state is dirty and we have a buffer.
     */
    if ((FpState->Flags & KI_FP_CTX_FLAG_DIRTY) == 0)
    {
        return;
    }

    if (FpState->FpBuffer == NULL)
    {
        return;
    }

#if DBG
    InterlockedIncrement(&KiArm64FpSaves);
#endif

    /*
     * Save based on what type of state we have.
     */
    if (FpState->SveVectorLength > 0)
    {
        /*
         * SVE state save.
         * TODO: Implement SVE-specific save using STR (SVE) instructions.
         * For now, save at least the NEON portion.
         */
        KiArm64SaveNeonState(FpState->FpBuffer);
    }
    else
    {
        /*
         * NEON-only state save.
         */
        KiArm64SaveNeonState(FpState->FpBuffer);
    }

    /*
     * Clear dirty flag - state is now saved.
     */
    FpState->Flags &= ~KI_FP_CTX_FLAG_DIRTY;
    FpState->Flags &= ~KI_FP_CTX_FLAG_RESTORED;
}

/**
 * @brief Restore thread's FP state during context switch.
 */
VOID
KiArm64RestoreFpState(
    _Inout_ PKTHREAD Thread)
{
    PKARM64_FP_STATE FpState;

    if (Thread == NULL || Thread->StateSaveArea == NULL)
    {
        /*
         * No FP state - configure trap on first use.
         */
        KiArm64ConfigureFpTrap();
        return;
    }

    FpState = (PKARM64_FP_STATE)Thread->StateSaveArea;

    if (FpState->FpBuffer == NULL)
    {
        /*
         * Thread has FP state struct but no buffer - trap on first use.
         */
        KiArm64ConfigureFpTrap();
        return;
    }

#if DBG
    InterlockedIncrement(&KiArm64FpRestores);
#endif

    /*
     * Enable FP access first so we can restore registers.
     */
    if (FpState->SveVectorLength > 0)
    {
        KiArm64EnableSve();
    }
    else
    {
        KiArm64EnableFp();
    }

    /*
     * Restore based on state type.
     */
    if (FpState->SveVectorLength > 0)
    {
        /*
         * SVE state restore.
         * TODO: Implement SVE-specific restore using LDR (SVE) instructions.
         */
        KiArm64RestoreNeonState(FpState->FpBuffer);
    }
    else
    {
        /*
         * NEON-only state restore.
         */
        KiArm64RestoreNeonState(FpState->FpBuffer);
    }

    FpState->Flags |= KI_FP_CTX_FLAG_RESTORED;
}

/**
 * @brief Free thread's FP state buffer.
 */
VOID
KiArm64FreeFpState(
    _Inout_ PKTHREAD Thread)
{
    PKARM64_FP_STATE FpState;

    if (Thread == NULL || Thread->StateSaveArea == NULL)
    {
        return;
    }

    FpState = (PKARM64_FP_STATE)Thread->StateSaveArea;

    if (FpState->FpBuffer != NULL)
    {
        ExFreePoolWithTag(FpState->FpBuffer, KI_FP_STATE_TAG);
        FpState->FpBuffer = NULL;
        FpState->FpBufferSize = 0;
    }

    /*
     * Note: We don't free StateSaveArea here as it may be used for
     * other purposes. The caller is responsible for that.
     */
}

/*
 * Debug helper to dump FP state statistics.
 */
#if DBG
VOID
KiArm64DumpFpStats(
    VOID)
{
    DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_INFO_LEVEL,
               "[arm64] FP Stats: Traps=%ld Allocs=%ld Saves=%ld Restores=%ld SVE=%ld\n",
               KiArm64FpTrapsTotal,
               KiArm64FpAllocations,
               KiArm64FpSaves,
               KiArm64FpRestores,
               KiArm64SveTraps);
}
#endif
