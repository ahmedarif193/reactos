/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            ntoskrnl/arch/arm64/include/fpstate.h
 * PURPOSE:         ARM64 FP/SVE/SME state management definitions
 * COPYRIGHT:       Copyright 2025 ReactOS ARM64 Team
 *
 * This file defines the structures and constants for lazy FP/SVE/SME
 * context switching on ARM64. The design follows Windows on ARM64
 * conventions for efficient floating-point state management.
 *
 * Key Design Principles:
 * 1. Lazy Context Switching - FP state is only saved/restored when needed
 * 2. Trap-on-First-Use - Threads trap on first FP/SVE use, enabling lazy alloc
 * 3. Per-Thread State - Each thread has its own FP context buffer
 * 4. SVE Vector Length Awareness - State size depends on hardware VL
 */

#pragma once

#include <ntdef.h>

/*
 * ESR Exception Class values for FP/SVE/SME traps.
 * These are the EC field (bits [31:26]) of ESR_EL1.
 */
#define ESR_EC_FP_TRAP          0x07    /* Access to FP/ASIMD when disabled */
#define ESR_EC_SVE_TRAP         0x19    /* Access to SVE when disabled */
#define ESR_EC_SME_TRAP         0x1D    /* Access to SME when disabled */

/*
 * CPACR_EL1 bit positions for FP/SVE/SME control.
 * These control trapping behavior for floating-point features.
 */
#define CPACR_EL1_FPEN_SHIFT    20      /* FP/NEON enable bits [21:20] */
#define CPACR_EL1_FPEN_MASK     (3ULL << CPACR_EL1_FPEN_SHIFT)
#define CPACR_EL1_FPEN_TRAP     (0ULL << CPACR_EL1_FPEN_SHIFT)  /* Trap EL0/EL1 */
#define CPACR_EL1_FPEN_EL0_TRAP (1ULL << CPACR_EL1_FPEN_SHIFT)  /* Trap EL0 only */
#define CPACR_EL1_FPEN_ENABLE   (3ULL << CPACR_EL1_FPEN_SHIFT)  /* No trap */

#define CPACR_EL1_ZEN_SHIFT     16      /* SVE enable bits [17:16] */
#define CPACR_EL1_ZEN_MASK      (3ULL << CPACR_EL1_ZEN_SHIFT)
#define CPACR_EL1_ZEN_TRAP      (0ULL << CPACR_EL1_ZEN_SHIFT)   /* Trap EL0/EL1 */
#define CPACR_EL1_ZEN_EL0_TRAP  (1ULL << CPACR_EL1_ZEN_SHIFT)   /* Trap EL0 only */
#define CPACR_EL1_ZEN_ENABLE    (3ULL << CPACR_EL1_ZEN_SHIFT)   /* No trap */

#define CPACR_EL1_SMEN_SHIFT    24      /* SME enable bits [25:24] */
#define CPACR_EL1_SMEN_MASK     (3ULL << CPACR_EL1_SMEN_SHIFT)
#define CPACR_EL1_SMEN_TRAP     (0ULL << CPACR_EL1_SMEN_SHIFT)  /* Trap EL0/EL1 */
#define CPACR_EL1_SMEN_EL0_TRAP (1ULL << CPACR_EL1_SMEN_SHIFT)  /* Trap EL0 only */
#define CPACR_EL1_SMEN_ENABLE   (3ULL << CPACR_EL1_SMEN_SHIFT)  /* No trap */

/*
 * FP State Type flags - indicates which extensions are in use.
 */
#define KI_FP_STATE_NONE        0x00    /* No FP state allocated */
#define KI_FP_STATE_NEON        0x01    /* NEON/ASIMD (128-bit Q registers) */
#define KI_FP_STATE_SVE         0x02    /* SVE (scalable vector) */
#define KI_FP_STATE_SME         0x04    /* SME (scalable matrix) */

/*
 * Standard NEON state size: 32 x 128-bit registers = 512 bytes
 * Plus FPCR (4 bytes) + FPSR (4 bytes) = 8 bytes
 * Total aligned: 528 bytes, rounded to 16-byte boundary = 528 bytes
 */
#define KI_NEON_STATE_SIZE      ((32 * 16) + 16)  /* 32 Q-regs + FPCR/FPSR + padding */

/*
 * SVE register state includes:
 * - Z0-Z31: 32 scalable vector registers (VL bytes each)
 * - P0-P15: 16 predicate registers (VL/8 bytes each)
 * - FFR: First-Fault Register (VL/8 bytes)
 * - FPCR/FPSR: 8 bytes total
 *
 * For VL=128 bits (minimum): (32*16) + (17*2) + 8 = 512 + 34 + 8 = 554 bytes
 * For VL=2048 bits (maximum): (32*256) + (17*32) + 8 = 8192 + 544 + 8 = 8744 bytes
 *
 * We query actual VL from hardware at runtime.
 */
#define KI_SVE_MIN_VL           128     /* Minimum SVE vector length in bits */
#define KI_SVE_MAX_VL           2048    /* Maximum SVE vector length in bits */

/*
 * ARM64 FP Context Flags for per-thread state.
 */
#define KI_FP_CTX_FLAG_DIRTY        0x01    /* State modified, needs save */
#define KI_FP_CTX_FLAG_SVE_ACTIVE   0x02    /* Thread is using SVE */
#define KI_FP_CTX_FLAG_SME_ACTIVE   0x04    /* Thread is using SME */
#define KI_FP_CTX_FLAG_RESTORED     0x08    /* State has been restored */

/*
 * KARM64_FP_STATE - Per-thread FP/SVE context tracking.
 *
 * This structure is embedded in KTHREAD (via architecture-specific extension)
 * and tracks the floating-point state for lazy context switching.
 *
 * Layout is designed for cache-line alignment of frequently accessed fields.
 */
typedef struct _KARM64_FP_STATE
{
    /*
     * Pointer to allocated FP state buffer. NULL if thread has never
     * used FP/SVE. Buffer is allocated on first FP/SVE trap.
     * Memory is from NonPagedPool with 'FpSv' tag.
     */
    PVOID FpBuffer;

    /*
     * Size of FpBuffer in bytes. Zero if not allocated.
     * For NEON-only: KI_NEON_STATE_SIZE
     * For SVE: Depends on hardware vector length
     */
    ULONG FpBufferSize;

    /*
     * Flags indicating FP context state (KI_FP_CTX_FLAG_*).
     */
    ULONG Flags;

    /*
     * SVE vector length in bits. Zero if SVE not in use.
     * Valid values: 128, 256, 384, 512, ... up to KI_SVE_MAX_VL
     * Always a multiple of 128 bits.
     */
    ULONG SveVectorLength;

    /*
     * Reserved for SME streaming vector length.
     */
    ULONG SmeVectorLength;

} KARM64_FP_STATE, *PKARM64_FP_STATE;

/*
 * KARM64_NEON_STATE - Layout of saved NEON/ASIMD state.
 *
 * This is the format used when saving/restoring basic FP state
 * for threads that only use NEON (not SVE).
 */
typedef struct _KARM64_NEON_STATE
{
    /*
     * FPCR/FPSR control and status registers.
     */
    ULONG Fpcr;
    ULONG Fpsr;

    /*
     * Padding for 16-byte alignment of Q registers.
     */
    ULONG64 Reserved;

    /*
     * Q0-Q31: 32 128-bit NEON registers.
     * Stored as pairs of 64-bit values (low, high).
     */
    DECLSPEC_ALIGN(16) ULONG64 Q[32][2];

} KARM64_NEON_STATE, *PKARM64_NEON_STATE;

C_ASSERT(sizeof(KARM64_NEON_STATE) == KI_NEON_STATE_SIZE);
C_ASSERT((sizeof(KARM64_NEON_STATE) & 0xF) == 0);  /* 16-byte aligned */

/*
 * Global system FP/SVE configuration.
 * Set during boot and read-only thereafter.
 */
extern BOOLEAN KiArm64HasNeon;          /* CPU has NEON/ASIMD */
extern BOOLEAN KiArm64HasSve;           /* CPU has SVE */
extern BOOLEAN KiArm64HasSme;           /* CPU has SME */
extern ULONG KiArm64MaxSveVectorLength; /* Maximum SVE VL in bits */
extern ULONG KiArm64SveStateSize;       /* Size of SVE state buffer */

/*
 * Function prototypes for FP/SVE context management.
 */

/**
 * @brief Handle FP/SVE/SME trap when thread first accesses FP registers.
 *
 * This function is called from the synchronous exception handler when
 * ESR_EC indicates an FP/SVE/SME trap. It allocates FP state if needed,
 * restores any previously saved state, and enables FP access for the thread.
 *
 * @param TrapFrame Pointer to the current trap frame.
 * @param Esr Exception Syndrome Register value.
 *
 * @return TRUE if the trap was handled and execution should resume.
 *         FALSE if the trap indicates an error condition.
 */
BOOLEAN
KiArm64HandleFpTrap(
    _Inout_ PKTRAP_FRAME TrapFrame,
    _In_ ULONG Esr);

/**
 * @brief Save the current thread's FP state if dirty.
 *
 * Called during context switch to save FP registers to the thread's
 * FP state buffer. Only saves if the state is marked dirty.
 *
 * @param Thread Thread whose FP state to save.
 */
VOID
KiArm64SaveFpState(
    _Inout_ PKTHREAD Thread);

/**
 * @brief Restore a thread's FP state if it has been saved.
 *
 * Called during context switch to restore FP registers from the thread's
 * FP state buffer. If no state exists, configures trap-on-first-use.
 *
 * @param Thread Thread whose FP state to restore.
 */
VOID
KiArm64RestoreFpState(
    _Inout_ PKTHREAD Thread);

/**
 * @brief Free FP state buffer when thread is destroyed.
 *
 * Called during thread cleanup to free the allocated FP state buffer.
 *
 * @param Thread Thread whose FP state to free.
 */
VOID
KiArm64FreeFpState(
    _Inout_ PKTHREAD Thread);

/**
 * @brief Initialize FP/SVE subsystem during boot.
 *
 * Detects hardware capabilities and configures global FP/SVE state.
 * Called during KiInitializeSystem.
 */
VOID
KiArm64InitializeFpSupport(
    VOID);

/**
 * @brief Get the SVE state size for a given vector length.
 *
 * Calculates the buffer size needed to store SVE state for the
 * specified vector length.
 *
 * @param VectorLengthBits SVE vector length in bits.
 *
 * @return Size in bytes needed for SVE state buffer.
 */
ULONG
KiArm64GetSveStateSize(
    _In_ ULONG VectorLengthBits);

/**
 * @brief Configure CPACR_EL1 for trap-on-first-use.
 *
 * Disables FP/SVE access so the next FP instruction will trap,
 * enabling lazy context switching.
 */
FORCEINLINE
VOID
KiArm64ConfigureFpTrap(
    VOID)
{
    ULONG64 Cpacr;

    __asm__ __volatile__("mrs %0, cpacr_el1" : "=r"(Cpacr));

    /* Trap on FP/NEON access */
    Cpacr &= ~CPACR_EL1_FPEN_MASK;
    Cpacr |= CPACR_EL1_FPEN_TRAP;

    /* Trap on SVE access if hardware supports it */
    if (KiArm64HasSve)
    {
        Cpacr &= ~CPACR_EL1_ZEN_MASK;
        Cpacr |= CPACR_EL1_ZEN_TRAP;
    }

    /* Trap on SME access if hardware supports it */
    if (KiArm64HasSme)
    {
        Cpacr &= ~CPACR_EL1_SMEN_MASK;
        Cpacr |= CPACR_EL1_SMEN_TRAP;
    }

    __asm__ __volatile__("msr cpacr_el1, %0" :: "r"(Cpacr));
    __asm__ __volatile__("isb" ::: "memory");
}

/**
 * @brief Enable FP/NEON access (no trap).
 */
FORCEINLINE
VOID
KiArm64EnableFp(
    VOID)
{
    ULONG64 Cpacr;

    __asm__ __volatile__("mrs %0, cpacr_el1" : "=r"(Cpacr));
    Cpacr &= ~CPACR_EL1_FPEN_MASK;
    Cpacr |= CPACR_EL1_FPEN_ENABLE;
    __asm__ __volatile__("msr cpacr_el1, %0" :: "r"(Cpacr));
    __asm__ __volatile__("isb" ::: "memory");
}

/**
 * @brief Enable SVE access (no trap).
 */
FORCEINLINE
VOID
KiArm64EnableSve(
    VOID)
{
    ULONG64 Cpacr;

    __asm__ __volatile__("mrs %0, cpacr_el1" : "=r"(Cpacr));
    Cpacr &= ~CPACR_EL1_ZEN_MASK;
    Cpacr |= CPACR_EL1_ZEN_ENABLE;
    /* Also enable FP since SVE requires it */
    Cpacr &= ~CPACR_EL1_FPEN_MASK;
    Cpacr |= CPACR_EL1_FPEN_ENABLE;
    __asm__ __volatile__("msr cpacr_el1, %0" :: "r"(Cpacr));
    __asm__ __volatile__("isb" ::: "memory");
}

/**
 * @brief Check if FP state is dirty (needs saving).
 */
FORCEINLINE
BOOLEAN
KiArm64IsFpStateDirty(
    _In_ PKTHREAD Thread)
{
    /* Note: Implementation will access thread's Arm64FpState field */
    UNREFERENCED_PARAMETER(Thread);
    /* TODO: Return (Thread->Arm64FpState.Flags & KI_FP_CTX_FLAG_DIRTY) != 0; */
    return FALSE;
}

/**
 * @brief Mark FP state as dirty.
 */
FORCEINLINE
VOID
KiArm64MarkFpStateDirty(
    _Inout_ PKTHREAD Thread)
{
    /* Note: Implementation will access thread's Arm64FpState field */
    UNREFERENCED_PARAMETER(Thread);
    /* TODO: Thread->Arm64FpState.Flags |= KI_FP_CTX_FLAG_DIRTY; */
}

/* End of fpstate.h */
