/*
 * PROJECT:     ReactOS VMX Hypervisor Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Exit ring buffer recording, dump, and statistics
 * COPYRIGHT:   Copyright 2025 Ahmed Arif
 */

#include <rosv/rosv.h>
#include <rosv/vm.h>

/* ---- Exit reason names for log output ----------------------------------- */

static const char *g_LogExitNames[] = {
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

#define LOG_EXIT_NAME_COUNT (sizeof(g_LogExitNames) / sizeof(g_LogExitNames[0]))

static const char *
RosvpGetLogExitName(
    _In_ ULONG Reason)
{
    ULONG BasicReason = Reason & 0xFFFF;

    if (BasicReason < LOG_EXIT_NAME_COUNT && g_LogExitNames[BasicReason] != NULL)
        return g_LogExitNames[BasicReason];

    return "UNKNOWN";
}

/* ---- RosvExitLogRecord -------------------------------------------------- */

VOID
RosvExitLogRecord(
    _Inout_ PROSV_EXIT_RING Ring,
    _In_ ULONG ExitReason,
    _In_ ULONG64 ExitQualification,
    _In_ ULONG64 GuestRip,
    _In_ ULONG InstructionLength)
{
    PROSV_EXIT_LOG_ENTRY Entry;

    Entry = &Ring->Entries[Ring->Head];
    Entry->Timestamp = __rdtsc();
    Entry->ExitReason = ExitReason;
    Entry->ExitQualification = ExitQualification;
    Entry->GuestRip = GuestRip;
    Entry->InstructionLength = InstructionLength;

    Ring->Head = (Ring->Head + 1) % ROSV_EXIT_LOG_SIZE;

    /* Saturate at ULONG_MAX to prevent wrap-around; the dump/read
       functions use Count to decide whether the ring has wrapped,
       so wrapping back to zero would produce incorrect output. */
    if (Ring->Count < 0xFFFFFFFF)
        Ring->Count++;
}

/* ---- RosvExitLogRead ---------------------------------------------------- */

NTSTATUS
RosvExitLogRead(
    _In_ PROSV_EXIT_RING Ring,
    _Out_writes_(MaxEntries) PROSV_EXIT_LOG_ENTRY Buffer,
    _In_ ULONG MaxEntries,
    _Out_ PULONG EntriesReturned)
{
    ULONG Available;
    ULONG Start;
    ULONG i;

    *EntriesReturned = 0;

    if (Ring->Count == 0)
    {
        ROSV_TRACE("RosvExitLogRead: ring is empty");
        return STATUS_SUCCESS;
    }

    /* Determine how many entries are available */
    Available = (Ring->Count < ROSV_EXIT_LOG_SIZE) ? Ring->Count : ROSV_EXIT_LOG_SIZE;
    if (Available > MaxEntries)
        Available = MaxEntries;

    /* Calculate start position: oldest available entry */
    if (Ring->Count >= ROSV_EXIT_LOG_SIZE)
        Start = Ring->Head; /* Head points to oldest (just overwritten) */
    else
        Start = 0;

    for (i = 0; i < Available; i++)
    {
        Buffer[i] = Ring->Entries[(Start + i) % ROSV_EXIT_LOG_SIZE];
    }

    *EntriesReturned = Available;

    ROSV_TRACE("RosvExitLogRead: returned %u entries (total recorded: %u)",
               Available, Ring->Count);
    return STATUS_SUCCESS;
}

/* ---- RosvExitLogDump ---------------------------------------------------- */

VOID
RosvExitLogDump(
    _In_ PROSV_EXIT_RING Ring,
    _In_ ULONG NumEntries)
{
    ULONG Available;
    ULONG Start;
    ULONG i;

    if (Ring->Count == 0)
    {
        DbgPrint("[ROSV:LOG] Exit log is empty\n");
        return;
    }

    Available = (Ring->Count < ROSV_EXIT_LOG_SIZE) ? Ring->Count : ROSV_EXIT_LOG_SIZE;
    if (NumEntries > 0 && NumEntries < Available)
        Available = NumEntries;

    /* Start from the tail of the ring (most recent entries last) */
    if (Ring->Count >= ROSV_EXIT_LOG_SIZE)
    {
        /* Ring has wrapped; oldest is at Head, but we want the last N entries */
        Start = (Ring->Head + ROSV_EXIT_LOG_SIZE - Available) % ROSV_EXIT_LOG_SIZE;
    }
    else
    {
        /* Ring hasn't wrapped; entries start at 0 */
        Start = Ring->Count - Available;
    }

    DbgPrint("[ROSV:LOG] === Last %u exits (of %u total) ===\n", Available, Ring->Count);

    for (i = 0; i < Available; i++)
    {
        ULONG Idx = (Start + i) % ROSV_EXIT_LOG_SIZE;
        PROSV_EXIT_LOG_ENTRY Entry = &Ring->Entries[Idx];

        DbgPrint("[ROSV:LOG] #%u: reason=%u (%s) qual=0x%016llx rip=0x%016llx len=%u ts=%llu\n",
                 i,
                 Entry->ExitReason,
                 RosvpGetLogExitName(Entry->ExitReason),
                 Entry->ExitQualification,
                 Entry->GuestRip,
                 Entry->InstructionLength,
                 Entry->Timestamp);
    }
}

/* ---- RosvExitLogDumpSummary --------------------------------------------- */

VOID
RosvExitLogDumpSummary(
    _In_ PROSV_EXIT_RING Ring)
{
    ULONG Available;
    ULONG Start;
    ULONG i;
    ULONG CpuidCount = 0;
    ULONG IoCount = 0;
    ULONG RdmsrCount = 0;
    ULONG WrmsrCount = 0;
    ULONG EptViolationCount = 0;
    ULONG HltCount = 0;
    ULONG CrAccessCount = 0;
    ULONG TripleFaultCount = 0;
    ULONG ExceptionNmiCount = 0;
    ULONG OtherCount = 0;

    if (Ring->Count == 0)
    {
        DbgPrint("[ROSV:STAT] Exit log is empty, no summary to display\n");
        return;
    }

    Available = (Ring->Count < ROSV_EXIT_LOG_SIZE) ? Ring->Count : ROSV_EXIT_LOG_SIZE;

    if (Ring->Count >= ROSV_EXIT_LOG_SIZE)
        Start = Ring->Head;
    else
        Start = 0;

    for (i = 0; i < Available; i++)
    {
        ULONG Idx = (Start + i) % ROSV_EXIT_LOG_SIZE;
        ULONG Reason = Ring->Entries[Idx].ExitReason & 0xFFFF;

        switch (Reason)
        {
            case 0:  ExceptionNmiCount++; break;   /* EXCEPTION_NMI */
            case 2:  TripleFaultCount++; break;     /* TRIPLE_FAULT */
            case 10: CpuidCount++; break;           /* CPUID */
            case 12: HltCount++; break;             /* HLT */
            case 28: CrAccessCount++; break;        /* CR_ACCESS */
            case 30: IoCount++; break;              /* IO_INSTRUCTION */
            case 31: RdmsrCount++; break;           /* RDMSR */
            case 32: WrmsrCount++; break;           /* WRMSR */
            case 48: EptViolationCount++; break;    /* EPT_VIOLATION */
            default: OtherCount++; break;
        }
    }

    DbgPrint("[ROSV:STAT] === Exit Summary (%u total exits, %u in ring) ===\n",
             Ring->Count, Available);
    DbgPrint("[ROSV:STAT] EXCEPTION_NMI:   %u exits\n", ExceptionNmiCount);
    DbgPrint("[ROSV:STAT] CPUID:           %u exits\n", CpuidCount);
    DbgPrint("[ROSV:STAT] HLT:             %u exits\n", HltCount);
    DbgPrint("[ROSV:STAT] CR_ACCESS:        %u exits\n", CrAccessCount);
    DbgPrint("[ROSV:STAT] IO_INSTRUCTION:  %u exits\n", IoCount);
    DbgPrint("[ROSV:STAT] RDMSR:           %u exits\n", RdmsrCount);
    DbgPrint("[ROSV:STAT] WRMSR:           %u exits\n", WrmsrCount);
    DbgPrint("[ROSV:STAT] EPT_VIOLATION:   %u exits\n", EptViolationCount);
    DbgPrint("[ROSV:STAT] TRIPLE_FAULT:    %u exits\n", TripleFaultCount);
    DbgPrint("[ROSV:STAT] OTHER:           %u exits\n", OtherCount);
}
