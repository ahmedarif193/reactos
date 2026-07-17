/*
 * PROJECT:         ReactOS HAL (ARM64)
 * FILE:            hal/halarm64/smp_trampoline.S
 * PURPOSE:         AP (Application Processor) startup trampoline for SMP boot
 *
 * DESCRIPTION:
 *   This file contains the identity-mapped trampoline code that secondary
 *   CPUs (APs) execute when brought online via PSCI CPU_ON. The trampoline:
 *
 *   1. Runs with MMU disabled initially (PSCI brings CPU up in this state)
 *   2. Sets up TTBR0/TTBR1 with the kernel page tables
 *   3. Configures MAIR, TCR, and other system registers
 *   4. Enables the MMU
 *   5. Jumps to the kernel AP entry point with proper context
 *
 *   The code must be position-independent and fit within a single page.
 *   It is copied to a physically contiguous, identity-mapped region at
 *   runtime, and the physical address is passed to PSCI CPU_ON.
 *
 * ARM64 PSCI CPU_ON semantics:
 *   - x0 contains the context_id passed to CPU_ON (we use CPU number)
 *   - Entry point runs at EL1 with MMU disabled
 *   - All caches are in an undefined state
 *   - Interrupts are masked (DAIF = 0xF)
 *
 * Memory ordering:
 *   We use DSB and ISB barriers extensively to ensure correct ordering
 *   of system register updates and memory operations on the weakly
 *   ordered ARM64 architecture.
 */

/*
 * HAL_ARM64_AP_TRAMPOLINE_DATA offset definitions
 * These must match the C structure in smp.c
 */
#define AP_DATA_TTBR0_OFF       0x00
#define AP_DATA_TTBR1_OFF       0x08
#define AP_DATA_MAIR_OFF        0x10
#define AP_DATA_TCR_OFF         0x18
#define AP_DATA_SCTLR_OFF       0x20
#define AP_DATA_VBAR_OFF        0x28
#define AP_DATA_ENTRY_OFF       0x30
#define AP_DATA_STACK_OFF       0x38
#define AP_DATA_ARG0_OFF        0x40
#define AP_DATA_ARG1_OFF        0x48
#define AP_DATA_SYNC_OFF        0x50
#define AP_DATA_CPU_NUM_OFF     0x58
#define AP_DATA_GICR_BASE_OFF   0x60
#define AP_DATA_PROGRESS_OFF    0x68
#define AP_DATA_CPACR_OFF       0x70
#define AP_DATA_SIZE            0x78

/*
 * GICv3 Redistributor register offsets
 */
#define GICR_WAKER              0x014
#define GICR_WAKER_SLEEP        (1 << 1)
#define GICR_WAKER_CHILDREN     (1 << 2)

/*
 * GICv3 CPU Interface System Registers
 */
#define ICC_SRE_EL1_SRE         (1 << 0)
#define ICC_SRE_EL1_DFB         (1 << 1)
#define ICC_SRE_EL1_DIB         (1 << 2)

    .text
    .balign 4096  /* Page-aligned for identity mapping */

/*
 * HalpArm64ApTrampolineEntry - AP startup trampoline entry point
 *
 * This is where PSCI CPU_ON drops us. We're running with:
 *   - MMU disabled
 *   - At EL1
 *   - DAIF = 0xF (all interrupts masked)
 *   - x0 = context_id (CPU number in our implementation)
 *   - No valid stack
 *
 * The BSP patches HalpArm64ApDataPhys (located at the end of this trampoline)
 * with the physical address of the data structure before copying and starting APs.
 */
    .globl HalpArm64ApTrampolineEntry
HalpArm64ApTrampolineEntry:
    /*
     * Phase 1: Initial setup (MMU disabled)
     *
     * Save context_id and get data structure pointer.
     * x19 = data structure pointer (callee-saved)
     * x20 = CPU number (callee-saved)
     *
     * Use ADR (not ADRP) to load the address of the embedded data pointer.
     * ADR computes PC-relative address within +/-1MB range.
     * Since the trampoline is < 4KB, this always works.
     */
    mov     x20, x0                 /* Save CPU number */

    mrs     x0, CurrentEL
    lsr     x0, x0, #2
    cmp     x0, #2
    b.ne    .Lap_el1_entry

    mov     x0, #0x80000000
    msr     hcr_el2, x0
    isb

    mov     x0, #3
    msr     cnthctl_el2, x0
    msr     cntvoff_el2, xzr
    msr     cptr_el2, xzr
    msr     hstr_el2, xzr
    msr     mdcr_el2, xzr
    msr     vttbr_el2, xzr

    mov     x0, #0x3C5
    msr     spsr_el2, x0
    adr     x0, .Lap_el1_entry
    msr     elr_el2, x0
    isb
    eret

.Lap_el1_entry:
    adr     x19, HalpArm64ApDataPhys
    ldr     x19, [x19]              /* Load the data structure physical address */
    cbz     x19, .Lap_halt          /* Safety: halt if no data */

    /* Progress 0x11: Data pointer loaded successfully */
    mov     w1, #0x11
    str     w1, [x19, #AP_DATA_PROGRESS_OFF]
    dsb     sy

    /*
     * Phase 2: Initialize GICv3 Redistributor
     *
     * We must wake the redistributor before enabling interrupts.
     * This clears ProcessorSleep and waits for ChildrenAsleep to clear.
     */
    ldr     x0, [x19, #AP_DATA_GICR_BASE_OFF]
    cbz     x0, .Lap_skip_gicr      /* Skip if no GICR base provided */

    /* Clear ProcessorSleep bit in GICR_WAKER */
    ldr     w1, [x0, #GICR_WAKER]
    bic     w1, w1, #GICR_WAKER_SLEEP
    str     w1, [x0, #GICR_WAKER]
    dsb     sy

    /* Wait for ChildrenAsleep to clear (with timeout) */
    mov     x2, #0x100000           /* Timeout counter */
.Lap_gicr_wait:
    ldr     w1, [x0, #GICR_WAKER]
    tst     w1, #GICR_WAKER_CHILDREN
    beq     .Lap_gicr_done
    sub     x2, x2, #1
    cbnz    x2, .Lap_gicr_wait
    /* Timeout - continue anyway, HAL will handle it */

.Lap_gicr_done:
    /* Enable system register interface for GICv3 */
    mov     x1, #(ICC_SRE_EL1_SRE | ICC_SRE_EL1_DFB | ICC_SRE_EL1_DIB)
    msr     icc_sre_el1, x1
    isb

.Lap_skip_gicr:
    /* Progress 0x22: GICR init done */
    mov     w1, #0x22
    str     w1, [x19, #AP_DATA_PROGRESS_OFF]
    dsb     sy

    /*
     * Phase 3: Configure MMU
     *
     * Set up the page table registers before enabling the MMU.
     * We load the same configuration as the BSP.
     */

    /* Invalidate TLB and I-cache before any configuration */
    tlbi    vmalle1
    ic      iallu
    dsb     sy
    isb

    /* Load and set MAIR_EL1 (memory attribute indirection) */
    ldr     x0, [x19, #AP_DATA_MAIR_OFF]
    msr     mair_el1, x0
    isb

    /* Load and set TCR_EL1 (translation control) */
    ldr     x0, [x19, #AP_DATA_TCR_OFF]
    msr     tcr_el1, x0
    isb

    /* Progress 0x33: TLB/MAIR/TCR configured */
    mov     w1, #0x33
    str     w1, [x19, #AP_DATA_PROGRESS_OFF]
    dsb     sy

    /* Load and set TTBR0_EL1 (user space / identity map) */
    ldr     x0, [x19, #AP_DATA_TTBR0_OFF]
    msr     ttbr0_el1, x0
    isb

    /* Load and set TTBR1_EL1 (kernel space) */
    ldr     x0, [x19, #AP_DATA_TTBR1_OFF]
    msr     ttbr1_el1, x0
    isb

    /* Progress 0x44: TTBR0/TTBR1 loaded */
    mov     w1, #0x44
    str     w1, [x19, #AP_DATA_PROGRESS_OFF]
    dsb     sy

    /*
     * Phase 4: Enable MMU
     *
     * The SCTLR write enables the MMU. After this, we're executing
     * via the identity map (TTBR0). We must execute an ISB to
     * synchronize the context.
     */

    /* Ensure all page table updates are visible */
    dsb     sy

    /* Invalidate TLB again before enabling MMU */
    tlbi    vmalle1
    dsb     sy
    isb

    /* Progress 0x55: About to enable MMU */
    mov     w1, #0x55
    str     w1, [x19, #AP_DATA_PROGRESS_OFF]
    dsb     sy

    /* Load and set SCTLR_EL1 (enable MMU, caches) */
    ldr     x0, [x19, #AP_DATA_SCTLR_OFF]
    msr     sctlr_el1, x0
    isb

    /* Progress 0x66: MMU enabled successfully (identity map works!) */
    mov     w1, #0x66
    str     w1, [x19, #AP_DATA_PROGRESS_OFF]
    dsb     sy

    /* Vector entry saves Q0-Q31, so enable FP/ASIMD before publishing VBAR. */
    ldr     x0, [x19, #AP_DATA_CPACR_OFF]
    msr     cpacr_el1, x0
    isb

    /* Phase 5: Install the kernel's exception-vector base. */
    ldr     x0, [x19, #AP_DATA_VBAR_OFF]
    msr     vbar_el1, x0
    isb

    /* Progress 0x77: VBAR set */
    mov     w1, #0x77
    str     w1, [x19, #AP_DATA_PROGRESS_OFF]
    dsb     sy

    /*
     * Phase 6: Switch to kernel stack and entry point
     *
     * Now that MMU is on, we can use virtual addresses.
     * Set up the stack and jump to the kernel AP entry.
     */

    /* Load kernel stack pointer */
    ldr     x0, [x19, #AP_DATA_STACK_OFF]
    mov     sp, x0

    /* Progress 0x88: Stack set */
    mov     w1, #0x88
    str     w1, [x19, #AP_DATA_PROGRESS_OFF]
    dsb     sy

    /* Signal that we're about to enter kernel code */
    ldr     x0, [x19, #AP_DATA_SYNC_OFF]
    cbz     x0, .Lap_no_sync

    /* Progress 0x99: About to signal sync */
    mov     w1, #0x99
    str     w1, [x19, #AP_DATA_PROGRESS_OFF]
    dsb     sy

    mov     w1, #1
    stlr    w1, [x0]                /* Release store: signal BSP */
    dsb     sy

.Lap_no_sync:
    /* Load arguments for kernel entry */
    ldr     x0, [x19, #AP_DATA_ARG0_OFF]   /* PLOADER_PARAMETER_BLOCK */
    ldr     x1, [x19, #AP_DATA_ARG1_OFF]   /* Additional context if needed */

    /* Load and branch to kernel AP entry point */
    ldr     x2, [x19, #AP_DATA_ENTRY_OFF]
    cbz     x2, .Lap_halt           /* Safety: halt if no entry point */

    /*
     * Jump to kernel entry point.
     * This is typically KiInitializeSystem or similar.
     * The function will not return.
     */
    br      x2

.Lap_halt:
    /*
     * Fatal error: halt the CPU in a low-power state.
     * This should never happen if everything is configured correctly.
     */
    wfi
    b       .Lap_halt

/*
 * HalpArm64ApDataPhys - Physical address of AP data structure
 *
 * This is EMBEDDED within the trampoline code area.
 * The BSP calculates the offset of this variable within the trampoline
 * and patches the COPIED trampoline with the data structure address.
 *
 * This variable is NOT accessed directly from C code - the C code
 * calculates the offset using HalpArm64ApDataPhysOffset and patches
 * the copied trampoline.
 */
    .balign 8
HalpArm64ApDataPhys:
    .quad   0

/*
 * Trampoline end marker - must be after all embedded data
 */
    .globl HalpArm64ApTrampolineEnd
HalpArm64ApTrampolineEnd:
    nop

/*
 * The following are in data section - accessed from C code, not from trampoline
 */
    .data

/*
 * HalpArm64ApTrampolineSize - Size of trampoline code (including embedded data)
 *
 * Used by the HAL to allocate and copy the trampoline.
 * This is in .data section to avoid ARM64 relocation issues.
 */
    .globl HalpArm64ApTrampolineSize
    .balign 4
HalpArm64ApTrampolineSize:
    .long   HalpArm64ApTrampolineEnd - HalpArm64ApTrampolineEntry

/*
 * HalpArm64ApDataPhysOffset - Offset of HalpArm64ApDataPhys within trampoline
 *
 * Used by C code to calculate where to patch the data address in the
 * copied trampoline.
 */
    .globl HalpArm64ApDataPhysOffset
    .balign 4
HalpArm64ApDataPhysOffset:
    .long   HalpArm64ApDataPhys - HalpArm64ApTrampolineEntry

