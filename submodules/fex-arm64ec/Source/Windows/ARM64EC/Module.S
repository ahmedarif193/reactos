.text
.balign 16

  // __os_arm64x_x64_jump in ARM64EC docs
  // Expects target code address in x9
.globl DispatchJump
DispatchJump:
  str lr, [sp, #-8]! // Push return address to stack, this will be popped by the x86 RET instr.
  b check_target_ec

  // __os_arm64x_dispatch_ret in ARM64EC docs
  // Expects target code address in lr
.globl RetToEntryThunk
RetToEntryThunk:
  mov x9, lr

check_target_ec:
  // Check if target is in fact x86 code
  ldr x16, [x18, #0x60] // TEB->PEB
  ldr x16, [x16, #0x368] // PEB->EcCodeBitMap
  lsr x17, x9, #18
  ldr x16, [x16, x17, lsl #3]
  lsr x17, x9, #12
  lsr x16, x16, x17
  tbnz x16, #0, ExitFunctionEC
  b enter_jit

  // __os_arm64x_dispatch_call_no_redirect in ARM64EC docs
  // Expects target code address in x9, and to be called using a 'blr x16' instruction.
.globl ExitToX64
ExitToX64:
  str lr, [sp, #-8]! // Push return address to stack, this will be popped by the x86 RET instr.

enter_jit:
  ldr x17, [x18, #0x1788] // TEB->ChpeV2CpuAreaInfo
  mov w16, #1
  strb w16, [x17, #0x0] // ChpeV2CpuAreaInfo->InSimulation
  ldr x16, [x17, #0x40] // ChpeV2CpuAreaInfo->EmulatorData[2] - DispatcherLoopTopEnterEC
  br x16 // DispatcherLoopTopEnterEC(RIP:x9, CPUArea:x17)

  // Invoked by KiUserEmulationDispatcher after e.g. an NtContinue to x86 code
.global BeginSimulation
BeginSimulation:
  mov w1, #0 // Flags converted from ARM64 contain only NZCV and SS.
  b begin_simulation_common

.global BeginSimulationFromContext
BeginSimulationFromContext:
  mov w1, #1 // An explicit x64 CONTEXT carries the complete EFLAGS value.

begin_simulation_common:
  ldr x17, [x18, #0x1788] // TEB->ChpeV2CpuAreaInfo
  ldr x16, [x17, #0x8] // ChpeV2CpuAreaInfo->EmulatorStackBase
  mov sp, x16
  ldr x0, [x17, #0x18] // ChpeV2CpuAreaInfo->ContextAmd64
  bl "#SyncThreadContext"
  ldr x17, [x18, #0x1788] // TEB->ChpeV2CpuAreaInfo
  ldr x16, [x17, #0x48] // ChpeV2CpuAreaInfo->EmulatorData[3] - DispatcherLoopTopEnterECFillSRA
  mov x11, #0 // Zero ENTRY_FILL_SRA_SINGLE_INST_REG to avoid single step
  br x16 // DispatcherLoopTopEnterECFillSRA(SingleInst:x10, CPUArea:x17)

  // Called into by FEXCore
  // Expects the target code address in x9
.global ExitFunctionEC
ExitFunctionEC:
  // Clear any the AFP NEP and AH bits in FPCR as native code won't expect their behaviour.
  mrs x17, fpcr
  and x17, x17, #~6 // NEP + AH
  msr fpcr, x17
  ldr x17, [x18, #0x1788] // TEB->ChpeV2CpuAreaInfo
  strb wzr, [x17, #0x0] // ChpeV2CpuAreaInfo->InSimulation
  ldr x17, [x17, #0x20] // ChpeV2CpuAreaInfo->SuspendDoorbell
  ldr w17, [x17]
  cbnz w17, ExitFunctionSuspendPoint

.global ExitFunctionSuspendResumePoint
ExitFunctionSuspendResumePoint:
  // Either return to an exit thunk (return to ARM64EC function) or call an entry thunk (call to ARM64EC function).
  // It is assumed that a 'blr x16' instruction is only ever used to call into x86 code from an exit thunk, and that all
  // exported ARM64EC functions have a 4-byte offset to their entry thunk immediately before their first instruction.
  mov x17, x9
  mov w16, #0x200
  movk w16, #0xd63f, lsl 16 // blr x16
  ldursw x23, [x17, #-0x4] // Load either the entry thunk offset or the calling instruction.
  cmp w23, w16
  beq ret_sp_aligned

#ifdef __REACTOS__
  // ReactOS keeps ntdll pure ARM64. Calls from translated x64 code therefore
  // need a native return bridge instead of ARM64EC entry-thunk resolution.
  adrp x16, NtDllBase
  ldr x16, [x16, #:lo12:NtDllBase]
  cmp x17, x16
  b.lo resolve_entry_thunk
  adrp x16, NtDllEnd
  ldr x16, [x16, #:lo12:NtDllEnd]
  cmp x17, x16
  b.lo call_native_ntdll

resolve_entry_thunk:
#endif
  and x23, x23, #-0x4
  add x17, x17, x23 // Resolve entry thunk address.

  mov x4, sp
  tbz x4, #3, ret_sp_misaligned
  ldr lr, [x4], #0x8 // Pop the return address into lr.
  mov sp, x4

ret_sp_aligned:
  br x17

ret_sp_misaligned:
  // In the case of the x64 caller leaving sp only 8-byte aligned, leave the return address on the stack to keep 16-byte
  // alignment and have the callee return to an x86 ret instruction. FEX can then return to the actual caller keeping
  // the misaligned RSP.
  adrp lr, X64ReturnInstr
  ldr lr, [lr, #:lo12:X64ReturnInstr]
  br x17
#ifdef __REACTOS__

call_native_ntdll:
  mov x16, x17
  mov x17, sp
  ldp x4, x5, [x17, #0x28]
  ldp x6, x7, [x17, #0x38]
  tbz x17, #3, call_native_ntdll_aligned
  // Keep the native call 16-byte aligned without consuming the x64 return
  // address. The bridge removes this padding before executing an x64 RET.
  sub sp, sp, #0x8

call_native_ntdll_aligned:
  stp q6, q7, [sp, #-0xa0]!
  stp q8, q9, [sp, #0x20]
  stp q10, q11, [sp, #0x40]
  stp q12, q13, [sp, #0x60]
  stp q14, q15, [sp, #0x80]
  str x17, [sp, #-0x10]!
  blr x16
  ldr x17, [sp], #0x10
  ldp q8, q9, [sp, #0x20]
  ldp q10, q11, [sp, #0x40]
  ldp q12, q13, [sp, #0x60]
  ldp q14, q15, [sp, #0x80]
  ldp q6, q7, [sp], #0xa0
  mov sp, x17
  mov x8, x0
  adrp x9, X64ReturnInstr
  ldr x9, [x9, #:lo12:X64ReturnInstr]
  b check_target_ec
#endif

.global ExitFunctionSuspendPoint
ExitFunctionSuspendPoint:
  brk #0xCAFE
  // Resume will jump back to `ExitFunctionSuspendResumePoint`

  // Makes a wrapper for calling a system call directly, skipping the usual ntdll thunks
#define HASH #
#ifdef __REACTOS__
#define DIRECT_SYSCALL_WRAPPER(Name, SyscallIdName, WindowsId) \
  .global Name; \
  Name:; \
    adrp x8, SyscallIdName; \
    ldr x8, [x8, HASH:lo12:SyscallIdName]; \
    svc HASH 0xffff; \
    ret
#else
#define DIRECT_SYSCALL_WRAPPER(Name, WineIdName, WindowsId) \
  .global Name; \
  Name:; \
    adrp x16, WineSyscallDispatcher; \
    ldr x16, [x16, HASH:lo12:WineSyscallDispatcher]; \
    cbz x16, 1f; \
    mov x9, x30; \
    adrp x8, WineIdName; \
    ldr x8, [x8, HASH:lo12:WineIdName]; \
    blr x16; \
    ret; \
  1:; \
    svc HASH WindowsId; \
    ret
#endif

  // Allows for continuing from a full native context, as the NTDLL NtContinue export takes in an x64 context with EC and
  // the conversion to that loses the ARM64EC ABI-disallowed registers that FEX uses.
DIRECT_SYSCALL_WRAPPER("#NtContinueNative", WineNtContinueSyscallId, 0x43)

  // Both of these are wrapped as FEX needs them to setup its call checker at startup time and their NTDLL thunks could
  // already be patched by then (and because the call checker isn't installed, their patched x86 versions would be invoked
  // when called by FEX).
DIRECT_SYSCALL_WRAPPER("#NtAllocateVirtualMemoryNative", WineNtAllocateVirtualMemorySyscallId, 0x18)
DIRECT_SYSCALL_WRAPPER("#NtProtectVirtualMemoryNative", WineNtProtectVirtualMemorySyscallId, 0x50)

  // A replacement for the standard ARM64EC call checker that ignores any FFS patches and always redirects to a function's
  // native implementation. As the only library FEX calls into is NTDLL, this is done using a LUT generated at init time.
  // Expects the FFS address in x11, exit thunk address in x10 (unused) and it's own address in x9. Return address is in x11.
.global "CheckCall"
"CheckCall":
  adrp x9, NtDllBase
  ldr x9, [x9, #:lo12:NtDllBase]
  subs x16, x11, x9
#ifdef __REACTOS__
  b.lo default_check
#else
  b.lo end
#endif
  adrp x17, NtDllRedirectionLUTSize
  ldr x17, [x17, #:lo12:NtDllRedirectionLUTSize]
  cmp x16, x17
#ifdef __REACTOS__
  b.hs default_check
#else
  b.hi end
#endif
  adrp x17, NtDllRedirectionLUT
  ldr x17, [x17, #:lo12:NtDllRedirectionLUT]
  ldr w11, [x17, x16, lsl #2]
  add x11, x11, x9
end:
  ret
#ifdef __REACTOS__

  // ReactOS: targets outside the ntdll LUT (other ARM64EC DLLs) are handed to the loader-installed call checker
  // captured in PatchCallChecker, which consumes x10/x11 like a regular __os_arm64x_dispatch_call.
default_check:
  adrp x9, DefaultCheckCall
  ldr x9, [x9, #:lo12:DefaultCheckCall]
  cbz x9, end
  br x9
#endif

  // Expects target address in x0, and the SP to set in x1
.global "#JumpSetStack"
"#JumpSetStack":
  mov sp, x1
  br x0
