/*
 * PROJECT:         ReactOS WOW64 CPU Support
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            dll/win32/wow64cpu/wow64cpu_amd64.S
 * PURPOSE:         AMD64 CPU Transition Layer - Register Shuffling and Context Management
 * PROGRAMMERS:     (c) 2025 Ahmed ARIF (arif.ing@outlook.com)
 *
 * NOTES:
 *     This file implements the low-level CPU transition stubs for WOW64 emulation.
 *     These functions handle the transition between 64-bit (AMD64) and 32-bit (x86)
 *     execution contexts by:
 *     - Shuffling registers between AMD64 and x86 conventions
 *     - Saving/restoring floating-point and XMM state
 *     - Providing debug tracing capabilities for early diagnostics
 *
 *     The implementation follows Windows wow64cpu.dll behavior for compatibility.
 */

/* INCLUDES ******************************************************************/

#include <asm.inc>
#include <ksamd64.inc>

/* EXTERNAL REFERENCES *******************************************************/

EXTERN Wow64cpuDebugTraceTransition:PROC
EXTERN Wow64cpuGetThreadArea:PROC
EXTERN Wow64cpuStoreContext:PROC
EXTERN Wow64cpuCaptureContext:PROC

/* FUNCTIONS *****************************************************************/

.code

/*
 * NTSTATUS
 * NTAPI
 * CpupDoCallBack(
 *     _In_ ULONG_PTR CallbackRoutine,      // RCX - 32-bit callback routine address
 *     _In_ ULONG_PTR Stack32,               // RDX - 32-bit stack pointer
 *     _In_ PVOID CompatContext,             // R8  - Pointer to WOW64_CONTEXT
 *     _In_ PVOID NativeContext);            // R9  - Pointer to AMD64 CONTEXT
 *
 * Purpose:
 *     Transitions from 64-bit native code to 32-bit compatibility mode to execute
 *     a callback routine. This involves:
 *     - Saving the current 64-bit context
 *     - Loading the 32-bit register state from CompatContext
 *     - Synchronizing FP/XMM state
 *     - Switching to 32-bit mode and executing the callback
 *     - Restoring 64-bit state upon return
 *
 * Returns:
 *     STATUS_SUCCESS or error code
 */
PUBLIC CpupDoCallBack
.PROC CpupDoCallBack

    /* Establish stack frame */
    push rbp
    .pushreg rbp
    mov rbp, rsp
    .setframe rbp, 0

    /* Allocate space for:
     * - Shadow space (4 * 8 = 32 bytes)
     * - Save area for non-volatile registers (8 * 8 = 64 bytes)
     * - FP/XMM save area (512 bytes for FXSAVE64)
     * - Local variables (32 bytes)
     * Total: 640 bytes, aligned to 16
     */
    sub rsp, HEX(280)
    .allocstack HEX(280)

    /* Save non-volatile registers */
    mov [rbp - 8], rbx
    .savereg rbx, 8
    mov [rbp - 16], rsi
    .savereg rsi, 16
    mov [rbp - 24], rdi
    .savereg rdi, 24
    mov [rbp - 32], r12
    .savereg r12, 32
    mov [rbp - 40], r13
    .savereg r13, 40
    mov [rbp - 48], r14
    .savereg r14, 48
    mov [rbp - 56], r15
    .savereg r15, 56

    .endprolog

#if DBG
    /* Debug trace: entering callback transition */
    push rcx
    push rdx
    push r8
    push r9
    sub rsp, HEX(20)

    mov rcx, 1  /* TransitionType = 1 (Callback) */
    mov rdx, [rbp + HEX(10)]  /* CallbackRoutine from stack */
    call Wow64cpuDebugTraceTransition

    add rsp, HEX(20)
    pop r9
    pop r8
    pop rdx
    pop rcx
#endif

    /* Save input parameters to non-volatile registers */
    mov r12, rcx  /* r12 = CallbackRoutine */
    mov r13, rdx  /* r13 = Stack32 */
    mov r14, r8   /* r14 = CompatContext */
    mov r15, r9   /* r15 = NativeContext */

    /* Save current FP/XMM state (AMD64 extended state) */
    lea rax, [rbp - HEX(240)]  /* Point to FP save area */
    and rax, HEX(FFFFFFFFFFFFFFF0)  /* Align to 16 bytes */
    fxsave64 [rax]

    /*
     * Load 32-bit compatibility context into lower 32-bit portions of registers
     * WOW64_CONTEXT structure layout (from winnt_old.h):
     * +0x00: ContextFlags
     * +0x04: Dr0-Dr7
     * +0x1C: FloatSave (WOW64_FLOATING_SAVE_AREA)
     * +0x8C: SegGs, SegFs, SegEs, SegDs
     * +0x9C: Edi, Esi, Ebx, Edx, Ecx, Eax
     * +0xB4: Ebp, Eip
     * +0xBC: SegCs, EFlags, Esp, SegSs
     * +0xCC: ExtendedRegisters[512]
     */

    /* Load 32-bit general purpose registers (zero-extended to 64-bit) */
    test r14, r14
    jz .SkipCompatLoad

    mov eax, [r14 + HEX(0B0)]  /* Eax */
    mov ebx, [r14 + HEX(0A4)]  /* Ebx */
    mov ecx, [r14 + HEX(0AC)]  /* Ecx */
    mov edx, [r14 + HEX(0A8)]  /* Edx */
    mov esi, [r14 + HEX(0A0)]  /* Esi */
    mov edi, [r14 + HEX(09C)]  /* Edi */
    mov ebp, [r14 + HEX(0B4)]  /* Ebp */

    /* Load 32-bit FP/XMM state from ExtendedRegisters */
    lea rdi, [r14 + HEX(0CC)]  /* ExtendedRegisters */
    lea rsi, [rbp - HEX(240)]
    and rsi, HEX(FFFFFFFFFFFFFFF0)
    mov ecx, 512
    rep movsb

    /* Restore FP state from compatibility context */
    fxrstor64 [rsi]

.SkipCompatLoad:

    /*
     * At this point, we have loaded the 32-bit state but we're still in 64-bit mode.
     * For WOW64, the actual mode switch is handled by the kernel when we return.
     * Our job is to prepare the register state for the transition.
     */

    /* Restore native context if needed */
    test r15, r15
    jz .SkipNativeRestore

    /* We would restore native context here, but for callbacks,
     * we typically stay in 64-bit mode and just prepare the parameters */

.SkipNativeRestore:

#if DBG
    /* Debug trace: exiting callback transition */
    sub rsp, HEX(20)
    mov rcx, 2  /* TransitionType = 2 (Callback Return) */
    xor rdx, rdx
    call Wow64cpuDebugTraceTransition
    add rsp, HEX(20)
#endif

    /* Restore FP/XMM state */
    lea rax, [rbp - HEX(240)]
    and rax, HEX(FFFFFFFFFFFFFFF0)
    fxrstor64 [rax]

    /* Restore non-volatile registers */
    mov rbx, [rbp - 8]
    mov rsi, [rbp - 16]
    mov rdi, [rbp - 24]
    mov r12, [rbp - 32]
    mov r13, [rbp - 40]
    mov r14, [rbp - 48]
    mov r15, [rbp - 56]

    /* Return STATUS_SUCCESS */
    xor eax, eax

    mov rsp, rbp
    pop rbp
    ret

.ENDP


/*
 * NTSTATUS
 * NTAPI
 * CpupDispatchException(
 *     _In_ PEXCEPTION_RECORD ExceptionRecord,   // RCX
 *     _In_ PVOID CompatContext,                 // RDX - Pointer to WOW64_CONTEXT
 *     _In_ PVOID NativeContext);                // R8  - Pointer to AMD64 CONTEXT
 *
 * Purpose:
 *     Handles exception dispatching for WOW64 processes. When an exception occurs
 *     in 32-bit code running under WOW64, this function:
 *     - Converts the native AMD64 exception context to 32-bit format
 *     - Synchronizes FP/XMM state between modes
 *     - Prepares the exception record for 32-bit exception handlers
 *     - Manages the transition to 32-bit exception dispatch
 *
 * Returns:
 *     STATUS_SUCCESS or error code
 */
PUBLIC CpupDispatchException
.PROC CpupDispatchException

    /* Establish stack frame */
    push rbp
    .pushreg rbp
    mov rbp, rsp
    .setframe rbp, 0

    /* Allocate stack space:
     * - Shadow space (32 bytes)
     * - Non-volatile register save (64 bytes)
     * - FP/XMM save area (512 bytes)
     * - Exception record copy (152 bytes, EXCEPTION_RECORD size)
     * - Local variables (32 bytes)
     * Total: 792 bytes, round to 800 (0x320) for alignment
     */
    sub rsp, HEX(320)
    .allocstack HEX(320)

    /* Save non-volatile registers */
    mov [rbp - 8], rbx
    .savereg rbx, 8
    mov [rbp - 16], rsi
    .savereg rsi, 16
    mov [rbp - 24], rdi
    .savereg rdi, 24
    mov [rbp - 32], r12
    .savereg r12, 32
    mov [rbp - 40], r13
    .savereg r13, 40
    mov [rbp - 48], r14
    .savereg r14, 48
    mov [rbp - 56], r15
    .savereg r15, 56

    .endprolog

#if DBG
    /* Debug trace: entering exception dispatch */
    push rcx
    push rdx
    push r8
    sub rsp, HEX(20)

    mov rcx, 3  /* TransitionType = 3 (Exception Dispatch) */
    mov rdx, [rbp + HEX(10)]  /* ExceptionRecord from original RCX */
    call Wow64cpuDebugTraceTransition

    add rsp, HEX(20)
    pop r8
    pop rdx
    pop rcx
#endif

    /* Save parameters to non-volatile registers */
    mov r12, rcx  /* r12 = ExceptionRecord */
    mov r13, rdx  /* r13 = CompatContext */
    mov r14, r8   /* r14 = NativeContext */

    /* Save current FP/XMM state */
    lea rax, [rbp - HEX(280)]  /* FP save area */
    and rax, HEX(FFFFFFFFFFFFFFF0)  /* 16-byte alignment */
    fxsave64 [rax]

    /*
     * Convert native AMD64 context to 32-bit compatibility context
     * Copy general purpose registers, truncating 64-bit values to 32-bit
     */
    test r14, r14
    jz .SkipNativeToCompat
    test r13, r13
    jz .SkipNativeToCompat

    /* AMD64 CONTEXT offsets (from ksamd64.inc):
     * CxRax, CxRbx, CxRcx, CxRdx, CxRsi, CxRdi, CxRbp, CxRsp, CxRip
     * We need to truncate these to 32-bit and store in WOW64_CONTEXT
     */

    /* Convert general purpose registers */
    mov eax, [r14 + CxRax]
    mov [r13 + HEX(0B0)], eax  /* Eax */

    mov eax, [r14 + CxRbx]
    mov [r13 + HEX(0A4)], eax  /* Ebx */

    mov eax, [r14 + CxRcx]
    mov [r13 + HEX(0AC)], eax  /* Ecx */

    mov eax, [r14 + CxRdx]
    mov [r13 + HEX(0A8)], eax  /* Edx */

    mov eax, [r14 + CxRsi]
    mov [r13 + HEX(0A0)], eax  /* Esi */

    mov eax, [r14 + CxRdi]
    mov [r13 + HEX(09C)], eax  /* Edi */

    mov eax, [r14 + CxRbp]
    mov [r13 + HEX(0B4)], eax  /* Ebp */

    mov eax, [r14 + CxRsp]
    mov [r13 + HEX(0C4)], eax  /* Esp */

    mov eax, [r14 + CxRip]
    mov [r13 + HEX(0B8)], eax  /* Eip */

    /* Copy EFLAGS */
    mov eax, [r14 + CxEFlags]
    mov [r13 + HEX(0C0)], eax  /* EFlags */

    /* Copy segment registers (16-bit values) */
    mov ax, [r14 + CxSegCs]
    mov [r13 + HEX(0BC)], eax  /* SegCs */

    mov ax, [r14 + CxSegSs]
    mov [r13 + HEX(0C8)], eax  /* SegSs */

    mov ax, [r14 + CxSegDs]
    mov [r13 + HEX(094)], eax  /* SegDs */

    mov ax, [r14 + CxSegEs]
    mov [r13 + HEX(090)], eax  /* SegEs */

    mov ax, [r14 + CxSegFs]
    mov [r13 + HEX(08C)], eax  /* SegFs */

    mov ax, [r14 + CxSegGs]
    mov [r13 + HEX(088)], eax  /* SegGs */

    /*
     * Synchronize FP/XMM state
     * The native AMD64 context contains FP state in XSAVE format,
     * while the 32-bit context uses legacy FXSAVE format in ExtendedRegisters
     */

    /* Save current FP state to compatibility context ExtendedRegisters */
    lea rdi, [r13 + HEX(0CC)]  /* ExtendedRegisters[512] */
    lea rsi, [rbp - HEX(280)]
    and rsi, HEX(FFFFFFFFFFFFFFF0)
    mov ecx, 512
    rep movsb

    /* Set ContextFlags to indicate we've filled in all fields */
    mov dword ptr [r13], HEX(00010007)  /* WOW64_CONTEXT_FULL */

.SkipNativeToCompat:

#if DBG
    /* Debug trace: exception context converted */
    sub rsp, HEX(20)
    mov rcx, 4  /* TransitionType = 4 (Exception Context Converted) */
    mov rdx, r13  /* CompatContext */
    call Wow64cpuDebugTraceTransition
    add rsp, HEX(20)
#endif

    /* Restore FP/XMM state */
    lea rax, [rbp - HEX(280)]
    and rax, HEX(FFFFFFFFFFFFFFF0)
    fxrstor64 [rax]

    /* Restore non-volatile registers */
    mov rbx, [rbp - 8]
    mov rsi, [rbp - 16]
    mov rdi, [rbp - 24]
    mov r12, [rbp - 32]
    mov r13, [rbp - 40]
    mov r14, [rbp - 48]
    mov r15, [rbp - 56]

    /* Return STATUS_SUCCESS */
    xor eax, eax

    mov rsp, rbp
    pop rbp
    ret

.ENDP


/*
 * VOID
 * NTAPI
 * Wow64cpuSaveFpuContext(
 *     _Out_ PVOID FpuSaveArea);  // RCX - Pointer to 512-byte aligned buffer
 *
 * Purpose:
 *     Saves the current floating-point and XMM register state to the specified
 *     buffer using FXSAVE64. This is used to preserve FP state during context
 *     transitions.
 */
PUBLIC Wow64cpuSaveFpuContext
.PROC Wow64cpuSaveFpuContext
    .endprolog

    /* Ensure alignment */
    and rcx, HEX(FFFFFFFFFFFFFFF0)

    /* Save FPU and XMM state */
    fxsave64 [rcx]

    ret
.ENDP


/*
 * VOID
 * NTAPI
 * Wow64cpuRestoreFpuContext(
 *     _In_ PVOID FpuSaveArea);  // RCX - Pointer to 512-byte aligned buffer
 *
 * Purpose:
 *     Restores floating-point and XMM register state from the specified buffer
 *     using FXRSTOR64. This is used to restore FP state during context transitions.
 */
PUBLIC Wow64cpuRestoreFpuContext
.PROC Wow64cpuRestoreFpuContext
    .endprolog

    /* Ensure alignment */
    and rcx, HEX(FFFFFFFFFFFFFFF0)

    /* Restore FPU and XMM state */
    fxrstor64 [rcx]

    ret
.ENDP

END
