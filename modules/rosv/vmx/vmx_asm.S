/*
 * PROJECT:     ReactOS VMX Hypervisor Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Assembly stubs for VMX instructions (amd64, Intel syntax via asm.inc)
 * COPYRIGHT:   Copyright 2025 Ahmed Arif
 */

#include <asm.inc>

/*
 * ROSV_GUEST_REGS offsets (from vm.h struct _ROSV_GUEST_REGS).
 * Each field is ULONG64 (8 bytes).
 */
#define GUEST_REG_RAX   0x00
#define GUEST_REG_RCX   0x08
#define GUEST_REG_RDX   0x10
#define GUEST_REG_RBX   0x18
#define GUEST_REG_RSP   0x20
#define GUEST_REG_RBP   0x28
#define GUEST_REG_RSI   0x30
#define GUEST_REG_RDI   0x38
#define GUEST_REG_R8    0x40
#define GUEST_REG_R9    0x48
#define GUEST_REG_R10   0x50
#define GUEST_REG_R11   0x58
#define GUEST_REG_R12   0x60
#define GUEST_REG_R13   0x68
#define GUEST_REG_R14   0x70
#define GUEST_REG_R15   0x78

/*
 * Offset of GuestRegs within ROSV_VCPU:
 *   ULONG VcpuId          = 0x00 (4 bytes + 4 padding for alignment)
 *   PROSV_VM Vm           = 0x08 (8 bytes)
 *   PVOID VmxonRegion     = 0x10 (8 bytes)
 *   PHYSICAL_ADDRESS ...  = 0x18 (8 bytes)
 *   PVOID VmcsRegion      = 0x20 (8 bytes)
 *   PHYSICAL_ADDRESS ...  = 0x28 (8 bytes)
 *   ROSV_GUEST_REGS       = 0x30
 */
#define VCPU_GUEST_REGS 0x30

.code64

/*
 * VMX_RESULT RosvAsmVmxOn(PHYSICAL_ADDRESS *VmxonRegion)
 *   rcx = pointer to 64-bit physical address of VMXON region
 * Returns: 0=OK, 1=CF (fail invalid), 2=ZF (fail valid)
 *
 * vmxon m64: prefix F3, opcode 0F C7 /6 => F3 0F C7 with ModRM for [rcx]
 * ModRM for [rcx] with /6: mod=00, reg=6, rm=001(rcx) => 0x31
 */
PUBLIC RosvAsmVmxOn
RosvAsmVmxOn:
    .byte 0xF3, 0x0F, 0xC7, 0x31
    jc .LVmxOnFailInvalid
    jz .LVmxOnFailValid
    xor eax, eax
    ret
.LVmxOnFailInvalid:
    mov eax, 1
    ret
.LVmxOnFailValid:
    mov eax, 2
    ret

/*
 * VMX_RESULT RosvAsmVmxOff(void)
 * Returns: 0=OK, 1=CF, 2=ZF
 */
PUBLIC RosvAsmVmxOff
RosvAsmVmxOff:
    vmxoff
    jc .LVmxOffFailInvalid
    jz .LVmxOffFailValid
    xor eax, eax
    ret
.LVmxOffFailInvalid:
    mov eax, 1
    ret
.LVmxOffFailValid:
    mov eax, 2
    ret

/*
 * VMX_RESULT RosvAsmVmClear(PHYSICAL_ADDRESS *VmcsRegion)
 *   rcx = pointer to 64-bit physical address
 *
 * vmclear m64: prefix 66, opcode 0F C7 /6 => 66 0F C7 with ModRM for [rcx]
 * ModRM: mod=00, reg=6, rm=001(rcx) => 0x31
 */
PUBLIC RosvAsmVmClear
RosvAsmVmClear:
    .byte 0x66, 0x0F, 0xC7, 0x31
    jc .LVmClearFailInvalid
    jz .LVmClearFailValid
    xor eax, eax
    ret
.LVmClearFailInvalid:
    mov eax, 1
    ret
.LVmClearFailValid:
    mov eax, 2
    ret

/*
 * VMX_RESULT RosvAsmVmPtrLoad(PHYSICAL_ADDRESS *VmcsRegion)
 *   rcx = pointer to 64-bit physical address
 *
 * vmptrld m64: opcode 0F C7 /6 => 0F C7 with ModRM for [rcx]
 * No prefix. ModRM: mod=00, reg=6, rm=001(rcx) => 0x31
 */
PUBLIC RosvAsmVmPtrLoad
RosvAsmVmPtrLoad:
    .byte 0x0F, 0xC7, 0x31
    jc .LVmPtrLoadFailInvalid
    jz .LVmPtrLoadFailValid
    xor eax, eax
    ret
.LVmPtrLoadFailInvalid:
    mov eax, 1
    ret
.LVmPtrLoadFailValid:
    mov eax, 2
    ret

/*
 * VMX_RESULT RosvAsmVmRead(ULONG64 Field, ULONG64 *Value)
 *   rcx = VMCS field encoding
 *   rdx = pointer to output value
 *
 * vmread r/m64, r64: opcode 0F 78 /r
 * We want vmread [rdx], rcx
 * ModRM: mod=00, reg=001(rcx), rm=010(rdx) => 0x0A
 */
PUBLIC RosvAsmVmRead
RosvAsmVmRead:
    .byte 0x0F, 0x78, 0x0A
    jc .LVmReadFailInvalid
    jz .LVmReadFailValid
    xor eax, eax
    ret
.LVmReadFailInvalid:
    mov eax, 1
    ret
.LVmReadFailValid:
    mov eax, 2
    ret

/*
 * VMX_RESULT RosvAsmVmWrite(ULONG64 Field, ULONG64 Value)
 *   rcx = VMCS field encoding
 *   rdx = value to write
 *
 * vmwrite r64, r/m64: opcode 0F 79 /r
 * We want vmwrite rcx, rdx
 * ModRM: mod=11, reg=001(rcx), rm=010(rdx) => 0xCA
 */
PUBLIC RosvAsmVmWrite
RosvAsmVmWrite:
    .byte 0x0F, 0x79, 0xCA
    jc .LVmWriteFailInvalid
    jz .LVmWriteFailValid
    xor eax, eax
    ret
.LVmWriteFailInvalid:
    mov eax, 1
    ret
.LVmWriteFailValid:
    mov eax, 2
    ret

/*
 * VMX_RESULT RosvAsmVmLaunch(void)
 * Bare VMLAUNCH - used for testing. Prefer RosvAsmVmRun for full GPR save/restore.
 */
PUBLIC RosvAsmVmLaunch
RosvAsmVmLaunch:
    vmlaunch
    jc .LVmLaunchFailInvalid
    jz .LVmLaunchFailValid
    xor eax, eax
    ret
.LVmLaunchFailInvalid:
    mov eax, 1
    ret
.LVmLaunchFailValid:
    mov eax, 2
    ret

/*
 * VMX_RESULT RosvAsmVmResume(void)
 * Bare VMRESUME - used for testing. Prefer RosvAsmVmRunResume for full GPR save/restore.
 */
PUBLIC RosvAsmVmResume
RosvAsmVmResume:
    vmresume
    jc .LVmResumeFailInvalid
    jz .LVmResumeFailValid
    xor eax, eax
    ret
.LVmResumeFailInvalid:
    mov eax, 1
    ret
.LVmResumeFailValid:
    mov eax, 2
    ret

/*
 * void RosvAsmVmExitHandler(void)
 *
 * VM-exit entry point. The processor loads HOST_RIP to here and HOST_RSP
 * to the value we wrote to the VMCS before VM-entry.
 *
 * Stack layout at entry (set up by RosvAsmVmRun/RosvAsmVmRunResume):
 *   [rsp+0]  = PROSV_VCPU pointer
 *   [rsp+8]  = stale guest RCX (pushed before VM-entry, now outdated)
 *   [rsp+16] = saved r15
 *   [rsp+24] = saved r14
 *   [rsp+32] = saved r13
 *   [rsp+40] = saved r12
 *   [rsp+48] = saved rdi
 *   [rsp+56] = saved rsi
 *   [rsp+64] = saved rbp
 *   [rsp+72] = saved rbx
 *   [rsp+80] = return address
 *
 * CRITICAL: On VM-exit, GPRs hold CURRENT guest values. We must save the
 * actual guest RCX before loading the VCPU pointer from the stack.
 * The old approach (pop rcx) destroyed the guest RCX and later saved
 * the stale pre-entry value, breaking rep-prefixed instructions.
 */
PUBLIC RosvAsmVmExitHandler
RosvAsmVmExitHandler:
    /* Atomically swap: RCX gets VCPU pointer, [rsp] gets actual guest RCX */
    xchg rcx, [rsp]

    /* Save guest GPRs into Vcpu->GuestRegs */
    mov [rcx + VCPU_GUEST_REGS + GUEST_REG_RAX], rax
    mov [rcx + VCPU_GUEST_REGS + GUEST_REG_RDX], rdx
    mov [rcx + VCPU_GUEST_REGS + GUEST_REG_RBX], rbx
    mov [rcx + VCPU_GUEST_REGS + GUEST_REG_RBP], rbp
    mov [rcx + VCPU_GUEST_REGS + GUEST_REG_RSI], rsi
    mov [rcx + VCPU_GUEST_REGS + GUEST_REG_RDI], rdi
    mov [rcx + VCPU_GUEST_REGS + GUEST_REG_R8],  r8
    mov [rcx + VCPU_GUEST_REGS + GUEST_REG_R9],  r9
    mov [rcx + VCPU_GUEST_REGS + GUEST_REG_R10], r10
    mov [rcx + VCPU_GUEST_REGS + GUEST_REG_R11], r11
    mov [rcx + VCPU_GUEST_REGS + GUEST_REG_R12], r12
    mov [rcx + VCPU_GUEST_REGS + GUEST_REG_R13], r13
    mov [rcx + VCPU_GUEST_REGS + GUEST_REG_R14], r14
    mov [rcx + VCPU_GUEST_REGS + GUEST_REG_R15], r15

    /* Save the ACTUAL guest RCX (placed on stack by xchg above) */
    pop rax
    mov [rcx + VCPU_GUEST_REGS + GUEST_REG_RCX], rax

    /* Skip the stale pre-entry guest RCX that was pushed by VmRun/VmRunResume */
    add rsp, 8

    /* Restore host non-volatile registers */
    pop r15
    pop r14
    pop r13
    pop r12
    pop rdi
    pop rsi
    pop rbp
    pop rbx

    /* Return to the C run loop (return address is at [rsp]) */
    xor eax, eax
    ret

/*
 * void RosvAsmVmRun(PROSV_VCPU Vcpu)
 *   rcx = pointer to ROSV_VCPU
 *
 * First VM-entry using VMLAUNCH:
 * 1. Save host non-volatile registers
 * 2. Push saved guest RCX and VCPU pointer on stack
 * 3. Write HOST_RSP = current RSP to VMCS
 * 4. Restore guest GPRs from Vcpu->GuestRegs
 * 5. Execute VMLAUNCH
 * 6. On VM-exit, processor jumps to RosvAsmVmExitHandler
 *    which saves guest state, restores host state, and returns here
 * 7. On VMLAUNCH failure, unwind stack and return error
 */
PUBLIC RosvAsmVmRun
RosvAsmVmRun:
    /* Save host non-volatile registers (Windows x64 ABI) */
    push rbx
    push rbp
    push rsi
    push rdi
    push r12
    push r13
    push r14
    push r15

    /* Push guest RCX (will be popped by exit handler) */
    mov rax, [rcx + VCPU_GUEST_REGS + GUEST_REG_RCX]
    push rax

    /* Push VCPU pointer (will be popped by exit handler) */
    push rcx

    /*
     * Write HOST_RSP in VMCS = current RSP.
     * VMCS_HOST_RSP = 0x6C14.
     * vmwrite rax, rdx: opcode 0F 79, ModRM=0xC2 (mod=11, reg=rax(0), rm=rdx(2))
     */
    mov rax, HEX(6C14)
    mov rdx, rsp
    .byte 0x0F, 0x79, 0xC2

    /* Reload VCPU pointer from stack (it's at [rsp]) */
    mov rcx, [rsp]

    /* Restore guest GPRs */
    mov rax, [rcx + VCPU_GUEST_REGS + GUEST_REG_RAX]
    mov rdx, [rcx + VCPU_GUEST_REGS + GUEST_REG_RDX]
    mov rbx, [rcx + VCPU_GUEST_REGS + GUEST_REG_RBX]
    mov rbp, [rcx + VCPU_GUEST_REGS + GUEST_REG_RBP]
    mov rsi, [rcx + VCPU_GUEST_REGS + GUEST_REG_RSI]
    mov rdi, [rcx + VCPU_GUEST_REGS + GUEST_REG_RDI]
    mov r8,  [rcx + VCPU_GUEST_REGS + GUEST_REG_R8]
    mov r9,  [rcx + VCPU_GUEST_REGS + GUEST_REG_R9]
    mov r10, [rcx + VCPU_GUEST_REGS + GUEST_REG_R10]
    mov r11, [rcx + VCPU_GUEST_REGS + GUEST_REG_R11]
    mov r12, [rcx + VCPU_GUEST_REGS + GUEST_REG_R12]
    mov r13, [rcx + VCPU_GUEST_REGS + GUEST_REG_R13]
    mov r14, [rcx + VCPU_GUEST_REGS + GUEST_REG_R14]
    mov r15, [rcx + VCPU_GUEST_REGS + GUEST_REG_R15]
    /* Load guest RCX last (clobbers our pointer) */
    mov rcx, [rcx + VCPU_GUEST_REGS + GUEST_REG_RCX]

    vmlaunch

    /* VMLAUNCH failed - fall through */
    jmp .LVmRunFailed

/*
 * void RosvAsmVmRunResume(PROSV_VCPU Vcpu)
 *   rcx = pointer to ROSV_VCPU
 *
 * Subsequent VM-entries using VMRESUME. Same pattern as RosvAsmVmRun.
 */
PUBLIC RosvAsmVmRunResume
RosvAsmVmRunResume:
    push rbx
    push rbp
    push rsi
    push rdi
    push r12
    push r13
    push r14
    push r15

    mov rax, [rcx + VCPU_GUEST_REGS + GUEST_REG_RCX]
    push rax
    push rcx

    /* Write HOST_RSP = current RSP */
    mov rax, HEX(6C14)
    mov rdx, rsp
    .byte 0x0F, 0x79, 0xC2

    mov rcx, [rsp]

    /* Restore guest GPRs */
    mov rax, [rcx + VCPU_GUEST_REGS + GUEST_REG_RAX]
    mov rdx, [rcx + VCPU_GUEST_REGS + GUEST_REG_RDX]
    mov rbx, [rcx + VCPU_GUEST_REGS + GUEST_REG_RBX]
    mov rbp, [rcx + VCPU_GUEST_REGS + GUEST_REG_RBP]
    mov rsi, [rcx + VCPU_GUEST_REGS + GUEST_REG_RSI]
    mov rdi, [rcx + VCPU_GUEST_REGS + GUEST_REG_RDI]
    mov r8,  [rcx + VCPU_GUEST_REGS + GUEST_REG_R8]
    mov r9,  [rcx + VCPU_GUEST_REGS + GUEST_REG_R9]
    mov r10, [rcx + VCPU_GUEST_REGS + GUEST_REG_R10]
    mov r11, [rcx + VCPU_GUEST_REGS + GUEST_REG_R11]
    mov r12, [rcx + VCPU_GUEST_REGS + GUEST_REG_R12]
    mov r13, [rcx + VCPU_GUEST_REGS + GUEST_REG_R13]
    mov r14, [rcx + VCPU_GUEST_REGS + GUEST_REG_R14]
    mov r15, [rcx + VCPU_GUEST_REGS + GUEST_REG_R15]
    mov rcx, [rcx + VCPU_GUEST_REGS + GUEST_REG_RCX]

    vmresume

    /* Fall through on failure */
.LVmRunFailed:
    /*
     * VMLAUNCH/VMRESUME failed. Unwind our stack.
     * Stack: [rsp]=VcpuPtr, [rsp+8]=guest RCX, then saved host non-volatiles
     */
    add rsp, 16     /* skip VcpuPtr and saved guest RCX */
    pop r15
    pop r14
    pop r13
    pop r12
    pop rdi
    pop rsi
    pop rbp
    pop rbx
    /* Return non-zero to indicate failure */
    mov eax, 1
    ret

/*
 * NTSTATUS RosvAsmInvept(ULONG Type, ULONG64 Eptp)
 *   ecx = INVEPT type (1=single-context, 2=all-context)
 *   rdx = EPTP value (used for single-context)
 *
 * INVEPT descriptor is 128 bits on the stack: [EPTP (64-bit), Reserved (64-bit)]
 * Returns: STATUS_SUCCESS (0) on success, STATUS_UNSUCCESSFUL on failure.
 *
 * invept rcx, m128: opcode 66 0F 38 80 /r
 * We build the 128-bit descriptor on the stack and use [rsp] as operand.
 * ModRM for invept rcx, [rsp]: mod=00, reg=001(rcx), rm=100(SIB) => 0x0C
 * SIB for [rsp]: scale=00, index=100(none), base=100(rsp) => 0x24
 */
PUBLIC RosvAsmInvept
RosvAsmInvept:
    /* Build INVEPT descriptor on stack: [EPTP, 0] */
    push 0          /* Reserved (upper 64 bits) */
    push rdx        /* EPTP (lower 64 bits) */
    /* invept rcx, [rsp] */
    .byte 0x66, 0x0F, 0x38, 0x80, 0x0C, 0x24
    add rsp, 16     /* Clean up descriptor */
    jc .LInveptFail
    jz .LInveptFail
    xor eax, eax    /* STATUS_SUCCESS */
    ret
.LInveptFail:
    mov eax, HEX(C0000001) /* STATUS_UNSUCCESSFUL */
    ret

END
