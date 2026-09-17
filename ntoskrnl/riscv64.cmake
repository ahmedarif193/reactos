# Add the architecture-owned sources to the standard kernel target. The kernel
# uses the same COFF object and PE image path as the rest of the NT module graph.
list(APPEND ASM_SOURCE
    ${REACTOS_SOURCE_DIR}/ntoskrnl/ke/riscv64/ctxswitch.S
    ${REACTOS_SOURCE_DIR}/ntoskrnl/ke/riscv64/entry.S
    ${REACTOS_SOURCE_DIR}/ntoskrnl/ke/riscv64/stack.S
    ${REACTOS_SOURCE_DIR}/ntoskrnl/ke/riscv64/trap.S
    ${REACTOS_SOURCE_DIR}/ntoskrnl/ke/riscv64/usercall.S
    ${REACTOS_SOURCE_DIR}/ntoskrnl/ke/riscv64/usercopy.S
    ${REACTOS_SOURCE_DIR}/ntoskrnl/ke/riscv64/syscall.S
    ${REACTOS_SOURCE_DIR}/ntoskrnl/rtl/riscv64/capture.S)

list(APPEND SOURCE
    config/riscv64/cmhardwr.c
    ex/riscv64/ioaccess.c
    io/pnpmgr/riscv64/platform.c
    kd64/riscv64/kdsup.c
    ke/riscv64/cache.c
    ke/riscv64/console.c
    ke/riscv64/context.c
    ke/riscv64/cpu.c
    ke/riscv64/features.c
    ke/riscv64/interrupt.c
    ke/riscv64/irql.c
    ke/riscv64/kiinit.c
    ke/riscv64/pcr.c
    ke/riscv64/spinlock.c
    ke/riscv64/stack.c
    ke/riscv64/stubs.c
    ke/riscv64/thrdini.c
    ke/riscv64/tlb.c
    ke/riscv64/trap.c
    ke/riscv64/usercopy.c
    ke/riscv64/usercall.c
    ke/riscv64/syscall.c
    mm/riscv64/cache.c
    mm/riscv64/init.c
    mm/riscv64/layout.c
    mm/riscv64/mmdata.c
    mm/riscv64/page.c
    mm/riscv64/pagewalk.c
    mm/riscv64/procsup.c
    mm/riscv64/pte.c
    mm/riscv64/window.c
    rtl/riscv64/rtlexcpt.c
    rtl/riscv64/slist.c)

# KeSwitchKernelStack relocates the live conversion chain's frame records.
# These callers also reload explicit pointers into the moved stack.
set_property(SOURCE ps/win32.c ke/riscv64/syscall.c ke/riscv64/trap.c
    APPEND PROPERTY COMPILE_OPTIONS -fno-omit-frame-pointer)
