
#if defined(_M_ARM64) || defined(_M_ARM64EC)
#define SYSCALLS_ARM64_STUBS 1
#else
#define SYSCALLS_ARM64_STUBS 0
#endif

#ifdef _M_ARM

#include <ksarm.h>

    GBLA SyscallId

SyscallId SETA 0

	MACRO
	STUB_K $Name
	LCLS ZwFuncName
	LCLS ZwFuncEndName
ZwFuncName    SETS "Zw$Name"
ZwFuncEndName SETS "$ZwFuncName":CC:"_end"
	ALIGN 2
	EXPORT $ZwFuncName [FUNC]
$ZwFuncName
	ROUT
    mov r12, #SyscallId
    svc #1
    bx lr
$ZwFuncEndName
    MEND

	MACRO
	STUB_U $Name
	LCLS NtFuncName
	LCLS NtFuncEndName
	LCLS ZwFuncName
	LCLS ZwFuncEndName
NtFuncName    SETS "Nt$Name"
NtFuncEndName SETS "$NtFuncName":CC:"_end"
ZwFuncName    SETS "Zw$Name"
ZwFuncEndName SETS "$ZwFuncName":CC:"_end"
	ALIGN 2
	EXPORT $NtFuncName [FUNC]
$NtFuncName
	EXPORT $ZwFuncName [FUNC]
$ZwFuncName
	ROUT
    mov r12, #SyscallId
    svc #1
    bx lr
$NtFuncEndName
$ZwFuncEndName
	MEND

#elif SYSCALLS_ARM64_STUBS

#include <ksarm64.h>

    .set SyscallId, 0

    .macro ARM64_ZW_CALL_NT Name, CookieOff
    /* Load current thread pointer from per-CPU PCR (tpidr_el1) */
    mrs     x19, tpidr_el1
    cbz     x19, .LZw\Name\()_call
    add     x19, x19, #PcCurrentThread
    ldr     x19, [x19]
    cbz     x19, .LZw\Name\()_call

    /* Save old PreviousMode and set to KernelMode (0) */
    ldrb    w20, [x19, #ThPreviousMode]
    strb    wzr, [x19, #ThPreviousMode]

    /*
     * Store PreviousMode in Thread->TrapFrame->Reserved with a cookie for
     * exception-unwind safety.  If Nt\Name raises or unwinds out (e.g.,
     * NtContinue via KiExceptionExit), the w20-based restore below is
     * skipped.  The cookie lets KiTrapReturn/trapc.c restore PreviousMode
     * from the trap frame after unwind.
     *
     * Preserve any outer cookie already in Reserved by spilling it to
     * the extra stack slot at sp + CookieOff.  The adjacent word at
     * CookieOff + 4 records whether the spill is valid.  On unwind the
     * slot is freed, but the new cookie survives in Reserved for the
     * trap-return path.
     */
    str     wzr, [sp, #(\CookieOff + 4)]
    ldr     x16, [x19, #ThTrapFrame]
    cbz     x16, .LZw\Name\()_no_cookie
    ldr     w17, [x16, #TfReserved]
    str     w17, [sp, #\CookieOff]
    mov     w17, #1
    str     w17, [sp, #(\CookieOff + 4)]
    movz    w17, #0x4D00
    movk    w17, #0x5056, lsl #16
    orr     w17, w17, w20
    str     w17, [x16, #TfReserved]
.LZw\Name\()_no_cookie:

.LZw\Name\()_call:
    /* Call the Nt function via indirect branch, avoiding bl's 128 MB range. */
    adrp    x16, Nt\Name
    add     x16, x16, #:lo12:Nt\Name
    blr     x16

    /* Restore PreviousMode if thread pointer was valid */
    cbz     x19, .LZw\Name\()_ret
    strb    w20, [x19, #ThPreviousMode]

    /* Restore outer cookie to TrapFrame->Reserved */
    ldr     w17, [sp, #(\CookieOff + 4)]
    cbz     w17, .LZw\Name\()_ret
    ldr     x16, [x19, #ThTrapFrame]
    cbz     x16, .LZw\Name\()_ret
    ldr     w17, [sp, #\CookieOff]
    str     w17, [x16, #TfReserved]
.LZw\Name\()_ret:
    .endm

    .macro STUB_K Name, ArgCount
    .align 2
    .global Zw\Name
Zw\Name:
    /*
     * ARM64 Zw* kernel wrapper: call Nt* with PreviousMode = KernelMode.
     *
     * Many Nt* functions check KeGetPreviousMode() and redirect via Zw*
     * when called from UserMode.  Without setting PreviousMode to KernelMode
     * here, the Nt* function would see UserMode, call Zw* again, which
     * branches back to Nt*, creating infinite recursion.
     *
     * Both paths (<=8 args and >8 args) save the old PreviousMode into a
     * callee-saved register, set KernelMode, call Nt\Name, and restore the
     * original PreviousMode before returning.
     *
     * The Nt\Name call uses adrp/add/blr to avoid the +/-128 MB range
     * limitation of the bl instruction.
     */
.if \ArgCount > 8
    /*
     * >8 args path.
     *
     * The caller placed stack arguments at [sp+0] .. [sp+(N-1)*8] where
     * N = ArgCount - 8.  We need frame space for callee-saved registers
     * (x29/x30 and x19/x20 = 32 bytes) while keeping the stack arguments
     * visible at [sp+0] for the Nt\Name callee.
     *
     * Strategy:
     *   1. Lower SP by frame_size = save_off + 48, where
     *      save_off = N*8 rounded up to 16-byte alignment.
     *   2. Save x29/x30 and x19/x20 at [sp + save_off] (prolog).
     *   3. Copy each stack argument from [sp + frame_size + i*8]
     *      down to [sp + i*8] (post-prologue, no SEH annotations needed).
     *   4. Save old PreviousMode, set KernelMode, store cookie in
     *      TrapFrame->Reserved, call, restore, return.
     *
     * Frame layout after setup (SP points to the bottom):
     *
     *   old sp + (N-1)*8   : original stack_arg[N-1]  (from caller)
     *   ...
     *   old sp + 0         : original stack_arg[0]
     *   sp + save_off + 32 : [cookie spill, valid flag]
     *   sp + save_off + 16 : [saved x19, x20]
     *   sp + save_off      : [saved x29, x30]
     *   sp + (N-1)*8       : stack_arg[N-1]  (copied for callee)
     *   ...
     *   sp + 0             : stack_arg[0]     (copied for callee)
     */
    .set _nstk, (\ArgCount - 8)
    .set _save_off, ((_nstk * 8 + 15) & ~15)
    .set _frame, (_save_off + 48)
    .set _cookie_off, (_save_off + 32)

    /* SEH unwind: begin procedure */
    .seh_proc Zw\Name

    /* Allocate frame: room for copied stack args + register save area
     * + cookie spill slot */
    sub     sp, sp, #_frame
    .seh_stackalloc _frame

    /* Save frame pointer, link register, and callee-saved registers */
    stp     x29, x30, [sp, #_save_off]
    .seh_save_fplr _save_off
    stp     x19, x20, [sp, #(_save_off + 16)]
    .seh_save_regp x19, (_save_off + 16)
    add     x29, sp, #_save_off
    .seh_add_fp _save_off
    .seh_endprologue

    /* Copy stack arguments down (unrolled, count known at assembly time).
     * This runs after seh_endprologue so no SEH annotations are needed.
     * Uses only x16 (IP0 scratch register). */
    .set _i, 0
    .rept _nstk
        ldr     x16, [sp, #(_frame + _i * 8)]
        str     x16, [sp, #(_i * 8)]
        .set _i, _i + 1
    .endr

    ARM64_ZW_CALL_NT \Name, _cookie_off
    ldp     x19, x20, [sp, #(_save_off + 16)]
    ldp     x29, x30, [sp, #_save_off]
    add     sp, sp, #_frame
    ret
    .seh_endproc
.else
    /*
     * <=8 args path.
     *
     * Kernel Zw* calls must not re-enter the SVC path. Same-EL SVC makes the
     * return path depend on a synthetic trap/exception frame even for ordinary
     * kernel calls, and a yielding service can resume with a corrupted LR.
     * Match the >8-argument path: save the caller frame, force KernelMode for
     * the Nt* body, call it directly, then restore PreviousMode and return.
     *
     * Frame layout: x29/x30 at sp+0, x19/x20 at sp+16,
     * cookie spill/valid flag at sp+32.
     */
    .seh_proc Zw\Name
    sub     sp, sp, #48
    .seh_stackalloc 48
    stp     x29, x30, [sp]
    .seh_save_fplr 0
    stp     x19, x20, [sp, #16]
    .seh_save_regp x19, 16
    add     x29, sp, #0
    .seh_add_fp 0
    .seh_endprologue

    ARM64_ZW_CALL_NT \Name, 32
    ldp     x19, x20, [sp, #16]
    ldp     x29, x30, [sp]
    add     sp, sp, #48
    ret
    .seh_endproc
.endif
    .set SyscallId, SyscallId + 1
    .endm

    .macro STUB_WRAP Name, ArgCount
    .align 2
    .global Zw\Name
Zw\Name:
    mov x8, #SyscallId
    svc #0
    ret
    .set SyscallId, SyscallId + 1
    .endm

    .macro STUB_U Name, ArgCount
    .align 2
    .global Nt\Name
    .global Zw\Name
Nt\Name:
Zw\Name:
    mov x8, #SyscallId
    svc #0
    ret
    .set SyscallId, SyscallId + 1
    .endm

#else

#include <asm.inc>

SyscallId = 0

#ifdef _M_IX86
#define KUSER_SHARED_SYSCALL HEX(7ffe0300)
#define KGDT_R0_CODE 8
MACRO(STUBCODE_U, Name, SyscallId, ArgCount)
    StackBytes = 4 * ArgCount
    FPO 0, 0, 0, 0, 0, FRAME_FPO
    mov eax, SyscallId
#ifdef WOW64_I386_RUNTIME
    call dword ptr fs:[HEX(c0)]
#else
    mov edx, KUSER_SHARED_SYSCALL
    call dword ptr [edx]
#endif
    ret StackBytes
ENDM
MACRO(STUBCODE_K, Name, SyscallId, ArgCount)
#ifdef _USE_ML
    StackBytes = 4 * &ArgCount
#else
    StackBytes = 4 * ArgCount
#endif
    FPO 0, 0, 0, 0, 0, FRAME_FPO
    mov eax, SyscallId
    lea edx, [esp + 4]
    pushfd
    push KGDT_R0_CODE
    call _KiSystemService
    ret StackBytes
ENDM
#elif defined(_M_AMD64)
MACRO(STUBCODE_U, Name, SyscallId, ArgCount)
    .ENDPROLOG
    mov eax, SyscallId
    mov r10, rcx
    syscall
    ret
    ALIGN 32
ENDM
MACRO(STUBCODE_K, Name, SyscallId, ArgCount)
    .ENDPROLOG
    EXTERN Nt\Name:PROC
    lea rax, Nt\Name[rip]
    mov r10, ArgCount * 8
    jmp KiZwSystemService
ENDM
#elif defined(_M_PPC)
MACRO(STUBCODE_U, Name, SyscallId, ArgCount)
    stwu 1,-16(1)
    mflr 0
    stw  0,0(1)
    li   0, SyscallId
    sc
    lwz 0,0(1)
    mtlr 0
    addi 1,1,16
    blr
ENDM
#define STUBCODE_K STUBCODE_U
#elif defined(_M_MIPS)
MACRO(STUBCODE_U, Name, SyscallId, ArgCount)
    li $8, KUSER_SHARED_SYSCALL
    lw $8,0($8)
    j $8
    nop
ENDM
MACRO(STUBCODE_K, Name, SyscallId, ArgCount)
    j KiSystemService
    nop
ENDM
#else
#error unsupported architecture
#endif

#ifdef _M_IX86
#ifdef _USE_ML
MACRO(MAKE_LABEL, Name, StackBytes)
    PUBLIC _&Name&@&StackBytes
    _&Name&@&StackBytes:
ENDM
MACRO(START_PROC, Name, StackBytes)
    PUBLIC _&Name&@&StackBytes
    .PROC _&Name&@&StackBytes
ENDM
#else
MACRO(MAKE_LABEL, Name, StackBytes)
    PUBLIC _\Name\()@\StackBytes
    _\Name\()@\StackBytes:
ENDM
MACRO(START_PROC, Name, StackBytes)
    PUBLIC _\Name\()@\StackBytes
    .PROC _\Name\()@\StackBytes
ENDM
#endif
#else
MACRO(MAKE_LABEL, Name, StackBytes)
    PUBLIC \Name
    \Name:
ENDM
MACRO(START_PROC, Name, StackBytes)
    PUBLIC \Name
    .PROC \Name
ENDM
#endif

MACRO(STUB_U, Name, ArgCount)
    MAKE_LABEL Zw\Name, %ArgCount * 4
    START_PROC Nt\Name, %ArgCount * 4
    STUBCODE_U Name, SyscallId, %ArgCount
    .ENDP
    SyscallId = SyscallId + 1
ENDM

MACRO(STUB_K, Name, ArgCount)
    START_PROC Zw\Name, %ArgCount * 4
    STUBCODE_K Name, SyscallId, %ArgCount
    .ENDP
    SyscallId = SyscallId + 1
ENDM

#endif
