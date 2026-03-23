/*
 * PROJECT:     ReactOS VMX Hypervisor Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     VMCS state dump and failure diagnostics
 * COPYRIGHT:   Copyright 2025 Ahmed Arif
 */

#include <rosv/rosv.h>
#include <rosv/vmx.h>
#include <rosv/vm.h>

/* ---- VMX instruction error strings (Intel SDM Vol 3, Section 30.4) ------ */

static const char *g_VmxErrorStrings[] = {
    /* [0] */ NULL,
    /* [1]  */ "VMCALL in VMX root",
    /* [2]  */ "VMCLEAR invalid address",
    /* [3]  */ "VMCLEAR with VMXON pointer",
    /* [4]  */ "VMLAUNCH non-clear VMCS",
    /* [5]  */ "VMRESUME non-launched VMCS",
    /* [6]  */ "VMRESUME after VMXOFF",
    /* [7]  */ "VM entry invalid control field(s)",
    /* [8]  */ "VM entry invalid host-state",
    /* [9]  */ "VMPTRLD invalid address",
    /* [10] */ "VMPTRLD with VMXON pointer",
    /* [11] */ "VMPTRLD incorrect VMCS revision",
    /* [12] */ "VMREAD/VMWRITE unsupported VMCS field",
    /* [13] */ "VMWRITE read-only VMCS field",
    /* [14] */ NULL,
    /* [15] */ "VMXON in VMX root",
    /* [16] */ NULL,
    /* [17] */ NULL,
    /* [18] */ NULL,
    /* [19] */ NULL,
    /* [20] */ "VMCALL invalid MSEG revision",
    /* [21] */ NULL,
    /* [22] */ "VMCALL invalid SMM-monitor features",
    /* [23] */ NULL,
    /* [24] */ NULL,
    /* [25] */ NULL,
    /* [26] */ "VM entry blocked by MOV SS"
};

#define VMX_ERROR_STRING_COUNT (sizeof(g_VmxErrorStrings) / sizeof(g_VmxErrorStrings[0]))

/* ---- Exit reason name table --------------------------------------------- */

static const char *g_ExitReasonNames[] = {
    /* [0]  */ "EXCEPTION_NMI",
    /* [1]  */ "EXTERNAL_INT",
    /* [2]  */ "TRIPLE_FAULT",
    /* [3]  */ "INIT_SIGNAL",
    /* [4]  */ "SIPI",
    /* [5]  */ "IO_SMI",
    /* [6]  */ "OTHER_SMI",
    /* [7]  */ "PENDING_INTERRUPT",
    /* [8]  */ "NMI_WINDOW",
    /* [9]  */ "TASK_SWITCH",
    /* [10] */ "CPUID",
    /* [11] */ "GETSEC",
    /* [12] */ "HLT",
    /* [13] */ "INVD",
    /* [14] */ "INVLPG",
    /* [15] */ "RDPMC",
    /* [16] */ "RDTSC",
    /* [17] */ "RSM",
    /* [18] */ "VMCALL",
    /* [19] */ "VMCLEAR",
    /* [20] */ "VMLAUNCH",
    /* [21] */ "VMPTRLD",
    /* [22] */ "VMPTRST",
    /* [23] */ "VMREAD",
    /* [24] */ "VMRESUME",
    /* [25] */ "VMWRITE",
    /* [26] */ "VMXOFF",
    /* [27] */ "VMXON",
    /* [28] */ "CR_ACCESS",
    /* [29] */ "DR_ACCESS",
    /* [30] */ "IO_INSTRUCTION",
    /* [31] */ "RDMSR",
    /* [32] */ "WRMSR",
    /* [33] */ "ENTRY_FAIL_GUEST",
    /* [34] */ "ENTRY_FAIL_MSR",
    /* [35] */ NULL,
    /* [36] */ "MWAIT",
    /* [37] */ "MONITOR_TRAP",
    /* [38] */ NULL,
    /* [39] */ "MONITOR",
    /* [40] */ "PAUSE",
    /* [41] */ "ENTRY_FAIL_MC",
    /* [42] */ NULL,
    /* [43] */ "TPR_BELOW_THRESHOLD",
    /* [44] */ "APIC_ACCESS",
    /* [45] */ "VIRTUALIZED_EOI",
    /* [46] */ "GDTR_IDTR_ACCESS",
    /* [47] */ "LDTR_TR_ACCESS",
    /* [48] */ "EPT_VIOLATION",
    /* [49] */ "EPT_MISCONFIG",
    /* [50] */ "INVEPT",
    /* [51] */ "RDTSCP",
    /* [52] */ "PREEMPT_TIMER",
    /* [53] */ "INVVPID",
    /* [54] */ "WBINVD",
    /* [55] */ "XSETBV",
    /* [56] */ "APIC_WRITE",
    /* [57] */ "RDRAND",
    /* [58] */ "INVPCID",
    /* [59] */ "VMFUNC",
    /* [60] */ "ENCLS",
    /* [61] */ "RDSEED",
    /* [62] */ "PML_FULL",
    /* [63] */ "XSAVES",
    /* [64] */ "XRSTORS"
};

#define EXIT_REASON_NAME_COUNT (sizeof(g_ExitReasonNames) / sizeof(g_ExitReasonNames[0]))

static const char *
RosvpGetExitReasonName(
    _In_ ULONG Reason)
{
    ULONG BasicReason = Reason & 0xFFFF;

    if (BasicReason < EXIT_REASON_NAME_COUNT && g_ExitReasonNames[BasicReason] != NULL)
        return g_ExitReasonNames[BasicReason];

    return "UNKNOWN";
}

/* ---- RosvVmxDumpVmcs ---------------------------------------------------- */

VOID
RosvVmxDumpVmcs(VOID)
{
    DbgPrint("[ROSV:VMCS] === VMCS STATE DUMP ===\n");

    /*
     * Guest State
     */
    DbgPrint("[ROSV:VMCS] --- Guest State ---\n");
    DbgPrint("[ROSV:VMCS] CR0=0x%016llx CR3=0x%016llx CR4=0x%016llx\n",
             RosvVmcsRead(VMCS_GUEST_CR0),
             RosvVmcsRead(VMCS_GUEST_CR3),
             RosvVmcsRead(VMCS_GUEST_CR4));
    DbgPrint("[ROSV:VMCS] DR7=0x%016llx RFLAGS=0x%016llx\n",
             RosvVmcsRead(VMCS_GUEST_DR7),
             RosvVmcsRead(VMCS_GUEST_RFLAGS));
    DbgPrint("[ROSV:VMCS] RIP=0x%016llx RSP=0x%016llx\n",
             RosvVmcsRead(VMCS_GUEST_RIP),
             RosvVmcsRead(VMCS_GUEST_RSP));

    /* Guest segments */
    DbgPrint("[ROSV:VMCS] CS: sel=0x%04llx base=0x%016llx limit=0x%08llx ar=0x%08llx\n",
             RosvVmcsRead(VMCS_GUEST_CS_SEL),
             RosvVmcsRead(VMCS_GUEST_CS_BASE),
             RosvVmcsRead(VMCS_GUEST_CS_LIMIT),
             RosvVmcsRead(VMCS_GUEST_CS_ACCESS));
    DbgPrint("[ROSV:VMCS] DS: sel=0x%04llx base=0x%016llx limit=0x%08llx ar=0x%08llx\n",
             RosvVmcsRead(VMCS_GUEST_DS_SEL),
             RosvVmcsRead(VMCS_GUEST_DS_BASE),
             RosvVmcsRead(VMCS_GUEST_DS_LIMIT),
             RosvVmcsRead(VMCS_GUEST_DS_ACCESS));
    DbgPrint("[ROSV:VMCS] SS: sel=0x%04llx base=0x%016llx limit=0x%08llx ar=0x%08llx\n",
             RosvVmcsRead(VMCS_GUEST_SS_SEL),
             RosvVmcsRead(VMCS_GUEST_SS_BASE),
             RosvVmcsRead(VMCS_GUEST_SS_LIMIT),
             RosvVmcsRead(VMCS_GUEST_SS_ACCESS));
    DbgPrint("[ROSV:VMCS] ES: sel=0x%04llx base=0x%016llx limit=0x%08llx ar=0x%08llx\n",
             RosvVmcsRead(VMCS_GUEST_ES_SEL),
             RosvVmcsRead(VMCS_GUEST_ES_BASE),
             RosvVmcsRead(VMCS_GUEST_ES_LIMIT),
             RosvVmcsRead(VMCS_GUEST_ES_ACCESS));
    DbgPrint("[ROSV:VMCS] FS: sel=0x%04llx base=0x%016llx limit=0x%08llx ar=0x%08llx\n",
             RosvVmcsRead(VMCS_GUEST_FS_SEL),
             RosvVmcsRead(VMCS_GUEST_FS_BASE),
             RosvVmcsRead(VMCS_GUEST_FS_LIMIT),
             RosvVmcsRead(VMCS_GUEST_FS_ACCESS));
    DbgPrint("[ROSV:VMCS] GS: sel=0x%04llx base=0x%016llx limit=0x%08llx ar=0x%08llx\n",
             RosvVmcsRead(VMCS_GUEST_GS_SEL),
             RosvVmcsRead(VMCS_GUEST_GS_BASE),
             RosvVmcsRead(VMCS_GUEST_GS_LIMIT),
             RosvVmcsRead(VMCS_GUEST_GS_ACCESS));
    DbgPrint("[ROSV:VMCS] TR: sel=0x%04llx base=0x%016llx limit=0x%08llx ar=0x%08llx\n",
             RosvVmcsRead(VMCS_GUEST_TR_SEL),
             RosvVmcsRead(VMCS_GUEST_TR_BASE),
             RosvVmcsRead(VMCS_GUEST_TR_LIMIT),
             RosvVmcsRead(VMCS_GUEST_TR_ACCESS));
    DbgPrint("[ROSV:VMCS] LDTR: sel=0x%04llx base=0x%016llx limit=0x%08llx ar=0x%08llx\n",
             RosvVmcsRead(VMCS_GUEST_LDTR_SEL),
             RosvVmcsRead(VMCS_GUEST_LDTR_BASE),
             RosvVmcsRead(VMCS_GUEST_LDTR_LIMIT),
             RosvVmcsRead(VMCS_GUEST_LDTR_ACCESS));

    /* Guest descriptor tables */
    DbgPrint("[ROSV:VMCS] GDTR: base=0x%016llx limit=0x%08llx\n",
             RosvVmcsRead(VMCS_GUEST_GDTR_BASE),
             RosvVmcsRead(VMCS_GUEST_GDTR_LIMIT));
    DbgPrint("[ROSV:VMCS] IDTR: base=0x%016llx limit=0x%08llx\n",
             RosvVmcsRead(VMCS_GUEST_IDTR_BASE),
             RosvVmcsRead(VMCS_GUEST_IDTR_LIMIT));

    /* Guest MSRs and interruptibility */
    DbgPrint("[ROSV:VMCS] EFER=0x%016llx\n",
             RosvVmcsRead(VMCS_GUEST_EFER));
    DbgPrint("[ROSV:VMCS] Interruptibility=0x%08llx Activity=0x%08llx\n",
             RosvVmcsRead(VMCS_GUEST_INTERRUPTIBILITY),
             RosvVmcsRead(VMCS_GUEST_ACTIVITY));
    DbgPrint("[ROSV:VMCS] SYSENTER_CS=0x%08llx SYSENTER_ESP=0x%016llx SYSENTER_EIP=0x%016llx\n",
             RosvVmcsRead(VMCS_GUEST_SYSENTER_CS),
             RosvVmcsRead(VMCS_GUEST_SYSENTER_ESP),
             RosvVmcsRead(VMCS_GUEST_SYSENTER_EIP));
    DbgPrint("[ROSV:VMCS] VMCS link pointer=0x%016llx\n",
             RosvVmcsRead(VMCS_GUEST_VMCS_LINK_PTR));

    /*
     * Host State
     */
    DbgPrint("[ROSV:VMCS] --- Host State ---\n");
    DbgPrint("[ROSV:VMCS] CR0=0x%016llx CR3=0x%016llx CR4=0x%016llx\n",
             RosvVmcsRead(VMCS_HOST_CR0),
             RosvVmcsRead(VMCS_HOST_CR3),
             RosvVmcsRead(VMCS_HOST_CR4));
    DbgPrint("[ROSV:VMCS] RIP=0x%016llx RSP=0x%016llx\n",
             RosvVmcsRead(VMCS_HOST_RIP),
             RosvVmcsRead(VMCS_HOST_RSP));
    DbgPrint("[ROSV:VMCS] CS=0x%04llx DS=0x%04llx SS=0x%04llx ES=0x%04llx FS=0x%04llx GS=0x%04llx TR=0x%04llx\n",
             RosvVmcsRead(VMCS_HOST_CS_SEL),
             RosvVmcsRead(VMCS_HOST_DS_SEL),
             RosvVmcsRead(VMCS_HOST_SS_SEL),
             RosvVmcsRead(VMCS_HOST_ES_SEL),
             RosvVmcsRead(VMCS_HOST_FS_SEL),
             RosvVmcsRead(VMCS_HOST_GS_SEL),
             RosvVmcsRead(VMCS_HOST_TR_SEL));
    DbgPrint("[ROSV:VMCS] FS base=0x%016llx GS base=0x%016llx TR base=0x%016llx\n",
             RosvVmcsRead(VMCS_HOST_FS_BASE),
             RosvVmcsRead(VMCS_HOST_GS_BASE),
             RosvVmcsRead(VMCS_HOST_TR_BASE));
    DbgPrint("[ROSV:VMCS] GDTR base=0x%016llx IDTR base=0x%016llx\n",
             RosvVmcsRead(VMCS_HOST_GDTR_BASE),
             RosvVmcsRead(VMCS_HOST_IDTR_BASE));
    DbgPrint("[ROSV:VMCS] EFER=0x%016llx\n",
             RosvVmcsRead(VMCS_HOST_EFER));
    DbgPrint("[ROSV:VMCS] SYSENTER_CS=0x%08llx SYSENTER_ESP=0x%016llx SYSENTER_EIP=0x%016llx\n",
             RosvVmcsRead(VMCS_HOST_SYSENTER_CS),
             RosvVmcsRead(VMCS_HOST_SYSENTER_ESP),
             RosvVmcsRead(VMCS_HOST_SYSENTER_EIP));

    /*
     * Controls
     */
    DbgPrint("[ROSV:VMCS] --- Controls ---\n");
    DbgPrint("[ROSV:VMCS] PinBased=0x%08llx ProcBased=0x%08llx\n",
             RosvVmcsRead(VMCS_CTRL_PIN_BASED),
             RosvVmcsRead(VMCS_CTRL_PROC_BASED));
    DbgPrint("[ROSV:VMCS] ProcBased2=0x%08llx\n",
             RosvVmcsRead(VMCS_CTRL_PROC_BASED2));
    DbgPrint("[ROSV:VMCS] ExitCtls=0x%08llx EntryCtls=0x%08llx\n",
             RosvVmcsRead(VMCS_CTRL_EXIT),
             RosvVmcsRead(VMCS_CTRL_ENTRY));
    DbgPrint("[ROSV:VMCS] ExceptionBitmap=0x%08llx\n",
             RosvVmcsRead(VMCS_CTRL_EXCEPTION_BITMAP));
    DbgPrint("[ROSV:VMCS] CR0 GuestHostMask=0x%016llx ReadShadow=0x%016llx\n",
             RosvVmcsRead(VMCS_CTRL_CR0_MASK),
             RosvVmcsRead(VMCS_CTRL_CR0_READ_SHADOW));
    DbgPrint("[ROSV:VMCS] CR4 GuestHostMask=0x%016llx ReadShadow=0x%016llx\n",
             RosvVmcsRead(VMCS_CTRL_CR4_MASK),
             RosvVmcsRead(VMCS_CTRL_CR4_READ_SHADOW));
    DbgPrint("[ROSV:VMCS] EPTP=0x%016llx\n",
             RosvVmcsRead(VMCS_CTRL_EPTP));

    /*
     * Exit Information
     */
    DbgPrint("[ROSV:VMCS] --- Exit Info ---\n");
    {
        ULONG64 ExitReason = RosvVmcsRead(VMCS_RO_EXIT_REASON);
        DbgPrint("[ROSV:VMCS] Reason=%llu (%s) Qual=0x%016llx\n",
                 ExitReason,
                 RosvpGetExitReasonName((ULONG)ExitReason),
                 RosvVmcsRead(VMCS_RO_EXIT_QUALIFICATION));
    }
    DbgPrint("[ROSV:VMCS] GuestLinear=0x%016llx GuestPhysical=0x%016llx\n",
             RosvVmcsRead(VMCS_RO_GUEST_LINEAR_ADDR),
             RosvVmcsRead(VMCS_RO_GUEST_PHYS_ADDR));
    DbgPrint("[ROSV:VMCS] IntrInfo=0x%08llx IntrError=0x%08llx\n",
             RosvVmcsRead(VMCS_RO_EXIT_INT_INFO),
             RosvVmcsRead(VMCS_RO_EXIT_INT_ERRCODE));
    DbgPrint("[ROSV:VMCS] InstrLen=%llu InstrInfo=0x%08llx\n",
             RosvVmcsRead(VMCS_RO_EXIT_INSTR_LENGTH),
             RosvVmcsRead(VMCS_RO_EXIT_INSTR_INFO));
    DbgPrint("[ROSV:VMCS] InstructionError=%llu\n",
             RosvVmcsRead(VMCS_RO_INSTR_ERROR));
    DbgPrint("[ROSV:VMCS] IDT vectoring info=0x%08llx errcode=0x%08llx\n",
             RosvVmcsRead(VMCS_RO_IDT_VECTOR_INFO),
             RosvVmcsRead(VMCS_RO_IDT_VECTOR_ERRCODE));

    DbgPrint("[ROSV:VMCS] === END VMCS DUMP ===\n");
}

/* ---- RosvVmxDumpExitReason ---------------------------------------------- */

VOID
RosvVmxDumpExitReason(
    _In_ ULONG ExitReason,
    _In_ ULONG64 ExitQualification)
{
    ULONG BasicReason = ExitReason & 0xFFFF;
    BOOLEAN VmEntryFailure = (ExitReason & (1U << 31)) != 0;

    DbgPrint("[ROSV:EXIT] Exit reason=%u (%s)%s qualification=0x%016llx\n",
             BasicReason,
             RosvpGetExitReasonName(BasicReason),
             VmEntryFailure ? " [VM-ENTRY FAILURE]" : "",
             ExitQualification);

    if (VmEntryFailure)
    {
        ROSV_ERR("VM-entry failure detected (reason=%u), dumping full VMCS state", BasicReason);
        RosvVmxDumpVmcs();
    }
}

/* ---- RosvVmxDumpFailure ------------------------------------------------- */

VOID
RosvVmxDumpFailure(VOID)
{
    ULONG64 ErrorField = RosvVmcsRead(VMCS_RO_INSTR_ERROR);
    ULONG ErrorCode = (ULONG)ErrorField;
    const char *ErrorString = NULL;

    if (ErrorCode < VMX_ERROR_STRING_COUNT)
        ErrorString = g_VmxErrorStrings[ErrorCode];

    ROSV_ERR("VMX instruction error %u: %s",
             ErrorCode,
             ErrorString ? ErrorString : "UNKNOWN ERROR CODE");

    RosvVmxDumpVmcs();
}

/* ---- RosvDumpGuestRegisters --------------------------------------------- */

VOID
RosvDumpGuestRegisters(
    _In_ PROSV_VCPU Vcpu)
{
    ULONG64 Rip = RosvVmcsRead(VMCS_GUEST_RIP);
    ULONG64 Rflags = RosvVmcsRead(VMCS_GUEST_RFLAGS);

    DbgPrint("[ROSV:REGS] RAX=0x%016llx RBX=0x%016llx RCX=0x%016llx RDX=0x%016llx\n",
             Vcpu->GuestRegs.Rax, Vcpu->GuestRegs.Rbx,
             Vcpu->GuestRegs.Rcx, Vcpu->GuestRegs.Rdx);
    DbgPrint("[ROSV:REGS] RSI=0x%016llx RDI=0x%016llx RBP=0x%016llx RSP=0x%016llx\n",
             Vcpu->GuestRegs.Rsi, Vcpu->GuestRegs.Rdi,
             Vcpu->GuestRegs.Rbp, RosvVmcsRead(VMCS_GUEST_RSP));
    DbgPrint("[ROSV:REGS] R8 =0x%016llx R9 =0x%016llx R10=0x%016llx R11=0x%016llx\n",
             Vcpu->GuestRegs.R8, Vcpu->GuestRegs.R9,
             Vcpu->GuestRegs.R10, Vcpu->GuestRegs.R11);
    DbgPrint("[ROSV:REGS] R12=0x%016llx R13=0x%016llx R14=0x%016llx R15=0x%016llx\n",
             Vcpu->GuestRegs.R12, Vcpu->GuestRegs.R13,
             Vcpu->GuestRegs.R14, Vcpu->GuestRegs.R15);
    DbgPrint("[ROSV:REGS] RIP=0x%016llx RFLAGS=0x%016llx\n",
             Rip, Rflags);
}

/* ---- RosvDumpGuestMemory ------------------------------------------------ */

VOID
RosvDumpGuestMemory(
    _In_ PROSV_VM Vm,
    _In_ ULONG64 Gpa,
    _In_ ULONG Length)
{
    PUCHAR Hva;
    ULONG Offset;
    ULONG LineLen;
    ULONG i;
    CHAR HexBuf[3 * 16 + 1];
    CHAR AsciiBuf[17];
    ULONG HexPos;

    if (Length == 0)
    {
        ROSV_WARN("RosvDumpGuestMemory: length is 0, nothing to dump");
        return;
    }

    /* Cap to something reasonable to avoid flooding the debug log */
    if (Length > 4096)
    {
        ROSV_WARN("RosvDumpGuestMemory: capping length from %u to 4096", Length);
        Length = 4096;
    }

    Hva = (PUCHAR)RosvMemoryGpaToHva(Vm, Gpa);
    if (Hva == NULL)
    {
        ROSV_ERR("RosvDumpGuestMemory: GPA 0x%016llx could not be resolved to HVA", Gpa);
        return;
    }

    DbgPrint("[ROSV:MEM] Guest memory dump at GPA 0x%016llx, length %u\n", Gpa, Length);

    for (Offset = 0; Offset < Length; Offset += 16)
    {
        LineLen = Length - Offset;
        if (LineLen > 16)
            LineLen = 16;

        HexPos = 0;
        for (i = 0; i < 16; i++)
        {
            if (i < LineLen)
            {
                UCHAR Byte = Hva[Offset + i];
                HexBuf[HexPos++] = "0123456789abcdef"[Byte >> 4];
                HexBuf[HexPos++] = "0123456789abcdef"[Byte & 0xF];
                HexBuf[HexPos++] = ' ';
                AsciiBuf[i] = (Byte >= 0x20 && Byte < 0x7F) ? (CHAR)Byte : '.';
            }
            else
            {
                HexBuf[HexPos++] = ' ';
                HexBuf[HexPos++] = ' ';
                HexBuf[HexPos++] = ' ';
                AsciiBuf[i] = ' ';
            }
        }
        HexBuf[HexPos] = '\0';
        AsciiBuf[16] = '\0';

        DbgPrint("[ROSV:MEM] %016llx: %s|%s|\n",
                 Gpa + Offset, HexBuf, AsciiBuf);
    }
}

/* ---- RosvDetectTripleFault ---------------------------------------------- */

VOID
RosvDetectTripleFault(
    _Inout_ PROSV_VCPU Vcpu,
    _Inout_ PROSV_VM Vm)
{
    ROSV_ERR("FATAL: Triple fault detected on vCPU %u, VM %u!",
             Vcpu->VcpuId, Vm->VmId);
    RosvDumpGuestRegisters(Vcpu);
    RosvVmxDumpVmcs();
    Vm->State = RosvVmStateCrashed;
    ROSV_ERR("FATAL: Guest has crashed. Check VMCS dump above for diagnosis.");
}

/* ---- RosvDetectGuestPanic ----------------------------------------------- */

VOID
RosvDetectGuestPanic(
    _In_ PROSV_VM Vm,
    _In_ PUCHAR SerialOutput,
    _In_ ULONG Length)
{
    ULONG i;

    UNREFERENCED_PARAMETER(Vm);

    if (SerialOutput == NULL || Length == 0)
        return;

    /* Scan for "Kernel panic" */
    for (i = 0; i + 12 <= Length; i++)
    {
        if (SerialOutput[i] == 'K' &&
            RtlCompareMemory(&SerialOutput[i], "Kernel panic", 12) == 12)
        {
            DbgPrint("[ROSV:PANIC] Guest kernel panic detected in serial output!\n");
            /* Print surrounding context (up to 256 bytes after the panic string) */
            {
                ULONG ContextLen = Length - i;
                if (ContextLen > 256)
                    ContextLen = 256;
                DbgPrint("[ROSV:PANIC] Context: %.*s\n", ContextLen, &SerialOutput[i]);
            }
            return;
        }
    }

    /* Scan for "Oops" */
    for (i = 0; i + 4 <= Length; i++)
    {
        if (SerialOutput[i] == 'O' &&
            RtlCompareMemory(&SerialOutput[i], "Oops", 4) == 4)
        {
            DbgPrint("[ROSV:PANIC] Guest kernel Oops detected in serial output!\n");
            {
                ULONG ContextLen = Length - i;
                if (ContextLen > 256)
                    ContextLen = 256;
                DbgPrint("[ROSV:PANIC] Context: %.*s\n", ContextLen, &SerialOutput[i]);
            }
            return;
        }
    }
}
