/*
 *  ReactOS kernel
 *  Copyright (C) 2005 ReactOS Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
/*
 * PROJECT:         ReactOS kernel
 * FILE:            ntoskrnl/kdbg/kdb_cli.c
 * PURPOSE:         Kernel debugger command line interface
 * PROGRAMMER:      Gregor Anich (blight@blight.eu.org)
 *                  Hervé Poussineau
 * UPDATE HISTORY:
 *                  Created 16/01/2005
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>

#include "kdb.h"
#include "../kd/kdterminal.h"

#define NDEBUG
#include "debug.h"

/* DEFINES *******************************************************************/

#define KDB_ENTER_CONDITION_TO_STRING(cond)                               \
                   ((cond) == KdbDoNotEnter ? "never" :                   \
                   ((cond) == KdbEnterAlways ? "always" :                 \
                   ((cond) == KdbEnterFromKmode ? "kmode" : "umode")))

#define KDB_ACCESS_TYPE_TO_STRING(type)                                   \
                   ((type) == KdbAccessRead ? "read" :                    \
                   ((type) == KdbAccessWrite ? "write" :                  \
                   ((type) == KdbAccessReadWrite ? "rdwr" : "exec")))

#define NPX_STATE_TO_STRING(state)                                        \
                   ((state) == NPX_STATE_LOADED ? "Loaded" :              \
                   ((state) == NPX_STATE_NOT_LOADED ? "Not loaded" : "Unknown"))

/* PROTOTYPES ****************************************************************/

static BOOLEAN KdbpCmdEvalExpression(ULONG Argc, PCHAR Argv[]);
static BOOLEAN KdbpCmdNearestSymbol(ULONG Argc, PCHAR Argv[]);
static BOOLEAN KdbpCmdSymbolSearch(ULONG Argc, PCHAR Argv[]);
static BOOLEAN KdbpCmdDisassembleX(ULONG Argc, PCHAR Argv[]);
static BOOLEAN KdbpCmdDumpMemory(ULONG Argc, PCHAR Argv[]);
static BOOLEAN KdbpCmdDumpString(ULONG Argc, PCHAR Argv[]);
static BOOLEAN KdbpCmdDumpPointers(ULONG Argc, PCHAR Argv[]);
static BOOLEAN KdbpCmdEditMemory(ULONG Argc, PCHAR Argv[]);
static BOOLEAN KdbpCmdSearchMemory(ULONG Argc, PCHAR Argv[]);
static BOOLEAN KdbpCmdCompareMemory(ULONG Argc, PCHAR Argv[]);
static BOOLEAN KdbpCmdFillMemory(ULONG Argc, PCHAR Argv[]);
static BOOLEAN KdbpCmdMoveMemory(ULONG Argc, PCHAR Argv[]);
static BOOLEAN KdbpCmdRegs(ULONG Argc, PCHAR Argv[]);
static BOOLEAN KdbpCmdSetRegister(ULONG Argc, PCHAR Argv[]);
static BOOLEAN KdbpCmdFpRegs(ULONG Argc, PCHAR Argv[]);
static BOOLEAN KdbpCmdSetFpRegister(ULONG Argc, PCHAR Argv[]);
static BOOLEAN KdbpCmdContextRecord(ULONG Argc, PCHAR Argv[]);
static BOOLEAN KdbpCmdTrapFrame(ULONG Argc, PCHAR Argv[]);
static BOOLEAN KdbpCmdExceptionRecord(ULONG Argc, PCHAR Argv[]);
static BOOLEAN KdbpCmdFrame(ULONG Argc, PCHAR Argv[]);
static BOOLEAN KdbpCmdBackTrace(ULONG Argc, PCHAR Argv[]);

static BOOLEAN KdbpCmdContinue(ULONG Argc, PCHAR Argv[]);
static BOOLEAN KdbpCmdStep(ULONG Argc, PCHAR Argv[]);
static BOOLEAN KdbpCmdBreakPointList(ULONG Argc, PCHAR Argv[]);
static BOOLEAN KdbpCmdEnableDisableClearBreakPoint(ULONG Argc, PCHAR Argv[]);
static BOOLEAN KdbpCmdBreakPoint(ULONG Argc, PCHAR Argv[]);

static BOOLEAN KdbpCmdThread(ULONG Argc, PCHAR Argv[]);
static BOOLEAN KdbpCmdProc(ULONG Argc, PCHAR Argv[]);
static BOOLEAN KdbpCmdTeb(ULONG Argc, PCHAR Argv[]);
static BOOLEAN KdbpCmdPeb(ULONG Argc, PCHAR Argv[]);
static BOOLEAN KdbpCmdObject(ULONG Argc, PCHAR Argv[]);
static BOOLEAN KdbpCmdDriverObject(ULONG Argc, PCHAR Argv[]);
static BOOLEAN KdbpCmdDeviceObject(ULONG Argc, PCHAR Argv[]);
static BOOLEAN KdbpCmdDeviceStack(ULONG Argc, PCHAR Argv[]);
static BOOLEAN KdbpCmdIrp(ULONG Argc, PCHAR Argv[]);
static BOOLEAN KdbpCmdFileObject(ULONG Argc, PCHAR Argv[]);
static BOOLEAN KdbpCmdPrcb(ULONG Argc, PCHAR Argv[]);
static BOOLEAN KdbpCmdReady(ULONG Argc, PCHAR Argv[]);
static BOOLEAN KdbpCmdDpc(ULONG Argc, PCHAR Argv[]);
static BOOLEAN KdbpCmdTimer(ULONG Argc, PCHAR Argv[]);
static BOOLEAN KdbpCmdInterrupt(ULONG Argc, PCHAR Argv[]);
static BOOLEAN KdbpCmdLocks(ULONG Argc, PCHAR Argv[]);
static BOOLEAN KdbpCmdApc(ULONG Argc, PCHAR Argv[]);
static BOOLEAN KdbpCmdDispatcher(ULONG Argc, PCHAR Argv[]);

static BOOLEAN KdbpCmdMod(ULONG Argc, PCHAR Argv[]);
static BOOLEAN KdbpCmdGdtLdtIdt(ULONG Argc, PCHAR Argv[]);
static BOOLEAN KdbpCmdPcr(ULONG Argc, PCHAR Argv[]);
static BOOLEAN KdbpCmdVersion(ULONG Argc, PCHAR Argv[]);
static BOOLEAN KdbpCmdCpu(ULONG Argc, PCHAR Argv[]);
#ifdef _M_IX86
static BOOLEAN KdbpCmdTss(ULONG Argc, PCHAR Argv[]);
#endif

static BOOLEAN KdbpCmdBugCheck(ULONG Argc, PCHAR Argv[]);
static BOOLEAN KdbpCmdReboot(ULONG Argc, PCHAR Argv[]);
static BOOLEAN KdbpCmdFilter(ULONG Argc, PCHAR Argv[]);
static BOOLEAN KdbpCmdSet(ULONG Argc, PCHAR Argv[]);
static BOOLEAN KdbpCmdHelp(ULONG Argc, PCHAR Argv[]);
static BOOLEAN KdbpCmdDmesg(ULONG Argc, PCHAR Argv[]);
static BOOLEAN KdbpCmdEcho(ULONG Argc, PCHAR Argv[]);
static BOOLEAN KdbpCmdAlias(ULONG Argc, PCHAR Argv[]);
static BOOLEAN KdbpCmdUnalias(ULONG Argc, PCHAR Argv[]);
static BOOLEAN KdbpCmdLog(ULONG Argc, PCHAR Argv[]);
static BOOLEAN KdbpCmdScript(ULONG Argc, PCHAR Argv[]);
static BOOLEAN KdbpCmdRepeat(ULONG Argc, PCHAR Argv[]);
static BOOLEAN KdbpCmdSelfTest(ULONG Argc, PCHAR Argv[]);
static BOOLEAN KdbpDoCommand(PCHAR Command);

BOOLEAN ExpKdbgExtPool(ULONG Argc, PCHAR Argv[]);
BOOLEAN ExpKdbgExtPoolUsed(ULONG Argc, PCHAR Argv[]);
BOOLEAN ExpKdbgExtPoolFind(ULONG Argc, PCHAR Argv[]);
BOOLEAN ExpKdbgExtFileCache(ULONG Argc, PCHAR Argv[]);
BOOLEAN ExpKdbgExtDefWrites(ULONG Argc, PCHAR Argv[]);
BOOLEAN ExpKdbgExtIrpFind(ULONG Argc, PCHAR Argv[]);
BOOLEAN ExpKdbgExtHandle(ULONG Argc, PCHAR Argv[]);
BOOLEAN ExpKdbgExtPte(ULONG Argc, PCHAR Argv[]);
BOOLEAN ExpKdbgExtPfn(ULONG Argc, PCHAR Argv[]);
BOOLEAN ExpKdbgExtVad(ULONG Argc, PCHAR Argv[]);
BOOLEAN ExpKdbgExtAddress(ULONG Argc, PCHAR Argv[]);
BOOLEAN ExpKdbgExtVm(ULONG Argc, PCHAR Argv[]);

extern char __ImageBase;

static BOOLEAN KdbpCmdPrintStruct(ULONG Argc, PCHAR Argv[]);
static VOID KdbpPrintRemoteUnicodeString(PCUNICODE_STRING String);

/* Be more descriptive than intrinsics */
#ifndef Ke386GetGlobalDescriptorTable
# define Ke386GetGlobalDescriptorTable __sgdt
#endif
#ifndef Ke386GetLocalDescriptorTable
# define Ke386GetLocalDescriptorTable __sldt
#endif

/* Portability */
FORCEINLINE
ULONG_PTR
strtoulptr(const char* nptr, char** endptr, int base)
{
#ifdef _M_IX86
    return strtoul(nptr, endptr, base);
#else
    return strtoull(nptr, endptr, base);
#endif
}

/* GLOBALS *******************************************************************/

typedef
BOOLEAN
(NTAPI *PKDBG_CLI_ROUTINE)(
    IN PCHAR Command,
    IN ULONG Argc,
    IN PCH Argv[]);

static PKDBG_CLI_ROUTINE KdbCliCallbacks[10];
static BOOLEAN KdbUseIntelSyntax = TRUE; /* Set to TRUE for intel syntax */
static BOOLEAN KdbBreakOnModuleLoad = FALSE; /* Set to TRUE to break into KDB when a module is loaded */

static ULONG KdbNumberOfRowsPrinted = 0;
static ULONG KdbNumberOfColsPrinted = 0;
static BOOLEAN KdbOutputAborted = FALSE;
static BOOLEAN KdbRepeatLastCommand = FALSE;

#define KDB_MAX_ALIASES             32
#define KDB_ALIAS_NAME_LENGTH       32
#define KDB_ALIAS_COMMAND_LENGTH    512
#define KDB_TRANSCRIPT_SIZE         (64 * 1024)
#define KDB_MAX_COMMAND_DEPTH       16

typedef struct _KDB_ALIAS_ENTRY
{
    BOOLEAN InUse;
    CHAR Name[KDB_ALIAS_NAME_LENGTH];
    CHAR Command[KDB_ALIAS_COMMAND_LENGTH];
} KDB_ALIAS_ENTRY, *PKDB_ALIAS_ENTRY;

typedef struct _KDB_COMMAND_FRAME
{
    PCHAR Argv[256];
    CHAR Original[1024];
    CHAR Expanded[1024];
} KDB_COMMAND_FRAME, *PKDB_COMMAND_FRAME;

static KDB_ALIAS_ENTRY KdbAliases[KDB_MAX_ALIASES];
static KDB_COMMAND_FRAME KdbCommandFrames[KDB_MAX_COMMAND_DEPTH];
static ULONG KdbCommandDepth;
static BOOLEAN KdbTranscriptEnabled;
static CHAR KdbTranscript[KDB_TRANSCRIPT_SIZE];
static ULONG KdbTranscriptWrite;
static ULONG KdbTranscriptLength;

volatile PCHAR KdbInitFileBuffer = NULL; /* Buffer where KDBinit file is loaded into during initialization */
BOOLEAN KdbpBugCheckRequested = FALSE;

BOOLEAN
KdbpIsOutputAborted(VOID)
{
    return KdbOutputAborted;
}

VOID
KdbpCaptureOutput(IN PCCH String, IN USHORT Length)
{
    USHORT Index;

    if (!KdbTranscriptEnabled || String == NULL)
        return;
    for (Index = 0; Index < Length; Index++)
    {
        KdbTranscript[KdbTranscriptWrite] = String[Index];
        KdbTranscriptWrite = (KdbTranscriptWrite + 1) % KDB_TRANSCRIPT_SIZE;
        if (KdbTranscriptLength < KDB_TRANSCRIPT_SIZE)
            KdbTranscriptLength++;
    }
}

/* .cxr state is kept here so commands that mutate live state can reject or
 * reset an inspection-only context record. */
static CONTEXT KdbSavedContextRecord;
static BOOLEAN KdbContextRecordActive;
static PKDB_KTRAP_FRAME KdbSavedTrapFrame;
static CONTEXT KdbFrameBaseContext;
static BOOLEAN KdbFrameBaseValid;
static ULONG KdbSelectedFrame;
static LONG KdbSelectedProcessor = -1;

static VOID
KdbpDiscardStaleContextRecord(VOID)
{
    if (KdbContextRecordActive && KdbCurrentTrapFrame != (PKDB_KTRAP_FRAME)&KdbSavedContextRecord)
    {
        KdbContextRecordActive = FALSE;
        KdbSavedTrapFrame = NULL;
        KdbFrameBaseValid = FALSE;
        KdbSelectedFrame = 0;
        KdbSelectedProcessor = -1;
    }
}

static VOID
KdbpResetContextRecord(IN BOOLEAN Announce)
{
    KdbpDiscardStaleContextRecord();
    if (!KdbContextRecordActive)
        return;

    KdbCurrentTrapFrame = KdbSavedTrapFrame;
    KdbSavedTrapFrame = NULL;
    KdbContextRecordActive = FALSE;
    KdbFrameBaseValid = FALSE;
    KdbSelectedFrame = 0;
    KdbSelectedProcessor = -1;
    if (Announce)
        KdbpPrint("Resetting default context.\n");
}

/* Variables for Dmesg */
static const ULONG KdpDmesgBufferSize = 128 * 1024; // 512*1024;
static PCHAR KdpDmesgBuffer = NULL;
static volatile ULONG KdpDmesgCurrentPosition = 0;
static volatile ULONG KdpDmesgFreeBytes = 0;
static volatile ULONG KdbDmesgTotalWritten = 0;
static volatile BOOLEAN KdbpIsInDmesgMode = FALSE;
static KSPIN_LOCK KdpDmesgLogSpinLock;

const CSTRING KdbPromptStr = RTL_CONSTANT_STRING("kdb:> ");

//
// Debug Filter Component Table
//
#define KD_DEBUG_PRINT_FILTER(Name) \
    { #Name, DPFLTR_##Name##_ID }

static struct
{
    PCSTR Name;
    ULONG Id;
}
ComponentTable[] =
{
//
// Default components
//
    { "WIN2000", MAXULONG },
    KD_DEBUG_PRINT_FILTER(DEFAULT),
//
// Standard components
//
    KD_DEBUG_PRINT_FILTER(SYSTEM),
    KD_DEBUG_PRINT_FILTER(SMSS),
    KD_DEBUG_PRINT_FILTER(SETUP),
    KD_DEBUG_PRINT_FILTER(NTFS),
    KD_DEBUG_PRINT_FILTER(FSTUB),
    KD_DEBUG_PRINT_FILTER(CRASHDUMP),
    KD_DEBUG_PRINT_FILTER(CDAUDIO),
    KD_DEBUG_PRINT_FILTER(CDROM),
    KD_DEBUG_PRINT_FILTER(CLASSPNP),
    KD_DEBUG_PRINT_FILTER(DISK),
    KD_DEBUG_PRINT_FILTER(REDBOOK),
    KD_DEBUG_PRINT_FILTER(STORPROP),
    KD_DEBUG_PRINT_FILTER(SCSIPORT),
    KD_DEBUG_PRINT_FILTER(SCSIMINIPORT),
    KD_DEBUG_PRINT_FILTER(CONFIG),
    KD_DEBUG_PRINT_FILTER(I8042PRT),
    KD_DEBUG_PRINT_FILTER(SERMOUSE),
    KD_DEBUG_PRINT_FILTER(LSERMOUS),
    KD_DEBUG_PRINT_FILTER(KBDHID),
    KD_DEBUG_PRINT_FILTER(MOUHID),
    KD_DEBUG_PRINT_FILTER(KBDCLASS),
    KD_DEBUG_PRINT_FILTER(MOUCLASS),
    KD_DEBUG_PRINT_FILTER(TWOTRACK),
    KD_DEBUG_PRINT_FILTER(WMILIB),
    KD_DEBUG_PRINT_FILTER(ACPI),
    KD_DEBUG_PRINT_FILTER(AMLI),
    KD_DEBUG_PRINT_FILTER(HALIA64),
    KD_DEBUG_PRINT_FILTER(VIDEO),
    KD_DEBUG_PRINT_FILTER(SVCHOST),
    KD_DEBUG_PRINT_FILTER(VIDEOPRT),
    KD_DEBUG_PRINT_FILTER(TCPIP),
    KD_DEBUG_PRINT_FILTER(DMSYNTH),
    KD_DEBUG_PRINT_FILTER(NTOSPNP),
    KD_DEBUG_PRINT_FILTER(FASTFAT),
    KD_DEBUG_PRINT_FILTER(SAMSS),
    KD_DEBUG_PRINT_FILTER(PNPMGR),
    KD_DEBUG_PRINT_FILTER(NETAPI),
    KD_DEBUG_PRINT_FILTER(SCSERVER),
    KD_DEBUG_PRINT_FILTER(SCCLIENT),
    KD_DEBUG_PRINT_FILTER(SERIAL),
    KD_DEBUG_PRINT_FILTER(SERENUM),
    KD_DEBUG_PRINT_FILTER(UHCD),
    KD_DEBUG_PRINT_FILTER(RPCPROXY),
    KD_DEBUG_PRINT_FILTER(AUTOCHK),
    KD_DEBUG_PRINT_FILTER(DCOMSS),
    KD_DEBUG_PRINT_FILTER(UNIMODEM),
    KD_DEBUG_PRINT_FILTER(SIS),
    KD_DEBUG_PRINT_FILTER(FLTMGR),
    KD_DEBUG_PRINT_FILTER(WMICORE),
    KD_DEBUG_PRINT_FILTER(BURNENG),
    KD_DEBUG_PRINT_FILTER(IMAPI),
    KD_DEBUG_PRINT_FILTER(SXS),
    KD_DEBUG_PRINT_FILTER(FUSION),
    KD_DEBUG_PRINT_FILTER(IDLETASK),
    KD_DEBUG_PRINT_FILTER(SOFTPCI),
    KD_DEBUG_PRINT_FILTER(TAPE),
    KD_DEBUG_PRINT_FILTER(MCHGR),
    KD_DEBUG_PRINT_FILTER(IDEP),
    KD_DEBUG_PRINT_FILTER(PCIIDE),
    KD_DEBUG_PRINT_FILTER(FLOPPY),
    KD_DEBUG_PRINT_FILTER(FDC),
    KD_DEBUG_PRINT_FILTER(TERMSRV),
    KD_DEBUG_PRINT_FILTER(W32TIME),
    KD_DEBUG_PRINT_FILTER(PREFETCHER),
    KD_DEBUG_PRINT_FILTER(RSFILTER),
    KD_DEBUG_PRINT_FILTER(FCPORT),
    KD_DEBUG_PRINT_FILTER(PCI),
    KD_DEBUG_PRINT_FILTER(DMIO),
    KD_DEBUG_PRINT_FILTER(DMCONFIG),
    KD_DEBUG_PRINT_FILTER(DMADMIN),
    KD_DEBUG_PRINT_FILTER(WSOCKTRANSPORT),
    KD_DEBUG_PRINT_FILTER(VSS),
    KD_DEBUG_PRINT_FILTER(PNPMEM),
    KD_DEBUG_PRINT_FILTER(PROCESSOR),
    KD_DEBUG_PRINT_FILTER(DMSERVER),
    KD_DEBUG_PRINT_FILTER(SR),
    KD_DEBUG_PRINT_FILTER(INFINIBAND),
    KD_DEBUG_PRINT_FILTER(IHVDRIVER),
    KD_DEBUG_PRINT_FILTER(IHVVIDEO),
    KD_DEBUG_PRINT_FILTER(IHVAUDIO),
    KD_DEBUG_PRINT_FILTER(IHVNETWORK),
    KD_DEBUG_PRINT_FILTER(IHVSTREAMING),
    KD_DEBUG_PRINT_FILTER(IHVBUS),
    KD_DEBUG_PRINT_FILTER(HPS),
    KD_DEBUG_PRINT_FILTER(RTLTHREADPOOL),
    KD_DEBUG_PRINT_FILTER(LDR),
    KD_DEBUG_PRINT_FILTER(TCPIP6),
    KD_DEBUG_PRINT_FILTER(ISAPNP),
    KD_DEBUG_PRINT_FILTER(SHPC),
    KD_DEBUG_PRINT_FILTER(STORPORT),
    KD_DEBUG_PRINT_FILTER(STORMINIPORT),
    KD_DEBUG_PRINT_FILTER(PRINTSPOOLER),
    KD_DEBUG_PRINT_FILTER(VSSDYNDISK),
    KD_DEBUG_PRINT_FILTER(VERIFIER),
    KD_DEBUG_PRINT_FILTER(VDS),
    KD_DEBUG_PRINT_FILTER(VDSBAS),
    KD_DEBUG_PRINT_FILTER(VDSDYN),  // Specified in Vista+
    KD_DEBUG_PRINT_FILTER(VDSDYNDR),
    KD_DEBUG_PRINT_FILTER(VDSLDR),  // Specified in Vista+
    KD_DEBUG_PRINT_FILTER(VDSUTIL),
    KD_DEBUG_PRINT_FILTER(DFRGIFC),
    KD_DEBUG_PRINT_FILTER(MM),
    KD_DEBUG_PRINT_FILTER(DFSC),
    KD_DEBUG_PRINT_FILTER(WOW64),
//
// Components specified in Vista+, some of which we also use in ReactOS
//
    KD_DEBUG_PRINT_FILTER(ALPC),
    KD_DEBUG_PRINT_FILTER(WDI),
    KD_DEBUG_PRINT_FILTER(PERFLIB),
    KD_DEBUG_PRINT_FILTER(KTM),
    KD_DEBUG_PRINT_FILTER(IOSTRESS),
    KD_DEBUG_PRINT_FILTER(HEAP),
    KD_DEBUG_PRINT_FILTER(WHEA),
    KD_DEBUG_PRINT_FILTER(USERGDI),
    KD_DEBUG_PRINT_FILTER(MMCSS),
    KD_DEBUG_PRINT_FILTER(TPM),
    KD_DEBUG_PRINT_FILTER(THREADORDER),
    KD_DEBUG_PRINT_FILTER(ENVIRON),
    KD_DEBUG_PRINT_FILTER(EMS),
    KD_DEBUG_PRINT_FILTER(WDT),
    KD_DEBUG_PRINT_FILTER(FVEVOL),
    KD_DEBUG_PRINT_FILTER(NDIS),
    KD_DEBUG_PRINT_FILTER(NVCTRACE),
    KD_DEBUG_PRINT_FILTER(LUAFV),
    KD_DEBUG_PRINT_FILTER(APPCOMPAT),
    KD_DEBUG_PRINT_FILTER(USBSTOR),
    KD_DEBUG_PRINT_FILTER(SBP2PORT),
    KD_DEBUG_PRINT_FILTER(COVERAGE),
    KD_DEBUG_PRINT_FILTER(CACHEMGR),
    KD_DEBUG_PRINT_FILTER(MOUNTMGR),
    KD_DEBUG_PRINT_FILTER(CFR),
    KD_DEBUG_PRINT_FILTER(TXF),
    KD_DEBUG_PRINT_FILTER(KSECDD),
    KD_DEBUG_PRINT_FILTER(FLTREGRESS),
    KD_DEBUG_PRINT_FILTER(MPIO),
    KD_DEBUG_PRINT_FILTER(MSDSM),
    KD_DEBUG_PRINT_FILTER(UDFS),
    KD_DEBUG_PRINT_FILTER(PSHED),
    KD_DEBUG_PRINT_FILTER(STORVSP),
    KD_DEBUG_PRINT_FILTER(LSASS),
    KD_DEBUG_PRINT_FILTER(SSPICLI),
    KD_DEBUG_PRINT_FILTER(CNG),
    KD_DEBUG_PRINT_FILTER(EXFAT),
    KD_DEBUG_PRINT_FILTER(FILETRACE),
    KD_DEBUG_PRINT_FILTER(XSAVE),
    KD_DEBUG_PRINT_FILTER(SE),
    KD_DEBUG_PRINT_FILTER(DRIVEEXTENDER),
//
// Components specified in Windows 8
//
    KD_DEBUG_PRINT_FILTER(POWER),
    KD_DEBUG_PRINT_FILTER(CRASHDUMPXHCI),
    KD_DEBUG_PRINT_FILTER(GPIO),
    KD_DEBUG_PRINT_FILTER(REFS),
    KD_DEBUG_PRINT_FILTER(WER),
//
// Components specified in Windows 10
//
    KD_DEBUG_PRINT_FILTER(CAPIMG),
    KD_DEBUG_PRINT_FILTER(VPCI),
    KD_DEBUG_PRINT_FILTER(STORAGECLASSMEMORY),
    KD_DEBUG_PRINT_FILTER(FSLIB),
};
#undef KD_DEBUG_PRINT_FILTER

//
// Command Table
//
static const struct
{
    PCHAR Name;
    PCHAR Syntax;
    PCHAR Help;
    BOOLEAN (*Fn)(ULONG Argc, PCHAR Argv[]);
} KdbDebuggerCommands[] = {
    /* Data */
    { NULL, NULL, "Data", NULL },
    { "?", "? expression", "Evaluate expression.", KdbpCmdEvalExpression },
    { "ln", "ln [address]", "Display the symbol and source nearest an address.", KdbpCmdNearestSymbol },
    { "sym", "sym [module!]pattern [L count]", "Search loaded symbols using '*' and '?'.", KdbpCmdSymbolSearch },
    { "disasm", "disasm [address] [L count]", "Disassemble count instructions at address.", KdbpCmdDisassembleX },
    { "u", "u [address] [L count]", "Alias for disasm.", KdbpCmdDisassembleX },
    { "x", "x [address] [L count]", "Display count dwords, starting at address.", KdbpCmdDisassembleX },
    { "db", "db address [L count]", "Display bytes from memory.", KdbpCmdDumpMemory },
    { "dw", "dw address [L count]", "Display words from memory.", KdbpCmdDumpMemory },
    { "dd", "dd address [L count]", "Display dwords from memory.", KdbpCmdDumpMemory },
    { "dq", "dq address [L count]", "Display qwords from memory.", KdbpCmdDumpMemory },
    { "dp", "dp address [L count]", "Display pointer-sized values from memory.", KdbpCmdDumpMemory },
    { "da", "da address [L count]", "Display an ANSI string from memory.", KdbpCmdDumpString },
    { "du", "du address [L count]", "Display a Unicode string from memory.", KdbpCmdDumpString },
    { "dds", "dds address [L count]", "Display dwords and their symbols.", KdbpCmdDumpPointers },
    { "dqs", "dqs address [L count]", "Display qwords and their symbols.", KdbpCmdDumpPointers },
    { "dps", "dps address [L count]", "Display pointers and their symbols.", KdbpCmdDumpPointers },
    { "eb", "eb address value [value ...]", "Write bytes to memory.", KdbpCmdEditMemory },
    { "ew", "ew address value [value ...]", "Write words to memory.", KdbpCmdEditMemory },
    { "ed", "ed address value [value ...]", "Write dwords to memory.", KdbpCmdEditMemory },
    { "eq", "eq address value [value ...]", "Write qwords to memory.", KdbpCmdEditMemory },
    { "search", "search address length byte [byte ...]", "Search memory for a byte pattern.", KdbpCmdSearchMemory },
    { "compare", "compare address1 address2 length", "Compare two memory ranges.", KdbpCmdCompareMemory },
    { "fill", "fill address length byte [byte ...]", "Fill memory with a byte pattern.", KdbpCmdFillMemory },
    { "move", "move source destination length", "Move a possibly overlapping memory range.", KdbpCmdMoveMemory },
    { "regs", "regs", "Display general purpose registers.", KdbpCmdRegs },
    { "setreg", "setreg register expression", "Set a register in the live exception context.", KdbpCmdSetRegister },
    { "fpregs", "fpregs [register]", "Display floating-point and vector registers.", KdbpCmdFpRegs },
    { "vregs", "vregs [register]", "Alias for fpregs.", KdbpCmdFpRegs },
    { "setfpreg", "setfpreg register hexvalue", "Set a floating-point or vector register.", KdbpCmdSetFpRegister },
    { "setvreg", "setvreg register hexvalue", "Alias for setfpreg.", KdbpCmdSetFpRegister },
    { "cregs", "cregs", "Display control, descriptor table and task segment registers.", KdbpCmdRegs },
    { "sregs", "sregs", "Display status registers.", KdbpCmdRegs },
    { "dregs", "dregs", "Display debug registers.", KdbpCmdRegs },
    { ".cxr", ".cxr [address]", "Set or reset context record. With address: display context at address. Without: reset to current trap frame.", KdbpCmdContextRecord },
    { ".trap", ".trap [address]", "Set or reset an inspection context from a trap frame.", KdbpCmdTrapFrame },
    { ".exr", ".exr [-1|address]", "Display the current or specified exception record.", KdbpCmdExceptionRecord },
    { ".frame", ".frame [number]", "Select and display a stack frame for inspection.", KdbpCmdFrame },
    { "bt", "bt [all|verbose|*frameaddr|thread id]", "Print one or all thread backtraces.", KdbpCmdBackTrace },
    { "dt", "dt [module!]type [address]", "Display an embedded kernel type layout or instance.", KdbpCmdPrintStruct },
    /* Flow control */
    { NULL, NULL, "Flow control", NULL },
    { "cont", "cont", "Continue execution (leave debugger).", KdbpCmdContinue },
    { "step", "step [count]", "Execute single instructions, stepping into interrupts.", KdbpCmdStep },
    { "next", "next [count]", "Execute single instructions, skipping calls and reps.", KdbpCmdStep },
    { "bl", "bl", "List breakpoints.", KdbpCmdBreakPointList },
    { "be", "be [breakpoint]", "Enable breakpoint.", KdbpCmdEnableDisableClearBreakPoint },
    { "bd", "bd [breakpoint]", "Disable breakpoint.", KdbpCmdEnableDisableClearBreakPoint },
    { "bc", "bc [breakpoint]", "Clear breakpoint.", KdbpCmdEnableDisableClearBreakPoint },
    { "bpx", "bpx [address] [IF condition]", "Set software execution breakpoint at address.", KdbpCmdBreakPoint },
    { "bpm", "bpm [r|w|rw|x] [byte|word|dword|qword] [address] [IF condition]", "Set memory breakpoint at address.", KdbpCmdBreakPoint },

    /* Process/Thread */
    { NULL, NULL, "Process/Thread", NULL },
    { "thread", "thread [list[ pid]|[attach ]tid]", "List threads in current or specified process, display thread with given id or attach to thread.", KdbpCmdThread },
    { "!thread", "!thread [tid]", "Display a thread or the current thread.", KdbpCmdThread },
    { "proc", "proc [list|[attach ]pid]", "List processes, display process with given id or attach to process.", KdbpCmdProc },
    { "!process", "!process [pid]", "Display a process or the current process.", KdbpCmdProc },
    { "!teb", "!teb [address]", "Display a native TEB using guarded reads.", KdbpCmdTeb },
    { "!peb", "!peb [address]", "Display a native PEB using guarded reads.", KdbpCmdPeb },

    /* Objects and I/O */
    { NULL, NULL, "Objects and I/O", NULL },
    { "!object", "!object address", "Display an object header, type and name.", KdbpCmdObject },
    { "!drvobj", "!drvobj address", "Display a driver object, devices and dispatch table.", KdbpCmdDriverObject },
    { "!devobj", "!devobj address", "Display a device object.", KdbpCmdDeviceObject },
    { "!devstack", "!devstack address", "Display the attached device stack from a device object.", KdbpCmdDeviceStack },
    { "!irp", "!irp address", "Display an IRP and its I/O stack locations.", KdbpCmdIrp },
    { "!fileobj", "!fileobj address", "Display a file object and its name.", KdbpCmdFileObject },

    /* System information */
    { NULL, NULL, "System info", NULL },
    { "mod", "mod [address]", "List all modules or the one containing address.", KdbpCmdMod },
    { "gdt", "gdt", "Display the global descriptor table.", KdbpCmdGdtLdtIdt },
    { "ldt", "ldt", "Display the local descriptor table.", KdbpCmdGdtLdtIdt },
    { "idt", "idt", "Display the interrupt descriptor table.", KdbpCmdGdtLdtIdt },
    { "pcr", "pcr", "Display the processor control region.", KdbpCmdPcr },
    { "version", "version", "Display kernel, architecture and debugger version information.", KdbpCmdVersion },
    { "cpu", "cpu [number|current]", "List processors or inspect a frozen processor context.", KdbpCmdCpu },
#ifdef _M_IX86
    { "tss", "tss [selector|*descaddr]", "Display the current task state segment, or the one specified by its selector number or descriptor address.", KdbpCmdTss },
#endif

    /* Others */
    { NULL, NULL, "Others", NULL },
    { "bugcheck", "bugcheck", "Bugchecks the system.", KdbpCmdBugCheck },
    { "reboot", "reboot", "Reboots the system.", KdbpCmdReboot},
    { "filter", "filter [error|warning|trace|info|level]+|-[componentname|default]", "Enable/disable debug channels.", KdbpCmdFilter },
    { "set", "set [var] [value]", "Sets var to value or displays value of var.", KdbpCmdSet },
    { "dmesg", "dmesg", "Display debug messages on screen, with navigation on pages.", KdbpCmdDmesg },
    { "kmsg", "kmsg", "Kernel dmesg. Alias for dmesg.", KdbpCmdDmesg },
    { "echo", "echo [text]", "Print text in the debugger transcript.", KdbpCmdEcho },
    { "alias", "alias [name [command]]", "List, inspect, or define a fixed debugger alias.", KdbpCmdAlias },
    { "unalias", "unalias name", "Delete a debugger alias.", KdbpCmdUnalias },
    { "log", "log [on|off|clear|show]", "Capture and display an in-memory debugger transcript.", KdbpCmdLog },
    { "script", "script address length", "Execute newline-delimited commands from guarded memory.", KdbpCmdScript },
    { "repeat", "repeat count command", "Execute one debugger command repeatedly.", KdbpCmdRepeat },
    { "selftest", "selftest", "Run non-destructive KDB command-engine and decoder tests.", KdbpCmdSelfTest },
    { "help", "help [command]", "Display all commands or detailed help for one command.", KdbpCmdHelp },
    { "!pool", "!pool [Address [Flags]]", "Display information about pool allocations.", ExpKdbgExtPool },
    { "!poolused", "!poolused [Flags [Tag]]", "Display pool usage.", ExpKdbgExtPoolUsed },
    { "!poolfind", "!poolfind Tag [Pool]", "Search for pool tag allocations.", ExpKdbgExtPoolFind },
    { "!filecache", "!filecache", "Display cache usage.", ExpKdbgExtFileCache },
    { "!defwrites", "!defwrites", "Display cache write values.", ExpKdbgExtDefWrites },
    { "!irpfind", "!irpfind [Pool [startaddress [criteria data]]]", "Lists IRPs potentially matching criteria.", ExpKdbgExtIrpFind },
    { "!handle", "!handle [Handle]", "Displays info about handles.", ExpKdbgExtHandle },
    { "!pte", "!pte address", "Display the paging hierarchy for a virtual address.", ExpKdbgExtPte },
    { "!pfn", "!pfn page-frame-number", "Display a guarded PFN database entry.", ExpKdbgExtPfn },
    { "!vad", "!vad [address]", "List VADs or display the VAD containing an address.", ExpKdbgExtVad },
    { "!address", "!address address", "Describe an address, its VAD and page-table translation.", ExpKdbgExtAddress },
    { "!vm", "!vm", "Display global virtual-memory state.", ExpKdbgExtVm },

    /* Scheduler and hardware */
    { NULL, NULL, "Scheduler and hardware", NULL },
    { "!prcb", "!prcb [cpu]", "Display a processor control block using guarded reads.", KdbpCmdPrcb },
    { "!ready", "!ready [cpu]", "Display per-priority ready queues for a processor.", KdbpCmdReady },
    { "!dpc", "!dpc [cpu]", "Display normal and threaded DPC queues for a processor.", KdbpCmdDpc },
    { "!timer", "!timer [address]", "List queued timers or display one timer.", KdbpCmdTimer },
    { "!interrupt", "!interrupt address", "Display a kernel interrupt object.", KdbpCmdInterrupt },
    { "!locks", "!locks [address]", "List executive resources or display one resource.", KdbpCmdLocks },
    { "!apc", "!apc [tid]", "Display queued kernel and user APCs for a thread.", KdbpCmdApc },
    { "!dispatcher", "!dispatcher address", "Display a dispatcher object header and waiters.", KdbpCmdDispatcher },
};

/* FUNCTIONS *****************************************************************/

/*!\brief Evaluates an expression...
 *
 * Much like KdbpRpnEvaluateExpression, but prints the error message (if any)
 * at the given offset.
 *
 * \param Expression  Expression to evaluate.
 * \param ErrOffset   Offset (in characters) to print the error message at.
 * \param Result      Receives the result on success.
 *
 * \retval TRUE   Success.
 * \retval FALSE  Failure.
 */
static BOOLEAN
KdbpEvaluateExpression(
    IN  PCHAR Expression,
    IN  LONG ErrOffset,
    OUT PULONGLONG Result)
{
    static CHAR ErrMsgBuffer[130] = "^ ";
    LONG ExpressionErrOffset = -1;
    PCHAR ErrMsg = ErrMsgBuffer;
    BOOLEAN Ok;

    Ok = KdbpRpnEvaluateExpression(Expression, KdbCurrentTrapFrame, Result,
                                   &ExpressionErrOffset, ErrMsgBuffer + 2);
    if (!Ok)
    {
        if (ExpressionErrOffset >= 0)
            ExpressionErrOffset += ErrOffset;
        else
            ErrMsg += 2;

        KdbpPrint("%*s%s\n", ExpressionErrOffset, "", ErrMsg);
    }

    return Ok;
}

BOOLEAN
NTAPI
KdbpGetHexNumber(
    IN PCHAR pszNum,
    OUT ULONG_PTR *pulValue)
{
    ULONG_PTR Value = 0;
    ULONG Digit;
    BOOLEAN SawDigit = FALSE;

    /* Skip optional '0x' prefix */
    if ((pszNum[0] == '0') && ((pszNum[1] == 'x') || (pszNum[1] == 'X')))
        pszNum += 2;

    while (*pszNum != ANSI_NULL)
    {
        if (*pszNum >= '0' && *pszNum <= '9')
            Digit = *pszNum - '0';
        else if (*pszNum >= 'a' && *pszNum <= 'f')
            Digit = *pszNum - 'a' + 10;
        else if (*pszNum >= 'A' && *pszNum <= 'F')
            Digit = *pszNum - 'A' + 10;
        else
            return FALSE;

        if (Value > (MAXULONG_PTR - Digit) / 16)
            return FALSE;

        Value = Value * 16 + Digit;
        SawDigit = TRUE;
        pszNum++;
    }

    if (!SawDigit)
        return FALSE;

    *pulValue = Value;
    return TRUE;
}

/*!\brief Evaluates an expression and displays the result.
 */
static BOOLEAN
KdbpCmdEvalExpression(
    ULONG Argc,
    PCHAR Argv[])
{
    ULONG i;
    SIZE_T len;
    ULONGLONG Result = 0;
    ULONG ul;
    LONG l = 0;
    BOOLEAN Ok;

    if (Argc < 2)
    {
        KdbpPrint("?: Argument required\n");
        return TRUE;
    }

    /* Put the arguments back together */
    Argc--;
    for (i = 1; i < Argc; i++)
    {
        len = strlen(Argv[i]);
        Argv[i][len] = ' ';
    }

    /* Evaluate the expression */
    Ok = KdbpEvaluateExpression(Argv[1], KdbPromptStr.Length + (Argv[1]-Argv[0]), &Result);
    if (Ok)
    {
        if (Result > 0x00000000ffffffffLL)
        {
            if (Result & 0x8000000000000000LL)
                KdbpPrint("0x%016I64x  %20I64u  %20I64d\n", Result, Result, Result);
            else
                KdbpPrint("0x%016I64x  %20I64u\n", Result, Result);
        }
        else
        {
            ul = (ULONG)Result;

            if (ul <= 0xff && ul >= 0x80)
                l = (LONG)((CHAR)ul);
            else if (ul <= 0xffff && ul >= 0x8000)
                l = (LONG)((SHORT)ul);
            else
                l = (LONG)ul;

            if (l < 0)
                KdbpPrint("0x%08lx  %10lu  %10ld\n", ul, ul, l);
            else
                KdbpPrint("0x%08lx  %10lu\n", ul, ul);
        }
    }

    return TRUE;
}

static BOOLEAN
KdbpEvaluateAddress(IN PCHAR Expression, IN LONG ErrOffset, OUT PULONG_PTR Address)
{
    ULONGLONG Result;

    if (!KdbpEvaluateExpression(Expression, ErrOffset, &Result))
        return FALSE;

    if (Result > (ULONGLONG)MAXULONG_PTR)
    {
        KdbpPrint("Address 0x%I64x does not fit in a pointer.\n", Result);
        return FALSE;
    }

    *Address = (ULONG_PTR)Result;
    return TRUE;
}

BOOLEAN
NTAPI
KdbpGetAddressExpression(IN PCHAR Expression, OUT PULONG_PTR Address)
{
    return KdbpEvaluateAddress(Expression, 0, Address);
}

static BOOLEAN
KdbpCmdNearestSymbol(ULONG Argc, PCHAR Argv[])
{
    ULONG_PTR Address;

    if (Argc > 2)
    {
        KdbpPrint("Usage: ln [address]\n");
        return TRUE;
    }

    if (Argc == 1)
    {
        Address = KeGetContextPc(KdbCurrentTrapFrame);
    }
    else if (!KdbpEvaluateAddress(Argv[1], KdbPromptStr.Length + (Argv[1] - Argv[0]), &Address))
    {
        return TRUE;
    }

    if (!KdbSymPrintNearest((PVOID)Address, KdbCurrentTrapFrame))
        KdbpPrint("ln: No loaded module contains %p.\n", (PVOID)Address);
    return TRUE;
}

static
BOOLEAN
NTAPI
KdbpPrintSymbolMatch(IN ULONG_PTR Address, IN PCSTR ModuleName, IN PCSTR FunctionName, IN PCSTR FileName, IN ULONG SourceLine, IN PVOID Context OPTIONAL)
{
    UNREFERENCED_PARAMETER(Context);

    if (FileName[0] != ANSI_NULL)
    {
        KdbpPrint("%p %s!%s [%s:%lu]\n", (PVOID)Address, ModuleName, FunctionName, FileName, SourceLine);
    }
    else
    {
        KdbpPrint("%p %s!%s\n", (PVOID)Address, ModuleName, FunctionName);
    }
    return !KdbOutputAborted;
}

static BOOLEAN
KdbpParseMemoryCount(IN ULONG Argc, IN PCHAR Argv[], IN ULONG DefaultCount, OUT PULONG Count)
{
    PCHAR CountText;
    PCHAR End;
    ULONG Value;

    if (Argc == 2)
    {
        *Count = DefaultCount;
        return TRUE;
    }

    if (Argc == 3 && (Argv[2][0] == 'L' || Argv[2][0] == 'l'))
    {
        CountText = Argv[2] + 1;
    }
    else if (Argc == 4 && _stricmp(Argv[2], "L") == 0)
    {
        CountText = Argv[3];
    }
    else
    {
        return FALSE;
    }

    End = CountText;
    Value = strtoul(CountText, &End, 0);
    if (End == CountText || *End != '\0' || Value == 0)
        return FALSE;

    *Count = Value;
    return TRUE;
}

static BOOLEAN
KdbpCmdSymbolSearch(ULONG Argc, PCHAR Argv[])
{
    CHAR ModulePattern[128];
    CHAR SymbolPattern[512];
    PCSTR Separator;
    ULONG MaximumMatches;
    ULONG Matches;
    BOOLEAN Truncated;
    NTSTATUS Status;
    SIZE_T ModuleLength;
    SIZE_T SymbolLength;

    if (Argc != 2 && Argc != 3 && Argc != 4)
    {
        KdbpPrint("Usage: sym [module!]pattern [L count]\n");
        return TRUE;
    }

    if (!KdbpParseMemoryCount(Argc, Argv, 1024, &MaximumMatches) ||
        MaximumMatches > 65536)
    {
        KdbpPrint("sym: Match count must be between 1 and 65536.\n");
        return TRUE;
    }

    Separator = strchr(Argv[1], '!');
    if (Separator != NULL)
    {
        ModuleLength = Separator - Argv[1];
        SymbolLength = strlen(Separator + 1);
        if (ModuleLength == 0)
        {
            ModulePattern[0] = '*';
            ModulePattern[1] = ANSI_NULL;
        }
        else if (ModuleLength >= sizeof(ModulePattern))
        {
            KdbpPrint("sym: Module pattern is too long.\n");
            return TRUE;
        }
        else
        {
            RtlCopyMemory(ModulePattern, Argv[1], ModuleLength);
            ModulePattern[ModuleLength] = ANSI_NULL;
        }
        if (SymbolLength == 0)
        {
            SymbolPattern[0] = '*';
            SymbolPattern[1] = ANSI_NULL;
        }
        else if (SymbolLength >= sizeof(SymbolPattern))
        {
            KdbpPrint("sym: Symbol pattern is too long.\n");
            return TRUE;
        }
        else
        {
            RtlCopyMemory(SymbolPattern, Separator + 1, SymbolLength + 1);
        }
    }
    else
    {
        SymbolLength = strlen(Argv[1]);
        if (SymbolLength == 0 || SymbolLength >= sizeof(SymbolPattern))
        {
            KdbpPrint("sym: Symbol pattern is empty or too long.\n");
            return TRUE;
        }
        ModulePattern[0] = '*';
        ModulePattern[1] = ANSI_NULL;
        RtlCopyMemory(SymbolPattern, Argv[1], SymbolLength + 1);
    }

    Status = KdbSymEnumerate(ModulePattern, SymbolPattern, MaximumMatches, KdbpPrintSymbolMatch, NULL, &Matches, &Truncated);
    if (!NT_SUCCESS(Status))
    {
        KdbpPrint("sym: Enumeration failed with status 0x%08lx.\n", Status);
    }
    else if (Matches == 0)
    {
        KdbpPrint("sym: No matching loaded symbols.\n");
    }
    else if (Truncated && !KdbOutputAborted)
    {
        KdbpPrint("sym: Output stopped after %lu match(es) at a safety limit.\n", Matches);
    }
    return TRUE;
}

static ULONG
KdbpMemoryUnitSize(IN CHAR Suffix)
{
    switch (tolower(Suffix))
    {
        case 'b': return sizeof(UCHAR);
        case 'w': return sizeof(USHORT);
        case 'd': return sizeof(ULONG);
        case 'q': return sizeof(ULONGLONG);
        case 'p': return sizeof(ULONG_PTR);
        default: return 0;
    }
}

static VOID
KdbpPrintMemoryValue(IN ULONGLONG Value, IN ULONG Size)
{
    switch (Size)
    {
        case sizeof(UCHAR):
            KdbpPrint(" %02x", (UCHAR)Value);
            break;
        case sizeof(USHORT):
            KdbpPrint(" %04x", (USHORT)Value);
            break;
        case sizeof(ULONG):
            KdbpPrint(" %08x", (ULONG)Value);
            break;
        case sizeof(ULONGLONG):
            KdbpPrint(" %016I64x", Value);
            break;
    }
}

static VOID
KdbpPrintUnreadableMemoryValue(IN ULONG Size)
{
    switch (Size)
    {
        case sizeof(UCHAR): KdbpPrint(" ??"); break;
        case sizeof(USHORT): KdbpPrint(" ????"); break;
        case sizeof(ULONG): KdbpPrint(" ????????"); break;
        case sizeof(ULONGLONG): KdbpPrint(" ????????????????"); break;
    }
}

static BOOLEAN
KdbpCmdDumpMemory(ULONG Argc, PCHAR Argv[])
{
    ULONG Size;
    ULONG Count;
    ULONG PerLine;
    ULONG Column;
    ULONG_PTR Address;
    ULONGLONG Value;

    Size = KdbpMemoryUnitSize(Argv[0][1]);
    ASSERT(Size != 0);

    if (Argc < 2)
    {
        KdbpPrint("%s: Address argument required.\n", Argv[0]);
        return TRUE;
    }

    if (!KdbpParseMemoryCount(Argc, Argv, 16 / Size, &Count))
    {
        KdbpPrint("Usage: %s address [L count]\n", Argv[0]);
        return TRUE;
    }

    if (!KdbpEvaluateAddress(Argv[1], KdbPromptStr.Length + (Argv[1] - Argv[0]), &Address))
    {
        return TRUE;
    }

    PerLine = 16 / Size;
    while (Count != 0)
    {
        KdbpPrint("%p ", (PVOID)Address);
        for (Column = 0; Column < PerLine && Count != 0; Column++, Count--)
        {
            if (Address > MAXULONG_PTR - (Size - 1))
            {
                KdbpPrint("\n%s: Address range wraps around.\n", Argv[0]);
                return TRUE;
            }

            Value = 0;
            if (NT_SUCCESS(KdbpSafeReadMemory(&Value, (PVOID)Address, Size)))
                KdbpPrintMemoryValue(Value, Size);
            else
                KdbpPrintUnreadableMemoryValue(Size);

            if (Count > 1 && Address > MAXULONG_PTR - Size)
            {
                KdbpPrint("\n%s: Address range wraps around.\n", Argv[0]);
                return TRUE;
            }
            if (Count > 1)
                Address += Size;
        }
        KdbpPrint("\n");
    }

    return TRUE;
}

static BOOLEAN
KdbpCmdDumpString(ULONG Argc, PCHAR Argv[])
{
    ULONG Count;
    ULONG Index;
    ULONG UnitSize;
    ULONG_PTR Address;
    USHORT Character;
    NTSTATUS Status;

    UnitSize = (tolower(Argv[0][1]) == 'u') ? sizeof(WCHAR) : sizeof(CHAR);
    if (Argc < 2)
    {
        KdbpPrint("%s: Address argument required.\n", Argv[0]);
        return TRUE;
    }

    if (!KdbpParseMemoryCount(Argc, Argv, 128, &Count))
    {
        KdbpPrint("Usage: %s address [L count]\n", Argv[0]);
        return TRUE;
    }

    if (!KdbpEvaluateAddress(Argv[1], KdbPromptStr.Length + (Argv[1] - Argv[0]), &Address))
        return TRUE;

    KdbpPrint("%p  \"", (PVOID)Address);
    for (Index = 0; Index < Count; Index++)
    {
        if (Address > MAXULONG_PTR - (UnitSize - 1))
        {
            KdbpPrint("\"\n%s: Address range wraps around.\n", Argv[0]);
            return TRUE;
        }

        Character = 0;
        Status = KdbpSafeReadMemory(&Character, (PVOID)Address, UnitSize);
        if (!NT_SUCCESS(Status))
        {
            KdbpPrint("\"\n%s: Failed to read at %p (status 0x%08lx).\n", Argv[0], (PVOID)Address, Status);
            return TRUE;
        }

        if (Character == UNICODE_NULL)
            break;

        switch (Character)
        {
            case '\\': KdbpPrint("\\\\"); break;
            case '"':  KdbpPrint("\\\""); break;
            case '\n': KdbpPrint("\\n"); break;
            case '\r': KdbpPrint("\\r"); break;
            case '\t': KdbpPrint("\\t"); break;
            default:
                if (Character >= 0x20 && Character < 0x7f)
                    KdbpPrint("%c", (CHAR)Character);
                else if (UnitSize == sizeof(WCHAR))
                    KdbpPrint("\\u%04x", Character);
                else
                    KdbpPrint("\\x%02x", (UCHAR)Character);
                break;
        }

        if (KdbOutputAborted)
            return TRUE;
        if (Index + 1 < Count)
        {
            if (Address > MAXULONG_PTR - UnitSize)
            {
                KdbpPrint("\"\n%s: Address range wraps around.\n", Argv[0]);
                return TRUE;
            }
            Address += UnitSize;
        }
    }
    KdbpPrint("\"\n");
    return TRUE;
}

static BOOLEAN
KdbpCmdDumpPointers(ULONG Argc, PCHAR Argv[])
{
    ULONG Count;
    ULONG Index;
    ULONG Size;
    ULONG_PTR Address;
    ULONGLONG Value;
    NTSTATUS Status;

    Size = KdbpMemoryUnitSize(Argv[0][1]);
    ASSERT(Size != 0);

    if (Argc < 2)
    {
        KdbpPrint("%s: Address argument required.\n", Argv[0]);
        return TRUE;
    }
    if (!KdbpParseMemoryCount(Argc, Argv, 8, &Count))
    {
        KdbpPrint("Usage: %s address [L count]\n", Argv[0]);
        return TRUE;
    }
    if (!KdbpEvaluateAddress(Argv[1], KdbPromptStr.Length + (Argv[1] - Argv[0]), &Address))
        return TRUE;

    for (Index = 0; Index < Count; Index++)
    {
        if (Address > MAXULONG_PTR - (Size - 1))
        {
            KdbpPrint("%s: Address range wraps around.\n", Argv[0]);
            return TRUE;
        }

        Value = 0;
        Status = KdbpSafeReadMemory(&Value, (PVOID)Address, Size);
        KdbpPrint("%p  ", (PVOID)Address);
        if (NT_SUCCESS(Status))
        {
            KdbpPrintMemoryValue(Value, Size);
            KdbpPrint("  ");
            if (!KdbSymPrintAddress((PVOID)(ULONG_PTR)Value, NULL))
                KdbpPrint("<%p>", (PVOID)(ULONG_PTR)Value);
            KdbpPrint("\n");
        }
        else
        {
            KdbpPrintUnreadableMemoryValue(Size);
            KdbpPrint("  (status 0x%08lx)\n", Status);
        }

        if (KdbOutputAborted)
            return TRUE;
        if (Index + 1 < Count)
        {
            if (Address > MAXULONG_PTR - Size)
            {
                KdbpPrint("%s: Address range wraps around.\n", Argv[0]);
                return TRUE;
            }
            Address += Size;
        }
    }
    return TRUE;
}

static BOOLEAN
KdbpCmdEditMemory(ULONG Argc, PCHAR Argv[])
{
    ULONG Size;
    ULONG Index;
    ULONG_PTR Address;
    ULONGLONG Value;
    NTSTATUS Status;

    Size = KdbpMemoryUnitSize(Argv[0][1]);
    ASSERT(Size != 0);

    if (Argc < 3)
    {
        KdbpPrint("Usage: %s address value [value ...]\n", Argv[0]);
        return TRUE;
    }

    if (!KdbpEvaluateAddress(Argv[1], KdbPromptStr.Length + (Argv[1] - Argv[0]), &Address))
    {
        return TRUE;
    }

    for (Index = 2; Index < Argc; Index++)
    {
        if (Address > MAXULONG_PTR - (Size - 1))
        {
            KdbpPrint("%s: Address range wraps around.\n", Argv[0]);
            return TRUE;
        }

        if (!KdbpEvaluateExpression(Argv[Index], KdbPromptStr.Length + (Argv[Index] - Argv[0]), &Value))
        {
            return TRUE;
        }

        if (Size < sizeof(Value) && (Value >> (Size * 8)) != 0)
        {
            KdbpPrint("%s: Value 0x%I64x does not fit in %lu bits.\n", Argv[0], Value, Size * 8);
            return TRUE;
        }

        Status = KdbpSafeWriteMemory((PVOID)Address, &Value, Size);
        if (!NT_SUCCESS(Status))
        {
            KdbpPrint("%s: Failed to write %lu bytes at %p (status 0x%08lx).\n", Argv[0], Size, (PVOID)Address, Status);
            return TRUE;
        }

        if (Index + 1 < Argc && Address > MAXULONG_PTR - Size)
        {
            KdbpPrint("%s: Address range wraps around.\n", Argv[0]);
            return TRUE;
        }
        if (Index + 1 < Argc)
            Address += Size;
    }

    return TRUE;
}

#define KDB_MEMORY_IO_CHUNK_SIZE 256
#define KDB_MEMORY_MAX_PATTERN 64

static BOOLEAN
KdbpCmdSearchMemory(ULONG Argc, PCHAR Argv[])
{
    UCHAR Pattern[KDB_MEMORY_MAX_PATTERN];
    UCHAR Buffer[KDB_MEMORY_IO_CHUNK_SIZE + KDB_MEMORY_MAX_PATTERN - 1];
    ULONGLONG Length;
    ULONGLONG Value;
    ULONGLONG Remaining;
    ULONG_PTR Address;
    ULONG_PTR Current;
    ULONG PatternLength;
    ULONG Carry = 0;
    ULONG Index;
    ULONG ToRead;
    ULONG Available;
    ULONGLONG Matches = 0;

    if (Argc < 4 || Argc - 3 > KDB_MEMORY_MAX_PATTERN)
    {
        KdbpPrint("Usage: search address length byte [byte ...] (maximum %u bytes)\n", KDB_MEMORY_MAX_PATTERN);
        return TRUE;
    }

    if (!KdbpEvaluateAddress(Argv[1], KdbPromptStr.Length + (Argv[1] - Argv[0]), &Address) ||
        !KdbpEvaluateExpression(Argv[2], KdbPromptStr.Length + (Argv[2] - Argv[0]), &Length))
    {
        return TRUE;
    }

    if (Length == 0 || Length - 1 > (ULONGLONG)(MAXULONG_PTR - Address))
    {
        KdbpPrint("search: Invalid or wrapping range.\n");
        return TRUE;
    }

    PatternLength = Argc - 3;
    for (Index = 0; Index < PatternLength; Index++)
    {
        if (!KdbpEvaluateExpression(Argv[Index + 3], KdbPromptStr.Length + (Argv[Index + 3] - Argv[0]), &Value))
            return TRUE;
        if (Value > MAXUCHAR)
        {
            KdbpPrint("search: Pattern value 0x%I64x does not fit in a byte.\n", Value);
            return TRUE;
        }
        Pattern[Index] = (UCHAR)Value;
    }

    Current = Address;
    Remaining = Length;
    while (Remaining != 0)
    {
        ToRead = Remaining > KDB_MEMORY_IO_CHUNK_SIZE ? KDB_MEMORY_IO_CHUNK_SIZE : (ULONG)Remaining;
        if (!NT_SUCCESS(KdbpSafeReadMemory(Buffer + Carry, (PVOID)Current, ToRead)))
        {
            KdbpPrint("search: Couldn't read memory at %p.\n", (PVOID)Current);
            return TRUE;
        }

        Available = Carry + ToRead;
        if (Available >= PatternLength)
        {
            for (Index = 0; Index <= Available - PatternLength; Index++)
            {
                if (RtlCompareMemory(Buffer + Index, Pattern, PatternLength) == PatternLength)
                {
                    KdbpPrint("%p\n", (PVOID)(Current - Carry + Index));
                    Matches++;
                    if (KdbOutputAborted)
                        return TRUE;
                }
            }
        }

        Remaining -= ToRead;
        if (Remaining == 0)
            break;

        Carry = min(PatternLength - 1, Available);
        if (Carry != 0)
            RtlMoveMemory(Buffer, Buffer + Available - Carry, Carry);
        Current += ToRead;
    }

    KdbpPrint("%I64u match%s.\n", Matches, Matches == 1 ? "" : "es");
    return TRUE;
}

static BOOLEAN
KdbpValidateMemoryRange(IN PCSTR Command, IN ULONG_PTR Address, IN ULONGLONG Length)
{
    if (Length == 0 || Length - 1 > (ULONGLONG)(MAXULONG_PTR - Address))
    {
        KdbpPrint("%s: Invalid or wrapping range.\n", Command);
        return FALSE;
    }
    return TRUE;
}

static BOOLEAN
KdbpCmdCompareMemory(ULONG Argc, PCHAR Argv[])
{
    UCHAR First[KDB_MEMORY_IO_CHUNK_SIZE];
    UCHAR Second[KDB_MEMORY_IO_CHUNK_SIZE];
    ULONG_PTR FirstAddress;
    ULONG_PTR SecondAddress;
    ULONGLONG Length;
    ULONGLONG Offset = 0;
    ULONGLONG Differences = 0;
    ULONG Chunk;
    ULONG Index;
    NTSTATUS Status;

    if (Argc != 4)
    {
        KdbpPrint("Usage: compare address1 address2 length\n");
        return TRUE;
    }
    if (!KdbpEvaluateAddress(Argv[1], KdbPromptStr.Length + (Argv[1] - Argv[0]), &FirstAddress) ||
        !KdbpEvaluateAddress(Argv[2], KdbPromptStr.Length + (Argv[2] - Argv[0]), &SecondAddress) ||
        !KdbpEvaluateExpression(Argv[3], KdbPromptStr.Length + (Argv[3] - Argv[0]), &Length))
    {
        return TRUE;
    }
    if (!KdbpValidateMemoryRange(Argv[0], FirstAddress, Length) ||
        !KdbpValidateMemoryRange(Argv[0], SecondAddress, Length))
    {
        return TRUE;
    }

    while (Offset < Length)
    {
        Chunk = (ULONG)min((ULONGLONG)sizeof(First), Length - Offset);
        Status = KdbpSafeReadMemory(First, (PVOID)(FirstAddress + (ULONG_PTR)Offset), Chunk);
        if (!NT_SUCCESS(Status))
        {
            KdbpPrint("compare: Failed to read at %p (status 0x%08lx).\n", (PVOID)(FirstAddress + (ULONG_PTR)Offset), Status);
            return TRUE;
        }
        Status = KdbpSafeReadMemory(Second, (PVOID)(SecondAddress + (ULONG_PTR)Offset), Chunk);
        if (!NT_SUCCESS(Status))
        {
            KdbpPrint("compare: Failed to read at %p (status 0x%08lx).\n", (PVOID)(SecondAddress + (ULONG_PTR)Offset), Status);
            return TRUE;
        }

        for (Index = 0; Index < Chunk; Index++)
        {
            if (First[Index] != Second[Index])
            {
                KdbpPrint("%p %02x  %p %02x\n", (PVOID)(FirstAddress + (ULONG_PTR)Offset + Index), First[Index], (PVOID)(SecondAddress + (ULONG_PTR)Offset + Index), Second[Index]);
                Differences++;
                if (KdbOutputAborted)
                    return TRUE;
            }
        }
        Offset += Chunk;
    }

    KdbpPrint("%I64u difference%s.\n", Differences, Differences == 1 ? "" : "s");
    return TRUE;
}

static BOOLEAN
KdbpCmdFillMemory(ULONG Argc, PCHAR Argv[])
{
    UCHAR Pattern[KDB_MEMORY_MAX_PATTERN];
    UCHAR Buffer[KDB_MEMORY_IO_CHUNK_SIZE];
    ULONG_PTR Address;
    ULONGLONG Length;
    ULONGLONG Value;
    ULONGLONG Offset = 0;
    ULONG PatternLength;
    ULONG Index;
    ULONG Chunk;
    NTSTATUS Status;

    if (Argc < 4 || Argc - 3 > KDB_MEMORY_MAX_PATTERN)
    {
        KdbpPrint("Usage: fill address length byte [byte ...] (maximum %u bytes)\n", KDB_MEMORY_MAX_PATTERN);
        return TRUE;
    }
    if (!KdbpEvaluateAddress(Argv[1], KdbPromptStr.Length + (Argv[1] - Argv[0]), &Address) ||
        !KdbpEvaluateExpression(Argv[2], KdbPromptStr.Length + (Argv[2] - Argv[0]), &Length))
    {
        return TRUE;
    }
    if (!KdbpValidateMemoryRange(Argv[0], Address, Length))
        return TRUE;

    PatternLength = Argc - 3;
    for (Index = 0; Index < PatternLength; Index++)
    {
        if (!KdbpEvaluateExpression(Argv[Index + 3], KdbPromptStr.Length + (Argv[Index + 3] - Argv[0]), &Value))
        {
            return TRUE;
        }
        if (Value > MAXUCHAR)
        {
            KdbpPrint("fill: Pattern value 0x%I64x does not fit in a byte.\n", Value);
            return TRUE;
        }
        Pattern[Index] = (UCHAR)Value;
    }

    while (Offset < Length)
    {
        Chunk = (ULONG)min((ULONGLONG)sizeof(Buffer), Length - Offset);
        for (Index = 0; Index < Chunk; Index++)
            Buffer[Index] = Pattern[(ULONG)((Offset + Index) % PatternLength)];
        Status = KdbpSafeWriteMemory((PVOID)(Address + (ULONG_PTR)Offset), Buffer, Chunk);
        if (!NT_SUCCESS(Status))
        {
            KdbpPrint("fill: Failed to write at %p (status 0x%08lx).\n", (PVOID)(Address + (ULONG_PTR)Offset), Status);
            return TRUE;
        }
        Offset += Chunk;
    }
    KdbpPrint("Filled %I64u byte%s.\n", Length, Length == 1 ? "" : "s");
    return TRUE;
}

static BOOLEAN
KdbpCmdMoveMemory(ULONG Argc, PCHAR Argv[])
{
    UCHAR Buffer[KDB_MEMORY_IO_CHUNK_SIZE];
    ULONG_PTR Source;
    ULONG_PTR Destination;
    ULONGLONG Length;
    ULONGLONG Remaining;
    ULONGLONG Offset;
    ULONG Chunk;
    BOOLEAN Backwards;
    NTSTATUS Status;

    if (Argc != 4)
    {
        KdbpPrint("Usage: move source destination length\n");
        return TRUE;
    }
    if (!KdbpEvaluateAddress(Argv[1], KdbPromptStr.Length + (Argv[1] - Argv[0]), &Source) ||
        !KdbpEvaluateAddress(Argv[2], KdbPromptStr.Length + (Argv[2] - Argv[0]), &Destination) ||
        !KdbpEvaluateExpression(Argv[3], KdbPromptStr.Length + (Argv[3] - Argv[0]), &Length))
    {
        return TRUE;
    }
    if (!KdbpValidateMemoryRange(Argv[0], Source, Length) ||
        !KdbpValidateMemoryRange(Argv[0], Destination, Length))
    {
        return TRUE;
    }

    Backwards = (Destination > Source && Destination < Source + (ULONG_PTR)Length);
    Remaining = Length;
    while (Remaining != 0)
    {
        Chunk = (ULONG)min((ULONGLONG)sizeof(Buffer), Remaining);
        Offset = Backwards ? Remaining - Chunk : Length - Remaining;

        Status = KdbpSafeReadMemory(Buffer, (PVOID)(Source + (ULONG_PTR)Offset), Chunk);
        if (!NT_SUCCESS(Status))
        {
            KdbpPrint("move: Failed to read at %p (status 0x%08lx).\n", (PVOID)(Source + (ULONG_PTR)Offset), Status);
            return TRUE;
        }
        Status = KdbpSafeWriteMemory((PVOID)(Destination + (ULONG_PTR)Offset), Buffer, Chunk);
        if (!NT_SUCCESS(Status))
        {
            KdbpPrint("move: Failed to write at %p (status 0x%08lx).\n", (PVOID)(Destination + (ULONG_PTR)Offset), Status);
            return TRUE;
        }
        Remaining -= Chunk;
    }
    KdbpPrint("Moved %I64u byte%s.\n", Length, Length == 1 ? "" : "s");
    return TRUE;
}

static BOOLEAN
KdbpCmdSetRegister(ULONG Argc, PCHAR Argv[])
{
    PCHAR RegisterName;
    ULONGLONG OldValue;
    ULONGLONG NewValue;
    ULONG RegisterSize;
    ULONG Index;
    NTSTATUS Status;

    if (Argc < 3)
    {
        KdbpPrint("Usage: setreg register expression\n");
        return TRUE;
    }

    KdbpDiscardStaleContextRecord();
    if (KdbContextRecordActive)
    {
        KdbpPrint("setreg: .cxr is inspection-only; reset it before editing live registers.\n");
        return TRUE;
    }

    if (KdbCurrentThread != KdbOriginalThread)
    {
        KdbpPrint("setreg: registers of an attached, switched-out thread cannot be changed safely.\n");
        return TRUE;
    }

    RegisterName = Argv[1];
    if (RegisterName[0] == '$')
        RegisterName++;

    Status = KdbpGetRegisterValue(KdbCurrentTrapFrame, RegisterName, &OldValue, &RegisterSize);
    if (!NT_SUCCESS(Status))
    {
        KdbpPrint("setreg: Unknown register '%s'.\n", RegisterName);
        return TRUE;
    }

    for (Index = 2; Index + 1 < Argc; Index++)
        Argv[Index][strlen(Argv[Index])] = ' ';

    if (!KdbpEvaluateExpression(Argv[2], KdbPromptStr.Length + (Argv[2] - Argv[0]), &NewValue))
    {
        return TRUE;
    }

    Status = KdbpSetRegisterValue(KdbCurrentTrapFrame, RegisterName, NewValue);
    if (Status == STATUS_INTEGER_OVERFLOW)
    {
        KdbpPrint("setreg: Value 0x%I64x does not fit in %lu bits.\n", NewValue, RegisterSize * 8);
        return TRUE;
    }
    if (!NT_SUCCESS(Status))
    {
        KdbpPrint("setreg: Failed with status 0x%08lx.\n", Status);
        return TRUE;
    }

    KdbpPrint("%s: 0x%I64x -> 0x%I64x\n", RegisterName, OldValue, NewValue);
    return TRUE;
}

static BOOLEAN
KdbpGetFpRegisterStorage(IN PCONTEXT Context, IN PCSTR RegisterName, OUT PVOID *Storage, OUT PULONG Size)
{
    PCHAR End;
    ULONG Index;

    if (*RegisterName == '$')
        RegisterName++;

#if defined(_M_ARM64)
    if (strchr("vqdshb", tolower(RegisterName[0])) != NULL)
    {
        Index = strtoul(RegisterName + 1, &End, 10);
        if (End != RegisterName + 1 && *End == ANSI_NULL && Index < RTL_NUMBER_OF(Context->V))
        {
            *Storage = &Context->V[Index];
            switch (tolower(RegisterName[0]))
            {
                case 'b': *Size = sizeof(UCHAR); break;
                case 'h': *Size = sizeof(USHORT); break;
                case 's': *Size = sizeof(ULONG); break;
                case 'd': *Size = sizeof(ULONG64); break;
                default:  *Size = sizeof(Context->V[Index]); break;
            }
            return TRUE;
        }
    }
    if (_stricmp(RegisterName, "fpcr") == 0)
    {
        *Storage = &Context->Fpcr;
        *Size = sizeof(Context->Fpcr);
        return TRUE;
    }
    if (_stricmp(RegisterName, "fpsr") == 0)
    {
        *Storage = &Context->Fpsr;
        *Size = sizeof(Context->Fpsr);
        return TRUE;
    }
#elif defined(_M_AMD64)
    if (_strnicmp(RegisterName, "xmm", 3) == 0)
    {
        Index = strtoul(RegisterName + 3, &End, 10);
        if (End != RegisterName + 3 && *End == ANSI_NULL &&
            Index < RTL_NUMBER_OF(Context->FltSave.XmmRegisters))
        {
            *Storage = &Context->FltSave.XmmRegisters[Index];
            *Size = sizeof(Context->FltSave.XmmRegisters[Index]);
            return TRUE;
        }
    }
    if (_strnicmp(RegisterName, "st", 2) == 0)
    {
        Index = strtoul(RegisterName + 2, &End, 10);
        if (End != RegisterName + 2 && *End == ANSI_NULL &&
            Index < RTL_NUMBER_OF(Context->FltSave.FloatRegisters))
        {
            *Storage = &Context->FltSave.FloatRegisters[Index];
            *Size = 10; /* x87 80-bit extended precision */
            return TRUE;
        }
    }

#define KDB_FP_FIELD(Name, Field)                                      \
    if (_stricmp(RegisterName, Name) == 0)                             \
    {                                                                  \
        *Storage = &Context->FltSave.Field;                             \
        *Size = sizeof(Context->FltSave.Field);                         \
        return TRUE;                                                    \
    }
    KDB_FP_FIELD("fctrl", ControlWord);
    KDB_FP_FIELD("fstat", StatusWord);
    KDB_FP_FIELD("ftag", TagWord);
    KDB_FP_FIELD("fop", ErrorOpcode);
    KDB_FP_FIELD("fioff", ErrorOffset);
    KDB_FP_FIELD("fiseg", ErrorSelector);
    KDB_FP_FIELD("fooff", DataOffset);
    KDB_FP_FIELD("foseg", DataSelector);
#undef KDB_FP_FIELD

    if (_stricmp(RegisterName, "mxcsr") == 0)
    {
        *Storage = &Context->MxCsr;
        *Size = sizeof(Context->MxCsr);
        return TRUE;
    }
#else
    UNREFERENCED_PARAMETER(Context);
#endif

    return FALSE;
}

static VOID
KdbpPrintFpRegister(IN PCSTR Name, IN const VOID *Storage, IN ULONG Size)
{
    const UCHAR *Bytes = Storage;

    KdbpPrint("%-6s = 0x", Name);
    while (Size != 0)
        KdbpPrint("%02x", Bytes[--Size]);
    KdbpPrint("\n");
}

static BOOLEAN
KdbpCmdFpRegs(ULONG Argc, PCHAR Argv[])
{
    PCONTEXT Context = KdbCurrentTrapFrame;
    CHAR Name[8];
    PVOID Storage;
    ULONG Size;
    ULONG Index;

    if (Argc > 2)
    {
        KdbpPrint("Usage: %s [register]\n", Argv[0]);
        return TRUE;
    }
    if ((Context->ContextFlags & CONTEXT_FLOATING_POINT) != CONTEXT_FLOATING_POINT)
    {
        KdbpPrint("%s: Floating-point state is unavailable in this context.\n", Argv[0]);
        return TRUE;
    }

    if (Argc == 2)
    {
        if (!KdbpGetFpRegisterStorage(Context, Argv[1], &Storage, &Size))
            KdbpPrint("%s: Unknown floating-point register '%s'.\n", Argv[0], Argv[1]);
        else
            KdbpPrintFpRegister(Argv[1], Storage, Size);
        return TRUE;
    }

#if defined(_M_ARM64)
    for (Index = 0; Index < RTL_NUMBER_OF(Context->V); Index++)
    {
        sprintf(Name, "v%lu", Index);
        KdbpPrintFpRegister(Name, &Context->V[Index], sizeof(Context->V[Index]));
        if (KdbOutputAborted)
            return TRUE;
    }
    KdbpPrintFpRegister("fpsr", &Context->Fpsr, sizeof(Context->Fpsr));
    KdbpPrintFpRegister("fpcr", &Context->Fpcr, sizeof(Context->Fpcr));
#elif defined(_M_AMD64)
    for (Index = 0; Index < RTL_NUMBER_OF(Context->FltSave.FloatRegisters); Index++)
    {
        sprintf(Name, "st%lu", Index);
        KdbpPrintFpRegister(Name, &Context->FltSave.FloatRegisters[Index], 10);
    }
    for (Index = 0; Index < RTL_NUMBER_OF(Context->FltSave.XmmRegisters); Index++)
    {
        sprintf(Name, "xmm%lu", Index);
        KdbpPrintFpRegister(Name, &Context->FltSave.XmmRegisters[Index], sizeof(Context->FltSave.XmmRegisters[Index]));
        if (KdbOutputAborted)
            return TRUE;
    }
    KdbpPrintFpRegister("fctrl", &Context->FltSave.ControlWord, sizeof(Context->FltSave.ControlWord));
    KdbpPrintFpRegister("fstat", &Context->FltSave.StatusWord, sizeof(Context->FltSave.StatusWord));
    KdbpPrintFpRegister("ftag", &Context->FltSave.TagWord, sizeof(Context->FltSave.TagWord));
    KdbpPrintFpRegister("fop", &Context->FltSave.ErrorOpcode, sizeof(Context->FltSave.ErrorOpcode));
    KdbpPrintFpRegister("fioff", &Context->FltSave.ErrorOffset, sizeof(Context->FltSave.ErrorOffset));
    KdbpPrintFpRegister("fiseg", &Context->FltSave.ErrorSelector, sizeof(Context->FltSave.ErrorSelector));
    KdbpPrintFpRegister("fooff", &Context->FltSave.DataOffset, sizeof(Context->FltSave.DataOffset));
    KdbpPrintFpRegister("foseg", &Context->FltSave.DataSelector, sizeof(Context->FltSave.DataSelector));
    KdbpPrintFpRegister("mxcsr", &Context->MxCsr, sizeof(Context->MxCsr));
#else
    KdbpPrint("%s: Floating-point registers are not supported on this architecture.\n", Argv[0]);
#endif
    return TRUE;
}

static LONG
KdbpHexDigitValue(IN CHAR Character)
{
    if (Character >= '0' && Character <= '9')
        return Character - '0';
    Character = (CHAR)tolower(Character);
    if (Character >= 'a' && Character <= 'f')
        return Character - 'a' + 10;
    return -1;
}

static BOOLEAN
KdbpParseRegisterHexValue(IN PCSTR Text, OUT PUCHAR Value, IN ULONG Size)
{
    SIZE_T Length;
    SIZE_T Position;
    ULONG Digits = 0;
    ULONG Nibble = 0;
    LONG Digit;

    if (Text[0] == '0' && tolower(Text[1]) == 'x')
        Text += 2;
    Length = strlen(Text);
    if (Length == 0)
        return FALSE;

    for (Position = 0; Position < Length; Position++)
    {
        if (Text[Position] == '`' || Text[Position] == '_')
            continue;
        if (KdbpHexDigitValue(Text[Position]) < 0 || ++Digits > Size * 2)
            return FALSE;
    }
    if (Digits == 0)
        return FALSE;

    RtlZeroMemory(Value, Size);
    Position = Length;
    while (Position != 0)
    {
        CHAR Character = Text[--Position];

        if (Character == '`' || Character == '_')
            continue;
        Digit = KdbpHexDigitValue(Character);
        ASSERT(Digit >= 0);
        Value[Nibble / 2] |= (UCHAR)(Digit << ((Nibble & 1) * 4));
        Nibble++;
    }
    return TRUE;
}

static BOOLEAN
KdbpCmdSetFpRegister(ULONG Argc, PCHAR Argv[])
{
    UCHAR OldValue[16];
    UCHAR NewValue[16];
    PVOID Storage;
    ULONG Size;

    if (Argc != 3)
    {
        KdbpPrint("Usage: %s register hexvalue\n", Argv[0]);
        return TRUE;
    }

    KdbpDiscardStaleContextRecord();
    if (KdbContextRecordActive)
    {
        KdbpPrint("%s: .cxr is inspection-only; reset it before editing live registers.\n", Argv[0]);
        return TRUE;
    }
    if (KdbCurrentThread != KdbOriginalThread)
    {
        KdbpPrint("%s: Registers of an attached, switched-out thread cannot be changed safely.\n", Argv[0]);
        return TRUE;
    }
    if ((KdbCurrentTrapFrame->ContextFlags & CONTEXT_FLOATING_POINT) != CONTEXT_FLOATING_POINT)
    {
        KdbpPrint("%s: Floating-point state is unavailable in this context.\n", Argv[0]);
        return TRUE;
    }
    if (!KdbpGetFpRegisterStorage(KdbCurrentTrapFrame, Argv[1], &Storage, &Size))
    {
        KdbpPrint("%s: Unknown floating-point register '%s'.\n", Argv[0], Argv[1]);
        return TRUE;
    }
    ASSERT(Size <= sizeof(NewValue));
    if (!KdbpParseRegisterHexValue(Argv[2], NewValue, Size))
    {
        KdbpPrint("%s: Value must contain at most %lu hexadecimal digits.\n", Argv[0], Size * 2);
        return TRUE;
    }

    RtlCopyMemory(OldValue, Storage, Size);
    RtlCopyMemory(Storage, NewValue, Size);
#if defined(_M_AMD64)
    if (_stricmp(Argv[1], "mxcsr") == 0 || _stricmp(Argv[1], "$mxcsr") == 0)
        KdbCurrentTrapFrame->FltSave.MxCsr = KdbCurrentTrapFrame->MxCsr;
#endif
    KdbpPrintFpRegister(Argv[1], OldValue, Size);
    KdbpPrintFpRegister(Argv[1], NewValue, Size);
    return TRUE;
}

#ifdef __ROS_DWARF__

/*!\brief Print a struct
 */
static VOID
KdbpPrintStructInternal
(PROSSYM_INFO Info, PCHAR Indent, BOOLEAN DoRead, PVOID BaseAddress, PROSSYM_AGGREGATE Aggregate)
{
    ULONG i;
    ULONGLONG Result;
    PROSSYM_AGGREGATE_MEMBER Member;
    ULONG IndentLen = strlen(Indent);
    ROSSYM_AGGREGATE MemberAggregate = {0 };

    for (i = 0; i < Aggregate->NumElements; i++) {
        Member = &Aggregate->Elements[i];
        KdbpPrint("%s%p+%x: %s", Indent, ((PCHAR)BaseAddress) + Member->BaseOffset, Member->Size, Member->Name ? Member->Name : "<anoymous>");
        if (DoRead) {
            if (!strcmp(Member->Type, "_UNICODE_STRING")) {
                KdbpPrint("\"");
                KdbpPrintUnicodeString(((PCHAR)BaseAddress) + Member->BaseOffset);
                KdbpPrint("\"\n");
                continue;
            } else if (!strcmp(Member->Type, "PUNICODE_STRING")) {
                PUNICODE_STRING String;

                KdbpPrint("\"");
                if (NT_SUCCESS(KdbpSafeReadMemory(&String, ((PCHAR)BaseAddress) + Member->BaseOffset, sizeof(String))))
                    KdbpPrintUnicodeString(String);
                else
                    KdbpPrint("<unreadable>");
                KdbpPrint("\"\n");
                continue;
            }
            switch (Member->Size) {
            case 1:
            case 2:
            case 4:
            case 8: {
                Result = 0;
                if (NT_SUCCESS(KdbpSafeReadMemory(&Result, ((PCHAR)BaseAddress) + Member->BaseOffset, Member->Size))) {
                    if (Member->Bits) {
                        if (Member->FirstBit >= sizeof(Result) * 8)
                            Result = 0;
                        else
                            Result >>= Member->FirstBit;
                        if (Member->Bits < sizeof(Result) * 8)
                            Result &= ((1ULL << Member->Bits) - 1);
                    }
                    KdbpPrint(" %I64x\n", Result);
                }
                else goto readfail;
                break;
            }
            default: {
                if (Member->Size < 8) {
                    if (NT_SUCCESS(KdbpSafeReadMemory(&Result, ((PCHAR)BaseAddress) + Member->BaseOffset, Member->Size))) {
                        ULONG j;
                        for (j = 0; j < Member->Size; j++) {
                            KdbpPrint(" %02x", (int)(Result & 0xff));
                            Result >>= 8;
                        }
                    } else goto readfail;
                } else {
                    KdbpPrint(" %s @ %p {\n", Member->Type, ((PCHAR)BaseAddress) + Member->BaseOffset);
                    Indent[IndentLen] = ' ';
                    if (RosSymAggregate(Info, Member->Type, &MemberAggregate)) {
                        KdbpPrintStructInternal(Info, Indent, DoRead, ((PCHAR)BaseAddress) + Member->BaseOffset, &MemberAggregate);
                        RosSymFreeAggregate(&MemberAggregate);
                    }
                    Indent[IndentLen] = 0;
                    KdbpPrint("%s}\n", Indent);
                } break;
            }
            }
        } else {
        readfail:
            if (Member->Size <= 8) {
                KdbpPrint(" ??\n");
            } else {
                KdbpPrint(" %s @ %x {\n", Member->Type, Member->BaseOffset);
                Indent[IndentLen] = ' ';
                if (RosSymAggregate(Info, Member->Type, &MemberAggregate)) {
                    KdbpPrintStructInternal(Info, Indent, DoRead, BaseAddress, &MemberAggregate);
                    RosSymFreeAggregate(&MemberAggregate);
                }
                Indent[IndentLen] = 0;
                KdbpPrint("%s}\n", Indent);
            }
        }
    }
}

PROSSYM_INFO KdbpSymFindCachedFile(PUNICODE_STRING ModName);

static BOOLEAN
KdbpCmdPrintStruct(ULONG Argc, PCHAR Argv[])
{
    ULONG i;
    ULONGLONG Result = 0;
    PVOID BaseAddress = NULL;
    ROSSYM_AGGREGATE Aggregate = {0};
    UNICODE_STRING ModName = {0};
    ANSI_STRING AnsiName = {0};
    CHAR Indent[100] = {0};
    PROSSYM_INFO Info;

    if (Argc < 3) goto end;
    AnsiName.Length = AnsiName.MaximumLength = strlen(Argv[1]);
    AnsiName.Buffer = Argv[1];
    RtlAnsiStringToUnicodeString(&ModName, &AnsiName, TRUE);
    Info = KdbpSymFindCachedFile(&ModName);

    if (!Info || !RosSymAggregate(Info, Argv[2], &Aggregate)) {
        DPRINT1("Could not get aggregate\n");
        goto end;
    }

    // Get an argument for location if it was given
    if (Argc > 3) {
        ULONG len;
        PCHAR ArgStart = Argv[3];
        DPRINT("Trying to get expression\n");
        for (i = 3; i < Argc - 1; i++)
        {
            len = strlen(Argv[i]);
            Argv[i][len] = ' ';
        }

        /* Evaluate the expression */
        DPRINT("Arg: %s\n", ArgStart);
        if (KdbpEvaluateExpression(ArgStart, strlen(ArgStart), &Result))
        {
            if (Result > (ULONGLONG)MAXULONG_PTR)
            {
                KdbpPrint("Address 0x%I64x does not fit in a pointer.\n", Result);
                goto end;
            }
            else
                BaseAddress = (PVOID)(ULONG_PTR)Result;
        }
    }
    DPRINT("BaseAddress: %p\n", BaseAddress);
    KdbpPrintStructInternal(Info, Indent, !!BaseAddress, BaseAddress, &Aggregate);
end:
    RosSymFreeAggregate(&Aggregate);
    RtlFreeUnicodeString(&ModName);
    return TRUE;
}
#else /* !__ROS_DWARF__ */

typedef enum _KDB_FIELD_KIND
{
    KdbFieldHex,
    KdbFieldSigned,
    KdbFieldPointer,
    KdbFieldSymbol,
    KdbFieldUnicode,
    KdbFieldAnsi
} KDB_FIELD_KIND;

typedef struct _KDB_TYPE_FIELD
{
    PCSTR Name;
    PCSTR TypeName;
    ULONG Offset;
    ULONG Size;
    KDB_FIELD_KIND Kind;
} KDB_TYPE_FIELD, *PKDB_TYPE_FIELD;

typedef struct _KDB_BUILTIN_TYPE
{
    PCSTR Name;
    ULONG Size;
    PKDB_TYPE_FIELD Fields;
    ULONG FieldCount;
} KDB_BUILTIN_TYPE, *PKDB_BUILTIN_TYPE;

#define KDB_FIELD(Struct, Field, Type, Kind) \
    { #Field, Type, FIELD_OFFSET(Struct, Field), RTL_FIELD_SIZE(Struct, Field), Kind }
#define KDB_TYPE(Struct, Fields) \
    { #Struct, sizeof(Struct), Fields, RTL_NUMBER_OF(Fields) }

static KDB_TYPE_FIELD KdbFieldsListEntry[] =
{
    KDB_FIELD(LIST_ENTRY, Flink, "pointer", KdbFieldPointer),
    KDB_FIELD(LIST_ENTRY, Blink, "pointer", KdbFieldPointer)
};

static KDB_TYPE_FIELD KdbFieldsUnicodeString[] =
{
    KDB_FIELD(UNICODE_STRING, Length, "USHORT", KdbFieldHex),
    KDB_FIELD(UNICODE_STRING, MaximumLength, "USHORT", KdbFieldHex),
    KDB_FIELD(UNICODE_STRING, Buffer, "PWSTR", KdbFieldPointer)
};

static KDB_TYPE_FIELD KdbFieldsClientId[] =
{
    KDB_FIELD(CLIENT_ID, UniqueProcess, "HANDLE", KdbFieldPointer),
    KDB_FIELD(CLIENT_ID, UniqueThread, "HANDLE", KdbFieldPointer)
};

static KDB_TYPE_FIELD KdbFieldsEprocess[] =
{
    KDB_FIELD(EPROCESS, Pcb, "_KPROCESS", KdbFieldHex),
    KDB_FIELD(EPROCESS, CreateTime, "LARGE_INTEGER", KdbFieldHex),
    KDB_FIELD(EPROCESS, ExitTime, "LARGE_INTEGER", KdbFieldHex),
    KDB_FIELD(EPROCESS, UniqueProcessId, "HANDLE", KdbFieldPointer),
    KDB_FIELD(EPROCESS, ActiveProcessLinks, "_LIST_ENTRY", KdbFieldHex),
    KDB_FIELD(EPROCESS, ObjectTable, "_HANDLE_TABLE*", KdbFieldPointer),
    KDB_FIELD(EPROCESS, Token, "_EX_FAST_REF", KdbFieldHex),
    KDB_FIELD(EPROCESS, SectionObject, "pointer", KdbFieldPointer),
    KDB_FIELD(EPROCESS, SectionBaseAddress, "pointer", KdbFieldSymbol),
    KDB_FIELD(EPROCESS, InheritedFromUniqueProcessId, "HANDLE", KdbFieldPointer),
    KDB_FIELD(EPROCESS, Session, "pointer", KdbFieldPointer),
    KDB_FIELD(EPROCESS, ImageFileName, "CHAR[16]", KdbFieldAnsi),
    KDB_FIELD(EPROCESS, ThreadListHead, "_LIST_ENTRY", KdbFieldHex),
    KDB_FIELD(EPROCESS, ActiveThreads, "ULONG", KdbFieldHex),
    KDB_FIELD(EPROCESS, Peb, "_PEB*", KdbFieldPointer),
    KDB_FIELD(EPROCESS, VirtualSize, "SIZE_T", KdbFieldHex),
    KDB_FIELD(EPROCESS, PeakVirtualSize, "SIZE_T", KdbFieldHex),
    KDB_FIELD(EPROCESS, CommitCharge, "SIZE_T", KdbFieldHex),
    KDB_FIELD(EPROCESS, NumberOfPrivatePages, "PFN_NUMBER", KdbFieldHex),
    KDB_FIELD(EPROCESS, Flags2, "ULONG", KdbFieldHex),
    KDB_FIELD(EPROCESS, Flags, "ULONG", KdbFieldHex),
    KDB_FIELD(EPROCESS, ExitStatus, "NTSTATUS", KdbFieldHex)
};

static KDB_TYPE_FIELD KdbFieldsEthread[] =
{
    KDB_FIELD(ETHREAD, Tcb, "_KTHREAD", KdbFieldHex),
    KDB_FIELD(ETHREAD, CreateTime, "LARGE_INTEGER", KdbFieldHex),
    KDB_FIELD(ETHREAD, ExitTime, "LARGE_INTEGER", KdbFieldHex),
    KDB_FIELD(ETHREAD, ExitStatus, "NTSTATUS", KdbFieldHex),
    KDB_FIELD(ETHREAD, Cid, "_CLIENT_ID", KdbFieldHex),
    KDB_FIELD(ETHREAD, IrpList, "_LIST_ENTRY", KdbFieldHex),
    KDB_FIELD(ETHREAD, Win32StartAddress, "pointer", KdbFieldSymbol),
    KDB_FIELD(ETHREAD, StartAddress, "pointer", KdbFieldSymbol),
    KDB_FIELD(ETHREAD, ThreadListEntry, "_LIST_ENTRY", KdbFieldHex),
    KDB_FIELD(ETHREAD, CrossThreadFlags, "ULONG", KdbFieldHex),
    KDB_FIELD(ETHREAD, SameThreadPassiveFlags, "ULONG", KdbFieldHex),
    KDB_FIELD(ETHREAD, SameThreadApcFlags, "ULONG", KdbFieldHex),
    KDB_FIELD(ETHREAD, ThreadName, "_UNICODE_STRING*", KdbFieldPointer)
};

static KDB_TYPE_FIELD KdbFieldsKthread[] =
{
    KDB_FIELD(KTHREAD, InitialStack, "pointer", KdbFieldPointer),
    KDB_FIELD(KTHREAD, StackLimit, "ULONG_PTR", KdbFieldPointer),
    KDB_FIELD(KTHREAD, StackBase, "pointer", KdbFieldPointer),
    KDB_FIELD(KTHREAD, KernelStack, "pointer", KdbFieldPointer),
    KDB_FIELD(KTHREAD, CycleTime, "ULONG64", KdbFieldHex),
    KDB_FIELD(KTHREAD, TrapFrame, "_KTRAP_FRAME*", KdbFieldPointer),
    KDB_FIELD(KTHREAD, Priority, "CHAR", KdbFieldSigned),
    KDB_FIELD(KTHREAD, WaitStatus, "LONG_PTR", KdbFieldHex),
    KDB_FIELD(KTHREAD, WaitBlockList, "_KWAIT_BLOCK*", KdbFieldPointer),
    KDB_FIELD(KTHREAD, Teb, "_TEB*", KdbFieldPointer),
    KDB_FIELD(KTHREAD, State, "UCHAR", KdbFieldHex),
#if (NTDDI_VERSION >= NTDDI_WIN8) || defined(_M_ARM64)
    KDB_FIELD(KTHREAD, QueuePriority, "LONG", KdbFieldSigned),
#endif
    KDB_FIELD(KTHREAD, Process, "_KPROCESS*", KdbFieldPointer),
    KDB_FIELD(KTHREAD, UserAffinity, "KAFFINITY", KdbFieldHex),
    KDB_FIELD(KTHREAD, BasePriority, "CHAR", KdbFieldSigned),
    KDB_FIELD(KTHREAD, Affinity, "KAFFINITY", KdbFieldHex),
#if (NTDDI_VERSION >= NTDDI_WIN8) || defined(_M_ARM64)
    KDB_FIELD(KTHREAD, WaitBlockCount, "UCHAR", KdbFieldHex),
#endif
    KDB_FIELD(KTHREAD, WaitReason, "UCHAR", KdbFieldHex),
    KDB_FIELD(KTHREAD, SuspendCount, "CHAR", KdbFieldSigned),
    KDB_FIELD(KTHREAD, KernelTime, "ULONG", KdbFieldHex),
    KDB_FIELD(KTHREAD, UserTime, "ULONG", KdbFieldHex)
};

static KDB_TYPE_FIELD KdbFieldsPeb[] =
{
    KDB_FIELD(PEB, BeingDebugged, "BOOLEAN", KdbFieldHex),
    KDB_FIELD(PEB, ImageBaseAddress, "pointer", KdbFieldSymbol),
    KDB_FIELD(PEB, Ldr, "_PEB_LDR_DATA*", KdbFieldPointer),
    KDB_FIELD(PEB, ProcessParameters, "_RTL_USER_PROCESS_PARAMETERS*", KdbFieldPointer),
    KDB_FIELD(PEB, ProcessHeap, "pointer", KdbFieldPointer),
    KDB_FIELD(PEB, NumberOfProcessors, "ULONG", KdbFieldHex),
    KDB_FIELD(PEB, NtGlobalFlag, "ULONG", KdbFieldHex),
    KDB_FIELD(PEB, NumberOfHeaps, "ULONG", KdbFieldHex),
    KDB_FIELD(PEB, MaximumNumberOfHeaps, "ULONG", KdbFieldHex),
    KDB_FIELD(PEB, OSMajorVersion, "ULONG", KdbFieldHex),
    KDB_FIELD(PEB, OSMinorVersion, "ULONG", KdbFieldHex),
    KDB_FIELD(PEB, OSBuildNumber, "USHORT", KdbFieldHex),
    KDB_FIELD(PEB, ImageSubsystem, "ULONG", KdbFieldHex),
    KDB_FIELD(PEB, SessionId, "ULONG", KdbFieldHex)
};

static KDB_TYPE_FIELD KdbFieldsTeb[] =
{
    KDB_FIELD(TEB, NtTib, "_NT_TIB", KdbFieldHex),
    KDB_FIELD(TEB, ClientId, "_CLIENT_ID", KdbFieldHex),
    KDB_FIELD(TEB, ProcessEnvironmentBlock, "_PEB*", KdbFieldPointer),
    KDB_FIELD(TEB, LastErrorValue, "ULONG", KdbFieldHex),
    KDB_FIELD(TEB, CountOfOwnedCriticalSections, "ULONG", KdbFieldHex),
    KDB_FIELD(TEB, CsrClientThread, "pointer", KdbFieldPointer),
    KDB_FIELD(TEB, Win32ThreadInfo, "pointer", KdbFieldPointer),
    KDB_FIELD(TEB, CurrentLocale, "LCID", KdbFieldHex),
    KDB_FIELD(TEB, LastStatusValue, "ULONG", KdbFieldHex),
    KDB_FIELD(TEB, DeallocationStack, "pointer", KdbFieldPointer),
    KDB_FIELD(TEB, GuaranteedStackBytes, "ULONG", KdbFieldHex),
    KDB_FIELD(TEB, WaitingOnLoaderLock, "ULONG", KdbFieldHex)
};

static KDB_TYPE_FIELD KdbFieldsLdrEntry[] =
{
    KDB_FIELD(LDR_DATA_TABLE_ENTRY, InLoadOrderLinks, "_LIST_ENTRY", KdbFieldHex),
    KDB_FIELD(LDR_DATA_TABLE_ENTRY, DllBase, "pointer", KdbFieldSymbol),
    KDB_FIELD(LDR_DATA_TABLE_ENTRY, EntryPoint, "pointer", KdbFieldSymbol),
    KDB_FIELD(LDR_DATA_TABLE_ENTRY, SizeOfImage, "ULONG", KdbFieldHex),
    KDB_FIELD(LDR_DATA_TABLE_ENTRY, FullDllName, "_UNICODE_STRING", KdbFieldUnicode),
    KDB_FIELD(LDR_DATA_TABLE_ENTRY, BaseDllName, "_UNICODE_STRING", KdbFieldUnicode),
    KDB_FIELD(LDR_DATA_TABLE_ENTRY, Flags, "ULONG", KdbFieldHex),
    KDB_FIELD(LDR_DATA_TABLE_ENTRY, LoadCount, "USHORT", KdbFieldHex),
    KDB_FIELD(LDR_DATA_TABLE_ENTRY, PatchInformation, "pointer", KdbFieldPointer)
};

static KDB_TYPE_FIELD KdbFieldsDriverObject[] =
{
    KDB_FIELD(DRIVER_OBJECT, Type, "CSHORT", KdbFieldSigned),
    KDB_FIELD(DRIVER_OBJECT, Size, "CSHORT", KdbFieldSigned),
    KDB_FIELD(DRIVER_OBJECT, DeviceObject, "_DEVICE_OBJECT*", KdbFieldPointer),
    KDB_FIELD(DRIVER_OBJECT, Flags, "ULONG", KdbFieldHex),
    KDB_FIELD(DRIVER_OBJECT, DriverStart, "pointer", KdbFieldSymbol),
    KDB_FIELD(DRIVER_OBJECT, DriverSize, "ULONG", KdbFieldHex),
    KDB_FIELD(DRIVER_OBJECT, DriverSection, "pointer", KdbFieldPointer),
    KDB_FIELD(DRIVER_OBJECT, DriverExtension, "_DRIVER_EXTENSION*", KdbFieldPointer),
    KDB_FIELD(DRIVER_OBJECT, DriverName, "_UNICODE_STRING", KdbFieldUnicode),
    KDB_FIELD(DRIVER_OBJECT, FastIoDispatch, "_FAST_IO_DISPATCH*", KdbFieldPointer),
    KDB_FIELD(DRIVER_OBJECT, DriverInit, "pointer", KdbFieldSymbol),
    KDB_FIELD(DRIVER_OBJECT, DriverStartIo, "pointer", KdbFieldSymbol),
    KDB_FIELD(DRIVER_OBJECT, DriverUnload, "pointer", KdbFieldSymbol)
};

static KDB_TYPE_FIELD KdbFieldsDeviceObject[] =
{
    KDB_FIELD(DEVICE_OBJECT, Type, "CSHORT", KdbFieldSigned),
    KDB_FIELD(DEVICE_OBJECT, Size, "USHORT", KdbFieldHex),
    KDB_FIELD(DEVICE_OBJECT, ReferenceCount, "LONG", KdbFieldSigned),
    KDB_FIELD(DEVICE_OBJECT, DriverObject, "_DRIVER_OBJECT*", KdbFieldPointer),
    KDB_FIELD(DEVICE_OBJECT, NextDevice, "_DEVICE_OBJECT*", KdbFieldPointer),
    KDB_FIELD(DEVICE_OBJECT, AttachedDevice, "_DEVICE_OBJECT*", KdbFieldPointer),
    KDB_FIELD(DEVICE_OBJECT, CurrentIrp, "_IRP*", KdbFieldPointer),
    KDB_FIELD(DEVICE_OBJECT, Flags, "ULONG", KdbFieldHex),
    KDB_FIELD(DEVICE_OBJECT, Characteristics, "ULONG", KdbFieldHex),
    KDB_FIELD(DEVICE_OBJECT, Vpb, "_VPB*", KdbFieldPointer),
    KDB_FIELD(DEVICE_OBJECT, DeviceExtension, "pointer", KdbFieldPointer),
    KDB_FIELD(DEVICE_OBJECT, DeviceType, "DEVICE_TYPE", KdbFieldHex),
    KDB_FIELD(DEVICE_OBJECT, StackSize, "CCHAR", KdbFieldSigned),
    KDB_FIELD(DEVICE_OBJECT, AlignmentRequirement, "ULONG", KdbFieldHex),
    KDB_FIELD(DEVICE_OBJECT, ActiveThreadCount, "ULONG", KdbFieldHex),
    KDB_FIELD(DEVICE_OBJECT, SectorSize, "USHORT", KdbFieldHex),
    KDB_FIELD(DEVICE_OBJECT, DeviceObjectExtension, "_DEVOBJ_EXTENSION*", KdbFieldPointer)
};

static KDB_TYPE_FIELD KdbFieldsIrp[] =
{
    KDB_FIELD(IRP, Type, "CSHORT", KdbFieldSigned),
    KDB_FIELD(IRP, Size, "USHORT", KdbFieldHex),
    KDB_FIELD(IRP, MdlAddress, "_MDL*", KdbFieldPointer),
    KDB_FIELD(IRP, Flags, "ULONG", KdbFieldHex),
    KDB_FIELD(IRP, AssociatedIrp.SystemBuffer, "pointer", KdbFieldPointer),
    KDB_FIELD(IRP, IoStatus, "_IO_STATUS_BLOCK", KdbFieldHex),
    KDB_FIELD(IRP, RequestorMode, "KPROCESSOR_MODE", KdbFieldHex),
    KDB_FIELD(IRP, PendingReturned, "BOOLEAN", KdbFieldHex),
    KDB_FIELD(IRP, StackCount, "CHAR", KdbFieldSigned),
    KDB_FIELD(IRP, CurrentLocation, "CHAR", KdbFieldSigned),
    KDB_FIELD(IRP, Cancel, "BOOLEAN", KdbFieldHex),
    KDB_FIELD(IRP, CancelRoutine, "pointer", KdbFieldSymbol),
    KDB_FIELD(IRP, UserBuffer, "pointer", KdbFieldPointer),
    KDB_FIELD(IRP, Tail.Overlay.Thread, "_ETHREAD*", KdbFieldPointer),
    KDB_FIELD(IRP, Tail.Overlay.CurrentStackLocation, "_IO_STACK_LOCATION*", KdbFieldPointer),
    KDB_FIELD(IRP, Tail.Overlay.OriginalFileObject, "_FILE_OBJECT*", KdbFieldPointer)
};

static KDB_TYPE_FIELD KdbFieldsIoStack[] =
{
    KDB_FIELD(IO_STACK_LOCATION, MajorFunction, "UCHAR", KdbFieldHex),
    KDB_FIELD(IO_STACK_LOCATION, MinorFunction, "UCHAR", KdbFieldHex),
    KDB_FIELD(IO_STACK_LOCATION, Flags, "UCHAR", KdbFieldHex),
    KDB_FIELD(IO_STACK_LOCATION, Control, "UCHAR", KdbFieldHex),
    KDB_FIELD(IO_STACK_LOCATION, Parameters, "union", KdbFieldHex),
    KDB_FIELD(IO_STACK_LOCATION, DeviceObject, "_DEVICE_OBJECT*", KdbFieldPointer),
    KDB_FIELD(IO_STACK_LOCATION, FileObject, "_FILE_OBJECT*", KdbFieldPointer),
    KDB_FIELD(IO_STACK_LOCATION, CompletionRoutine, "pointer", KdbFieldSymbol),
    KDB_FIELD(IO_STACK_LOCATION, Context, "pointer", KdbFieldPointer)
};

static KDB_TYPE_FIELD KdbFieldsFileObject[] =
{
    KDB_FIELD(FILE_OBJECT, Type, "CSHORT", KdbFieldSigned),
    KDB_FIELD(FILE_OBJECT, Size, "CSHORT", KdbFieldSigned),
    KDB_FIELD(FILE_OBJECT, DeviceObject, "_DEVICE_OBJECT*", KdbFieldPointer),
    KDB_FIELD(FILE_OBJECT, Vpb, "_VPB*", KdbFieldPointer),
    KDB_FIELD(FILE_OBJECT, FsContext, "pointer", KdbFieldPointer),
    KDB_FIELD(FILE_OBJECT, FsContext2, "pointer", KdbFieldPointer),
    KDB_FIELD(FILE_OBJECT, FinalStatus, "NTSTATUS", KdbFieldHex),
    KDB_FIELD(FILE_OBJECT, RelatedFileObject, "_FILE_OBJECT*", KdbFieldPointer),
    KDB_FIELD(FILE_OBJECT, Flags, "ULONG", KdbFieldHex),
    KDB_FIELD(FILE_OBJECT, FileName, "_UNICODE_STRING", KdbFieldUnicode),
    KDB_FIELD(FILE_OBJECT, CurrentByteOffset, "LARGE_INTEGER", KdbFieldHex),
    KDB_FIELD(FILE_OBJECT, IrpList, "_LIST_ENTRY", KdbFieldHex)
};

static KDB_TYPE_FIELD KdbFieldsMdl[] =
{
    KDB_FIELD(MDL, Next, "_MDL*", KdbFieldPointer),
    KDB_FIELD(MDL, Size, "CSHORT", KdbFieldSigned),
    KDB_FIELD(MDL, MdlFlags, "CSHORT", KdbFieldHex),
    KDB_FIELD(MDL, Process, "_EPROCESS*", KdbFieldPointer),
    KDB_FIELD(MDL, MappedSystemVa, "pointer", KdbFieldPointer),
    KDB_FIELD(MDL, StartVa, "pointer", KdbFieldPointer),
    KDB_FIELD(MDL, ByteCount, "ULONG", KdbFieldHex),
    KDB_FIELD(MDL, ByteOffset, "ULONG", KdbFieldHex)
};

static KDB_TYPE_FIELD KdbFieldsObjectHeader[] =
{
    KDB_FIELD(OBJECT_HEADER, PointerCount, "LONG_PTR", KdbFieldSigned),
    KDB_FIELD(OBJECT_HEADER, HandleCount, "LONG_PTR", KdbFieldSigned),
    KDB_FIELD(OBJECT_HEADER, Type, "_OBJECT_TYPE*", KdbFieldPointer),
    KDB_FIELD(OBJECT_HEADER, NameInfoOffset, "UCHAR", KdbFieldHex),
    KDB_FIELD(OBJECT_HEADER, HandleInfoOffset, "UCHAR", KdbFieldHex),
    KDB_FIELD(OBJECT_HEADER, QuotaInfoOffset, "UCHAR", KdbFieldHex),
    KDB_FIELD(OBJECT_HEADER, Flags, "UCHAR", KdbFieldHex),
    KDB_FIELD(OBJECT_HEADER, ObjectCreateInfo, "pointer", KdbFieldPointer),
    KDB_FIELD(OBJECT_HEADER, SecurityDescriptor, "pointer", KdbFieldPointer),
    KDB_FIELD(OBJECT_HEADER, Body, "QUAD", KdbFieldHex)
};

#if defined(_M_ARM64)
static KDB_TYPE_FIELD KdbFieldsContext[] =
{
    KDB_FIELD(CONTEXT, ContextFlags, "ULONG", KdbFieldHex),
    KDB_FIELD(CONTEXT, Cpsr, "ULONG", KdbFieldHex),
    KDB_FIELD(CONTEXT, X0, "ULONG64", KdbFieldHex),
    KDB_FIELD(CONTEXT, X1, "ULONG64", KdbFieldHex),
    KDB_FIELD(CONTEXT, X19, "ULONG64", KdbFieldHex),
    KDB_FIELD(CONTEXT, X28, "ULONG64", KdbFieldHex),
    KDB_FIELD(CONTEXT, Fp, "ULONG64", KdbFieldPointer),
    KDB_FIELD(CONTEXT, Lr, "ULONG64", KdbFieldSymbol),
    KDB_FIELD(CONTEXT, Sp, "ULONG64", KdbFieldPointer),
    KDB_FIELD(CONTEXT, Pc, "ULONG64", KdbFieldSymbol),
    KDB_FIELD(CONTEXT, Fpcr, "ULONG", KdbFieldHex),
    KDB_FIELD(CONTEXT, Fpsr, "ULONG", KdbFieldHex)
};
#elif defined(_M_AMD64)
static KDB_TYPE_FIELD KdbFieldsContext[] =
{
    KDB_FIELD(CONTEXT, ContextFlags, "ULONG", KdbFieldHex),
    KDB_FIELD(CONTEXT, MxCsr, "ULONG", KdbFieldHex),
    KDB_FIELD(CONTEXT, SegCs, "USHORT", KdbFieldHex),
    KDB_FIELD(CONTEXT, EFlags, "ULONG", KdbFieldHex),
    KDB_FIELD(CONTEXT, Rax, "ULONG64", KdbFieldHex),
    KDB_FIELD(CONTEXT, Rbx, "ULONG64", KdbFieldHex),
    KDB_FIELD(CONTEXT, Rsp, "ULONG64", KdbFieldPointer),
    KDB_FIELD(CONTEXT, Rbp, "ULONG64", KdbFieldPointer),
    KDB_FIELD(CONTEXT, R8, "ULONG64", KdbFieldHex),
    KDB_FIELD(CONTEXT, R15, "ULONG64", KdbFieldHex),
    KDB_FIELD(CONTEXT, Rip, "ULONG64", KdbFieldSymbol)
};
#elif defined(_M_IX86)
static KDB_TYPE_FIELD KdbFieldsContext[] =
{
    KDB_FIELD(CONTEXT, ContextFlags, "ULONG", KdbFieldHex),
    KDB_FIELD(CONTEXT, Dr0, "ULONG", KdbFieldHex),
    KDB_FIELD(CONTEXT, Dr7, "ULONG", KdbFieldHex),
    KDB_FIELD(CONTEXT, SegCs, "ULONG", KdbFieldHex),
    KDB_FIELD(CONTEXT, EFlags, "ULONG", KdbFieldHex),
    KDB_FIELD(CONTEXT, Eax, "ULONG", KdbFieldHex),
    KDB_FIELD(CONTEXT, Ebx, "ULONG", KdbFieldHex),
    KDB_FIELD(CONTEXT, Esp, "ULONG", KdbFieldPointer),
    KDB_FIELD(CONTEXT, Ebp, "ULONG", KdbFieldPointer),
    KDB_FIELD(CONTEXT, Eip, "ULONG", KdbFieldSymbol)
};
#endif

static KDB_BUILTIN_TYPE KdbBuiltinTypes[] =
{
    KDB_TYPE(LIST_ENTRY, KdbFieldsListEntry),
    KDB_TYPE(UNICODE_STRING, KdbFieldsUnicodeString),
    KDB_TYPE(CLIENT_ID, KdbFieldsClientId),
    KDB_TYPE(EPROCESS, KdbFieldsEprocess),
    KDB_TYPE(ETHREAD, KdbFieldsEthread),
    KDB_TYPE(KTHREAD, KdbFieldsKthread),
    KDB_TYPE(PEB, KdbFieldsPeb),
    KDB_TYPE(TEB, KdbFieldsTeb),
    KDB_TYPE(LDR_DATA_TABLE_ENTRY, KdbFieldsLdrEntry),
    KDB_TYPE(DRIVER_OBJECT, KdbFieldsDriverObject),
    KDB_TYPE(DEVICE_OBJECT, KdbFieldsDeviceObject),
    KDB_TYPE(IRP, KdbFieldsIrp),
    KDB_TYPE(IO_STACK_LOCATION, KdbFieldsIoStack),
    KDB_TYPE(FILE_OBJECT, KdbFieldsFileObject),
    KDB_TYPE(MDL, KdbFieldsMdl),
    KDB_TYPE(OBJECT_HEADER, KdbFieldsObjectHeader),
    KDB_TYPE(CONTEXT, KdbFieldsContext)
};

static PKDB_BUILTIN_TYPE
KdbpFindBuiltinType(IN PCSTR Name)
{
    ULONG Index;
    PCSTR Separator = strrchr(Name, '!');

    if (Separator != NULL)
        Name = Separator + 1;
    if (*Name == '_')
        Name++;

    for (Index = 0; Index < RTL_NUMBER_OF(KdbBuiltinTypes); Index++)
    {
        PCSTR Candidate = KdbBuiltinTypes[Index].Name;
        if (*Candidate == '_')
            Candidate++;
        if (_stricmp(Name, Candidate) == 0)
            return &KdbBuiltinTypes[Index];
    }
    return NULL;
}

static VOID
KdbpPrintBuiltinField(IN PKDB_TYPE_FIELD Field, IN PVOID Base OPTIONAL)
{
    UCHAR Data[8] = {0};
    ULONG_PTR FieldAddress = 0;
    ULONGLONG Value = 0;
    NTSTATUS Status;

    KdbpPrint("  +0x%03lx %-28s : %-28s", Field->Offset, Field->Name, Field->TypeName);
    if (Base == NULL)
    {
        KdbpPrint(" [%lu]\n", Field->Size);
        return;
    }
    if ((ULONG_PTR)Base > MAXULONG_PTR - Field->Offset)
    {
        KdbpPrint(" <address overflow>\n");
        return;
    }
    FieldAddress = (ULONG_PTR)Base + Field->Offset;

    if (Field->Kind == KdbFieldUnicode && Field->Size == sizeof(UNICODE_STRING))
    {
        UNICODE_STRING String;

        Status = KdbpSafeReadMemory(&String, (PVOID)FieldAddress, sizeof(String));
        if (!NT_SUCCESS(Status))
            KdbpPrint(" <unreadable: 0x%08lx>\n", Status);
        else
        {
            KdbpPrint(" ");
            KdbpPrintRemoteUnicodeString(&String);
            KdbpPrint("\n");
        }
        return;
    }

    if (Field->Kind == KdbFieldAnsi)
    {
        CHAR Text[65];
        ULONG Length = min(Field->Size, (ULONG)sizeof(Text) - 1);

        Status = KdbpSafeReadMemory(Text, (PVOID)FieldAddress, Length);
        if (!NT_SUCCESS(Status))
            KdbpPrint(" <unreadable: 0x%08lx>\n", Status);
        else
        {
            Text[Length] = ANSI_NULL;
            KdbpPrint(" \"%s\"\n", Text);
        }
        return;
    }

    if (Field->Size > sizeof(Data))
    {
        KdbpPrint(" <%lu-byte aggregate @ %p>\n", Field->Size, (PVOID)FieldAddress);
        return;
    }

    Status = KdbpSafeReadMemory(Data, (PVOID)FieldAddress, Field->Size);
    if (!NT_SUCCESS(Status))
    {
        KdbpPrint(" <unreadable: 0x%08lx>\n", Status);
        return;
    }
    RtlCopyMemory(&Value, Data, Field->Size);

    if (Field->Kind == KdbFieldPointer || Field->Kind == KdbFieldSymbol)
        KdbpPrint(" %p", (PVOID)(ULONG_PTR)Value);
    else if (Field->Kind == KdbFieldSigned)
    {
        LONGLONG Signed;
        if (Field->Size == 1) Signed = (CHAR)Value;
        else if (Field->Size == 2) Signed = (SHORT)Value;
        else if (Field->Size == 4) Signed = (LONG)Value;
        else Signed = (LONGLONG)Value;
        KdbpPrint(" %I64d (0x%I64x)", Signed, Value);
    }
    else
        KdbpPrint(" 0x%I64x", Value);

    if (Field->Kind == KdbFieldSymbol && Value != 0)
    {
        KdbpPrint(" ");
        if (!KdbSymPrintAddress((PVOID)(ULONG_PTR)Value, KdbCurrentTrapFrame))
            KdbpPrint("<no symbol>");
    }
    KdbpPrint("\n");
}

static BOOLEAN
KdbpCmdPrintStruct(ULONG Argc, PCHAR Argv[])
{
    PKDB_BUILTIN_TYPE Type;
    ULONG Index;
    PVOID Base = NULL;
    ULONG_PTR Address;

    if (Argc == 1)
    {
        KdbpPrint("Embedded types (compiled for this exact kernel):\n");
        for (Index = 0; Index < RTL_NUMBER_OF(KdbBuiltinTypes); Index++)
            KdbpPrint("  _%-30s size 0x%lx\n", KdbBuiltinTypes[Index].Name, KdbBuiltinTypes[Index].Size);
        return TRUE;
    }

    Type = KdbpFindBuiltinType(Argv[1]);
    if (Type == NULL)
    {
        KdbpPrint("dt: Type '%s' is not embedded; run dt to list exact supported layouts.\n", Argv[1]);
        return TRUE;
    }

    if (Argc > 2)
    {
        ULONG Argument;

        for (Argument = 2; Argument + 1 < Argc; Argument++)
            Argv[Argument][strlen(Argv[Argument])] = ' ';
        if (!KdbpEvaluateAddress(Argv[2], KdbPromptStr.Length + (Argv[2] - Argv[0]), &Address))
        {
            return TRUE;
        }
        Base = (PVOID)Address;
    }

    KdbpPrint("%s%s size 0x%lx%s", Type->Name[0] == '_' ? "" : "_", Type->Name, Type->Size, Base != NULL ? " @ " : "\n");
    if (Base != NULL)
        KdbpPrint("%p\n", Base);

    for (Index = 0; Index < Type->FieldCount; Index++)
    {
        KdbpPrintBuiltinField(&Type->Fields[Index], Base);
        if (KdbOutputAborted)
            break;
    }
    return TRUE;
}
#endif // __ROS_DWARF__

/*!\brief Retrieves the component ID corresponding to a given component name.
 *
 * \param ComponentName  The name of the component.
 * \param ComponentId    Receives the component id on success.
 *
 * \retval TRUE   Success.
 * \retval FALSE  Failure.
 */
static BOOLEAN
KdbpGetComponentId(IN PCSTR ComponentName, OUT PULONG ComponentId)
{
    ULONG i;

    for (i = 0; i < RTL_NUMBER_OF(ComponentTable); i++)
    {
        if (_stricmp(ComponentName, ComponentTable[i].Name) == 0)
        {
            *ComponentId = ComponentTable[i].Id;
            return TRUE;
        }
    }

    return FALSE;
}

/*!\brief Displays the list of active debug channels, or enable/disable debug channels.
 */
static BOOLEAN
KdbpCmdFilter(ULONG Argc, PCHAR Argv[])
{
    ULONG i, j, ComponentId, Level;
    ULONG set = DPFLTR_MASK, clear = DPFLTR_MASK;
    PCHAR pend;
    PCSTR opt, p;

    static struct
    {
        PCSTR Name;
        ULONG Level;
    }
    debug_classes[] =
    {
        { "error",   1 << DPFLTR_ERROR_LEVEL   },
        { "warning", 1 << DPFLTR_WARNING_LEVEL },
        { "trace",   1 << DPFLTR_TRACE_LEVEL   },
        { "info",    1 << DPFLTR_INFO_LEVEL    },
    };

    if (Argc <= 1)
    {
        /* Display the list of available debug filter components */
        KdbpPrint("REMARKS:\n"
                  "- The 'WIN2000' system-wide debug filter component is used for DbgPrint()\n"
                  "  messages without Component ID and Level.\n"
                  "- The 'DEFAULT' debug filter component is used for DbgPrint() messages with\n"
                  "  an unknown Component ID.\n\n");
        KdbpPrint("The list of debug filter components currently available on your system is:\n\n");
        KdbpPrint("    Component Name         Component ID\n" "  ==================     ================\n");
        for (i = 0; i < RTL_NUMBER_OF(ComponentTable); i++)
        {
            KdbpPrint("%20s        0x%08lx\n", ComponentTable[i].Name, ComponentTable[i].Id);
        }
        return TRUE;
    }

    for (i = 1; i < Argc; i++)
    {
        opt = Argv[i];
        p = opt + strcspn(opt, "+-");
        if (!p[0]) p = opt; /* Assume it's a debug channel name */

        if (p > opt)
        {
            for (j = 0; j < RTL_NUMBER_OF(debug_classes); j++)
            {
                SIZE_T len = strlen(debug_classes[j].Name);
                if (len != (p - opt))
                    continue;
                if (_strnicmp(opt, debug_classes[j].Name, len) == 0) /* Found it */
                {
                    if (*p == '+')
                        set |= debug_classes[j].Level;
                    else
                        clear |= debug_classes[j].Level;
                    break;
                }
            }
            if (j == RTL_NUMBER_OF(debug_classes))
            {
                Level = strtoul(opt, &pend, 0);
                if (pend != p)
                {
                    KdbpPrint("filter: bad class name '%.*s'\n", p - opt, opt);
                    continue;
                }
                if (*p == '+')
                    set |= Level;
                else
                    clear |= Level;
            }
        }
        else
        {
            if (*p == '-')
                clear = MAXULONG;
            else
                set = MAXULONG;
        }
        if (*p == '+' || *p == '-')
            p++;

        if (!KdbpGetComponentId(p, &ComponentId))
        {
            KdbpPrint("filter: '%s' is not a valid component name!\n", p);
            return TRUE;
        }

        /* Get current mask value */
        NtSetDebugFilterState(ComponentId, set, TRUE);
        NtSetDebugFilterState(ComponentId, clear, FALSE);
    }

    return TRUE;
}

/*!\brief Disassembles 10 instructions at eip or given address or
 *        displays 16 dwords from memory at given address.
 */
static BOOLEAN
KdbpCmdDisassembleX(ULONG Argc, PCHAR Argv[])
{
    ULONG Count;
    ULONG ul;
    INT i;
    ULONGLONG Result = 0;
    ULONG_PTR Address = KeGetContextPc(KdbCurrentTrapFrame);
    LONG InstLen;

    if (Argv[0][0] == 'x') /* display memory */
        Count = 16;
    else /* disassemble */
        Count = 10;

    if (Argc >= 2)
    {
        /* Check for [L count] part */
        ul = 0;
        if (strcmp(Argv[Argc-2], "L") == 0)
        {
            ul = strtoul(Argv[Argc-1], NULL, 0);
            if (ul > 0)
            {
                Count = ul;
                Argc -= 2;
            }
        }
        else if (Argv[Argc-1][0] == 'L')
        {
            ul = strtoul(Argv[Argc-1] + 1, NULL, 0);
            if (ul > 0)
            {
                Count = ul;
                Argc--;
            }
        }

        /* Put the remaining arguments back together */
        Argc--;
        for (ul = 1; ul < Argc; ul++)
        {
            Argv[ul][strlen(Argv[ul])] = ' ';
        }
        Argc++;
    }

    /* Evaluate the expression */
    if (Argc > 1)
    {
        if (!KdbpEvaluateExpression(Argv[1], KdbPromptStr.Length + (Argv[1]-Argv[0]), &Result))
            return TRUE;

        if (Result > (ULONGLONG)MAXULONG_PTR)
        {
            KdbpPrint("Address 0x%I64x does not fit in a pointer.\n", Result);
            return TRUE;
        }

        Address = (ULONG_PTR)Result;
    }
    else if (Argv[0][0] == 'x')
    {
        KdbpPrint("x: Address argument required.\n");
        return TRUE;
    }

    if (Argv[0][0] == 'x')
    {
        /* Display dwords */
        ul = 0;

        while (Count > 0)
        {
            if (!KdbSymPrintAddress((PVOID)Address, NULL))
                KdbpPrint("<%p>:", (PVOID)Address);
            else
                KdbpPrint(":");

            i = min(4, Count);
            Count -= i;

            while (--i >= 0)
            {
                if (!NT_SUCCESS(KdbpSafeReadMemory(&ul, (PVOID)Address, sizeof(ul))))
                    KdbpPrint(" ????????");
                else
                    KdbpPrint(" %08x", ul);

                Address += sizeof(ul);
            }

            KdbpPrint("\n");
        }
    }
    else
    {
        /* Disassemble */
        while (Count-- > 0)
        {
            if (!KdbSymPrintAddress((PVOID)Address, NULL))
                KdbpPrint("<%p>: ", (PVOID)Address);
            else
                KdbpPrint(": ");

            InstLen = KdbpDisassemble(Address, KdbUseIntelSyntax);
            if (InstLen < 0)
            {
                KdbpPrint("<INVALID>\n");
                return TRUE;
            }

            KdbpPrint("\n");
            Address += InstLen;
        }
    }

    return TRUE;
}

/*!\brief Prints the general register set of the given CONTEXT.
 *
 * Shared by the "regs" and ".cxr" commands.
 */
static VOID
KdbpPrintContext(PCONTEXT Context)
{
#if !defined(_M_ARM64)
    INT i;
    static const PCHAR EflagsBits[32] = { " CF", NULL, " PF", " BIT3", " AF", " BIT5",
                                          " ZF", " SF", " TF", " IF", " DF", " OF",
                                          NULL, NULL, " NT", " BIT15", " RF", " VF",
                                          " AC", " VIF", " VIP", " ID", " BIT22",
                                          " BIT23", " BIT24", " BIT25", " BIT26",
                                          " BIT27", " BIT28", " BIT29", " BIT30",
                                          " BIT31" };
#endif

#ifdef _M_IX86
    KdbpPrint("CS:EIP  0x%04x:0x%08x\n"
              "SS:ESP  0x%04x:0x%08x\n"
              "   EAX  0x%08x   EBX  0x%08x\n"
              "   ECX  0x%08x   EDX  0x%08x\n"
              "   ESI  0x%08x   EDI  0x%08x\n"
              "   EBP  0x%08x\n",
              Context->SegCs & 0xFFFF, Context->Eip,
              Context->SegSs, Context->Esp,
              Context->Eax, Context->Ebx,
              Context->Ecx, Context->Edx,
              Context->Esi, Context->Edi,
              Context->Ebp);
#elif defined(_M_AMD64)
    KdbpPrint("CS:RIP  0x%04x:%p\n"
              "SS:RSP  0x%04x:%p\n"
              "   RAX  %p     RBX  %p\n"
              "   RCX  %p     RDX  %p\n"
              "   RSI  %p     RDI  %p\n"
              "   RBP  %p      R8  %p\n"
              "    R9  %p     R10  %p\n"
              "   R11  %p     R12  %p\n"
              "   R13  %p     R14  %p\n"
              "   R15  %p\n",
              Context->SegCs & 0xFFFF, (PVOID)(ULONG_PTR)Context->Rip,
              Context->SegSs, (PVOID)(ULONG_PTR)Context->Rsp,
              (PVOID)(ULONG_PTR)Context->Rax, (PVOID)(ULONG_PTR)Context->Rbx,
              (PVOID)(ULONG_PTR)Context->Rcx, (PVOID)(ULONG_PTR)Context->Rdx,
              (PVOID)(ULONG_PTR)Context->Rsi, (PVOID)(ULONG_PTR)Context->Rdi,
              (PVOID)(ULONG_PTR)Context->Rbp, (PVOID)(ULONG_PTR)Context->R8,
              (PVOID)(ULONG_PTR)Context->R9, (PVOID)(ULONG_PTR)Context->R10,
              (PVOID)(ULONG_PTR)Context->R11, (PVOID)(ULONG_PTR)Context->R12,
              (PVOID)(ULONG_PTR)Context->R13, (PVOID)(ULONG_PTR)Context->R14,
              (PVOID)(ULONG_PTR)Context->R15);
#elif defined(_M_ARM64)
    KdbpPrint("PC  0x%p     SP  0x%p\n"
              "LR  0x%p     FP  0x%p\n"
              "X0  0x%p     X1  0x%p\n"
              "X2  0x%p     X3  0x%p\n"
              "X4  0x%p     X5  0x%p\n"
              "X6  0x%p     X7  0x%p\n"
              "X8  0x%p     X9  0x%p\n"
              "X10 0x%p     X11 0x%p\n"
              "X12 0x%p     X13 0x%p\n"
              "X14 0x%p     X15 0x%p\n"
              "X16 0x%p     X17 0x%p\n"
              "X18 0x%p     X19 0x%p\n"
              "X20 0x%p     X21 0x%p\n"
              "X22 0x%p     X23 0x%p\n"
              "X24 0x%p     X25 0x%p\n"
              "X26 0x%p     X27 0x%p\n"
              "X28 0x%p     CPSR 0x%08x\n",
              (PVOID)(ULONG_PTR)Context->Pc,
              (PVOID)(ULONG_PTR)Context->Sp,
              (PVOID)(ULONG_PTR)Context->Lr,
              (PVOID)(ULONG_PTR)Context->Fp,
              (PVOID)(ULONG_PTR)Context->X0,
              (PVOID)(ULONG_PTR)Context->X1,
              (PVOID)(ULONG_PTR)Context->X2,
              (PVOID)(ULONG_PTR)Context->X3,
              (PVOID)(ULONG_PTR)Context->X4,
              (PVOID)(ULONG_PTR)Context->X5,
              (PVOID)(ULONG_PTR)Context->X6,
              (PVOID)(ULONG_PTR)Context->X7,
              (PVOID)(ULONG_PTR)Context->X8,
              (PVOID)(ULONG_PTR)Context->X9,
              (PVOID)(ULONG_PTR)Context->X10,
              (PVOID)(ULONG_PTR)Context->X11,
              (PVOID)(ULONG_PTR)Context->X12,
              (PVOID)(ULONG_PTR)Context->X13,
              (PVOID)(ULONG_PTR)Context->X14,
              (PVOID)(ULONG_PTR)Context->X15,
              (PVOID)(ULONG_PTR)Context->X16,
              (PVOID)(ULONG_PTR)Context->X17,
              (PVOID)(ULONG_PTR)Context->X18,
              (PVOID)(ULONG_PTR)Context->X19,
              (PVOID)(ULONG_PTR)Context->X20,
              (PVOID)(ULONG_PTR)Context->X21,
              (PVOID)(ULONG_PTR)Context->X22,
              (PVOID)(ULONG_PTR)Context->X23,
              (PVOID)(ULONG_PTR)Context->X24,
              (PVOID)(ULONG_PTR)Context->X25,
              (PVOID)(ULONG_PTR)Context->X26,
              (PVOID)(ULONG_PTR)Context->X27,
              (PVOID)(ULONG_PTR)Context->X28,
              Context->Cpsr);
#endif
#if !defined(_M_ARM64)
    /* Display the EFlags */
    KdbpPrint("EFLAGS  0x%08x ", Context->EFlags);
    for (i = 0; i < 32; i++)
    {
        if (i == 1)
        {
            if ((Context->EFlags & (1 << 1)) == 0)
                KdbpPrint(" !BIT1");
        }
        else if (i == 12)
        {
            KdbpPrint(" IOPL%d", (Context->EFlags >> 12) & 3);
        }
        else if (i == 13)
        {
        }
        else if ((Context->EFlags & (1 << i)) != 0)
        {
            KdbpPrint(EflagsBits[i]);
        }
    }
    KdbpPrint("\n");
#endif
}

/*!\brief Displays CPU registers.
 */
static BOOLEAN
KdbpCmdRegs(ULONG Argc, PCHAR Argv[])
{
    PCONTEXT Context = KdbCurrentTrapFrame;
#if !defined(_M_ARM64)
    INT i;
#endif

    if (Argv[0][0] == 'r') /* regs */
    {
        KdbpPrintContext(Context);
    }
    else if (Argv[0][0] == 'c') /* cregs */
    {
        static KPROCESSOR_STATE ProcessorState;
        PKPROCESSOR_STATE CapturedState = NULL;

        if (KdbSelectedProcessor >= 0)
        {
            PKPRCB Prcb;
            ULONG FrozenState;

            Prcb = KiProcessorBlock[KdbSelectedProcessor];
            if (Prcb == NULL ||
                !NT_SUCCESS(KdbpSafeReadMemory(&FrozenState, (PVOID)&Prcb->IpiFrozen, sizeof(FrozenState))) ||
                ((FrozenState & ~IPI_FROZEN_FLAG_ACTIVE) != IPI_FROZEN_STATE_FROZEN) ||
                !NT_SUCCESS(KdbpSafeReadMemory(&ProcessorState, &Prcb->ProcessorState, sizeof(ProcessorState))))
            {
                KdbpPrint("cregs: CPU %ld no longer has a stable frozen control state.\n", KdbSelectedProcessor);
                return TRUE;
            }
            CapturedState = &ProcessorState;
            KdbpPrint("CPU %ld frozen control state:\n", KdbSelectedProcessor);
        }
#if defined(_M_ARM64)
        ULONG64 CurrentEl, Daif, SpEl0;
        PKARM64_ARCH_STATE ArchState;

        if (CapturedState == NULL)
        {
            /* Capture the system registers through the canonical save path;
             * read directly only what it does not capture. */
            KiSaveProcessorControlState(&ProcessorState);
            CapturedState = &ProcessorState;
            __asm__ __volatile__("mrs %0, CurrentEL" : "=r"(CurrentEl));
            __asm__ __volatile__("mrs %0, DAIF" : "=r"(Daif));
            __asm__ __volatile__("mrs %0, SP_EL0" : "=r"(SpEl0));
        }
        else
        {
            /* CurrentEL and DAIF are PSTATE fields captured in the frozen
             * CONTEXT. SP_EL0 has no slot in KPROCESSOR_STATE. */
            CurrentEl = CapturedState->ContextFrame.Cpsr & 0xCUL;
            Daif = CapturedState->ContextFrame.Cpsr & 0x3C0UL;
            SpEl0 = 0;
        }
        ArchState = &CapturedState->ArchState;

        KdbpPrint("CurrentEL 0x%p     DAIF      0x%p\n", (PVOID)(ULONG_PTR)CurrentEl, (PVOID)(ULONG_PTR)Daif);
        KdbpPrint("SCTLR_EL1 0x%p     TCR_EL1   0x%p\n", (PVOID)(ULONG_PTR)ArchState->Sctlr_El1, (PVOID)(ULONG_PTR)ArchState->Tcr_El1);
        KdbpPrint("TTBR0_EL1 0x%p     TTBR1_EL1 0x%p\n", (PVOID)(ULONG_PTR)ArchState->Ttbr0_El1, (PVOID)(ULONG_PTR)ArchState->Ttbr1_El1);
        KdbpPrint("MAIR_EL1  0x%p     VBAR_EL1  0x%p\n", (PVOID)(ULONG_PTR)ArchState->Mair_El1, (PVOID)(ULONG_PTR)ArchState->Vbar_El1);
        KdbpPrint("ESR_EL1   0x%p     FAR_EL1   0x%p\n", (PVOID)(ULONG_PTR)ArchState->Esr_El1, (PVOID)(ULONG_PTR)ArchState->Far_El1);
        if (KdbSelectedProcessor >= 0)
            KdbpPrint("SP_EL0    <not captured>\n");
        else
            KdbpPrint("SP_EL0    0x%p\n", (PVOID)(ULONG_PTR)SpEl0);
#else
        ULONG_PTR Cr0, Cr2, Cr3, Cr4;
        KDESCRIPTOR Gdtr = {0}, Idtr = {0};
        USHORT Ldtr, Tr;
        static const PCHAR Cr0Bits[32] = { " PE", " MP", " EM", " TS", " ET", " NE", NULL, NULL,
                                           NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                                           " WP", NULL, " AM", NULL, NULL, NULL, NULL, NULL,
                                           NULL, NULL, NULL, NULL, NULL, " NW", " CD", " PG" };
        static const PCHAR Cr4Bits[32] = { " VME", " PVI", " TSD", " DE", " PSE", " PAE", " MCE", " PGE",
                                           " PCE", " OSFXSR", " OSXMMEXCPT", NULL, NULL, NULL, NULL, NULL,
                                           NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                                           NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL };
        if (CapturedState != NULL)
        {
            PKSPECIAL_REGISTERS Registers = &CapturedState->SpecialRegisters;

            Cr0 = (ULONG_PTR)Registers->Cr0;
            Cr2 = (ULONG_PTR)Registers->Cr2;
            Cr3 = (ULONG_PTR)Registers->Cr3;
            Cr4 = (ULONG_PTR)Registers->Cr4;
            Gdtr = Registers->Gdtr;
            Idtr = Registers->Idtr;
            Ldtr = Registers->Ldtr;
            Tr = Registers->Tr;
        }
        else
        {
            /* KDB already owns the machine and has frozen its peer processors.
             * Calling KdSystemDebugControl here re-enters the KD control-space
             * path and can wait forever for debugger or processor activity
             * that KDB itself has stopped. Capture the current CPU directly. */
            Cr0 = __readcr0();
            Cr2 = __readcr2();
            Cr3 = __readcr3();
            Cr4 = __readcr4();
            Ke386GetGlobalDescriptorTable(&Gdtr.Limit);
            __sldt(&Ldtr);
            __sidt(&Idtr.Limit);
#ifdef _M_IX86
            Tr = Ke386GetTr();
#else
            __str(&Tr);
#endif
        }

        /* Display the control registers */
        KdbpPrint("CR0  0x%Ix ", Cr0);
        for (i = 0; i < 32; i++)
        {
            if (!Cr0Bits[i])
                continue;

            if ((Cr0 & ((ULONG_PTR)1 << i)) != 0)
                KdbpPrint(Cr0Bits[i]);
        }
        KdbpPrint("\n");

        KdbpPrint("CR2  0x%Ix\n", Cr2);
        KdbpPrint("CR3  0x%Ix  Pagedir-Base 0x%Ix %s%s\n", Cr3, Cr3 & ~((ULONG_PTR)PAGE_SIZE - 1), (Cr3 & ((ULONG_PTR)1 << 3)) ? " PWT" : "", (Cr3 & ((ULONG_PTR)1 << 4)) ? " PCD" : "");
        KdbpPrint("CR4  0x%Ix ", Cr4);
        for (i = 0; i < 32; i++)
        {
            if (!Cr4Bits[i])
                continue;

            if ((Cr4 & ((ULONG_PTR)1 << i)) != 0)
                KdbpPrint(Cr4Bits[i]);
        }
        KdbpPrint("\n");

        /* Display the descriptor table and task segment registers */
        KdbpPrint("GDTR Base %p  Size 0x%04x\n", (PVOID)(ULONG_PTR)Gdtr.Base, Gdtr.Limit);
        KdbpPrint("LDTR 0x%04x\n", Ldtr);
        KdbpPrint("IDTR Base %p  Size 0x%04x\n", (PVOID)(ULONG_PTR)Idtr.Base, Idtr.Limit);
        KdbpPrint("TR   0x%04x\n", Tr);
#endif
    }
    else if (Argv[0][0] == 's') /* sregs */
    {
#if defined(_M_ARM64)
        KdbpPrint("Segment registers are not present on ARM64.\n");
#else
        KdbpPrint("CS  0x%04x  Index 0x%04x  %cDT RPL%d\n", Context->SegCs & 0xffff, (Context->SegCs & 0xffff) >> 3, (Context->SegCs & (1 << 2)) ? 'L' : 'G', Context->SegCs & 3);
        KdbpPrint("DS  0x%04x  Index 0x%04x  %cDT RPL%d\n", Context->SegDs, Context->SegDs >> 3, (Context->SegDs & (1 << 2)) ? 'L' : 'G', Context->SegDs & 3);
        KdbpPrint("ES  0x%04x  Index 0x%04x  %cDT RPL%d\n", Context->SegEs, Context->SegEs >> 3, (Context->SegEs & (1 << 2)) ? 'L' : 'G', Context->SegEs & 3);
        KdbpPrint("FS  0x%04x  Index 0x%04x  %cDT RPL%d\n", Context->SegFs, Context->SegFs >> 3, (Context->SegFs & (1 << 2)) ? 'L' : 'G', Context->SegFs & 3);
        KdbpPrint("GS  0x%04x  Index 0x%04x  %cDT RPL%d\n", Context->SegGs, Context->SegGs >> 3, (Context->SegGs & (1 << 2)) ? 'L' : 'G', Context->SegGs & 3);
        KdbpPrint("SS  0x%04x  Index 0x%04x  %cDT RPL%d\n", Context->SegSs, Context->SegSs >> 3, (Context->SegSs & (1 << 2)) ? 'L' : 'G', Context->SegSs & 3);
#endif
    }
    else /* dregs */
    {
        ASSERT(Argv[0][0] == 'd');
#if defined(_M_ARM64)
        ULONG i;

        for (i = 0; i < RTL_NUMBER_OF(Context->Bcr); i++)
        {
            KdbpPrint("BVR%lu 0x%p     BCR%lu 0x%08lx\n", i, (PVOID)(ULONG_PTR)Context->Bvr[i], i, Context->Bcr[i]);
        }

        for (i = 0; i < RTL_NUMBER_OF(Context->Wcr); i++)
        {
            KdbpPrint("WVR%lu 0x%p     WCR%lu 0x%08lx\n", i, (PVOID)(ULONG_PTR)Context->Wvr[i], i, Context->Wcr[i]);
        }
#else
        KdbpPrint("DR0  0x%Ix\n"
                  "DR1  0x%Ix\n"
                  "DR2  0x%Ix\n"
                  "DR3  0x%Ix\n"
                  "DR6  0x%Ix\n"
                  "DR7  0x%Ix\n",
                  (ULONG_PTR)Context->Dr0, (ULONG_PTR)Context->Dr1,
                  (ULONG_PTR)Context->Dr2, (ULONG_PTR)Context->Dr3,
                  (ULONG_PTR)Context->Dr6, (ULONG_PTR)Context->Dr7);
#endif
    }

    return TRUE;
}

/*!\brief Implements the ".cxr" (set/reset context record) command.
 *
 * With an address argument: reads a CONTEXT structure from the given
 * address and makes it the active context for register display, backtrace,
 * and expression evaluation (like WinDbg's .cxr command).
 *
 * Without arguments: resets back to the original trap frame context.
 */
static BOOLEAN
KdbpCmdContextRecord(ULONG Argc, PCHAR Argv[])
{
    ULONG_PTR Address;
    ULONG Index;
    NTSTATUS Status;

    KdbpDiscardStaleContextRecord();
    if (Argc < 2)
    {
        if (KdbContextRecordActive)
            KdbpResetContextRecord(TRUE);
        else
        {
            KdbpPrint("No context record is currently active.\n" "Usage: .cxr <address>  - Set context to CONTEXT at address\n" "       .cxr            - Reset to current trap frame\n");
        }
        return TRUE;
    }

    for (Index = 1; Index + 1 < Argc; Index++)
        Argv[Index][strlen(Argv[Index])] = ' ';
    if (!KdbpEvaluateAddress(Argv[1], KdbPromptStr.Length + (Argv[1] - Argv[0]), &Address))
        return TRUE;

    if (Address == 0)
    {
        KdbpPrint("Invalid context record address 0.\n");
        return TRUE;
    }

    /* Read the CONTEXT structure from the specified address */
    Status = KdbpSafeReadMemory(&KdbSavedContextRecord, (PVOID)Address, sizeof(CONTEXT));
    if (!NT_SUCCESS(Status))
    {
        KdbpPrint("Failed to read CONTEXT at 0x%p: status 0x%08lx\n", (PVOID)Address, Status);
        return TRUE;
    }

    /* Basic sanity check: ContextFlags should have at least CONTEXT_CONTROL set */
    if ((KdbSavedContextRecord.ContextFlags & CONTEXT_CONTROL) == 0)
    {
        KdbpPrint("WARNING: ContextFlags (0x%08x) does not include CONTEXT_CONTROL.\n" "         The data at 0x%p may not be a valid CONTEXT record.\n", KdbSavedContextRecord.ContextFlags, (PVOID)Address);
    }

    /* Save the current trap frame pointer and switch to the loaded context */
    if (!KdbContextRecordActive)
        KdbSavedTrapFrame = KdbCurrentTrapFrame;
    KdbCurrentTrapFrame = (PKDB_KTRAP_FRAME)&KdbSavedContextRecord;
    KdbContextRecordActive = TRUE;
    KdbFrameBaseValid = FALSE;

    /* Display the context */
    KdbpPrint("Context record @ 0x%p:\n", (PVOID)Address);
    KdbpPrintContext(&KdbSavedContextRecord);

    return TRUE;
}

static BOOLEAN
KdbpCmdTrapFrame(ULONG Argc, PCHAR Argv[])
{
    KTRAP_FRAME TrapFrame;
    CONTEXT Context;
    ULONG_PTR Address;
    ULONG Index;
    NTSTATUS Status;
    PKEXCEPTION_FRAME ExceptionFramePointer = NULL;
#if defined(_M_AMD64)
    KEXCEPTION_FRAME ExceptionFrame;
#endif

    KdbpDiscardStaleContextRecord();
    if (Argc == 1)
    {
        if (KdbContextRecordActive)
            KdbpResetContextRecord(TRUE);
        else
            KdbpPrint("No inspection context is currently active.\n");
        return TRUE;
    }

    for (Index = 1; Index + 1 < Argc; Index++)
        Argv[Index][strlen(Argv[Index])] = ' ';
    if (!KdbpEvaluateAddress(Argv[1], KdbPromptStr.Length + (Argv[1] - Argv[0]), &Address))
        return TRUE;
    if (Address == 0)
    {
        KdbpPrint(".trap: Invalid trap-frame address 0.\n");
        return TRUE;
    }

    Status = KdbpSafeReadMemory(&TrapFrame, (PVOID)Address, sizeof(TrapFrame));
    if (!NT_SUCCESS(Status))
    {
        KdbpPrint(".trap: Failed to read KTRAP_FRAME at %p (status 0x%08lx).\n", (PVOID)Address, Status);
        return TRUE;
    }

#if defined(_M_AMD64)
    if (TrapFrame.ExceptionFrame != 0)
    {
        Status = KdbpSafeReadMemory(&ExceptionFrame, (PVOID)(ULONG_PTR)TrapFrame.ExceptionFrame, sizeof(ExceptionFrame));
        if (NT_SUCCESS(Status))
            ExceptionFramePointer = &ExceptionFrame;
        else
            KdbpPrint(".trap: Warning: exception frame %p is unreadable (status 0x%08lx).\n", (PVOID)(ULONG_PTR)TrapFrame.ExceptionFrame, Status);
    }
#endif

    RtlZeroMemory(&Context, sizeof(Context));
#if defined(_M_ARM64)
    Context.ContextFlags = CONTEXT_CONTROL |
                           CONTEXT_INTEGER |
                           CONTEXT_DEBUG_REGISTERS |
                           CONTEXT_X18;
#else
    Context.ContextFlags = CONTEXT_CONTROL |
                           CONTEXT_INTEGER |
                           CONTEXT_SEGMENTS |
                           CONTEXT_DEBUG_REGISTERS;
#endif
    _SEH2_TRY
    {
        KeTrapFrameToContext(&TrapFrame, ExceptionFramePointer, &Context);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        KdbpPrint(".trap: Trap-frame conversion raised exception 0x%08lx.\n", _SEH2_GetExceptionCode());
        return TRUE;
    }
    _SEH2_END;

    if (!KdbContextRecordActive)
        KdbSavedTrapFrame = KdbCurrentTrapFrame;
    KdbSavedContextRecord = Context;
    KdbCurrentTrapFrame = (PKDB_KTRAP_FRAME)&KdbSavedContextRecord;
    KdbContextRecordActive = TRUE;
    KdbFrameBaseValid = FALSE;

    KdbpPrint("Trap frame @ %p (inspection only):\n", (PVOID)Address);
#if defined(_M_ARM64)
    KdbpPrint("FAR 0x%p  ESR 0x%08lx  previous mode %d  previous IRQL %u\n", (PVOID)(ULONG_PTR)TrapFrame.FaultAddress, TrapFrame.Esr, TrapFrame.PreviousMode, TrapFrame.PreviousIrql);
#elif defined(_M_AMD64)
    KdbpPrint("Fault address %p  error 0x%p  previous mode %d  previous IRQL %u\n", (PVOID)(ULONG_PTR)TrapFrame.FaultAddress, (PVOID)(ULONG_PTR)TrapFrame.ErrorCode, TrapFrame.PreviousMode, TrapFrame.PreviousIrql);
#endif
    KdbpPrintContext(&KdbSavedContextRecord);
    return TRUE;
}

static BOOLEAN
KdbpCmdExceptionRecord(ULONG Argc, PCHAR Argv[])
{
    EXCEPTION_RECORD64 Record;
    ULONG_PTR Address;
    ULONG ParameterCount;
    ULONG Index;
    NTSTATUS Status;

    if (Argc > 2)
    {
        KdbpPrint("Usage: .exr [-1|address]\n");
        return TRUE;
    }

    if (Argc == 1 || strcmp(Argv[1], "-1") == 0 || strcmp(Argv[1], ".") == 0)
    {
        if (!KdbCurrentExceptionRecordValid)
        {
            KdbpPrint(".exr: No current exception record is available.\n");
            return TRUE;
        }
        Record = KdbCurrentExceptionRecord;
        Address = 0;
    }
    else
    {
        if (!KdbpEvaluateAddress(Argv[1], KdbPromptStr.Length + (Argv[1] - Argv[0]), &Address))
            return TRUE;
        if (Address == 0)
        {
            KdbpPrint(".exr: Invalid exception-record address 0.\n");
            return TRUE;
        }
        Status = KdbpSafeReadMemory(&Record, (PVOID)Address, sizeof(Record));
        if (!NT_SUCCESS(Status))
        {
            KdbpPrint(".exr: Failed to read EXCEPTION_RECORD64 at %p (status 0x%08lx).\n", (PVOID)Address, Status);
            return TRUE;
        }
    }

    KdbpPrint("Exception record %s%p\n", Address == 0 ? "(current) " : "@ ", (PVOID)Address);
    KdbpPrint("Code 0x%08lx  flags 0x%08lx  address %p  nested %p\n", Record.ExceptionCode, Record.ExceptionFlags, (PVOID)(ULONG_PTR)Record.ExceptionAddress, (PVOID)(ULONG_PTR)Record.ExceptionRecord);

    ParameterCount = Record.NumberParameters;
    if (ParameterCount > EXCEPTION_MAXIMUM_PARAMETERS)
    {
        KdbpPrint("WARNING: Invalid parameter count %lu; displaying the first %u.\n", ParameterCount, EXCEPTION_MAXIMUM_PARAMETERS);
        ParameterCount = EXCEPTION_MAXIMUM_PARAMETERS;
    }
    for (Index = 0; Index < ParameterCount; Index++)
        KdbpPrint("Parameter[%lu] = 0x%I64x\n", Index, Record.ExceptionInformation[Index]);
    return TRUE;
}

#ifdef _M_IX86
static PKTSS
KdbpRetrieveTss(IN USHORT TssSelector, OUT PULONG pType OPTIONAL, IN PKDESCRIPTOR pGdtr OPTIONAL)
{
    KDESCRIPTOR Gdtr;
    KGDTENTRY Desc;
    PKTSS Tss;

    /* Retrieve the Global Descriptor Table (user-provided or system) */
    if (pGdtr)
        Gdtr = *pGdtr;
    else
        Ke386GetGlobalDescriptorTable(&Gdtr.Limit);

    /* Check limits */
    if ((TssSelector & (sizeof(KGDTENTRY) - 1)) ||
        (TssSelector < sizeof(KGDTENTRY)) ||
        (TssSelector + sizeof(KGDTENTRY) - 1 > Gdtr.Limit))
    {
        return NULL;
    }

    /* Retrieve the descriptor */
    if (!NT_SUCCESS(KdbpSafeReadMemory(&Desc, (PVOID)(Gdtr.Base + TssSelector), sizeof(KGDTENTRY))))
    {
        return NULL;
    }

    /* Check for TSS32(Avl) or TSS32(Busy) */
    if (Desc.HighWord.Bits.Type != 9 && Desc.HighWord.Bits.Type != 11)
    {
        return NULL;
    }
    if (pType) *pType = Desc.HighWord.Bits.Type;

    Tss = (PKTSS)(ULONG_PTR)(Desc.BaseLow | Desc.HighWord.Bytes.BaseMid << 16 | Desc.HighWord.Bytes.BaseHi << 24);

    return Tss;
}

FORCEINLINE BOOLEAN
KdbpIsNestedTss(IN USHORT TssSelector, IN PKTSS Tss)
{
    USHORT Backlink;

    if (!Tss)
        return FALSE;

#ifdef _M_AMD64
    // HACK
    return FALSE;
#else
    /* Retrieve the TSS Backlink */
    if (!NT_SUCCESS(KdbpSafeReadMemory(&Backlink, (PVOID)&Tss->Backlink, sizeof(USHORT))))
    {
        return FALSE;
    }
#endif

    return (Backlink != 0 && Backlink != TssSelector);
}

static BOOLEAN
KdbpContextFromPrevTss(IN OUT PCONTEXT Context, OUT PUSHORT TssSelector, IN OUT PKTSS* pTss, IN PKDESCRIPTOR pGdtr)
{
    ULONG_PTR Eip, Ebp;
    USHORT Backlink;
    PKTSS Tss = *pTss;

#ifdef _M_AMD64
    // HACK
    return FALSE;
#else
    /* Retrieve the TSS Backlink */
    if (!NT_SUCCESS(KdbpSafeReadMemory(&Backlink, (PVOID)&Tss->Backlink, sizeof(USHORT))))
    {
        return FALSE;
    }

    /* Retrieve the parent TSS */
    Tss = KdbpRetrieveTss(Backlink, NULL, pGdtr);
    if (!Tss)
        return FALSE;

    if (!NT_SUCCESS(KdbpSafeReadMemory(&Eip, (PVOID)&Tss->Eip, sizeof(ULONG_PTR))))
    {
        return FALSE;
    }

    if (!NT_SUCCESS(KdbpSafeReadMemory(&Ebp, (PVOID)&Tss->Ebp, sizeof(ULONG_PTR))))
    {
        return FALSE;
    }

    /* Return the parent TSS and its trap frame */
    *TssSelector = Backlink;
    *pTss = Tss;
    Context->Eip = Eip;
    Context->Ebp = Ebp;
#endif
    return TRUE;
}
#endif // _M_IX86

#if defined(_M_AMD64) || defined(_M_ARM64)

static
BOOLEAN
GetNextFrame(_Inout_ PCONTEXT Context)
{
    PRUNTIME_FUNCTION FunctionEntry;
    ULONG64 ImageBase, EstablisherFrame;
    PVOID HandlerData;
    ULONG64 OldPc = KeGetContextPc(Context);
    ULONG64 OldSp = KeGetContextStackRegister(Context);
    UCHAR InstructionByte;

    if (!NT_SUCCESS(KdbpSafeReadMemory(&InstructionByte, (PVOID)(ULONG_PTR)OldPc, sizeof(InstructionByte))))
        return FALSE;

    _SEH2_TRY
    {
        /* Lookup the FunctionEntry for the current PC */
        FunctionEntry = RtlLookupFunctionEntry(OldPc, &ImageBase, NULL);
        if (FunctionEntry == NULL)
        {
            /* No function entry, so this must be a leaf function.
            Note: this can happen after the first frame as the result of an exception */
#ifdef _M_AMD64
            /* Pop the return address from the stack */
            Context->Rip = *(DWORD64*)Context->Rsp;
            Context->Rsp += sizeof(DWORD64);
            return TRUE;
#else
            /* The return address is still in Lr */
            if ((Context->Lr == 0) || (Context->Lr == Context->Pc))
                return FALSE;

            Context->Pc = Context->Lr;
            return TRUE;
#endif
        }

        RtlVirtualUnwind(UNW_FLAG_NHANDLER, ImageBase, OldPc, FunctionEntry, Context, &HandlerData, &EstablisherFrame, NULL);
    }
    _SEH2_EXCEPT(1)
    {
        return FALSE;
    }
    _SEH2_END

    if (KeGetContextPc(Context) == 0)
        return FALSE;

    /* Stop when the unwind made no progress, to not loop on a broken chain */
    if ((KeGetContextPc(Context) == OldPc) && (KeGetContextStackRegister(Context) == OldSp))
        return FALSE;

    return TRUE;
}

#define KDB_MAX_BACKTRACE_FRAMES 256
#define KDB_MAX_BACKTRACE_PROCESSES 4096
#define KDB_MAX_BACKTRACE_THREADS 65536

static VOID
KdbpPrintBackTraceContext(IN PCONTEXT InputContext, IN BOOLEAN Verbose)
{
    CONTEXT Context = *InputContext;
    ULONG FrameNumber;

    KdbpPrint("Frames:\n");
    for (FrameNumber = 0; FrameNumber < KDB_MAX_BACKTRACE_FRAMES; FrameNumber++)
    {
        BOOLEAN GotNextFrame;
        ULONG_PTR Pc = KeGetContextPc(&Context);
        ULONG_PTR Sp = KeGetContextStackRegister(&Context);
        ULONG_PTR Fp = KeGetContextFrameRegister(&Context);

        if (Pc == 0 || Sp == 0)
            break;

        if (Verbose)
            KdbpPrint("#%-3lu SP=%p FP=%p PC=%p ", FrameNumber, (PVOID)Sp, (PVOID)Fp, (PVOID)Pc);
        else
            KdbpPrint("[%p] ", (PVOID)Sp);

        if (!KdbSymPrintAddress((PVOID)Pc, &Context))
            KdbpPrint("<%p>", (PVOID)Pc);
        KdbpPrint("\n");

        if (KdbOutputAborted)
            return;
        GotNextFrame = GetNextFrame(&Context);
        if (!GotNextFrame)
        {
            KdbpPrint("Couldn't get next frame\n");
            return;
        }
    }

    if (FrameNumber == KDB_MAX_BACKTRACE_FRAMES)
        KdbpPrint("Backtrace stopped at the %lu-frame safety limit.\n", KDB_MAX_BACKTRACE_FRAMES);
}

static BOOLEAN
KdbpContextIsUsable(IN PCONTEXT Context)
{
    return (KeGetContextPc(Context) != 0 && KeGetContextStackRegister(Context) != 0);
}

static BOOLEAN
KdbpCaptureFrozenThreadContext(IN PETHREAD Thread, IN PCONTEXT OriginalContext, OUT PCONTEXT Context)
{
    ULONG Processor;

    if (Thread == KdbOriginalThread)
    {
        *Context = *OriginalContext;
        return TRUE;
    }

    for (Processor = 0; Processor < (ULONG)(UCHAR)KeNumberProcessors; Processor++)
    {
        PKPRCB Prcb = KiProcessorBlock[Processor];
        PETHREAD CurrentThread = NULL;
        ULONG FrozenState = 0;

        if (Prcb == NULL ||
            !NT_SUCCESS(KdbpSafeReadMemory(&CurrentThread, &Prcb->CurrentThread, sizeof(CurrentThread))) ||
            CurrentThread != Thread)
        {
            continue;
        }

        if (!NT_SUCCESS(KdbpSafeReadMemory(&FrozenState, (PVOID)&Prcb->IpiFrozen, sizeof(FrozenState))) ||
            ((FrozenState & ~IPI_FROZEN_FLAG_ACTIVE) != IPI_FROZEN_STATE_FROZEN))
        {
            return FALSE;
        }

        return (NT_SUCCESS(KdbpSafeReadMemory(Context, &Prcb->ProcessorState.ContextFrame, sizeof(*Context))) && KdbpContextIsUsable(Context));
    }

    *Context = *KdbCurrentTrapFrame;
    return TRUE;
}

static VOID
KdbpPrintAllThreadBackTraces(IN BOOLEAN Verbose)
{
    extern LIST_ENTRY PsActiveProcessHead;
    LIST_ENTRY ProcessHead;
    PLIST_ENTRY ProcessEntry;
    ULONG ProcessCount = 0;
    ULONG ThreadCount = 0;
    CONTEXT OriginalContext;
    ETHREAD OriginalThread;
    PVOID OriginalThreadId;

    KdbpResetContextRecord(FALSE);
    if (!NT_SUCCESS(KdbpSafeReadMemory(&OriginalThread, KdbOriginalThread, sizeof(OriginalThread))) ||
        !NT_SUCCESS(KdbpSafeReadMemory(&ProcessHead, &PsActiveProcessHead, sizeof(ProcessHead))))
    {
        KdbpPrint("bt all: Cannot read the debugger-entry thread or process list.\n");
        return;
    }
    OriginalThreadId = OriginalThread.Cid.UniqueThread;
    if (KdbCurrentThread != KdbOriginalThread &&
        !KdbpAttachToThread(OriginalThreadId))
    {
        KdbpPrint("bt all: Failed to restore the debugger-entry thread.\n");
        return;
    }
    OriginalContext = *KdbCurrentTrapFrame;

    ProcessEntry = ProcessHead.Flink;
    while (ProcessEntry != &PsActiveProcessHead &&
           ProcessEntry != NULL &&
           ProcessCount++ < KDB_MAX_BACKTRACE_PROCESSES)
    {
        PEPROCESS Process;
        EPROCESS ProcessSnapshot;
        LIST_ENTRY ProcessLinks;
        PLIST_ENTRY ThreadEntry;
        PLIST_ENTRY ThreadListHead;
        ULONG ProcessThreadCount = 0;

        if (!NT_SUCCESS(KdbpSafeReadMemory(&ProcessLinks, ProcessEntry, sizeof(ProcessLinks))))
        {
            KdbpPrint("bt all: Unreadable process list entry %p.\n", ProcessEntry);
            goto Cleanup;
        }
        Process = CONTAINING_RECORD(ProcessEntry, EPROCESS, ActiveProcessLinks);
        if (!NT_SUCCESS(KdbpSafeReadMemory(&ProcessSnapshot, Process, sizeof(ProcessSnapshot))))
        {
            KdbpPrint("bt all: Unreadable EPROCESS %p.\n", Process);
            goto Cleanup;
        }
        ProcessSnapshot.ImageFileName[RTL_NUMBER_OF(ProcessSnapshot.ImageFileName) - 1] = ANSI_NULL;
        ThreadEntry = ProcessSnapshot.ThreadListHead.Flink;
        ThreadListHead = &Process->ThreadListHead;
        while (ThreadEntry != ThreadListHead &&
               ThreadEntry != NULL &&
               ThreadCount++ < KDB_MAX_BACKTRACE_THREADS &&
               ProcessThreadCount++ < KDB_MAX_BACKTRACE_THREADS)
        {
            PETHREAD Thread;
            ETHREAD ThreadSnapshot;
            LIST_ENTRY ThreadLinks;
            PVOID ThreadId;
            CONTEXT Context;

            if (!NT_SUCCESS(KdbpSafeReadMemory(&ThreadLinks, ThreadEntry, sizeof(ThreadLinks))))
            {
                KdbpPrint("bt all: Unreadable thread list entry %p.\n", ThreadEntry);
                goto Cleanup;
            }
            Thread = CONTAINING_RECORD(ThreadEntry, ETHREAD, ThreadListEntry);
            if (!NT_SUCCESS(KdbpSafeReadMemory(&ThreadSnapshot, Thread, sizeof(ThreadSnapshot))))
            {
                KdbpPrint("bt all: Unreadable ETHREAD %p.\n", Thread);
                goto Cleanup;
            }
            ThreadId = ThreadSnapshot.Cid.UniqueThread;
            KdbpPrint("\nProcess %p %.15s  thread %p  ETHREAD %p\n", ProcessSnapshot.UniqueProcessId, ProcessSnapshot.ImageFileName, ThreadId, Thread);

            if (!KdbpAttachToThread(ThreadId))
            {
                KdbpPrint("bt all: Could not attach to thread %p.\n", ThreadId);
            }
            else if (!KdbpCaptureFrozenThreadContext(Thread, &OriginalContext, &Context))
            {
                KdbpPrint("bt all: Stable context is unavailable for running thread %p.\n", ThreadId);
            }
            else
            {
                KdbpPrintBackTraceContext(&Context, Verbose);
            }

            if (KdbOutputAborted)
                goto Cleanup;
            if (ThreadLinks.Flink == ThreadEntry)
            {
                KdbpPrint("bt all: Self-linked thread entry %p.\n", ThreadEntry);
                goto Cleanup;
            }
            ThreadEntry = ThreadLinks.Flink;
        }

        if (ProcessThreadCount == KDB_MAX_BACKTRACE_THREADS)
        {
            KdbpPrint("bt all: Corrupt or excessive thread list in process %p.\n", ProcessSnapshot.UniqueProcessId);
            goto Cleanup;
        }
        if (ProcessLinks.Flink == ProcessEntry)
        {
            KdbpPrint("bt all: Self-linked process entry %p.\n", ProcessEntry);
            goto Cleanup;
        }
        ProcessEntry = ProcessLinks.Flink;
    }

    if (ProcessCount == KDB_MAX_BACKTRACE_PROCESSES ||
        ThreadCount == KDB_MAX_BACKTRACE_THREADS)
    {
        KdbpPrint("bt all: Enumeration stopped at its safety limit.\n");
    }

Cleanup:
    if (!KdbpAttachToThread(OriginalThreadId))
        KdbpPrint("bt all: WARNING: Could not restore debugger-entry thread %p.\n", OriginalThreadId);
}

static BOOLEAN
KdbpCmdFrame(ULONG Argc, PCHAR Argv[])
{
    CONTEXT Context;
    ULONG FrameNumber;
    ULONG Index;
    PCHAR End;

    if (Argc == 1)
    {
        KdbpPrint("Selected frame %lu: SP=%p FP=%p PC=%p ", KdbSelectedFrame, (PVOID)KeGetContextStackRegister(KdbCurrentTrapFrame), (PVOID)KeGetContextFrameRegister(KdbCurrentTrapFrame), (PVOID)KeGetContextPc(KdbCurrentTrapFrame));
        if (!KdbSymPrintAddress((PVOID)KeGetContextPc(KdbCurrentTrapFrame), KdbCurrentTrapFrame))
        {
            KdbpPrint("<%p>", (PVOID)KeGetContextPc(KdbCurrentTrapFrame));
        }
        KdbpPrint("\n");
        return TRUE;
    }
    if (Argc != 2)
    {
        KdbpPrint("Usage: .frame [number]\n");
        return TRUE;
    }

    FrameNumber = strtoul(Argv[1], &End, 0);
    if (End == Argv[1] || *End != ANSI_NULL ||
        FrameNumber >= KDB_MAX_BACKTRACE_FRAMES)
    {
        KdbpPrint(".frame: Invalid frame number '%s' (maximum %lu).\n", Argv[1], KDB_MAX_BACKTRACE_FRAMES - 1);
        return TRUE;
    }

    if (!KdbFrameBaseValid)
    {
        KdbFrameBaseContext = *KdbCurrentTrapFrame;
        KdbFrameBaseValid = TRUE;
    }
    Context = KdbFrameBaseContext;
    for (Index = 0; Index < FrameNumber; Index++)
    {
        if (!GetNextFrame(&Context))
        {
            KdbpPrint(".frame: Frame %lu is unavailable; unwind stopped at frame %lu.\n", FrameNumber, Index);
            return TRUE;
        }
    }

    if (!KdbContextRecordActive)
        KdbSavedTrapFrame = KdbCurrentTrapFrame;
    KdbSavedContextRecord = Context;
    KdbCurrentTrapFrame = (PKDB_KTRAP_FRAME)&KdbSavedContextRecord;
    KdbContextRecordActive = TRUE;
    KdbSelectedFrame = FrameNumber;

    KdbpPrint("Selected frame %lu (inspection only):\n", FrameNumber);
    KdbpPrintContext(&KdbSavedContextRecord);
    if (!KdbSymPrintAddress((PVOID)KeGetContextPc(&KdbSavedContextRecord), &KdbSavedContextRecord))
    {
        KdbpPrint("<%p>", (PVOID)KeGetContextPc(&KdbSavedContextRecord));
    }
    KdbpPrint("\n");
    return TRUE;
}

static BOOLEAN
KdbpCmdBackTrace(ULONG Argc, PCHAR Argv[])
{
    CONTEXT Context = *KdbCurrentTrapFrame;
    PCHAR End;
    ULONG_PTR Value;
    BOOLEAN Verbose = FALSE;

    if (Argc >= 2 &&
        (_stricmp(Argv[Argc - 1], "verbose") == 0 ||
         _stricmp(Argv[Argc - 1], "v") == 0))
    {
        Verbose = TRUE;
        Argc--;
    }

    if (Argc == 2 && _stricmp(Argv[1], "all") == 0)
    {
        KdbpPrintAllThreadBackTraces(Verbose);
        return TRUE;
    }

    if (Argc > 2)
    {
        KdbpPrint("Usage: bt [all|verbose|*frameaddr|thread id] [verbose]\n");
        return TRUE;
    }

    if (Argc == 2 && Argv[1][0] == '*')
    {
        ULONGLONG Result;

        if (!KdbpEvaluateExpression(Argv[1] + 1, KdbPromptStr.Length + (Argv[1] + 1 - Argv[0]), &Result))
            return TRUE;
        if (Result > (ULONGLONG)MAXULONG_PTR)
        {
            KdbpPrint("Address 0x%I64x does not fit in a pointer.\n", Result);
            return TRUE;
        }

        Value = (ULONG_PTR)Result;
#ifdef _M_AMD64
        {
            ULONG64 FrameRecord[2];

            if (Value > MAXULONG_PTR - sizeof(FrameRecord) || !NT_SUCCESS(KdbpSafeReadMemory(FrameRecord, (PVOID)Value, sizeof(FrameRecord))))
            {
                KdbpPrint("Couldn't read the AMD64 frame at %p.\n", (PVOID)Value);
                return TRUE;
            }
            Context.Rbp = FrameRecord[0];
            Context.Rip = FrameRecord[1];
            Context.Rsp = Value + sizeof(FrameRecord);
        }
#else
        {
            ULONG64 FrameRecord[2];

            if (Value > MAXULONG_PTR - sizeof(FrameRecord) || !NT_SUCCESS(KdbpSafeReadMemory(FrameRecord, (PVOID)Value, sizeof(FrameRecord))))
            {
                KdbpPrint("Couldn't read the ARM64 frame at %p.\n", (PVOID)Value);
                return TRUE;
            }
            Context.Fp = FrameRecord[0];
            Context.Lr = Context.Pc = FrameRecord[1];
            Context.Sp = Value + sizeof(FrameRecord);
        }
#endif
    }
    else if (Argc == 2)
    {
        Value = strtoulptr(Argv[1], &End, 0);
        if (End == Argv[1] || *End != '\0')
        {
            KdbpPrint("bt: '%s' is not a valid thread id.\n", Argv[1]);
            return TRUE;
        }

        KdbpResetContextRecord(FALSE);
        if (!KdbpAttachToThread((PVOID)Value))
        {
            return TRUE;
        }
        Context = *KdbCurrentTrapFrame;
    }

    KdbpPrintBackTraceContext(&Context, Verbose);

    return TRUE;
}
#else
static BOOLEAN
KdbpCmdFrame(ULONG Argc, PCHAR Argv[])
{
    UNREFERENCED_PARAMETER(Argc);
    UNREFERENCED_PARAMETER(Argv);
    KdbpPrint(".frame is not implemented on this architecture.\n");
    return TRUE;
}

/*!\brief Displays a backtrace.
 */
static BOOLEAN
KdbpCmdBackTrace(ULONG Argc, PCHAR Argv[])
{
    ULONG ul;
    ULONGLONG Result = 0;
    CONTEXT Context = *KdbCurrentTrapFrame;
    ULONG_PTR Frame = KeGetContextFrameRegister(&Context);
    ULONG_PTR Address;

    if (Argc >= 2)
    {
        /* Check for [L count] part */
        ul = 0;
        if (strcmp(Argv[Argc-2], "L") == 0)
        {
            ul = strtoul(Argv[Argc-1], NULL, 0);
            if (ul > 0)
            {
                Argc -= 2;
            }
        }
        else if (Argv[Argc-1][0] == 'L')
        {
            ul = strtoul(Argv[Argc-1] + 1, NULL, 0);
            if (ul > 0)
            {
                Argc--;
            }
        }

        /* Put the remaining arguments back together */
        Argc--;
        for (ul = 1; ul < Argc; ul++)
        {
            Argv[ul][strlen(Argv[ul])] = ' ';
        }
        Argc++;
    }

    /* Check if a Frame Address or Thread ID is given */
    if (Argc > 1)
    {
        if (Argv[1][0] == '*')
        {
            Argv[1]++;

            /* Evaluate the expression */
            if (!KdbpEvaluateExpression(Argv[1], KdbPromptStr.Length + (Argv[1]-Argv[0]), &Result))
                return TRUE;

            if (Result > (ULONGLONG)MAXULONG_PTR)
            {
                KdbpPrint("Address 0x%I64x does not fit in a pointer.\n", Result);
                return TRUE;
            }

            Frame = (ULONG_PTR)Result;
        }
        else
        {
            KdbpPrint("Thread backtrace not supported yet!\n");
            return TRUE;
        }
    }

#ifdef _M_IX86
    KDESCRIPTOR Gdtr;
    USHORT TssSelector;
    PKTSS Tss;

    /* Retrieve the Global Descriptor Table */
    Ke386GetGlobalDescriptorTable(&Gdtr.Limit);

    /* Retrieve the current (active) TSS */
    TssSelector = Ke386GetTr();
    Tss = KdbpRetrieveTss(TssSelector, NULL, &Gdtr);
    if (KdbpIsNestedTss(TssSelector, Tss))
    {
        /* Display the active TSS if it is nested */
        KdbpPrint("[Active TSS 0x%04x @ 0x%p]\n", TssSelector, Tss);
    }
#endif

    /* If no Frame Address or Thread ID was given, try printing the function at EIP */
    if (Argc <= 1)
    {
        KdbpPrint("Eip:\n");
        if (!KdbSymPrintAddress((PVOID)KeGetContextPc(&Context), &Context))
            KdbpPrint("<%p>\n", KeGetContextPc(&Context));
        else
            KdbpPrint("\n");
    }

    /* Walk through the frames */
    KdbpPrint("Frames:\n");
    for (;;)
    {
        BOOLEAN GotNextFrame;

        if (Frame == 0)
            goto CheckForParentTSS;

        Address = 0;
        if (!NT_SUCCESS(KdbpSafeReadMemory(&Address, (PVOID)(Frame + sizeof(ULONG_PTR)), sizeof(ULONG_PTR))))
        {
            KdbpPrint("Couldn't access memory at 0x%p!\n", Frame + sizeof(ULONG_PTR));
            goto CheckForParentTSS;
        }

        if (Address == 0)
            goto CheckForParentTSS;

        GotNextFrame = NT_SUCCESS(KdbpSafeReadMemory(&Frame, (PVOID)Frame, sizeof(ULONG_PTR)));
        if (GotNextFrame)
        {
            KeSetContextFrameRegister(&Context, Frame);
        }
        // else
            // Frame = 0;

        /* Print the location of the call instruction (assumed 5 bytes length) */
        if (!KdbSymPrintAddress((PVOID)(Address - 5), &Context))
            KdbpPrint("<%p>\n", (PVOID)Address);
        else
            KdbpPrint("\n");

        if (KdbOutputAborted)
            break;

        if (!GotNextFrame)
        {
            KdbpPrint("Couldn't access memory at 0x%p!\n", Frame);
            goto CheckForParentTSS; // break;
        }

        continue;

CheckForParentTSS:
#ifndef _M_IX86
        break;
#else
        /*
         * We have ended the stack walking for the current (active) TSS.
         * Check whether this TSS was nested, and if so switch to its parent
         * and walk its stack.
         */
        if (!KdbpIsNestedTss(TssSelector, Tss))
            break; // The TSS is not nested, we stop there.

        GotNextFrame = KdbpContextFromPrevTss(&Context, &TssSelector, &Tss, &Gdtr);
        if (!GotNextFrame)
        {
            KdbpPrint("Couldn't access parent TSS 0x%04x\n", Tss->Backlink);
            break; // Cannot retrieve the parent TSS, we stop there.
        }


        Address = Context.Eip;
        Frame = Context.Ebp;

        KdbpPrint("[Parent TSS 0x%04x @ 0x%p]\n", TssSelector, Tss);

        if (!KdbSymPrintAddress((PVOID)Address, &Context))
            KdbpPrint("<%p>\n", (PVOID)Address);
        else
            KdbpPrint("\n");
#endif
    }

    return TRUE;
}

#endif // _M_AMD64 || _M_ARM64

/*!\brief Continues execution of the system/leaves KDB.
 */
static BOOLEAN
KdbpCmdContinue(ULONG Argc, PCHAR Argv[])
{
    /* Exit the main loop */
    return FALSE;
}

/*!\brief Continues execution of the system/leaves KDB.
 */
static BOOLEAN
KdbpCmdStep(ULONG Argc, PCHAR Argv[])
{
#if defined(_M_ARM64)
    /* KdbNumSingleSteps has no consumer on ARM64 yet; without this message
     * the command would silently behave like "cont". */
    KdbpPrint("Single-stepping is not supported on ARM64 yet.\n");
    return TRUE;
#else
    ULONG Count = 1;

    if (Argc > 1)
    {
        Count = strtoul(Argv[1], NULL, 0);
        if (Count == 0)
        {
            KdbpPrint("%s: Integer argument required\n", Argv[0]);
            return TRUE;
        }
    }

    if (Argv[0][0] == 'n')
        KdbSingleStepOver = TRUE;
    else
        KdbSingleStepOver = FALSE;

    /* Set the number of single steps and return to the interrupted code. */
    KdbNumSingleSteps = Count;

    return FALSE;
#endif
}

/*!\brief Lists breakpoints.
 */
static BOOLEAN
KdbpCmdBreakPointList(ULONG Argc, PCHAR Argv[])
{
    LONG l;
    ULONG_PTR Address = 0;
    KDB_BREAKPOINT_TYPE Type = 0;
    KDB_ACCESS_TYPE AccessType = 0;
    UCHAR Size = 0;
    UCHAR DebugReg = 0;
    BOOLEAN Enabled = FALSE;
    BOOLEAN Global = FALSE;
    PEPROCESS Process = NULL;
    PCHAR str1, str2, ConditionExpr, GlobalOrLocal;
    CHAR Buffer[20];

    l = KdbpGetNextBreakPointNr(0);
    if (l < 0)
    {
        KdbpPrint("No breakpoints.\n");
        return TRUE;
    }

    KdbpPrint("Breakpoints:\n");
    do
    {
        if (!KdbpGetBreakPointInfo(l, &Address, &Type, &Size, &AccessType, &DebugReg, &Enabled, &Global, &Process, &ConditionExpr))
        {
            continue;
        }

        if (l == KdbLastBreakPointNr)
        {
            str1 = "\x1b[1m*";
            str2 = "\x1b[0m";
        }
        else
        {
            str1 = " ";
            str2 = "";
        }

        if (Global)
        {
            GlobalOrLocal = "  global";
        }
        else
        {
            GlobalOrLocal = Buffer;
            sprintf(Buffer, "  PID 0x%Ix", (ULONG_PTR)(Process ? Process->UniqueProcessId : INVALID_HANDLE_VALUE));
        }

        if (Type == KdbBreakPointSoftware || Type == KdbBreakPointTemporary)
        {
            KdbpPrint(" %s%03d  BPX  0x%p%s%s%s%s%s\n", str1, l, (PVOID)Address, Enabled ? "" : "  disabled", GlobalOrLocal, ConditionExpr ? "  IF " : "", ConditionExpr ? ConditionExpr : "", str2);
        }
        else if (Type == KdbBreakPointHardware)
        {
            if (!Enabled)
            {
                KdbpPrint(" %s%03d  BPM  0x%p  %-5s %-5s  disabled%s%s%s%s\n", str1, l, (PVOID)Address,
                          KDB_ACCESS_TYPE_TO_STRING(AccessType),
                          Size == 1 ? "byte" : (Size == 2 ? "word" : (Size == 4 ? "dword" : "qword")),
                          GlobalOrLocal,
                          ConditionExpr ? "  IF " : "",
                          ConditionExpr ? ConditionExpr : "",
                          str2);
            }
            else
            {
                KdbpPrint(" %s%03d  BPM  0x%p  %-5s %-5s  DR%d%s%s%s%s\n", str1, l, (PVOID)Address,
                          KDB_ACCESS_TYPE_TO_STRING(AccessType),
                          Size == 1 ? "byte" : (Size == 2 ? "word" : (Size == 4 ? "dword" : "qword")),
                          DebugReg,
                          GlobalOrLocal,
                          ConditionExpr ? "  IF " : "",
                          ConditionExpr ? ConditionExpr : "",
                          str2);
            }
        }
    }
    while ((l = KdbpGetNextBreakPointNr(l+1)) >= 0);

    return TRUE;
}

/*!\brief Enables, disables or clears a breakpoint.
 */
static BOOLEAN
KdbpCmdEnableDisableClearBreakPoint(ULONG Argc, PCHAR Argv[])
{
    PCHAR pend;
    ULONG BreakPointNr;

    if (Argc < 2)
    {
        KdbpPrint("%s: argument required\n", Argv[0]);
        return TRUE;
    }

    pend = Argv[1];
    BreakPointNr = strtoul(Argv[1], &pend, 0);
    if (pend == Argv[1] || *pend != '\0')
    {
        KdbpPrint("%s: integer argument required\n", Argv[0]);
        return TRUE;
    }

    if (Argv[0][1] == 'e') /* enable */
    {
        KdbpEnableBreakPoint(BreakPointNr, NULL);
    }
    else if (Argv [0][1] == 'd') /* disable */
    {
        KdbpDisableBreakPoint(BreakPointNr, NULL);
    }
    else /* clear */
    {
        ASSERT(Argv[0][1] == 'c');
        KdbpDeleteBreakPoint(BreakPointNr, NULL);
    }

    return TRUE;
}

/*!\brief Sets a software or hardware (memory) breakpoint at the given address.
 */
static BOOLEAN
KdbpCmdBreakPoint(ULONG Argc, PCHAR Argv[])
{
    ULONGLONG Result = 0;
    ULONG_PTR Address;
    KDB_BREAKPOINT_TYPE Type;
    UCHAR Size = 0;
    KDB_ACCESS_TYPE AccessType = 0;
    ULONG AddressArgIndex, i;
    LONG ConditionArgIndex;
    BOOLEAN Global = TRUE;

    if (Argv[0][2] == 'x') /* software breakpoint */
    {
        if (Argc < 2)
        {
            KdbpPrint("bpx: Address argument required.\n");
            return TRUE;
        }

        AddressArgIndex = 1;
        Type = KdbBreakPointSoftware;
    }
    else /* memory breakpoint */
    {
        ASSERT(Argv[0][2] == 'm');

        if (Argc < 2)
        {
            KdbpPrint("bpm: Access type argument required (one of r, w, rw, x)\n");
            return TRUE;
        }

        if (_stricmp(Argv[1], "x") == 0)
            AccessType = KdbAccessExec;
        else if (_stricmp(Argv[1], "r") == 0)
            AccessType = KdbAccessRead;
        else if (_stricmp(Argv[1], "w") == 0)
            AccessType = KdbAccessWrite;
        else if (_stricmp(Argv[1], "rw") == 0)
            AccessType = KdbAccessReadWrite;
        else
        {
            KdbpPrint("bpm: Unknown access type '%s'\n", Argv[1]);
            return TRUE;
        }

        if (Argc < 3)
        {
            KdbpPrint("bpm: %s argument required.\n", AccessType == KdbAccessExec ? "Address" : "Memory size");
            return TRUE;
        }

        AddressArgIndex = 3;
        if (_stricmp(Argv[2], "byte") == 0)
            Size = 1;
        else if (_stricmp(Argv[2], "word") == 0)
            Size = 2;
        else if (_stricmp(Argv[2], "dword") == 0)
            Size = 4;
        else if (_stricmp(Argv[2], "qword") == 0)
        {
#if defined(_M_AMD64) || defined(_M_ARM64)
            Size = 8;
#else
            KdbpPrint("bpm: qword watchpoints require AMD64 or ARM64.\n");
            return TRUE;
#endif
        }
        else if (AccessType == KdbAccessExec)
        {
            Size = 1;
            AddressArgIndex--;
        }
        else
        {
            KdbpPrint("bpm: Unknown memory size '%s'\n", Argv[2]);
            return TRUE;
        }

        if (Argc <= AddressArgIndex)
        {
            KdbpPrint("bpm: Address argument required.\n");
            return TRUE;
        }

        Type = KdbBreakPointHardware;
    }

    /* Put the arguments back together */
    ConditionArgIndex = -1;
    for (i = AddressArgIndex; i < (Argc-1); i++)
    {
        if (strcmp(Argv[i+1], "IF") == 0) /* IF found */
        {
            ConditionArgIndex = i + 2;
            if ((ULONG)ConditionArgIndex >= Argc)
            {
                KdbpPrint("%s: IF requires condition expression.\n", Argv[0]);
                return TRUE;
            }

            for (i = ConditionArgIndex; i < (Argc-1); i++)
                Argv[i][strlen(Argv[i])] = ' ';

            break;
        }

        Argv[i][strlen(Argv[i])] = ' ';
    }

    /* Evaluate the address expression */
    if (!KdbpEvaluateExpression(Argv[AddressArgIndex], KdbPromptStr.Length + (Argv[AddressArgIndex]-Argv[0]), &Result))
    {
        return TRUE;
    }

    if (Result > (ULONGLONG)MAXULONG_PTR)
    {
        KdbpPrint("%s: Address 0x%I64x does not fit in a pointer.\n", Argv[0], Result);
        return TRUE;
    }

    Address = (ULONG_PTR)Result;

    KdbpInsertBreakPoint(Address, Type, Size, AccessType, (ConditionArgIndex < 0) ? NULL : Argv[ConditionArgIndex], Global, NULL);

    return TRUE;
}

static PCSTR
KdbpThreadStateName(IN ULONG State)
{
    static const PCSTR Names[] =
    {
        "Initialized", "Ready", "Running", "Standby", "Terminated",
        "Waiting", "Transition", "DeferredReady"
    };

    return State < RTL_NUMBER_OF(Names) ? Names[State] : "Unknown";
}

static PCSTR
KdbpWaitReasonName(IN ULONG Reason)
{
    static const PCSTR Names[] =
    {
        "Executive", "FreePage", "PageIn", "PoolAllocation",
        "DelayExecution", "Suspended", "UserRequest", "WrExecutive",
        "WrFreePage", "WrPageIn", "WrPoolAllocation", "WrDelayExecution",
        "WrSuspended", "WrUserRequest", "WrEventPair", "WrQueue",
        "WrLpcReceive", "WrLpcReply", "WrVirtualMemory", "WrPageOut",
        "WrRendezvous", "WrKeyedEvent", "WrTerminated", "WrProcessInSwap",
        "WrCpuRateControl", "WrCalloutStack", "WrKernel", "WrResource",
        "WrPushLock", "WrMutex", "WrQuantumEnd", "WrDispatchInt",
        "WrPreempted", "WrYieldExecution", "WrFastMutex", "WrGuardedMutex",
        "WrRundown", "WrAlertByThreadId", "WrDeferredPreempt"
    };

    return Reason < RTL_NUMBER_OF(Names) ? Names[Reason] : "Unknown";
}

static LONG
KdbpFindThreadProcessor(IN PETHREAD Thread, OUT PCONTEXT Context OPTIONAL)
{
    ULONG Processor;

    for (Processor = 0; Processor < (ULONG)(UCHAR)KeNumberProcessors; Processor++)
    {
        PKPRCB Prcb = KiProcessorBlock[Processor];
        PETHREAD CurrentThread;

        if (Prcb == NULL ||
            !NT_SUCCESS(KdbpSafeReadMemory(&CurrentThread, &Prcb->CurrentThread, sizeof(CurrentThread))) ||
            CurrentThread != Thread)
        {
            continue;
        }

        if (Context != NULL &&
            !NT_SUCCESS(KdbpSafeReadMemory(Context, &Prcb->ProcessorState.ContextFrame, sizeof(*Context))))
        {
            return -1;
        }
        return (LONG)Processor;
    }
    return -1;
}

static BOOLEAN
KdbpGetThreadLocation(IN PETHREAD Thread, IN PETHREAD Snapshot, OUT PULONG_PTR Stack, OUT PULONG_PTR Frame, OUT PULONG_PTR Pc)
{
    CONTEXT Context;
    LONG Processor;

    *Stack = *Frame = *Pc = 0;
    if (Thread == KdbCurrentThread)
    {
        Context = *KdbCurrentTrapFrame;
    }
    else if ((Processor = KdbpFindThreadProcessor(Thread, &Context)) >= 0)
    {
        UNREFERENCED_PARAMETER(Processor);
    }
    else if (Snapshot->Tcb.TrapFrame != NULL)
    {
        KTRAP_FRAME TrapFrame;

        if (!NT_SUCCESS(KdbpSafeReadMemory(&TrapFrame, Snapshot->Tcb.TrapFrame, sizeof(TrapFrame))))
        {
            return FALSE;
        }
        *Stack = KeGetTrapFrameStackRegister(&TrapFrame);
        *Frame = KeGetTrapFrameFrameRegister(&TrapFrame);
        *Pc = KeGetTrapFramePc(&TrapFrame);
        return *Pc != 0;
    }
    else
    {
        if (Snapshot->Tcb.KernelStack == NULL)
            return FALSE;
        if (!KdbpKdbTrapFrameFromKernelStack(Snapshot->Tcb.KernelStack, (PKDB_KTRAP_FRAME)&Context))
        {
            return FALSE;
        }
    }

    *Stack = KeGetContextStackRegister(&Context);
    *Frame = KeGetContextFrameRegister(&Context);
    *Pc = KeGetContextPc(&Context);
    return *Pc != 0;
}

static VOID
KdbpPrintThreadWaitBlocks(IN PETHREAD Thread)
{
#if (NTDDI_VERSION >= NTDDI_WIN8) || defined(_M_ARM64)
    ULONG Count = min((ULONG)Thread->Tcb.WaitBlockCount, 64UL);
    ULONG Index;
    PKWAIT_BLOCK WaitBlock = Thread->Tcb.WaitBlockList;

    if (WaitBlock == NULL || Count == 0)
        return;

    for (Index = 0; Index < Count; Index++)
    {
        KWAIT_BLOCK Block;

        if (!NT_SUCCESS(KdbpSafeReadMemory(&Block, WaitBlock + Index, sizeof(Block))))
        {
            KdbpPrint("  Wait[%lu]:       unreadable at %p\n", Index, WaitBlock + Index);
            break;
        }
        KdbpPrint("  Wait[%lu]:       block %p object %p key 0x%x type %u state %u\n", Index, WaitBlock + Index, Block.Object, Block.WaitKey, Block.WaitType, Block.BlockState);
    }
    if (Thread->Tcb.WaitBlockCount > Count)
        KdbpPrint("  Wait blocks:     truncated at %lu entries\n", Count);
#else
    PKWAIT_BLOCK FirstWaitBlock = Thread->Tcb.WaitBlockList;
    PKWAIT_BLOCK WaitBlock = FirstWaitBlock;
    ULONG Index;

    if (WaitBlock == NULL)
        return;

    for (Index = 0; Index < 64; Index++)
    {
        KWAIT_BLOCK Block;

        if (!NT_SUCCESS(KdbpSafeReadMemory(&Block, WaitBlock, sizeof(Block))))
        {
            KdbpPrint("  Wait[%lu]:       unreadable at %p\n", Index, WaitBlock);
            return;
        }
        KdbpPrint("  Wait[%lu]:       block %p object %p key 0x%x type %u state %u\n", Index, WaitBlock, Block.Object, Block.WaitKey, Block.WaitType, Block.BlockState);
        WaitBlock = Block.NextWaitBlock;
        if (WaitBlock == FirstWaitBlock)
            return;
        if (WaitBlock == NULL)
        {
            KdbpPrint("  Wait blocks:     null link after %lu entries\n", Index + 1);
            return;
        }
    }
    KdbpPrint("  Wait blocks:     truncated at %lu entries\n", Index);
#endif
}

/*!\brief Lists threads or switches to another thread context.
 */
static BOOLEAN
KdbpCmdThread(ULONG Argc, PCHAR Argv[])
{
    PLIST_ENTRY Entry;
    PETHREAD Thread = NULL;
    PEPROCESS Process = NULL;
    EPROCESS ProcessSnapshot;
    ETHREAD ThreadSnapshot;
    ULONG_PTR Stack;
    ULONG_PTR Frame;
    ULONG_PTR Pc;
    ULONG_PTR ul = 0;
    ULONG ThreadCount;
    PCHAR pend, str1, str2;
    BOOLEAN HaveLocation;

    ASSERT(KdbCurrentProcess);

    if (Argc >= 2 && _stricmp(Argv[1], "list") == 0)
    {
        if (Argc > 3)
        {
            KdbpPrint("Usage: thread list [pid]\n");
            return TRUE;
        }
        Process = KdbCurrentProcess;

        if (Argc >= 3)
        {
            ul = strtoulptr(Argv[2], &pend, 0);
            if (Argv[2] == pend || *pend != '\0')
            {
                KdbpPrint("thread: '%s' is not a valid process id!\n", Argv[2]);
                return TRUE;
            }

            if (!KdbpFindProcessById((PVOID)ul, &Process))
            {
                KdbpPrint("thread: Invalid process id!\n");
                return TRUE;
            }

        }

        if (!NT_SUCCESS(KdbpSafeReadMemory(&ProcessSnapshot, Process, sizeof(ProcessSnapshot))))
        {
            KdbpPrint("thread: Cannot read process %p.\n", Process);
            return TRUE;
        }

        Entry = ProcessSnapshot.ThreadListHead.Flink;
        if (Entry == &Process->ThreadListHead)
        {
            if (Argc >= 3)
                KdbpPrint("No threads in process %p!\n", (PVOID)ul);
            else
                KdbpPrint("No threads in current process!\n");

            return TRUE;
        }

        KdbpPrint("  TID               ETHREAD            CPU  State        Pri  Stack              Frame              PC\n");
        ThreadCount = 0;
        while (Entry != NULL &&
               Entry != &Process->ThreadListHead &&
               ThreadCount++ < 65536)
        {
            LIST_ENTRY Links;
            LONG Processor;

            if (!NT_SUCCESS(KdbpSafeReadMemory(&Links, Entry, sizeof(Links))))
            {
                KdbpPrint("thread: Thread list entry %p is unreadable.\n", Entry);
                break;
            }
            Thread = CONTAINING_RECORD(Entry, ETHREAD, ThreadListEntry);
            if (!NT_SUCCESS(KdbpSafeReadMemory(&ThreadSnapshot, Thread, sizeof(ThreadSnapshot))))
            {
                KdbpPrint("thread: ETHREAD %p is unreadable.\n", Thread);
                break;
            }

            if (Thread == KdbCurrentThread)
            {
                str1 = "\x1b[1m*";
                str2 = "\x1b[0m";
            }
            else
            {
                str1 = " ";
                str2 = "";
            }

            HaveLocation = KdbpGetThreadLocation(Thread, &ThreadSnapshot, &Stack, &Frame, &Pc);
            Processor = KdbpFindThreadProcessor(Thread, NULL);

            KdbpPrint(" %s%p  %p  %3ld  %-11s %3d  %p  %p  %p%s%s\n",
                      str1,
                      ThreadSnapshot.Cid.UniqueThread,
                      Thread,
                      Processor,
                      KdbpThreadStateName(ThreadSnapshot.Tcb.State),
                      ThreadSnapshot.Tcb.Priority,
                      (PVOID)Stack,
                      (PVOID)Frame,
                      (PVOID)Pc,
                      HaveLocation ? "" : " [context unavailable]",
                      str2);

            if (Links.Flink == Entry)
            {
                KdbpPrint("thread: Self-linked thread entry %p; stopping.\n", Entry);
                break;
            }
            Entry = Links.Flink;
            if (KdbOutputAborted)
                break;
        }
        if (ThreadCount >= 65536)
            KdbpPrint("thread: Enumeration stopped at the 65536-thread safety limit.\n");

    }
    else if (Argc >= 2 && _stricmp(Argv[1], "attach") == 0)
    {
        if (Argc != 3)
        {
            KdbpPrint("Usage: thread attach tid\n");
            return TRUE;
        }

        ul = strtoulptr(Argv[2], &pend, 0);
        if (Argv[2] == pend || *pend != '\0')
        {
            KdbpPrint("thread attach: '%s' is not a valid thread id!\n", Argv[2]);
            return TRUE;
        }

        KdbpResetContextRecord(FALSE);
        if (!KdbpAttachToThread((PVOID)ul))
        {
            return TRUE;
        }

        KdbpPrint("Attached to thread %p.\n", (PVOID)ul);
    }
    else
    {
        if (Argc > 2)
        {
            KdbpPrint("Usage: thread [tid]\n");
            return TRUE;
        }
        Thread = KdbCurrentThread;

        if (Argc >= 2)
        {
            ul = strtoulptr(Argv[1], &pend, 0);
            if (Argv[1] == pend || *pend != '\0')
            {
                KdbpPrint("thread: '%s' is not a valid thread id!\n", Argv[1]);
                return TRUE;
            }

            if (!KdbpFindThreadById((PVOID)ul, &Thread))
            {
                KdbpPrint("thread: Invalid thread id!\n");
                return TRUE;
            }

        }

        if (!NT_SUCCESS(KdbpSafeReadMemory(&ThreadSnapshot, Thread, sizeof(ThreadSnapshot))))
        {
            KdbpPrint("thread: Cannot read ETHREAD %p.\n", Thread);
            return TRUE;
        }

#ifdef _M_IX86
#define KDB_THREAD_NPX_FORMAT "  NPX State:       %s (0x%x)\n"
#define KDB_THREAD_NPX_ARGUMENTS , NPX_STATE_TO_STRING(ThreadSnapshot.Tcb.NpxState), ThreadSnapshot.Tcb.NpxState
#else
#define KDB_THREAD_NPX_FORMAT
#define KDB_THREAD_NPX_ARGUMENTS
#endif
#if (NTDDI_VERSION >= NTDDI_WIN8) || defined(_M_ARM64)
#define KDB_THREAD_PRIORITY_FORMAT "  Priority:        current %d, base %d, queue %ld\n"
#define KDB_THREAD_WAIT_FORMAT "  Wait:            %s (%u), status %p, blocks %u @ %p\n"
#else
#define KDB_THREAD_PRIORITY_FORMAT "  Priority:        current %d, base %d\n"
#define KDB_THREAD_WAIT_FORMAT "  Wait:            %s (%u), status %p, blocks @ %p\n"
#endif
        KdbpPrint("%s"
                  "  ETHREAD:         %p\n"
                  "  CID:             %p.%p\n"
                  "  State:           %s (0x%x), CPU %ld\n"
                  KDB_THREAD_PRIORITY_FORMAT
                  "  Affinity:        0x%Ix, user 0x%Ix\n"
                  "  TEB:             %p\n"
                  "  Start Address:   %p\n"
                  "  Win32 Start:     %p\n"
                  "  Create Time:     0x%I64x\n"
                  "  CPU Time:        kernel %I64u, user %I64u\n"
                  KDB_THREAD_WAIT_FORMAT
                  "  Suspend/Freeze:  %d / %d\n"
                  "  Initial Stack:   %p\n"
                  "  Stack Limit:     %p\n"
                  "  Stack Base:      %p\n"
                  "  Kernel Stack:    %p\n"
                  "  Trap Frame:      %p\n"
                  "  Flags:           cross 0x%08lx, passive 0x%08lx, apc 0x%08lx\n"
                  KDB_THREAD_NPX_FORMAT,
                  (Argc < 2) ? "Current Thread:\n" : "",
                  Thread,
                  ThreadSnapshot.Cid.UniqueProcess,
                  ThreadSnapshot.Cid.UniqueThread,
                  KdbpThreadStateName(ThreadSnapshot.Tcb.State),
                  ThreadSnapshot.Tcb.State,
                  KdbpFindThreadProcessor(Thread, NULL),
                  ThreadSnapshot.Tcb.Priority,
                  ThreadSnapshot.Tcb.BasePriority,
#if (NTDDI_VERSION >= NTDDI_WIN8) || defined(_M_ARM64)
                  ThreadSnapshot.Tcb.QueuePriority,
#endif
                  ThreadSnapshot.Tcb.Affinity,
                  ThreadSnapshot.Tcb.UserAffinity,
                  ThreadSnapshot.Tcb.Teb,
                  ThreadSnapshot.StartAddress,
                  ThreadSnapshot.Win32StartAddress,
                  ThreadSnapshot.CreateTime.QuadPart,
                  (ULONGLONG)ThreadSnapshot.Tcb.KernelTime,
                  (ULONGLONG)ThreadSnapshot.Tcb.UserTime,
                  KdbpWaitReasonName(ThreadSnapshot.Tcb.WaitReason),
                  ThreadSnapshot.Tcb.WaitReason,
                  (PVOID)ThreadSnapshot.Tcb.WaitStatus,
#if (NTDDI_VERSION >= NTDDI_WIN8) || defined(_M_ARM64)
                  ThreadSnapshot.Tcb.WaitBlockCount,
#endif
                  ThreadSnapshot.Tcb.WaitBlockList,
                  ThreadSnapshot.Tcb.SuspendCount,
                  ThreadSnapshot.Tcb.FreezeCount,
                  ThreadSnapshot.Tcb.InitialStack,
                  (PVOID)ThreadSnapshot.Tcb.StackLimit,
                  ThreadSnapshot.Tcb.StackBase,
                  ThreadSnapshot.Tcb.KernelStack,
                  ThreadSnapshot.Tcb.TrapFrame,
                  ThreadSnapshot.CrossThreadFlags,
                  ThreadSnapshot.SameThreadPassiveFlags,
                  ThreadSnapshot.SameThreadApcFlags
                  KDB_THREAD_NPX_ARGUMENTS);
#undef KDB_THREAD_NPX_FORMAT
#undef KDB_THREAD_NPX_ARGUMENTS
#undef KDB_THREAD_PRIORITY_FORMAT
#undef KDB_THREAD_WAIT_FORMAT

        if (ThreadSnapshot.Terminated)
        {
            KdbpPrint("  Exit:            time 0x%I64x, status 0x%08lx\n", ThreadSnapshot.ExitTime.QuadPart, ThreadSnapshot.ExitStatus);
        }

        if (ThreadSnapshot.StartAddress != NULL)
        {
            KdbpPrint("  Start Symbol:    ");
            if (!KdbSymPrintAddress(ThreadSnapshot.StartAddress, KdbCurrentTrapFrame))
                KdbpPrint("<%p>", ThreadSnapshot.StartAddress);
            KdbpPrint("\n");
        }
        if (ThreadSnapshot.Tcb.State == Waiting)
            KdbpPrintThreadWaitBlocks(&ThreadSnapshot);

    }

    return TRUE;
}

static BOOLEAN
KdbpGetSingleAddressArgument(IN PCSTR Command, IN ULONG Argc, IN PCHAR Argv[], OUT PULONG_PTR Address)
{
    if (Argc != 2)
    {
        KdbpPrint("Usage: %s address\n", Command);
        return FALSE;
    }
    return KdbpEvaluateAddress(Argv[1], KdbPromptStr.Length + (Argv[1] - Argv[0]), Address);
}

static VOID
KdbpPrintRoutineAddress(IN PVOID Address)
{
    KdbpPrint("%p", Address);
    if (Address != NULL)
    {
        KdbpPrint(" ");
        if (!KdbSymPrintAddress(Address, KdbCurrentTrapFrame))
            KdbpPrint("<unknown>");
    }
}

static BOOLEAN
KdbpCmdObject(ULONG Argc, PCHAR Argv[])
{
    ULONG_PTR Address;
    ULONG_PTR HeaderAddress;
    OBJECT_HEADER Header;
    OBJECT_TYPE Type;
    OBJECT_HEADER_NAME_INFO NameInfo;
    NTSTATUS Status;

    if (!KdbpGetSingleAddressArgument(Argv[0], Argc, Argv, &Address))
        return TRUE;
    if (Address < FIELD_OFFSET(OBJECT_HEADER, Body))
    {
        KdbpPrint("!object: %p cannot be an object body.\n", (PVOID)Address);
        return TRUE;
    }
    HeaderAddress = Address - FIELD_OFFSET(OBJECT_HEADER, Body);
    Status = KdbpSafeReadMemory(&Header, (PVOID)HeaderAddress, sizeof(Header));
    if (!NT_SUCCESS(Status))
    {
        KdbpPrint("!object: Header %p is unreadable (0x%08lx).\n", (PVOID)HeaderAddress, Status);
        return TRUE;
    }

    KdbpPrint("Object %p, header %p\n"
              "  Pointer/handle count: %Id / %Id\n"
              "  Type:                 %p\n"
              "  Flags:                0x%02x\n"
              "  Security descriptor:  %p\n",
              (PVOID)Address,
              (PVOID)HeaderAddress,
              Header.PointerCount,
              Header.HandleCount,
              Header.Type,
              Header.Flags,
              Header.SecurityDescriptor);

    if (Header.Type != NULL &&
        NT_SUCCESS(KdbpSafeReadMemory(&Type, Header.Type, sizeof(Type))))
    {
        KdbpPrint("  Type name:             ");
        KdbpPrintRemoteUnicodeString(&Type.Name);
        KdbpPrint(" (index %lu, objects %lu, handles %lu)\n", Type.Index, Type.TotalNumberOfObjects, Type.TotalNumberOfHandles);
    }
    else
    {
        KdbpPrint("  Type name:             <unreadable>\n");
    }

    if (Header.NameInfoOffset != 0 &&
        Header.NameInfoOffset <= HeaderAddress &&
        NT_SUCCESS(KdbpSafeReadMemory(&NameInfo, (PVOID)(HeaderAddress - Header.NameInfoOffset), sizeof(NameInfo))))
    {
        KdbpPrint("  Name:                  ");
        KdbpPrintRemoteUnicodeString(&NameInfo.Name);
        KdbpPrint("\n  Directory:             %p\n", NameInfo.Directory);
    }
    else
    {
        KdbpPrint("  Name:                  <unnamed or unreadable>\n");
    }
    return TRUE;
}

static PCSTR
KdbpIrpMajorName(IN UCHAR Major)
{
    static const PCSTR Names[] =
    {
        "CREATE", "CREATE_NAMED_PIPE", "CLOSE", "READ", "WRITE",
        "QUERY_INFORMATION", "SET_INFORMATION", "QUERY_EA", "SET_EA",
        "FLUSH_BUFFERS", "QUERY_VOLUME_INFORMATION", "SET_VOLUME_INFORMATION",
        "DIRECTORY_CONTROL", "FILE_SYSTEM_CONTROL", "DEVICE_CONTROL",
        "INTERNAL_DEVICE_CONTROL", "SHUTDOWN", "LOCK_CONTROL", "CLEANUP",
        "CREATE_MAILSLOT", "QUERY_SECURITY", "SET_SECURITY", "POWER",
        "SYSTEM_CONTROL", "DEVICE_CHANGE", "QUERY_QUOTA", "SET_QUOTA", "PNP"
    };

    return Major < RTL_NUMBER_OF(Names) ? Names[Major] : "UNKNOWN";
}

static BOOLEAN
KdbpPrintDeviceObjectSummary(IN PDEVICE_OBJECT DeviceObject, IN PCSTR Prefix, OUT PDEVICE_OBJECT *AttachedDevice OPTIONAL)
{
    DEVICE_OBJECT Device;
    NTSTATUS Status;

    Status = KdbpSafeReadMemory(&Device, DeviceObject, sizeof(Device));
    if (!NT_SUCCESS(Status))
    {
        KdbpPrint("%s%p <unreadable: 0x%08lx>\n", Prefix, DeviceObject, Status);
        return FALSE;
    }
    KdbpPrint("%s%p type 0x%lx flags 0x%08lx chars 0x%08lx stack %d driver %p attached %p\n", Prefix, DeviceObject, Device.DeviceType, Device.Flags, Device.Characteristics, Device.StackSize, Device.DriverObject, Device.AttachedDevice);
    if (AttachedDevice != NULL)
        *AttachedDevice = Device.AttachedDevice;
    return TRUE;
}

static BOOLEAN
KdbpCmdDriverObject(ULONG Argc, PCHAR Argv[])
{
    ULONG_PTR Address;
    DRIVER_OBJECT Driver;
    PDEVICE_OBJECT Device;
    ULONG Count;
    ULONG Index;
    NTSTATUS Status;

    if (!KdbpGetSingleAddressArgument(Argv[0], Argc, Argv, &Address))
        return TRUE;
    Status = KdbpSafeReadMemory(&Driver, (PVOID)Address, sizeof(Driver));
    if (!NT_SUCCESS(Status))
    {
        KdbpPrint("!drvobj: DRIVER_OBJECT %p is unreadable (0x%08lx).\n", (PVOID)Address, Status);
        return TRUE;
    }
    KdbpPrint("DRIVER_OBJECT %p%s\n" "  Name:         ", (PVOID)Address, (Driver.Type == IO_TYPE_DRIVER && Driver.Size >= sizeof(Driver)) ? "" : " (invalid type/size)");
    KdbpPrintRemoteUnicodeString(&Driver.DriverName);
    KdbpPrint("\n  Image:        %p, size 0x%lx, section %p\n"
              "  Flags:        0x%08lx\n"
              "  Extension:    %p\n"
              "  Fast I/O:     %p\n"
              "  Init:         ",
              Driver.DriverStart,
              Driver.DriverSize,
              Driver.DriverSection,
              Driver.Flags,
              Driver.DriverExtension,
              Driver.FastIoDispatch);
    KdbpPrintRoutineAddress(Driver.DriverInit);
    KdbpPrint("\n  Start I/O:    ");
    KdbpPrintRoutineAddress(Driver.DriverStartIo);
    KdbpPrint("\n  Unload:       ");
    KdbpPrintRoutineAddress(Driver.DriverUnload);
    KdbpPrint("\n  Devices:\n");

    Device = Driver.DeviceObject;
    for (Count = 0; Device != NULL && Count < 256; Count++)
    {
        DEVICE_OBJECT DeviceSnapshot;

        if (!KdbpPrintDeviceObjectSummary(Device, "    ", NULL) ||
            !NT_SUCCESS(KdbpSafeReadMemory(&DeviceSnapshot, Device, sizeof(DeviceSnapshot))))
        {
            break;
        }
        if (DeviceSnapshot.NextDevice == Device)
        {
            KdbpPrint("    <self-linked device list>\n");
            break;
        }
        Device = DeviceSnapshot.NextDevice;
    }
    if (Device != NULL && Count == 256)
        KdbpPrint("    <device list truncated at 256 entries>\n");

    KdbpPrint("  Dispatch table:\n");
    for (Index = 0; Index <= IRP_MJ_MAXIMUM_FUNCTION; Index++)
    {
        KdbpPrint("    %02lx %-25s ", Index, KdbpIrpMajorName((UCHAR)Index));
        KdbpPrintRoutineAddress(Driver.MajorFunction[Index]);
        KdbpPrint("\n");
        if (KdbOutputAborted)
            break;
    }
    return TRUE;
}

static BOOLEAN
KdbpCmdDeviceObject(ULONG Argc, PCHAR Argv[])
{
    ULONG_PTR Address;
    DEVICE_OBJECT Device;
    NTSTATUS Status;

    if (!KdbpGetSingleAddressArgument(Argv[0], Argc, Argv, &Address))
        return TRUE;
    Status = KdbpSafeReadMemory(&Device, (PVOID)Address, sizeof(Device));
    if (!NT_SUCCESS(Status))
    {
        KdbpPrint("!devobj: DEVICE_OBJECT %p is unreadable (0x%08lx).\n", (PVOID)Address, Status);
        return TRUE;
    }
    KdbpPrint("DEVICE_OBJECT %p%s\n"
              "  Driver/next/attached: %p / %p / %p\n"
              "  Type/stack/alignment: 0x%lx / %d / 0x%lx\n"
              "  Flags/characteristics: 0x%08lx / 0x%08lx\n"
              "  References/active:    %ld / %lu\n"
              "  Current IRP/timer:    %p / %p\n"
              "  VPB/extension:        %p / %p\n"
              "  Sector/security:      %u / %p\n"
              "  Device extension:     %p\n",
              (PVOID)Address,
              (Device.Type == IO_TYPE_DEVICE && Device.Size >= sizeof(Device)) ? "" : " (invalid type/size)",
              Device.DriverObject,
              Device.NextDevice,
              Device.AttachedDevice,
              Device.DeviceType,
              Device.StackSize,
              Device.AlignmentRequirement,
              Device.Flags,
              Device.Characteristics,
              Device.ReferenceCount,
              Device.ActiveThreadCount,
              Device.CurrentIrp,
              Device.Timer,
              Device.Vpb,
              Device.DeviceExtension,
              Device.SectorSize,
              Device.SecurityDescriptor,
              Device.DeviceObjectExtension);
    return TRUE;
}

static BOOLEAN
KdbpCmdDeviceStack(ULONG Argc, PCHAR Argv[])
{
    ULONG_PTR Address;
    PDEVICE_OBJECT Device;
    PDEVICE_OBJECT Next;
    ULONG Count;

    if (!KdbpGetSingleAddressArgument(Argv[0], Argc, Argv, &Address))
        return TRUE;
    Device = (PDEVICE_OBJECT)Address;
    KdbpPrint("Device stack from %p (bottom to top):\n", Device);
    for (Count = 0; Device != NULL && Count < 256; Count++)
    {
        if (!KdbpPrintDeviceObjectSummary(Device, "  ", &Next))
            return TRUE;
        if (Next == Device)
        {
            KdbpPrint("  <self-linked attached device>\n");
            return TRUE;
        }
        Device = Next;
    }
    if (Device != NULL)
        KdbpPrint("  <stack truncated at 256 devices>\n");
    return TRUE;
}

static BOOLEAN
KdbpCmdIrp(ULONG Argc, PCHAR Argv[])
{
    ULONG_PTR Address;
    ULONG_PTR StackBase;
    IRP Irp;
    ULONG StackCount;
    ULONG Index;
    NTSTATUS Status;

    if (!KdbpGetSingleAddressArgument(Argv[0], Argc, Argv, &Address))
        return TRUE;
    Status = KdbpSafeReadMemory(&Irp, (PVOID)Address, sizeof(Irp));
    if (!NT_SUCCESS(Status))
    {
        KdbpPrint("!irp: IRP %p is unreadable (0x%08lx).\n", (PVOID)Address, Status);
        return TRUE;
    }
    StackCount = (UCHAR)Irp.StackCount;
    KdbpPrint("IRP %p%s\n"
              "  Size/flags:           %u / 0x%08lx\n"
              "  MDL/system/user:      %p / %p / %p\n"
              "  Status/information:   0x%08lx / %p\n"
              "  Mode/pending/cancel:  %u / %u / %u\n"
              "  Stack count/location: %u / %u, current %p\n"
              "  Thread/file:          %p / %p\n"
              "  User event/IOSB:      %p / %p\n"
              "  Cancel routine:       ",
              (PVOID)Address,
              (Irp.Type == IO_TYPE_IRP) ? "" : " (invalid type)",
              Irp.Size,
              Irp.Flags,
              Irp.MdlAddress,
              Irp.AssociatedIrp.SystemBuffer,
              Irp.UserBuffer,
              Irp.IoStatus.Status,
              (PVOID)Irp.IoStatus.Information,
              Irp.RequestorMode,
              Irp.PendingReturned,
              Irp.Cancel,
              StackCount,
              (UCHAR)Irp.CurrentLocation,
              Irp.Tail.Overlay.CurrentStackLocation,
              Irp.Tail.Overlay.Thread,
              Irp.Tail.Overlay.OriginalFileObject,
              Irp.UserEvent,
              Irp.UserIosb);
    KdbpPrintRoutineAddress((PVOID)Irp.CancelRoutine);
    KdbpPrint("\n");

    if (StackCount == 0 || StackCount > 64 ||
        Address > MAXULONG_PTR - sizeof(IRP) ||
        Irp.Size < sizeof(IRP) ||
        Irp.Size < sizeof(IRP) + StackCount * sizeof(IO_STACK_LOCATION))
    {
        KdbpPrint("  Stack: <invalid count or IRP size; not traversed>\n");
        return TRUE;
    }
    StackBase = Address + sizeof(IRP);
    KdbpPrint("  Stack locations:\n");
    for (Index = 0; Index < StackCount; Index++)
    {
        IO_STACK_LOCATION Stack;
        PIO_STACK_LOCATION StackAddress;

        if (Index > (MAXULONG_PTR - StackBase) / sizeof(Stack))
        {
            KdbpPrint("    <stack address overflow>\n");
            break;
        }
        StackAddress = (PIO_STACK_LOCATION)(StackBase + Index * sizeof(Stack));
        Status = KdbpSafeReadMemory(&Stack, StackAddress, sizeof(Stack));
        if (!NT_SUCCESS(Status))
        {
            KdbpPrint("    [%lu] %p <unreadable: 0x%08lx>\n", Index, StackAddress, Status);
            break;
        }
        KdbpPrint("    %c[%02lu] %p %02x/%02x %-25s dev %p file %p\n"
                  "           flags/control %02x/%02x completion ",
                  StackAddress == Irp.Tail.Overlay.CurrentStackLocation ? '>' : ' ',
                  Index,
                  StackAddress,
                  Stack.MajorFunction,
                  Stack.MinorFunction,
                  KdbpIrpMajorName(Stack.MajorFunction),
                  Stack.DeviceObject,
                  Stack.FileObject,
                  Stack.Flags,
                  Stack.Control);
        KdbpPrintRoutineAddress((PVOID)Stack.CompletionRoutine);
        KdbpPrint(" context %p\n", Stack.Context);
        if (KdbOutputAborted)
            break;
    }
    return TRUE;
}

static BOOLEAN
KdbpCmdFileObject(ULONG Argc, PCHAR Argv[])
{
    ULONG_PTR Address;
    FILE_OBJECT File;
    NTSTATUS Status;

    if (!KdbpGetSingleAddressArgument(Argv[0], Argc, Argv, &Address))
        return TRUE;
    Status = KdbpSafeReadMemory(&File, (PVOID)Address, sizeof(File));
    if (!NT_SUCCESS(Status))
    {
        KdbpPrint("!fileobj: FILE_OBJECT %p is unreadable (0x%08lx).\n", (PVOID)Address, Status);
        return TRUE;
    }
    KdbpPrint("FILE_OBJECT %p%s\n"
              "  Device/VPB:       %p / %p\n"
              "  FsContext/2:      %p / %p\n"
              "  Related object:   %p\n"
              "  Flags/status:     0x%08lx / 0x%08lx\n"
              "  Current offset:   0x%I64x\n"
              "  Name:             ",
              (PVOID)Address,
              (File.Type == IO_TYPE_FILE && File.Size >= sizeof(File)) ? "" : " (invalid type/size)",
              File.DeviceObject,
              File.Vpb,
              File.FsContext,
              File.FsContext2,
              File.RelatedFileObject,
              File.Flags,
              File.FinalStatus,
              File.CurrentByteOffset.QuadPart);
    KdbpPrintRemoteUnicodeString(&File.FileName);
    KdbpPrint("\n");
    return TRUE;
}

static BOOLEAN
KdbpGetPrcbArgument(IN PCSTR Command, IN ULONG Argc, IN PCHAR Argv[], OUT PULONG Processor, OUT PKPRCB *Prcb)
{
    ULONG_PTR Value;

    if (Argc > 2)
    {
        KdbpPrint("Usage: %s [cpu]\n", Command);
        return FALSE;
    }
    if (Argc == 1)
    {
        *Processor = KeGetCurrentProcessorNumber();
    }
    else
    {
        if (!KdbpGetHexNumber(Argv[1], &Value) || Value > MAXULONG)
        {
            KdbpPrint("%s: Invalid processor '%s'.\n", Command, Argv[1]);
            return FALSE;
        }
        *Processor = (ULONG)Value;
    }
    if (*Processor >= (ULONG)(UCHAR)KeNumberProcessors ||
        KiProcessorBlock[*Processor] == NULL)
    {
        KdbpPrint("%s: Processor %lu is offline or unavailable.\n", Command, *Processor);
        return FALSE;
    }
    *Prcb = KiProcessorBlock[*Processor];
    return TRUE;
}

#define KDB_READ_PRCB_FIELD(Prcb, Field, Value) \
    KdbpSafeReadMemory(&(Value), (PVOID)&(Prcb)->Field, sizeof(Value))

static BOOLEAN
KdbpCmdPrcb(ULONG Argc, PCHAR Argv[])
{
    PKPRCB Prcb;
    ULONG Processor;
    PETHREAD CurrentThread;
    PETHREAD NextThread;
    PETHREAD IdleThread;
    ETHREAD ThreadSnapshot;
    ULONG ReadySummary;
    ULONG InterruptCount;
    ULONG KernelTime;
    ULONG UserTime;
    ULONG DpcTime;
    ULONG InterruptTime;
    ULONG SystemCalls;
    ULONG ContextSwitches;
    ULONG IpiFrozen;
    ULONG MHz;
    LONG PageFaults;
    LONG CopyOnWrite;
    LONG Transitions;
    LONG DemandZero;
    KDPC_DATA DpcNormal;
    KDPC_DATA DpcThreaded;
    NTSTATUS Status;

    if (!KdbpGetPrcbArgument(Argv[0], Argc, Argv, &Processor, &Prcb))
        return TRUE;
#define READ_OR_FAIL(Field, Value)                                        \
    do                                                                    \
    {                                                                     \
        Status = KDB_READ_PRCB_FIELD(Prcb, Field, Value);                 \
        if (!NT_SUCCESS(Status)) goto PrcbUnreadable;                     \
    } while (0)
    READ_OR_FAIL(CurrentThread, CurrentThread);
    READ_OR_FAIL(NextThread, NextThread);
    READ_OR_FAIL(IdleThread, IdleThread);
    READ_OR_FAIL(ReadySummary, ReadySummary);
    READ_OR_FAIL(InterruptCount, InterruptCount);
    READ_OR_FAIL(KernelTime, KernelTime);
    READ_OR_FAIL(UserTime, UserTime);
    READ_OR_FAIL(DpcTime, DpcTime);
    READ_OR_FAIL(InterruptTime, InterruptTime);
    READ_OR_FAIL(KeSystemCalls, SystemCalls);
    READ_OR_FAIL(KeContextSwitches, ContextSwitches);
    READ_OR_FAIL(IpiFrozen, IpiFrozen);
    READ_OR_FAIL(MHz, MHz);
    READ_OR_FAIL(MmPageFaultCount, PageFaults);
    READ_OR_FAIL(MmCopyOnWriteCount, CopyOnWrite);
    READ_OR_FAIL(MmTransitionCount, Transitions);
    READ_OR_FAIL(MmDemandZeroCount, DemandZero);
    READ_OR_FAIL(DpcData[0], DpcNormal);
    READ_OR_FAIL(DpcData[1], DpcThreaded);
#undef READ_OR_FAIL

    KdbpPrint("KPRCB %p for processor %lu\n"
              "  Current/next/idle:  %p / %p / %p\n"
              "  Ready summary:      0x%08lx\n"
              "  Frozen state:       0x%08lx\n"
              "  Frequency:          %lu MHz\n"
              "  Time K/U/DPC/INT:   %lu / %lu / %lu / %lu\n"
              "  Interrupts/syscalls/context switches: %lu / %lu / %lu\n"
              "  Fault/COW/trans/dz: %ld / %ld / %ld / %ld\n"
              "  DPC normal:         depth %ld, total %lu, active %p\n"
              "  DPC threaded:       depth %ld, total %lu, active %p\n",
              Prcb,
              Processor,
              CurrentThread,
              NextThread,
              IdleThread,
              ReadySummary,
              IpiFrozen,
              MHz,
              KernelTime,
              UserTime,
              DpcTime,
              InterruptTime,
              InterruptCount,
              SystemCalls,
              ContextSwitches,
              PageFaults,
              CopyOnWrite,
              Transitions,
              DemandZero,
              (LONG)DpcNormal.DpcQueueDepth,
              DpcNormal.DpcCount,
              DpcNormal.ActiveDpc,
              (LONG)DpcThreaded.DpcQueueDepth,
              DpcThreaded.DpcCount,
              DpcThreaded.ActiveDpc);
    if (CurrentThread != NULL &&
        NT_SUCCESS(KdbpSafeReadMemory(&ThreadSnapshot, CurrentThread, sizeof(ThreadSnapshot))))
    {
        KdbpPrint("  Current CID:        %p.%p, state %s, priority %d\n", ThreadSnapshot.Cid.UniqueProcess, ThreadSnapshot.Cid.UniqueThread, KdbpThreadStateName(ThreadSnapshot.Tcb.State), ThreadSnapshot.Tcb.Priority);
    }
    return TRUE;

PrcbUnreadable:
#undef READ_OR_FAIL
    KdbpPrint("!prcb: Field in KPRCB %p is unreadable (0x%08lx).\n", Prcb, Status);
    return TRUE;
}

static BOOLEAN
KdbpCmdReady(ULONG Argc, PCHAR Argv[])
{
    PKPRCB Prcb;
    ULONG Processor;
    ULONG Priority;
    ULONG Total = 0;

    if (!KdbpGetPrcbArgument(Argv[0], Argc, Argv, &Processor, &Prcb))
        return TRUE;
    KdbpPrint("Ready queues for processor %lu:\n", Processor);
    for (Priority = 0; Priority < 32 && Total < 65536; Priority++)
    {
        LIST_ENTRY Head;
        PLIST_ENTRY HeadAddress = &Prcb->DispatcherReadyListHead[Priority];
        PLIST_ENTRY Entry;
        ULONG Count = 0;

        if (!NT_SUCCESS(KdbpSafeReadMemory(&Head, HeadAddress, sizeof(Head))))
        {
            KdbpPrint("  Pri %02lu: <unreadable head %p>\n", Priority, HeadAddress);
            continue;
        }
        Entry = Head.Flink;
        if (Entry == HeadAddress)
            continue;
        KdbpPrint("  Priority %02lu:\n", Priority);
        while (Entry != NULL && Entry != HeadAddress &&
               Count++ < 4096 && Total++ < 65536)
        {
            LIST_ENTRY Links;
            PETHREAD Thread;
            ETHREAD Snapshot;

            if (!NT_SUCCESS(KdbpSafeReadMemory(&Links, Entry, sizeof(Links))))
            {
                KdbpPrint("    %p <unreadable list entry>\n", Entry);
                break;
            }
            Thread = CONTAINING_RECORD(Entry, ETHREAD, Tcb.WaitListEntry);
            if (!NT_SUCCESS(KdbpSafeReadMemory(&Snapshot, Thread, sizeof(Snapshot))))
            {
                KdbpPrint("    %p <unreadable ETHREAD>\n", Thread);
                break;
            }
            KdbpPrint("    TID %p ETHREAD %p state %s priority %d affinity 0x%Ix\n", Snapshot.Cid.UniqueThread, Thread, KdbpThreadStateName(Snapshot.Tcb.State), Snapshot.Tcb.Priority, Snapshot.Tcb.Affinity);
            if (Links.Flink == Entry)
            {
                KdbpPrint("    <self-linked ready entry>\n");
                break;
            }
            Entry = Links.Flink;
            if (KdbOutputAborted)
                return TRUE;
        }
        if (Count >= 4096)
            KdbpPrint("    <priority queue truncated at 4096 entries>\n");
    }
    if (Total >= 65536)
        KdbpPrint("  <ready enumeration stopped at 65536 threads>\n");
    return TRUE;
}

static BOOLEAN
KdbpCmdDpc(ULONG Argc, PCHAR Argv[])
{
    PKPRCB Prcb;
    ULONG Processor;
    ULONG Queue;

    if (!KdbpGetPrcbArgument(Argv[0], Argc, Argv, &Processor, &Prcb))
        return TRUE;
    KdbpPrint("DPC queues for processor %lu:\n", Processor);
    for (Queue = 0; Queue < 2; Queue++)
    {
        KDPC_DATA Data;
        PSINGLE_LIST_ENTRY Entry;
        ULONG Count = 0;
        NTSTATUS Status;

        Status = KdbpSafeReadMemory(&Data, &Prcb->DpcData[Queue], sizeof(Data));
        if (!NT_SUCCESS(Status))
        {
            KdbpPrint("  %s: <unreadable: 0x%08lx>\n", Queue == 0 ? "normal" : "threaded", Status);
            continue;
        }
        KdbpPrint("  %s: depth %ld total %lu active %p\n", Queue == 0 ? "normal" : "threaded", (LONG)Data.DpcQueueDepth, Data.DpcCount, Data.ActiveDpc);
        Entry = Data.DpcList.ListHead.Next;
        while (Entry != NULL && Count++ < 65536)
        {
            SINGLE_LIST_ENTRY Link;
            PKDPC Dpc = CONTAINING_RECORD(Entry, KDPC, DpcListEntry);
            KDPC Snapshot;

            if (!NT_SUCCESS(KdbpSafeReadMemory(&Link, Entry, sizeof(Link))) ||
                !NT_SUCCESS(KdbpSafeReadMemory(&Snapshot, Dpc, sizeof(Snapshot))))
            {
                KdbpPrint("    %p <unreadable DPC entry>\n", Entry);
                break;
            }
            KdbpPrint("    DPC %p type %u importance %u target %u routine ", Dpc, Snapshot.Type, Snapshot.Importance, Snapshot.Number);
            KdbpPrintRoutineAddress((PVOID)Snapshot.DeferredRoutine);
            KdbpPrint(" context %p args %p/%p\n", Snapshot.DeferredContext, Snapshot.SystemArgument1, Snapshot.SystemArgument2);
            if (Link.Next == Entry)
            {
                KdbpPrint("    <self-linked DPC entry>\n");
                break;
            }
            Entry = Link.Next;
            if (KdbOutputAborted)
                return TRUE;
        }
        if (Count >= 65536)
            KdbpPrint("    <DPC queue truncated at 65536 entries>\n");
    }
    return TRUE;
}

static BOOLEAN
KdbpPrintTimer(IN PKTIMER Timer)
{
    KTIMER Snapshot;
    NTSTATUS Status;

    Status = KdbpSafeReadMemory(&Snapshot, Timer, sizeof(Snapshot));
    if (!NT_SUCCESS(Status))
    {
        KdbpPrint("  TIMER %p <unreadable: 0x%08lx>\n", Timer, Status);
        return FALSE;
    }
    KdbpPrint("  TIMER %p due 0x%I64x period %lu signal %ld DPC %p", Timer, Snapshot.DueTime.QuadPart, Snapshot.Period, Snapshot.Header.SignalState, Snapshot.Dpc);
    if (Snapshot.Header.Type == TimerNotificationObject)
        KdbpPrint(" notification");
    else if (Snapshot.Header.Type == TimerSynchronizationObject)
        KdbpPrint(" synchronization");
    else
        KdbpPrint(" invalid-type(%u)", Snapshot.Header.Type);
    KdbpPrint("\n");
    return TRUE;
}

static BOOLEAN
KdbpCmdTimer(ULONG Argc, PCHAR Argv[])
{
    extern KTIMER_TABLE_ENTRY KiTimerTableListHead[TIMER_TABLE_SIZE];
    ULONG_PTR Address;
    ULONG Bucket;
    ULONG Total = 0;

    if (Argc > 2)
    {
        KdbpPrint("Usage: !timer [address]\n");
        return TRUE;
    }
    if (Argc == 2)
    {
        if (!KdbpEvaluateAddress(Argv[1], KdbPromptStr.Length + (Argv[1] - Argv[0]), &Address))
        {
            return TRUE;
        }
        (VOID)KdbpPrintTimer((PKTIMER)Address);
        return TRUE;
    }

    KdbpPrint("Queued timers (maximum 65536):\n");
    for (Bucket = 0; Bucket < TIMER_TABLE_SIZE && Total < 65536; Bucket++)
    {
        PLIST_ENTRY HeadAddress = &KiTimerTableListHead[Bucket].Entry;
        LIST_ENTRY Head;
        PLIST_ENTRY Entry;
        ULONG BucketCount = 0;

        if (!NT_SUCCESS(KdbpSafeReadMemory(&Head, HeadAddress, sizeof(Head))))
            continue;
        Entry = Head.Flink;
        while (Entry != NULL && Entry != HeadAddress &&
               BucketCount++ < 4096 && Total++ < 65536)
        {
            LIST_ENTRY Links;
            PKTIMER Timer = CONTAINING_RECORD(Entry, KTIMER, TimerListEntry);

            if (!NT_SUCCESS(KdbpSafeReadMemory(&Links, Entry, sizeof(Links))))
            {
                KdbpPrint("  Bucket %lu entry %p <unreadable>\n", Bucket, Entry);
                break;
            }
            KdbpPrint("  [%03lu] ", Bucket);
            if (!KdbpPrintTimer(Timer))
                break;
            if (Links.Flink == Entry)
            {
                KdbpPrint("  <self-linked timer entry>\n");
                break;
            }
            Entry = Links.Flink;
            if (KdbOutputAborted)
                return TRUE;
        }
        if (BucketCount >= 4096)
            KdbpPrint("  Bucket %lu truncated at 4096 timers.\n", Bucket);
    }
    if (Total >= 65536)
        KdbpPrint("  Timer enumeration stopped at 65536 entries.\n");
    return TRUE;
}

static BOOLEAN
KdbpCmdInterrupt(ULONG Argc, PCHAR Argv[])
{
    ULONG_PTR Address;
    KINTERRUPT Interrupt;
    NTSTATUS Status;

    if (!KdbpGetSingleAddressArgument(Argv[0], Argc, Argv, &Address))
        return TRUE;
    Status = KdbpSafeReadMemory(&Interrupt, (PVOID)Address, sizeof(Interrupt));
    if (!NT_SUCCESS(Status))
    {
        KdbpPrint("!interrupt: KINTERRUPT %p is unreadable (0x%08lx).\n", (PVOID)Address, Status);
        return TRUE;
    }
    KdbpPrint("KINTERRUPT %p%s\n"
              "  Vector/CPU:       %lu / %d\n"
              "  IRQL/sync IRQL:   %u / %u\n"
              "  Connected/shared: %u / %u\n"
              "  Mode/polarity:    %u / %u\n"
              "  Counts:           service %lu, dispatch %lu\n"
              "  Service context:  %p\n"
              "  Service routine:  ",
              (PVOID)Address,
              (Interrupt.Type == InterruptObject && Interrupt.Size >= sizeof(Interrupt)) ? "" : " (invalid type/size)",
              Interrupt.Vector,
              Interrupt.Number,
              Interrupt.Irql,
              Interrupt.SynchronizeIrql,
              Interrupt.Connected,
              Interrupt.ShareVector,
              Interrupt.Mode,
              Interrupt.Polarity,
              Interrupt.ServiceCount,
              Interrupt.DispatchCount,
              Interrupt.ServiceContext);
    KdbpPrintRoutineAddress((PVOID)Interrupt.ServiceRoutine);
    KdbpPrint("\n  Dispatch address: ");
    KdbpPrintRoutineAddress((PVOID)Interrupt.DispatchAddress);
    KdbpPrint("\n");
    return TRUE;
}

static VOID
KdbpPrintResource(IN PERESOURCE Resource)
{
    ERESOURCE Snapshot;
    OWNER_ENTRY TableHeader;
    ULONG Index;
    ULONG TableSize;
    NTSTATUS Status;

    Status = KdbpSafeReadMemory(&Snapshot, Resource, sizeof(Snapshot));
    if (!NT_SUCCESS(Status))
    {
        KdbpPrint("ERESOURCE %p <unreadable: 0x%08lx>\n", Resource, Status);
        return;
    }
    KdbpPrint("ERESOURCE %p active %d entries %lu flags 0x%04x contention %lu\n"
              "  Waiters shared/exclusive: %lu / %lu, objects %p / %p\n"
              "  Primary owner:            %p count %lu\n",
              Resource,
              Snapshot.ActiveCount,
              Snapshot.ActiveEntries,
              Snapshot.Flag,
              Snapshot.ContentionCount,
              Snapshot.NumberOfSharedWaiters,
              Snapshot.NumberOfExclusiveWaiters,
              (PVOID)Snapshot.SharedWaiters,
              (PVOID)Snapshot.ExclusiveWaiters,
              (PVOID)Snapshot.OwnerEntry.OwnerThread,
              Snapshot.OwnerEntry.OwnerCount);
    if (Snapshot.OwnerTable == NULL)
        return;
    Status = KdbpSafeReadMemory(&TableHeader, Snapshot.OwnerTable, sizeof(TableHeader));
    if (!NT_SUCCESS(Status) || TableHeader.TableSize == 0 || TableHeader.TableSize > 1024)
    {
        KdbpPrint("  Owner table %p is unreadable or invalid.\n", Snapshot.OwnerTable);
        return;
    }
    TableSize = TableHeader.TableSize;
    for (Index = 1; Index < TableSize; Index++)
    {
        OWNER_ENTRY Owner;

        if (!NT_SUCCESS(KdbpSafeReadMemory(&Owner, Snapshot.OwnerTable + Index, sizeof(Owner))))
        {
            KdbpPrint("  Owner[%lu] <unreadable>\n", Index);
            break;
        }
        if (Owner.OwnerThread != 0)
        {
            KdbpPrint("  Owner[%lu]: thread %p count %lu%s%s\n", Index, (PVOID)Owner.OwnerThread, Owner.OwnerCount, Owner.OwnerReferenced ? " referenced" : "", Owner.IoPriorityBoosted ? " I/O-boosted" : "");
        }
        if (KdbOutputAborted)
            break;
    }
}

static BOOLEAN
KdbpCmdLocks(ULONG Argc, PCHAR Argv[])
{
    extern LIST_ENTRY ExpSystemResourcesList;
    ULONG_PTR Address;
    LIST_ENTRY Head;
    PLIST_ENTRY Entry;
    ULONG Count = 0;

    if (Argc > 2)
    {
        KdbpPrint("Usage: !locks [address]\n");
        return TRUE;
    }
    if (Argc == 2)
    {
        if (!KdbpEvaluateAddress(Argv[1], KdbPromptStr.Length + (Argv[1] - Argv[0]), &Address))
        {
            return TRUE;
        }
        KdbpPrintResource((PERESOURCE)Address);
        return TRUE;
    }
    if (!NT_SUCCESS(KdbpSafeReadMemory(&Head, &ExpSystemResourcesList, sizeof(Head))))
    {
        KdbpPrint("!locks: System resource list head is unreadable.\n");
        return TRUE;
    }
    Entry = Head.Flink;
    while (Entry != NULL && Entry != &ExpSystemResourcesList && Count++ < 65536)
    {
        LIST_ENTRY Links;
        PERESOURCE Resource;

        if (!NT_SUCCESS(KdbpSafeReadMemory(&Links, Entry, sizeof(Links))))
        {
            KdbpPrint("!locks: List entry %p is unreadable.\n", Entry);
            break;
        }
        Resource = CONTAINING_RECORD(Entry, ERESOURCE, SystemResourcesList);
        KdbpPrintResource(Resource);
        if (Links.Flink == Entry)
        {
            KdbpPrint("!locks: Self-linked resource entry %p.\n", Entry);
            break;
        }
        Entry = Links.Flink;
        if (KdbOutputAborted)
            break;
    }
    if (Count >= 65536)
        KdbpPrint("!locks: Enumeration stopped at 65536 resources.\n");
    return TRUE;
}

static BOOLEAN
KdbpCmdApc(ULONG Argc, PCHAR Argv[])
{
    PETHREAD Thread = KdbCurrentThread;
    ETHREAD ThreadSnapshot;
    ULONG_PTR ThreadId;
    PCHAR End;
    ULONG Mode;

    if (Argc > 2)
    {
        KdbpPrint("Usage: !apc [tid]\n");
        return TRUE;
    }
    if (Argc == 2)
    {
        ThreadId = strtoulptr(Argv[1], &End, 0);
        if (End == Argv[1] || *End != ANSI_NULL ||
            !KdbpFindThreadById((PVOID)ThreadId, &Thread))
        {
            KdbpPrint("!apc: Invalid thread id '%s'.\n", Argv[1]);
            return TRUE;
        }
    }
    if (!NT_SUCCESS(KdbpSafeReadMemory(&ThreadSnapshot, Thread, sizeof(ThreadSnapshot))))
    {
        KdbpPrint("!apc: ETHREAD %p is unreadable.\n", Thread);
        return TRUE;
    }
    KdbpPrint("APCs for thread %p (TID %p): kernel pending %u/in progress %u, user pending %u\n",
              Thread,
              ThreadSnapshot.Cid.UniqueThread,
              ThreadSnapshot.Tcb.ApcState.KernelApcPending,
              ThreadSnapshot.Tcb.ApcState.KernelApcInProgress,
              ThreadSnapshot.Tcb.ApcState.UserApcPending);
    for (Mode = 0; Mode < MaximumMode; Mode++)
    {
        PLIST_ENTRY HeadAddress = &Thread->Tcb.ApcState.ApcListHead[Mode];
        LIST_ENTRY Head = ThreadSnapshot.Tcb.ApcState.ApcListHead[Mode];
        PLIST_ENTRY Entry = Head.Flink;
        ULONG Count = 0;

        KdbpPrint("  %s APC queue:\n", Mode == KernelMode ? "kernel" : "user");
        if (Entry == HeadAddress)
        {
            KdbpPrint("    <empty>\n");
            continue;
        }
        while (Entry != NULL && Entry != HeadAddress && Count++ < 4096)
        {
            LIST_ENTRY Links;
            PKAPC Apc = CONTAINING_RECORD(Entry, KAPC, ApcListEntry);
            KAPC Snapshot;

            if (!NT_SUCCESS(KdbpSafeReadMemory(&Links, Entry, sizeof(Links))) ||
                !NT_SUCCESS(KdbpSafeReadMemory(&Snapshot, Apc, sizeof(Snapshot))))
            {
                KdbpPrint("    %p <unreadable APC entry>\n", Entry);
                break;
            }
            KdbpPrint("    APC %p inserted %u mode %d state %d kernel ", Apc, Snapshot.Inserted, Snapshot.ApcMode, Snapshot.ApcStateIndex);
            KdbpPrintRoutineAddress((PVOID)Snapshot.KernelRoutine);
            KdbpPrint(" normal ");
            KdbpPrintRoutineAddress((PVOID)Snapshot.NormalRoutine);
            KdbpPrint(" rundown ");
            KdbpPrintRoutineAddress((PVOID)Snapshot.RundownRoutine);
            KdbpPrint(" context %p args %p/%p\n", Snapshot.NormalContext, Snapshot.SystemArgument1, Snapshot.SystemArgument2);
            if (Links.Flink == Entry)
            {
                KdbpPrint("    <self-linked APC entry>\n");
                break;
            }
            Entry = Links.Flink;
            if (KdbOutputAborted)
                return TRUE;
        }
        if (Count >= 4096)
            KdbpPrint("    <APC queue truncated at 4096 entries>\n");
    }
    return TRUE;
}

static BOOLEAN
KdbpCmdDispatcher(ULONG Argc, PCHAR Argv[])
{
    ULONG_PTR Address;
    DISPATCHER_HEADER Header;
    PLIST_ENTRY HeadAddress;
    PLIST_ENTRY Entry;
    ULONG Count = 0;
    NTSTATUS Status;

    if (!KdbpGetSingleAddressArgument(Argv[0], Argc, Argv, &Address))
        return TRUE;
    if (Address > MAXULONG_PTR - FIELD_OFFSET(DISPATCHER_HEADER, WaitListHead))
    {
        KdbpPrint("!dispatcher: Address %p overflows the dispatcher header.\n", (PVOID)Address);
        return TRUE;
    }
    Status = KdbpSafeReadMemory(&Header, (PVOID)Address, sizeof(Header));
    if (!NT_SUCCESS(Status))
    {
        KdbpPrint("!dispatcher: Header %p is unreadable (0x%08lx).\n", (PVOID)Address, Status);
        return TRUE;
    }
    HeadAddress = (PLIST_ENTRY)(Address + FIELD_OFFSET(DISPATCHER_HEADER, WaitListHead));
    KdbpPrint("DISPATCHER_HEADER %p type %u size %u signal %ld waiters:\n", (PVOID)Address, Header.Type, Header.Size, Header.SignalState);
    Entry = Header.WaitListHead.Flink;
    if (Entry == HeadAddress)
    {
        KdbpPrint("  <empty>\n");
        return TRUE;
    }
    while (Entry != NULL && Entry != HeadAddress && Count++ < 4096)
    {
        LIST_ENTRY Links;
        PKWAIT_BLOCK WaitBlock = CONTAINING_RECORD(Entry, KWAIT_BLOCK, WaitListEntry);
        KWAIT_BLOCK Block;
        ETHREAD Thread;

        if (!NT_SUCCESS(KdbpSafeReadMemory(&Links, Entry, sizeof(Links))) ||
            !NT_SUCCESS(KdbpSafeReadMemory(&Block, WaitBlock, sizeof(Block))))
        {
            KdbpPrint("  %p <unreadable wait block>\n", Entry);
            break;
        }
        KdbpPrint("  block %p thread %p key 0x%x type %u state %u", WaitBlock, Block.Thread, Block.WaitKey, Block.WaitType, Block.BlockState);
        if (Block.Thread != NULL &&
            NT_SUCCESS(KdbpSafeReadMemory(&Thread, Block.Thread, sizeof(Thread))))
        {
            KdbpPrint(" TID %p (%s)", Thread.Cid.UniqueThread, KdbpWaitReasonName(Thread.Tcb.WaitReason));
        }
        KdbpPrint("\n");
        if (Links.Flink == Entry)
        {
            KdbpPrint("  <self-linked waiter entry>\n");
            break;
        }
        Entry = Links.Flink;
        if (KdbOutputAborted)
            break;
    }
    if (Count >= 4096)
        KdbpPrint("  <waiter list truncated at 4096 entries>\n");
    return TRUE;
}

#undef KDB_READ_PRCB_FIELD

static PCSTR
KdbpProcessStateName(IN ULONG State)
{
    switch (State)
    {
        case ProcessInMemory: return "In Memory";
        case ProcessOutOfMemory: return "Out of Memory";
        case ProcessInTransition: return "In Transition";
        case ProcessInSwap: return "In Swap";
        case ProcessOutSwap: return "Out Swap";
        default: return "Unknown";
    }
}

static BOOLEAN
KdbpGetProcessSessionIdSafe(IN PEPROCESS Process, IN PEPROCESS Snapshot, OUT PULONG SessionId)
{
    struct
    {
        PVOID GlobalVirtualAddress;
        LONG ReferenceCount;
        ULONG Flags;
        ULONG SessionId;
    } SessionPrefix;

    UNREFERENCED_PARAMETER(Process);
    if (Snapshot->Vm.Flags.SessionLeader || Snapshot->Session == NULL)
    {
        *SessionId = 0;
        return TRUE;
    }

    if (!NT_SUCCESS(KdbpSafeReadMemory(&SessionPrefix, Snapshot->Session, sizeof(SessionPrefix))))
    {
        return FALSE;
    }
    *SessionId = SessionPrefix.SessionId;
    return TRUE;
}

/*!\brief Lists processes or switches to another process context.
 */
static BOOLEAN
KdbpCmdProc(ULONG Argc, PCHAR Argv[])
{
    PLIST_ENTRY Entry;
    PEPROCESS Process;
    EPROCESS ProcessSnapshot;
    LIST_ENTRY Links;
    LIST_ENTRY Head;
    PCHAR pend, str1, str2;
    ULONG_PTR ul;
    ULONG ProcessCount;
    ULONG SessionId;
    ETHREAD ThreadSnapshot;
    extern LIST_ENTRY PsActiveProcessHead;

    if (Argc >= 2 && _stricmp(Argv[1], "list") == 0)
    {
        if (Argc != 2)
        {
            KdbpPrint("Usage: proc list\n");
            return TRUE;
        }
        if (!NT_SUCCESS(KdbpSafeReadMemory(&Head, &PsActiveProcessHead, sizeof(Head))))
        {
            KdbpPrint("proc: Process list head is unreadable.\n");
            return TRUE;
        }
        Entry = Head.Flink;
        if (!Entry || Entry == &PsActiveProcessHead)
        {
            KdbpPrint("No processes in the system!\n");
            return TRUE;
        }

        KdbpPrint("  PID               EPROCESS           Threads Session State          Filename\n");
        ProcessCount = 0;
        while (Entry != NULL &&
               Entry != &PsActiveProcessHead &&
               ProcessCount++ < 4096)
        {
            if (!NT_SUCCESS(KdbpSafeReadMemory(&Links, Entry, sizeof(Links))))
            {
                KdbpPrint("proc: Process list entry %p is unreadable.\n", Entry);
                break;
            }
            Process = CONTAINING_RECORD(Entry, EPROCESS, ActiveProcessLinks);
            if (!NT_SUCCESS(KdbpSafeReadMemory(&ProcessSnapshot, Process, sizeof(ProcessSnapshot))))
            {
                KdbpPrint("proc: EPROCESS %p is unreadable.\n", Process);
                break;
            }
            ProcessSnapshot.ImageFileName[RTL_NUMBER_OF(ProcessSnapshot.ImageFileName) - 1] = ANSI_NULL;
            if (!KdbpGetProcessSessionIdSafe(Process, &ProcessSnapshot, &SessionId))
            {
                SessionId = MAXULONG;
            }

            if (Process == KdbCurrentProcess)
            {
                str1 = "\x1b[1m*";
                str2 = "\x1b[0m";
            }
            else
            {
                str1 = " ";
                str2 = "";
            }

            KdbpPrint(" %s%p  %p  %7lu %7lu %-14s %s%s\n", str1, ProcessSnapshot.UniqueProcessId, Process, ProcessSnapshot.ActiveThreads, SessionId, KdbpProcessStateName(ProcessSnapshot.Pcb.State), ProcessSnapshot.ImageFileName, str2);

            if (Links.Flink == Entry)
            {
                KdbpPrint("proc: Self-linked process entry %p; stopping.\n", Entry);
                break;
            }
            Entry = Links.Flink;
            if (KdbOutputAborted)
                break;
        }
        if (ProcessCount >= 4096)
            KdbpPrint("proc: Enumeration stopped at the 4096-process safety limit.\n");
    }
    else if (Argc >= 2 && _stricmp(Argv[1], "attach") == 0)
    {
        if (Argc != 3)
        {
            KdbpPrint("Usage: proc attach pid\n");
            return TRUE;
        }

        ul = strtoulptr(Argv[2], &pend, 0);
        if (Argv[2] == pend || *pend != '\0')
        {
            KdbpPrint("process attach: '%s' is not a valid process id!\n", Argv[2]);
            return TRUE;
        }

        KdbpResetContextRecord(FALSE);
        if (!KdbpAttachToProcess((PVOID)ul))
        {
            return TRUE;
        }

        if (!NT_SUCCESS(KdbpSafeReadMemory(&ThreadSnapshot, KdbCurrentThread, sizeof(ThreadSnapshot))))
        {
            KdbpPrint("Attached to process %p; current ETHREAD %p is unreadable.\n", (PVOID)ul, KdbCurrentThread);
        }
        else
        {
            KdbpPrint("Attached to process %p, thread %p.\n", (PVOID)ul, ThreadSnapshot.Cid.UniqueThread);
        }
    }
    else
    {
        if (Argc > 2)
        {
            KdbpPrint("Usage: proc [pid]\n");
            return TRUE;
        }
        Process = KdbCurrentProcess;

        if (Argc >= 2)
        {
            ul = strtoulptr(Argv[1], &pend, 0);
            if (Argv[1] == pend || *pend != '\0')
            {
                KdbpPrint("proc: '%s' is not a valid process id!\n", Argv[1]);
                return TRUE;
            }

            if (!KdbpFindProcessById((PVOID)ul, &Process))
            {
                KdbpPrint("proc: Invalid process id!\n");
                return TRUE;
            }

        }

        if (!NT_SUCCESS(KdbpSafeReadMemory(&ProcessSnapshot, Process, sizeof(ProcessSnapshot))))
        {
            KdbpPrint("proc: Cannot read EPROCESS %p.\n", Process);
            return TRUE;
        }
        ProcessSnapshot.ImageFileName[RTL_NUMBER_OF(ProcessSnapshot.ImageFileName) - 1] = ANSI_NULL;
        if (!KdbpGetProcessSessionIdSafe(Process, &ProcessSnapshot, &SessionId))
        {
            SessionId = MAXULONG;
        }

        KdbpPrint("%s"
                  "  EPROCESS:         %p\n"
                  "  PID / Parent:     %p / %p\n"
                  "  State:            %s (0x%x), threads %lu\n"
                  "  Image Filename:   %s\n"
                  "  Section:          object %p, base %p\n"
                  "  PEB / Session:    %p / %p (id %lu)\n"
                  "  DTB:              %p\n"
                  "  Object table:     %p\n"
                  "  Token fast-ref:   %p\n"
                  "  Debug/Exception:  %p / %p\n"
                  "  Exit:             status 0x%08lx, last thread 0x%08lx\n"
                  "  CPU Time:         kernel %I64u, user %I64u\n"
                  "  Virtual memory:   current %Iu, peak %Iu, commit %Iu, peak commit %Iu\n"
                  "  Private/Locked:   %Iu / %Iu pages\n"
                  "  I/O operations:   read %I64d, write %I64d, other %I64d\n"
                  "  I/O transfer:     read %I64d, write %I64d, other %I64d\n"
                  "  Flags:            0x%08lx, flags2 0x%08lx\n",
                  (Argc < 2) ? "Current process:\n" : "",
                  Process,
                  ProcessSnapshot.UniqueProcessId,
                  ProcessSnapshot.InheritedFromUniqueProcessId,
                  KdbpProcessStateName(ProcessSnapshot.Pcb.State),
                  ProcessSnapshot.Pcb.State,
                  ProcessSnapshot.ActiveThreads,
                  ProcessSnapshot.ImageFileName,
                  ProcessSnapshot.SectionObject,
                  ProcessSnapshot.SectionBaseAddress,
                  ProcessSnapshot.Peb,
                  ProcessSnapshot.Session,
                  SessionId,
                  (PVOID)(ULONG_PTR)KPROCESS_DTB0(&ProcessSnapshot.Pcb),
                  ProcessSnapshot.ObjectTable,
                  (PVOID)ProcessSnapshot.Token.Value,
                  ProcessSnapshot.DebugPort,
                  ProcessSnapshot.ExceptionPort,
                  ProcessSnapshot.ExitStatus,
                  ProcessSnapshot.LastThreadExitStatus,
                  (ULONGLONG)ProcessSnapshot.Pcb.KernelTime,
                  (ULONGLONG)ProcessSnapshot.Pcb.UserTime,
                  ProcessSnapshot.VirtualSize,
                  ProcessSnapshot.PeakVirtualSize,
                  ProcessSnapshot.CommitCharge,
                  ProcessSnapshot.CommitChargePeak,
                  (SIZE_T)ProcessSnapshot.NumberOfPrivatePages,
                  (SIZE_T)ProcessSnapshot.NumberOfLockedPages,
                  ProcessSnapshot.ReadOperationCount.QuadPart,
                  ProcessSnapshot.WriteOperationCount.QuadPart,
                  ProcessSnapshot.OtherOperationCount.QuadPart,
                  ProcessSnapshot.ReadTransferCount.QuadPart,
                  ProcessSnapshot.WriteTransferCount.QuadPart,
                  ProcessSnapshot.OtherTransferCount.QuadPart,
                  ProcessSnapshot.Flags,
                  ProcessSnapshot.Flags2);

    }

    return TRUE;
}

static NTSTATUS
KdbpReadRemoteField(IN PVOID Base, IN SIZE_T Offset, OUT PVOID Value, IN ULONG Size)
{
    if (Base == NULL || Offset > MAXULONG_PTR - (ULONG_PTR)Base)
        return STATUS_INVALID_ADDRESS;
    return KdbpSafeReadMemory(Value, (PUCHAR)Base + Offset, Size);
}

static VOID
KdbpPrintRemoteUnicodeString(IN PCUNICODE_STRING String)
{
    WCHAR Wide[96];
    CHAR Ansi[sizeof(Wide) / sizeof(Wide[0]) + 1];
    ULONG Characters;
    ULONG Offset = 0;

    if (String->Length == 0)
    {
        KdbpPrint("\"\"");
        return;
    }
    if (String->Buffer == NULL ||
        (String->Length & (sizeof(WCHAR) - 1)) != 0 ||
        String->Length > String->MaximumLength ||
        (ULONG_PTR)String->Buffer > MAXULONG_PTR - String->Length)
    {
        KdbpPrint("<invalid UNICODE_STRING>");
        return;
    }

    KdbpPrint("\"");
    Characters = String->Length / sizeof(WCHAR);
    while (Offset < Characters && Offset < 2048)
    {
        ULONG Index;
        ULONG Chunk = min(Characters - Offset, (ULONG)RTL_NUMBER_OF(Wide));

        if (!NT_SUCCESS(KdbpSafeReadMemory(Wide, String->Buffer + Offset, Chunk * sizeof(WCHAR))))
        {
            KdbpPrint("<unreadable>");
            break;
        }
        for (Index = 0; Index < Chunk; Index++)
        {
            WCHAR Character = Wide[Index];
            Ansi[Index] = (Character >= 0x20 && Character < 0x7f) ?
                          (CHAR)Character : '?';
        }
        Ansi[Chunk] = ANSI_NULL;
        KdbpPrint("%s", Ansi);
        Offset += Chunk;
        if (KdbOutputAborted)
            break;
    }
    if (Characters > Offset && !KdbOutputAborted)
        KdbpPrint("...");
    KdbpPrint("\"");
}

static BOOLEAN
KdbpCmdTeb(ULONG Argc, PCHAR Argv[])
{
    ULONG_PTR Address;
    ETHREAD Thread;
    NT_TIB Tib;
    CLIENT_ID ClientId;
    PPEB Peb;
    PVOID CsrClientThread;
    PVOID Win32ThreadInfo;
    PVOID DeallocationStack;
    ULONG LastError;
    ULONG LastStatus;
    ULONG CriticalSections;
    ULONG GuaranteedStack;
    ULONG WaitingOnLoaderLock;
    LCID Locale;
    NTSTATUS Status;

    if (Argc > 2)
    {
        KdbpPrint("Usage: !teb [address]\n");
        return TRUE;
    }

    if (Argc == 2)
    {
        if (!KdbpEvaluateAddress(Argv[1], KdbPromptStr.Length + (Argv[1] - Argv[0]), &Address))
        {
            return TRUE;
        }
    }
    else
    {
        if (!NT_SUCCESS(KdbpSafeReadMemory(&Thread, KdbCurrentThread, sizeof(Thread))))
        {
            KdbpPrint("!teb: Cannot read current ETHREAD %p.\n", KdbCurrentThread);
            return TRUE;
        }
        Address = (ULONG_PTR)Thread.Tcb.Teb;
    }

    if (Address == 0)
    {
        KdbpPrint("!teb: This is a system thread or the TEB is unavailable.\n");
        return TRUE;
    }

#define READ_TEB_FIELD(Field, Value) do { Status = KdbpReadRemoteField((PVOID)Address, FIELD_OFFSET(TEB, Field), &(Value), sizeof(Value)); if (!NT_SUCCESS(Status)) goto TebUnreadable; } while (0)
    READ_TEB_FIELD(NtTib, Tib);
    READ_TEB_FIELD(ClientId, ClientId);
    READ_TEB_FIELD(ProcessEnvironmentBlock, Peb);
    READ_TEB_FIELD(LastErrorValue, LastError);
    READ_TEB_FIELD(LastStatusValue, LastStatus);
    READ_TEB_FIELD(CountOfOwnedCriticalSections, CriticalSections);
    READ_TEB_FIELD(CsrClientThread, CsrClientThread);
    READ_TEB_FIELD(Win32ThreadInfo, Win32ThreadInfo);
    READ_TEB_FIELD(CurrentLocale, Locale);
    READ_TEB_FIELD(DeallocationStack, DeallocationStack);
    READ_TEB_FIELD(GuaranteedStackBytes, GuaranteedStack);
    READ_TEB_FIELD(WaitingOnLoaderLock, WaitingOnLoaderLock);
#undef READ_TEB_FIELD

    KdbpPrint("TEB %p\n"
              "  CID:                 %p.%p\n"
              "  PEB:                 %p\n"
              "  Exception list:      %p\n"
              "  Stack base/limit:    %p / %p\n"
              "  Deallocation stack:  %p\n"
              "  Fiber/arbitrary:     %p / %p\n"
              "  Self:                %p\n"
              "  Last error/status:   0x%08lx / 0x%08lx\n"
              "  Critical sections:   %lu\n"
              "  CSR/Win32 thread:    %p / %p\n"
              "  Locale:              0x%08lx\n"
              "  Guaranteed stack:    %lu\n"
              "  Waiting loader lock: %lu\n",
              (PVOID)Address,
              ClientId.UniqueProcess,
              ClientId.UniqueThread,
              Peb,
              Tib.ExceptionList,
              Tib.StackBase,
              Tib.StackLimit,
              DeallocationStack,
              Tib.FiberData,
              Tib.ArbitraryUserPointer,
              Tib.Self,
              LastError,
              LastStatus,
              CriticalSections,
              CsrClientThread,
              Win32ThreadInfo,
              Locale,
              GuaranteedStack,
              WaitingOnLoaderLock);
    return TRUE;

TebUnreadable:
#undef READ_TEB_FIELD
    KdbpPrint("!teb: Cannot read native TEB %p (status 0x%08lx).\n", (PVOID)Address, Status);
    return TRUE;
}

static BOOLEAN
KdbpCmdPeb(ULONG Argc, PCHAR Argv[])
{
    ULONG_PTR Address;
    EPROCESS Process;
    PEB Peb;
    UNICODE_STRING ImagePath;
    UNICODE_STRING CommandLine;
    UNICODE_STRING CurrentDirectory;
    NTSTATUS Status;

    if (Argc > 2)
    {
        KdbpPrint("Usage: !peb [address]\n");
        return TRUE;
    }

    if (Argc == 2)
    {
        if (!KdbpEvaluateAddress(Argv[1], KdbPromptStr.Length + (Argv[1] - Argv[0]), &Address))
        {
            return TRUE;
        }
    }
    else
    {
        if (!NT_SUCCESS(KdbpSafeReadMemory(&Process, KdbCurrentProcess, sizeof(Process))))
        {
            KdbpPrint("!peb: Cannot read current EPROCESS %p.\n", KdbCurrentProcess);
            return TRUE;
        }
        Address = (ULONG_PTR)Process.Peb;
    }

    if (Address == 0)
    {
        KdbpPrint("!peb: The process has no native PEB.\n");
        return TRUE;
    }

    Status = KdbpSafeReadMemory(&Peb, (PVOID)Address, sizeof(Peb));
    if (!NT_SUCCESS(Status))
    {
        KdbpPrint("!peb: Cannot read native PEB %p (status 0x%08lx).\n", (PVOID)Address, Status);
        return TRUE;
    }

    KdbpPrint("PEB %p\n"
              "  Image base:          %p\n"
              "  Ldr:                 %p\n"
              "  Process parameters:  %p\n"
              "  Process heap:        %p\n"
              "  Being debugged:      %u\n"
              "  NtGlobalFlag:        0x%08lx\n"
              "  Processors/heaps:    %lu / %lu (maximum %lu)\n"
              "  OS version:          %lu.%lu build %u, platform %lu\n"
              "  Image subsystem:     %lu version %lu.%lu\n"
              "  Session:             %lu\n",
              (PVOID)Address,
              Peb.ImageBaseAddress,
              Peb.Ldr,
              Peb.ProcessParameters,
              Peb.ProcessHeap,
              Peb.BeingDebugged,
              Peb.NtGlobalFlag,
              Peb.NumberOfProcessors,
              Peb.NumberOfHeaps,
              Peb.MaximumNumberOfHeaps,
              Peb.OSMajorVersion,
              Peb.OSMinorVersion,
              Peb.OSBuildNumber,
              Peb.OSPlatformId,
              Peb.ImageSubsystem,
              Peb.ImageSubsystemMajorVersion,
              Peb.ImageSubsystemMinorVersion,
              Peb.SessionId);

    if (Peb.ProcessParameters == NULL)
        return TRUE;

#define READ_PARAMS_FIELD(Field, Value) KdbpReadRemoteField(Peb.ProcessParameters, FIELD_OFFSET(RTL_USER_PROCESS_PARAMETERS, Field), &(Value), sizeof(Value))
    if (!NT_SUCCESS(READ_PARAMS_FIELD(ImagePathName, ImagePath)) ||
        !NT_SUCCESS(READ_PARAMS_FIELD(CommandLine, CommandLine)) ||
        !NT_SUCCESS(READ_PARAMS_FIELD(CurrentDirectory.DosPath, CurrentDirectory)))
    {
        KdbpPrint("  Parameters:          <unreadable>\n");
        return TRUE;
    }
#undef READ_PARAMS_FIELD

    KdbpPrint("  Image path:          ");
    KdbpPrintRemoteUnicodeString(&ImagePath);
    KdbpPrint("\n  Command line:        ");
    KdbpPrintRemoteUnicodeString(&CommandLine);
    KdbpPrint("\n  Current directory:   ");
    KdbpPrintRemoteUnicodeString(&CurrentDirectory);
    KdbpPrint("\n");
    return TRUE;
}

/*!\brief Lists loaded modules or the one containing the specified address.
 */
static BOOLEAN
KdbpCmdMod(ULONG Argc, PCHAR Argv[])
{
    ULONGLONG Result = 0;
    ULONG_PTR Address;
    PLDR_DATA_TABLE_ENTRY LdrEntry;
    LDR_DATA_TABLE_ENTRY LdrEntrySnapshot;
    BOOLEAN DisplayOnlyOneModule = FALSE;
    INT i = 0;

    if (Argc >= 2)
    {
        /* Put the arguments back together */
        Argc--;
        while (--Argc >= 1)
            Argv[Argc][strlen(Argv[Argc])] = ' ';

        /* Evaluate the expression */
        if (!KdbpEvaluateExpression(Argv[1], KdbPromptStr.Length + (Argv[1]-Argv[0]), &Result))
        {
            return TRUE;
        }

        if (Result > (ULONGLONG)MAXULONG_PTR)
        {
            KdbpPrint("%s: Address 0x%I64x does not fit in a pointer.\n", Argv[0], Result);
            return TRUE;
        }

        Address = (ULONG_PTR)Result;

        if (!KdbpSymFindModule((PVOID)Address, -1, &LdrEntry))
        {
            KdbpPrint("No module containing address 0x%p found!\n", (PVOID)Address);
            return TRUE;
        }

        DisplayOnlyOneModule = TRUE;
    }
    else
    {
        if (!KdbpSymFindModule(NULL, 0, &LdrEntry))
        {
            ULONG_PTR ntoskrnlBase = (ULONG_PTR)__ImageBase;
            KdbpPrint("  Base      Size      Name\n");
            KdbpPrint("  %p  %08x  %s\n", (PVOID)ntoskrnlBase, 0, "ntoskrnl.exe");
            return TRUE;
        }

        i = 1;
    }

    KdbpPrint("  Base      Size      Name\n");
    for (;;)
    {
        if (!NT_SUCCESS(KdbpSafeReadMemory(&LdrEntrySnapshot, LdrEntry, sizeof(LdrEntrySnapshot))))
        {
            KdbpPrint("mod: Module entry %p is unreadable.\n", LdrEntry);
            break;
        }
        KdbpPrint("  %p  %08x  ", LdrEntrySnapshot.DllBase, LdrEntrySnapshot.SizeOfImage);
        KdbpPrintRemoteUnicodeString(&LdrEntrySnapshot.BaseDllName);
        KdbpPrint("\n");

        if(DisplayOnlyOneModule || !KdbpSymFindModule(NULL, i++, &LdrEntry))
            break;
    }

    return TRUE;
}

/*!\brief Displays GDT, LDT or IDT.
 */
static BOOLEAN
KdbpCmdGdtLdtIdt(ULONG Argc, PCHAR Argv[])
{
#if defined(_M_ARM64)
    KdbpPrint("%s is not an ARM64 descriptor-table command.\n", Argv[0]);
    return TRUE;
#elif defined(_M_AMD64)
    KDESCRIPTOR Reg;
    ULONG Offset;

    UNREFERENCED_PARAMETER(Argc);
    if (Argv[0][0] == 'i')
    {
        __sidt(&Reg.Limit);
        if (Reg.Limit + 1 < sizeof(KIDTENTRY64))
        {
            KdbpPrint("Interrupt descriptor table is empty.\n");
            return TRUE;
        }

        KdbpPrint("IDT Base: %p  Limit: 0x%04x\n", Reg.Base, Reg.Limit);
        KdbpPrint("  Idx  Type       Selector  Handler             DPL  IST\n");
        for (Offset = 0; Offset + sizeof(KIDTENTRY64) - 1 <= Reg.Limit; Offset += sizeof(KIDTENTRY64))
        {
            KIDTENTRY64 Entry;
            ULONG64 Handler;
            PCSTR Type;

            if (!NT_SUCCESS(KdbpSafeReadMemory(&Entry, (PUCHAR)Reg.Base + Offset, sizeof(Entry))))
            {
                KdbpPrint("Couldn't access memory at %p!\n", (PUCHAR)Reg.Base + Offset);
                return TRUE;
            }

            Type = Entry.Type == 0xE ? "INTGATE" : Entry.Type == 0xF ? "TRAPGATE" : "UNKNOWN";
            if (!Entry.Present)
            {
                KdbpPrint("  %03lu  %-9s  [NP]\n", Offset / sizeof(Entry), Type);
                continue;
            }

            Handler = Entry.OffsetLow | ((ULONG64)Entry.OffsetMiddle << 16) | ((ULONG64)Entry.OffsetHigh << 32);
            KdbpPrint("  %03lu  %-9s  0x%04x    %p  %02u   %u\n", Offset / sizeof(Entry), Type, Entry.Selector, (PVOID)(ULONG_PTR)Handler, Entry.Dpl, Entry.IstIndex);
        }
    }
    else
    {
        USHORT LdtSelector = 0;

        __sgdt(&Reg.Limit);
        if (Argv[0][0] == 'l')
        {
            KGDTENTRY64 LdtEntry = {0};
            ULONG DescriptorOffset;
            ULONG64 Base;
            ULONG Limit;

            __sldt(&LdtSelector);
            DescriptorOffset = LdtSelector & ~7U;
            if (LdtSelector == 0 || DescriptorOffset + sizeof(LdtEntry) - 1 > Reg.Limit ||
                !NT_SUCCESS(KdbpSafeReadMemory(&LdtEntry, (PUCHAR)Reg.Base + DescriptorOffset, sizeof(LdtEntry))))
            {
                KdbpPrint("Local descriptor table is empty or inaccessible.\n");
                return TRUE;
            }

            Base = LdtEntry.BaseLow | ((ULONG64)LdtEntry.Bits.BaseMiddle << 16) |
                   ((ULONG64)LdtEntry.Bits.BaseHigh << 24) | ((ULONG64)LdtEntry.BaseUpper << 32);
            Limit = LdtEntry.LimitLow | (LdtEntry.Bits.LimitHigh << 16);
            if (LdtEntry.Bits.Granularity)
                Limit = (Limit << PAGE_SHIFT) | (PAGE_SIZE - 1);
            Reg.Base = (PVOID)(ULONG_PTR)Base;
            Reg.Limit = (USHORT)min(Limit, MAXUSHORT);
            Offset = 0;
        }
        else
        {
            Offset = 8;
        }

        KdbpPrint("%cDT Base: %p  Limit: 0x%04x\n", Argv[0][0] == 'g' ? 'G' : 'L', Reg.Base, Reg.Limit);
        KdbpPrint("  Sel.    Type         Base                Limit       DPL  Attributes\n");
        while (Offset + sizeof(ULONG64) - 1 <= Reg.Limit)
        {
            KGDTENTRY64 Entry = {0};
            ULONG EntrySize = sizeof(ULONG64);
            ULONG64 Base;
            ULONG Limit;
            PCSTR Type;

            if (!NT_SUCCESS(KdbpSafeReadMemory(&Entry, (PUCHAR)Reg.Base + Offset, sizeof(ULONG64))))
            {
                KdbpPrint("Couldn't access memory at %p!\n", (PUCHAR)Reg.Base + Offset);
                return TRUE;
            }

            if (!(Entry.Bits.Type & 0x10) && ((Entry.Bits.Type & 0xF) == 2 || (Entry.Bits.Type & 0xF) == 9 ||
                (Entry.Bits.Type & 0xF) == 11) &&
                Offset + sizeof(Entry) - 1 <= Reg.Limit)
            {
                if (!NT_SUCCESS(KdbpSafeReadMemory((PUCHAR)&Entry + sizeof(ULONG64), (PUCHAR)Reg.Base + Offset + sizeof(ULONG64), sizeof(ULONG64))))
                {
                    KdbpPrint("Couldn't access memory at %p!\n", (PUCHAR)Reg.Base + Offset + sizeof(ULONG64));
                    return TRUE;
                }
                EntrySize = sizeof(Entry);
            }

            Base = Entry.BaseLow | ((ULONG64)Entry.Bits.BaseMiddle << 16) | ((ULONG64)Entry.Bits.BaseHigh << 24);
            if (EntrySize == sizeof(Entry))
                Base |= (ULONG64)Entry.BaseUpper << 32;
            Limit = Entry.LimitLow | (Entry.Bits.LimitHigh << 16);
            if (Entry.Bits.Granularity)
                Limit = (Limit << PAGE_SHIFT) | (PAGE_SIZE - 1);

            if (!(Entry.Bits.Type & 0x10))
                Type = (Entry.Bits.Type & 0xF) == 2 ? "LDT" : (Entry.Bits.Type & 0xF) == 9 ? "TSS64(Avl)" :
                       (Entry.Bits.Type & 0xF) == 11 ? "TSS64(Busy)" : "SYSTEM";
            else if (Entry.Bits.Type & 8)
                Type = Entry.Bits.LongMode ? "CODE64" : "CODE32";
            else
                Type = Entry.Bits.DefaultBig ? "DATA32" : "DATA16";

            if (!Entry.Bits.Present)
                KdbpPrint("  0x%04lx  %-11s  [NP]\n", Offset, Type);
            else
                KdbpPrint("  0x%04lx  %-11s  %p  0x%08lx  %02lu   %s%s%s\n", Offset, Type,
                          (PVOID)(ULONG_PTR)Base, Limit, Entry.Bits.Dpl,
                          Entry.Bits.Granularity ? "G " : "", Entry.Bits.LongMode ? "L " : "",
                          Entry.Bits.DefaultBig ? "DB" : Entry.Bits.System ? "AVL" : "");

            Offset += EntrySize;
        }
    }

    return TRUE;
#else
    KDESCRIPTOR Reg;
    ULONG SegDesc[2];
    ULONG SegBase;
    ULONG SegLimit;
    PCHAR SegType;
    USHORT SegSel;
    UCHAR Type, Dpl;
    INT i;
    ULONG ul;

    if (Argv[0][0] == 'i')
    {
        /* Read IDTR */
        __sidt(&Reg.Limit);

        if (Reg.Limit < 7)
        {
            KdbpPrint("Interrupt descriptor table is empty.\n");
            return TRUE;
        }

        KdbpPrint("IDT Base: 0x%08x  Limit: 0x%04x\n", Reg.Base, Reg.Limit);
        KdbpPrint("  Idx  Type        Seg. Sel.  Offset      DPL\n");

        for (i = 0; (i + sizeof(SegDesc) - 1) <= Reg.Limit; i += 8)
        {
            if (!NT_SUCCESS(KdbpSafeReadMemory(SegDesc, (PVOID)((ULONG_PTR)Reg.Base + i), sizeof(SegDesc))))
            {
                KdbpPrint("Couldn't access memory at 0x%p!\n", (PVOID)((ULONG_PTR)Reg.Base + i));
                return TRUE;
            }

            Dpl = ((SegDesc[1] >> 13) & 3);
            if ((SegDesc[1] & 0x1f00) == 0x0500)        /* Task gate */
                SegType = "TASKGATE";
            else if ((SegDesc[1] & 0x1fe0) == 0x0e00)   /* 32 bit Interrupt gate */
                SegType = "INTGATE32";
            else if ((SegDesc[1] & 0x1fe0) == 0x0600)   /* 16 bit Interrupt gate */
                SegType = "INTGATE16";
            else if ((SegDesc[1] & 0x1fe0) == 0x0f00)   /* 32 bit Trap gate */
                SegType = "TRAPGATE32";
            else if ((SegDesc[1] & 0x1fe0) == 0x0700)   /* 16 bit Trap gate */
                SegType = "TRAPGATE16";
            else
                SegType = "UNKNOWN";

            if ((SegDesc[1] & (1 << 15)) == 0) /* not present */
            {
                KdbpPrint("  %03d  %-10s  [NP]       [NP]        %02d\n", i / 8, SegType, Dpl);
            }
            else if ((SegDesc[1] & 0x1f00) == 0x0500) /* Task gate */
            {
                SegSel = SegDesc[0] >> 16;
                KdbpPrint("  %03d  %-10s  0x%04x                 %02d\n", i / 8, SegType, SegSel, Dpl);
            }
            else
            {
                SegSel = SegDesc[0] >> 16;
                SegBase = (SegDesc[1] & 0xffff0000) | (SegDesc[0] & 0x0000ffff);
                KdbpPrint("  %03d  %-10s  0x%04x     0x%08x  %02d\n", i / 8, SegType, SegSel, SegBase, Dpl);
            }
        }
    }
    else
    {
        ul = 0;

        if (Argv[0][0] == 'g')
        {
            /* Read GDTR */
            Ke386GetGlobalDescriptorTable(&Reg.Limit);
            i = 8;
        }
        else
        {
            ASSERT(Argv[0][0] == 'l');

            /* Read LDTR */
            Ke386GetLocalDescriptorTable(&Reg.Limit);
            Reg.Base = 0;
            i = 0;
            ul = 1 << 2;
        }

        if (Reg.Limit < 7)
        {
            KdbpPrint("%s descriptor table is empty.\n", Argv[0][0] == 'g' ? "Global" : "Local");
            return TRUE;
        }

        KdbpPrint("%cDT Base: 0x%08x  Limit: 0x%04x\n", Argv[0][0] == 'g' ? 'G' : 'L', Reg.Base, Reg.Limit);
        KdbpPrint("  Idx  Sel.    Type         Base        Limit       DPL  Attribs\n");

        for (; (i + sizeof(SegDesc) - 1) <= Reg.Limit; i += 8)
        {
            if (!NT_SUCCESS(KdbpSafeReadMemory(SegDesc, (PVOID)((ULONG_PTR)Reg.Base + i), sizeof(SegDesc))))
            {
                KdbpPrint("Couldn't access memory at 0x%p!\n", (ULONG_PTR)Reg.Base + i);
                return TRUE;
            }

            Dpl = ((SegDesc[1] >> 13) & 3);
            Type = ((SegDesc[1] >> 8) & 0xf);

            SegBase = SegDesc[0] >> 16;
            SegBase |= (SegDesc[1] & 0xff) << 16;
            SegBase |= SegDesc[1] & 0xff000000;
            SegLimit = SegDesc[0] & 0x0000ffff;
            SegLimit |= (SegDesc[1] >> 16) & 0xf;

            if ((SegDesc[1] & (1 << 23)) != 0)
            {
                SegLimit *= 4096;
                SegLimit += 4095;
            }
            else
            {
                SegLimit++;
            }

            if ((SegDesc[1] & (1 << 12)) == 0) /* System segment */
            {
                switch (Type)
                {
                    case  1: SegType = "TSS16(Avl)";    break;
                    case  2: SegType = "LDT";           break;
                    case  3: SegType = "TSS16(Busy)";   break;
                    case  4: SegType = "CALLGATE16";    break;
                    case  5: SegType = "TASKGATE";      break;
                    case  6: SegType = "INTGATE16";     break;
                    case  7: SegType = "TRAPGATE16";    break;
                    case  9: SegType = "TSS32(Avl)";    break;
                    case 11: SegType = "TSS32(Busy)";   break;
                    case 12: SegType = "CALLGATE32";    break;
                    case 14: SegType = "INTGATE32";     break;
                    case 15: SegType = "TRAPGATE32";    break;
                    default: SegType = "UNKNOWN";       break;
                }

                if (!(Type >= 1 && Type <= 3) &&
                    Type != 9 && Type != 11)
                {
                    SegBase = 0;
                    SegLimit = 0;
                }
            }
            else if ((SegDesc[1] & (1 << 11)) == 0) /* Data segment */
            {
                if ((SegDesc[1] & (1 << 22)) != 0)
                    SegType = "DATA32";
                else
                    SegType = "DATA16";
            }
            else /* Code segment */
            {
                if ((SegDesc[1] & (1 << 22)) != 0)
                    SegType = "CODE32";
                else
                    SegType = "CODE16";
            }

            if ((SegDesc[1] & (1 << 15)) == 0) /* Not present */
            {
                KdbpPrint("  %03d  0x%04x  %-11s  [NP]        [NP]        %02d   NP\n", i / 8, i | Dpl | ul, SegType, Dpl);
            }
            else
            {
                KdbpPrint("  %03d  0x%04x  %-11s  0x%08x  0x%08x  %02d  ", i / 8, i | Dpl | ul, SegType, SegBase, SegLimit, Dpl);

                if ((SegDesc[1] & (1 << 12)) == 0) /* System segment */
                {
                    /* FIXME: Display system segment */
                }
                else if ((SegDesc[1] & (1 << 11)) == 0) /* Data segment */
                {
                    if ((SegDesc[1] & (1 << 10)) != 0) /* Expand-down */
                        KdbpPrint(" E");

                    KdbpPrint((SegDesc[1] & (1 << 9)) ? " R/W" : " R");

                    if ((SegDesc[1] & (1 << 8)) != 0)
                        KdbpPrint(" A");
                }
                else /* Code segment */
                {
                    if ((SegDesc[1] & (1 << 10)) != 0) /* Conforming */
                        KdbpPrint(" C");

                    KdbpPrint((SegDesc[1] & (1 << 9)) ? " R/X" : " X");

                    if ((SegDesc[1] & (1 << 8)) != 0)
                        KdbpPrint(" A");
                }

                if ((SegDesc[1] & (1 << 20)) != 0)
                    KdbpPrint(" AVL");

                KdbpPrint("\n");
            }
        }
    }

    return TRUE;
#endif
}

/*!\brief Displays the KPCR
 */
static BOOLEAN
KdbpCmdPcr(ULONG Argc, PCHAR Argv[])
{
    PKIPCR Pcr = (PKIPCR)KeGetPcr();

    KdbpPrint("Current PCR is at 0x%p.\n", Pcr);
#ifdef _M_IX86
    KdbpPrint("  Tib.ExceptionList:         0x%08x\n"
              "  Tib.StackBase:             0x%08x\n"
              "  Tib.StackLimit:            0x%08x\n"
              "  Tib.SubSystemTib:          0x%08x\n"
              "  Tib.FiberData/Version:     0x%08x\n"
              "  Tib.ArbitraryUserPointer:  0x%08x\n"
              "  Tib.Self:                  0x%08x\n"
              "  SelfPcr:                   0x%08x\n"
              "  PCRCB:                     0x%08x\n"
              "  Irql:                      0x%02x\n"
              "  IRR:                       0x%08x\n"
              "  IrrActive:                 0x%08x\n"
              "  IDR:                       0x%08x\n"
              "  KdVersionBlock:            0x%08x\n"
              "  IDT:                       0x%08x\n"
              "  GDT:                       0x%08x\n"
              "  TSS:                       0x%08x\n"
              "  MajorVersion:              0x%04x\n"
              "  MinorVersion:              0x%04x\n"
              "  SetMember:                 0x%08x\n"
              "  StallScaleFactor:          0x%08x\n"
              "  Number:                    0x%02x\n"
              "  L2CacheAssociativity:      0x%02x\n"
              "  VdmAlert:                  0x%08x\n"
              "  L2CacheSize:               0x%08x\n"
              "  InterruptMode:             0x%08x\n"
              , Pcr->NtTib.ExceptionList, Pcr->NtTib.StackBase, Pcr->NtTib.StackLimit,
              Pcr->NtTib.SubSystemTib, Pcr->NtTib.FiberData, Pcr->NtTib.ArbitraryUserPointer,
              Pcr->NtTib.Self
              , Pcr->SelfPcr
              , Pcr->Prcb, Pcr->Irql
              , Pcr->IRR, Pcr->IrrActive , Pcr->IDR
              , Pcr->KdVersionBlock
              , Pcr->IDT, Pcr->GDT, Pcr->TSS
              , Pcr->MajorVersion, Pcr->MinorVersion
              , Pcr->SetMember
              , Pcr->StallScaleFactor
              , Pcr->Number
              , Pcr->SecondLevelCacheAssociativity
              , Pcr->VdmAlert
              , Pcr->SecondLevelCacheSize
              , Pcr->InterruptMode);
#else
#ifdef _M_ARM64
    KdbpPrint("  Self:                          0x%p\n", Pcr->Self);
    KdbpPrint("  CurrentPrcb:                   0x%p\n", Pcr->CurrentPrcb);
    KdbpPrint("  LockArray:                     0x%p\n", Pcr->LockArray);
    KdbpPrint("  Used_Self:                     0x%p\n", Pcr->Used_Self);
    KdbpPrint("  CurrentIrql:                   %u\n", Pcr->CurrentIrql);
    KdbpPrint("  SecondLevelCacheAssociativity: 0x%u\n", Pcr->SecondLevelCacheAssociativity);
    KdbpPrint("  MajorVersion:                  0x%x\n", Pcr->MajorVersion);
    KdbpPrint("  MinorVersion:                  0x%x\n", Pcr->MinorVersion);
    KdbpPrint("  StallScaleFactor:              0x%lx\n", Pcr->StallScaleFactor);
    KdbpPrint("  SecondLevelCacheSize:          0x%lx\n", Pcr->SecondLevelCacheSize);
    KdbpPrint("  KdVersionBlock:                0x%p\n", Pcr->KdVersionBlock);
#else
    KdbpPrint("  GdtBase:                       0x%p\n", Pcr->GdtBase);
    KdbpPrint("  TssBase:                       0x%p\n", Pcr->TssBase);
    KdbpPrint("  UserRsp:                       0x%p\n", (PVOID)Pcr->UserRsp);
    KdbpPrint("  Self:                          0x%p\n", Pcr->Self);
    KdbpPrint("  CurrentPrcb:                   0x%p\n", Pcr->CurrentPrcb);
    KdbpPrint("  LockArray:                     0x%p\n", Pcr->LockArray);
    KdbpPrint("  Used_Self:                     0x%p\n", Pcr->Used_Self);
    KdbpPrint("  IdtBase:                       0x%p\n", Pcr->IdtBase);
    KdbpPrint("  Irql:                          %u\n", Pcr->Irql);
    KdbpPrint("  SecondLevelCacheAssociativity: 0x%u\n", Pcr->SecondLevelCacheAssociativity);
    KdbpPrint("  ObsoleteNumber:                %u\n", Pcr->ObsoleteNumber);
    KdbpPrint("  MajorVersion:                  0x%x\n", Pcr->MajorVersion);
    KdbpPrint("  MinorVersion:                  0x%x\n", Pcr->MinorVersion);
    KdbpPrint("  StallScaleFactor:              0x%lx\n", Pcr->StallScaleFactor);
    KdbpPrint("  SecondLevelCacheSize:          0x%lx\n", Pcr->SecondLevelCacheSize);
    KdbpPrint("  KdVersionBlock:                0x%p\n", Pcr->KdVersionBlock);
#endif
#endif

    return TRUE;
}

static BOOLEAN
KdbpCmdVersion(ULONG Argc, PCHAR Argv[])
{
    UNREFERENCED_PARAMETER(Argv);

    if (Argc != 1)
    {
        KdbpPrint("Usage: version\n");
        return TRUE;
    }

    KdbpPrint("ReactOS NT %lu.%lu build %lu  kernel %p  processors %u  current %u\n", NtMajorVersion, NtMinorVersion, NtBuildNumber & 0xffff, &__ImageBase, (ULONG)(UCHAR)KeNumberProcessors, KeGetCurrentProcessorNumber());
    KdbpPrint("KD %u.%u protocol %u secondary %u machine 0x%04x flags 0x%04x kernbase %p\n",
              KdVersionBlock.MajorVersion,
              KdVersionBlock.MinorVersion,
              KdVersionBlock.ProtocolVersion,
              KdVersionBlock.KdSecondaryVersion,
              KdVersionBlock.MachineType,
              KdVersionBlock.Flags,
              (PVOID)(ULONG_PTR)KdVersionBlock.KernBase);
    return TRUE;
}

static BOOLEAN
KdbpCmdCpu(ULONG Argc, PCHAR Argv[])
{
    CONTEXT Context;
    PKPRCB Prcb;
    PETHREAD Thread;
    ULONG Processor;
    ULONG CurrentProcessor;
    ULONG FrozenState;
    PCHAR End;
    NTSTATUS Status;

    ETHREAD OriginalThread;

#define RESTORE_ORIGINAL_THREAD() (NT_SUCCESS(KdbpSafeReadMemory(&OriginalThread, KdbOriginalThread, sizeof(OriginalThread))) && KdbpAttachToThread(OriginalThread.Cid.UniqueThread))

    CurrentProcessor = KeGetCurrentProcessorNumber();
    if (Argc == 1)
    {
        KdbpPrint("CPU  PRCB                freeze      thread              PC                 SP\n");
        for (Processor = 0; Processor < (ULONG)(UCHAR)KeNumberProcessors; Processor++)
        {
            Prcb = KiProcessorBlock[Processor];
            if (Prcb == NULL)
            {
                KdbpPrint("%3lu  <offline>\n", Processor);
                continue;
            }

            RtlZeroMemory(&Context, sizeof(Context));
            Thread = NULL;
            FrozenState = 0;
            (VOID)KdbpSafeReadMemory(&FrozenState, (PVOID)&Prcb->IpiFrozen, sizeof(FrozenState));
            (VOID)KdbpSafeReadMemory(&Thread, &Prcb->CurrentThread, sizeof(Thread));
            if (Processor == CurrentProcessor && KdbCurrentTrapFrame != NULL)
            {
                Context = *KdbCurrentTrapFrame;
                Status = STATUS_SUCCESS;
            }
            else
            {
                Status = KdbpSafeReadMemory(&Context, &Prcb->ProcessorState.ContextFrame, sizeof(Context));
            }
            KdbpPrint("%c%2lu  %p  0x%08lx  %p  ", Processor == CurrentProcessor ? '*' : ' ', Processor, Prcb, FrozenState, Thread);
            if (NT_SUCCESS(Status) &&
                KdbpContextIsUsable(&Context) &&
                (Processor == CurrentProcessor ||
                 ((FrozenState & ~IPI_FROZEN_FLAG_ACTIVE) == IPI_FROZEN_STATE_FROZEN)))
                KdbpPrint("%p  %p\n", (PVOID)KeGetContextPc(&Context), (PVOID)KeGetContextStackRegister(&Context));
            else
                KdbpPrint("<unavailable>\n");

            if (KdbOutputAborted)
                return TRUE;
        }
        return TRUE;
    }

    if (Argc != 2)
    {
        KdbpPrint("Usage: cpu [number|current]\n");
        return TRUE;
    }

    if (_stricmp(Argv[1], "current") == 0)
    {
        KdbpResetContextRecord(FALSE);
        if (KdbCurrentThread != KdbOriginalThread &&
            !RESTORE_ORIGINAL_THREAD())
        {
            KdbpPrint("cpu: Failed to restore the debugger-entry thread.\n");
            return TRUE;
        }
        KdbpPrint("CPU %lu current context restored.\n", CurrentProcessor);
        KdbpPrintContext(KdbCurrentTrapFrame);
        return TRUE;
    }

    Processor = strtoul(Argv[1], &End, 0);
    if (End == Argv[1] || *End != ANSI_NULL ||
        Processor >= (ULONG)(UCHAR)KeNumberProcessors)
    {
        KdbpPrint("cpu: Invalid processor '%s'.\n", Argv[1]);
        return TRUE;
    }
    if (Processor == CurrentProcessor)
    {
        KdbpResetContextRecord(FALSE);
        if (KdbCurrentThread != KdbOriginalThread &&
            !RESTORE_ORIGINAL_THREAD())
        {
            KdbpPrint("cpu: Failed to restore the debugger-entry thread.\n");
            return TRUE;
        }
        KdbpPrint("CPU %lu is the current processor.\n", Processor);
        KdbpPrintContext(KdbCurrentTrapFrame);
        return TRUE;
    }

    Prcb = KiProcessorBlock[Processor];
    if (Prcb == NULL)
    {
        KdbpPrint("cpu: Processor %lu is offline.\n", Processor);
        return TRUE;
    }
    Status = KdbpSafeReadMemory(&FrozenState, (PVOID)&Prcb->IpiFrozen, sizeof(FrozenState));
    if (!NT_SUCCESS(Status) ||
        ((FrozenState & ~IPI_FROZEN_FLAG_ACTIVE) != IPI_FROZEN_STATE_FROZEN))
    {
        KdbpPrint("cpu: Processor %lu is not in a stable frozen state (state 0x%08lx).\n", Processor, NT_SUCCESS(Status) ? FrozenState : MAXULONG);
        return TRUE;
    }
    Status = KdbpSafeReadMemory(&Context, &Prcb->ProcessorState.ContextFrame, sizeof(Context));
    if (!NT_SUCCESS(Status))
    {
        KdbpPrint("cpu: Failed to read processor %lu context (status 0x%08lx).\n", Processor, Status);
        return TRUE;
    }
    if (!KdbpContextIsUsable(&Context))
    {
        KdbpPrint("cpu: Processor %lu did not publish a usable frozen context.\n", Processor);
        return TRUE;
    }

    KdbpResetContextRecord(FALSE);
    if (KdbCurrentThread != KdbOriginalThread &&
        !RESTORE_ORIGINAL_THREAD())
    {
        KdbpPrint("cpu: Failed to restore the debugger-entry thread.\n");
        return TRUE;
    }
    KdbSavedTrapFrame = KdbCurrentTrapFrame;
    KdbSavedContextRecord = Context;
    KdbCurrentTrapFrame = (PKDB_KTRAP_FRAME)&KdbSavedContextRecord;
    KdbContextRecordActive = TRUE;
    KdbSelectedProcessor = (LONG)Processor;
    KdbFrameBaseValid = FALSE;

    KdbpPrint("CPU %lu frozen context (inspection only):\n", Processor);
    KdbpPrintContext(&KdbSavedContextRecord);
#undef RESTORE_ORIGINAL_THREAD
    return TRUE;
}

#ifdef _M_IX86
/*!\brief Displays the TSS
 */
static BOOLEAN
KdbpCmdTss(ULONG Argc, PCHAR Argv[])
{
    USHORT TssSelector;
    PKTSS Tss = NULL;

    if (Argc >= 2)
    {
        /*
         * Specified TSS via its selector [selector] or descriptor address [*descaddr].
         * Note that we ignore any other argument values.
         */
        PCHAR Param, pszNext;
        ULONG ulValue;

        Param = Argv[1];
        if (Argv[1][0] == '*')
            ++Param;

        ulValue = strtoul(Param, &pszNext, 0);
        if (pszNext && *pszNext)
        {
            KdbpPrint("Invalid TSS specification.\n");
            return TRUE;
        }

        if (Argv[1][0] == '*')
        {
            /* Descriptor specified */
            TssSelector = 0; // Unknown selector!
            // TODO: Room for improvement: Find the TSS descriptor
            // in the GDT so as to validate it.
            Tss = (PKTSS)(ULONG_PTR)ulValue;
            if (!Tss)
            {
                KdbpPrint("Invalid 32-bit TSS descriptor.\n");
                return TRUE;
            }
        }
        else
        {
            /* Selector specified, retrive the corresponding TSS */
            TssSelector = (USHORT)ulValue;
            Tss = KdbpRetrieveTss(TssSelector, NULL, NULL);
            if (!Tss)
            {
                KdbpPrint("Invalid 32-bit TSS selector.\n");
                return TRUE;
            }
        }
    }

    if (!Tss)
    {
        /* If no TSS was specified, use the current TSS descriptor */
        TssSelector = Ke386GetTr();
        Tss = KeGetPcr()->TSS;
        // NOTE: If everything works OK, Tss is the current TSS corresponding to the TR selector.
    }

    KdbpPrint("%s TSS 0x%04x is at 0x%p.\n", (Tss == KeGetPcr()->TSS) ? "Current" : "Specified", TssSelector, Tss);
    // NOTE: Ss1:Esp1 and Ss2:Esp2: are in the NotUsed1 field.
    KdbpPrint("  Backlink:  0x%04x\n"
              "  Ss0:Esp0:  0x%04x:0x%08x\n"
              // NOTE: Ss1:Esp1 and Ss2:Esp2: are in the NotUsed1 field.
              "  CR3:       0x%08x\n"
              "  EFlags:    0x%08x\n"
              "  Eax:       0x%08x\n"
              "  Ebx:       0x%08x\n"
              "  Ecx:       0x%08x\n"
              "  Edx:       0x%08x\n"
              "  Esi:       0x%08x\n"
              "  Edi:       0x%08x\n"
              "  Eip:       0x%08x\n"
              "  Esp:       0x%08x\n"
              "  Ebp:       0x%08x\n"
              "  Cs:        0x%04x\n"
              "  Ss:        0x%04x\n"
              "  Ds:        0x%04x\n"
              "  Es:        0x%04x\n"
              "  Fs:        0x%04x\n"
              "  Gs:        0x%04x\n"
              "  LDT:       0x%04x\n"
              "  Flags:     0x%04x\n"
              "  IoMapBase: 0x%04x\n",
              Tss->Backlink, Tss->Ss0, Tss->Esp0, Tss->CR3, Tss->EFlags,
              Tss->Eax, Tss->Ebx, Tss->Ecx, Tss->Edx, Tss->Esi, Tss->Edi,
              Tss->Eip, Tss->Esp, Tss->Ebp,
              Tss->Cs, Tss->Ss, Tss->Ds, Tss->Es, Tss->Fs, Tss->Gs,
              Tss->LDT, Tss->Flags, Tss->IoMapBase);

    return TRUE;
}
#endif // _M_IX86

/*!\brief Bugchecks the system.
 */
static BOOLEAN
KdbpCmdBugCheck(ULONG Argc, PCHAR Argv[])
{
    /* Set the flag and quit looping */
    KdbpBugCheckRequested = TRUE;
    return FALSE;
}

static BOOLEAN
KdbpCmdReboot(ULONG Argc, PCHAR Argv[])
{
    /* Reboot immediately (we do not return) */
    HalReturnToFirmware(HalRebootRoutine);
    return FALSE;
}

/*!\brief Display debug messages on screen, with paging.
 *
 * Keys for per-page view: Home, End, PageUp, Arrow Up, PageDown,
 * all others are as PageDown.
 */
static BOOLEAN
KdbpCmdDmesg(ULONG Argc, PCHAR Argv[])
{
    ULONG beg, end;

    KdbpIsInDmesgMode = TRUE; /* Toggle logging flag */
    if (!KdpDmesgBuffer)
    {
        KdbpPrint("Dmesg: error, buffer is not allocated! /DEBUGPORT=SCREEN kernel param required for dmesg.\n");
        return TRUE;
    }

    KdbpPrint("*** Dmesg *** TotalWritten=%lu, BufferSize=%lu, CurrentPosition=%lu\n", KdbDmesgTotalWritten, KdpDmesgBufferSize, KdpDmesgCurrentPosition);

    /* Pass data to the pager */
    end = KdpDmesgCurrentPosition;
    beg = (end + KdpDmesgFreeBytes) % KdpDmesgBufferSize;

    /* No roll-overs, and overwritten=lost bytes */
    if (KdbDmesgTotalWritten <= KdpDmesgBufferSize)
    {
        /* Show buffer (KdpDmesgBuffer + beg, num) */
        KdbpPager(KdpDmesgBuffer, KdpDmesgCurrentPosition);
    }
    else
    {
        /* Show 2 buffers: (KdpDmesgBuffer + beg, KdpDmesgBufferSize - beg)
         *            and: (KdpDmesgBuffer,       end) */
        KdbpPager(KdpDmesgBuffer + beg, KdpDmesgBufferSize - beg);
        KdbpPrint("*** Dmesg: buffer rollup ***\n");
        KdbpPager(KdpDmesgBuffer,       end);
    }
    KdbpPrint("*** Dmesg: end of output ***\n");

    KdbpIsInDmesgMode = FALSE; /* Toggle logging flag */

    return TRUE;
}

static BOOLEAN
KdbpJoinArguments(IN ULONG First, IN ULONG Argc, IN PCHAR Argv[], OUT PCHAR Buffer, IN ULONG BufferLength)
{
    ULONG Index;
    ULONG Position = 0;

    if (BufferLength == 0)
        return FALSE;
    Buffer[0] = ANSI_NULL;
    for (Index = First; Index < Argc; Index++)
    {
        SIZE_T Length = strlen(Argv[Index]);

        if ((Position != 0 && Position + 1 >= BufferLength) ||
            Length >= BufferLength - Position - (Position != 0 ? 1 : 0))
        {
            Buffer[0] = ANSI_NULL;
            return FALSE;
        }
        if (Position != 0)
            Buffer[Position++] = ' ';
        RtlCopyMemory(Buffer + Position, Argv[Index], Length);
        Position += (ULONG)Length;
        Buffer[Position] = ANSI_NULL;
    }
    return TRUE;
}

static PKDB_ALIAS_ENTRY
KdbpFindAlias(IN PCSTR Name)
{
    ULONG Index;

    for (Index = 0; Index < RTL_NUMBER_OF(KdbAliases); Index++)
    {
        if (KdbAliases[Index].InUse &&
            _stricmp(KdbAliases[Index].Name, Name) == 0)
        {
            return &KdbAliases[Index];
        }
    }
    return NULL;
}

static BOOLEAN
KdbpCmdEcho(ULONG Argc, PCHAR Argv[])
{
    CHAR Text[1024];

    if (!KdbpJoinArguments(1, Argc, Argv, Text, sizeof(Text)))
    {
        KdbpPrint("echo: Text is too long.\n");
        return TRUE;
    }
    KdbpPrint("%s\n", Text);
    return TRUE;
}

static BOOLEAN
KdbpCmdAlias(ULONG Argc, PCHAR Argv[])
{
    PKDB_ALIAS_ENTRY Alias;
    ULONG Index;

    if (Argc == 1)
    {
        for (Index = 0; Index < RTL_NUMBER_OF(KdbAliases); Index++)
        {
            if (KdbAliases[Index].InUse)
                KdbpPrint("%s = %s\n", KdbAliases[Index].Name, KdbAliases[Index].Command);
        }
        return TRUE;
    }
    if (Argc == 2)
    {
        Alias = KdbpFindAlias(Argv[1]);
        if (Alias == NULL)
            KdbpPrint("alias: '%s' is not defined.\n", Argv[1]);
        else
            KdbpPrint("%s = %s\n", Alias->Name, Alias->Command);
        return TRUE;
    }
    if (strlen(Argv[1]) >= KDB_ALIAS_NAME_LENGTH)
    {
        KdbpPrint("alias: Name is limited to %u characters.\n", KDB_ALIAS_NAME_LENGTH - 1);
        return TRUE;
    }
    for (Index = 0; Index < RTL_NUMBER_OF(KdbDebuggerCommands); Index++)
    {
        if (KdbDebuggerCommands[Index].Name != NULL &&
            _stricmp(KdbDebuggerCommands[Index].Name, Argv[1]) == 0)
        {
            KdbpPrint("alias: '%s' is a built-in command and cannot be shadowed.\n", Argv[1]);
            return TRUE;
        }
    }
    Alias = KdbpFindAlias(Argv[1]);
    if (Alias == NULL)
    {
        for (Index = 0; Index < RTL_NUMBER_OF(KdbAliases); Index++)
        {
            if (!KdbAliases[Index].InUse)
            {
                Alias = &KdbAliases[Index];
                break;
            }
        }
    }
    if (Alias == NULL)
    {
        KdbpPrint("alias: The fixed table of %u aliases is full.\n", KDB_MAX_ALIASES);
        return TRUE;
    }
    if (!KdbpJoinArguments(2, Argc, Argv, Alias->Command, sizeof(Alias->Command)))
    {
        KdbpPrint("alias: Command is limited to %u characters.\n", KDB_ALIAS_COMMAND_LENGTH - 1);
        return TRUE;
    }
    RtlStringCbCopyA(Alias->Name, sizeof(Alias->Name), Argv[1]);
    Alias->InUse = TRUE;
    KdbpPrint("%s = %s\n", Alias->Name, Alias->Command);
    return TRUE;
}

static BOOLEAN
KdbpCmdUnalias(ULONG Argc, PCHAR Argv[])
{
    PKDB_ALIAS_ENTRY Alias;

    if (Argc != 2)
    {
        KdbpPrint("Usage: unalias name\n");
        return TRUE;
    }
    Alias = KdbpFindAlias(Argv[1]);
    if (Alias == NULL)
        KdbpPrint("unalias: '%s' is not defined.\n", Argv[1]);
    else
    {
        RtlZeroMemory(Alias, sizeof(*Alias));
        KdbpPrint("Alias '%s' removed.\n", Argv[1]);
    }
    return TRUE;
}

static BOOLEAN
KdbpCmdLog(ULONG Argc, PCHAR Argv[])
{
    BOOLEAN WasEnabled;
    ULONG Start;
    ULONG First;

    if (Argc > 2)
    {
        KdbpPrint("Usage: log [on|off|clear|show]\n");
        return TRUE;
    }
    if (Argc == 1)
    {
        KdbpPrint("Transcript is %s; %lu of %u bytes retained.\n", KdbTranscriptEnabled ? "enabled" : "disabled", KdbTranscriptLength, KDB_TRANSCRIPT_SIZE);
        return TRUE;
    }
    if (_stricmp(Argv[1], "on") == 0)
    {
        KdbTranscriptEnabled = TRUE;
        KdbpPrint("Transcript enabled.\n");
    }
    else if (_stricmp(Argv[1], "off") == 0)
    {
        KdbpPrint("Transcript disabled.\n");
        KdbTranscriptEnabled = FALSE;
    }
    else if (_stricmp(Argv[1], "clear") == 0)
    {
        WasEnabled = KdbTranscriptEnabled;
        KdbTranscriptEnabled = FALSE;
        RtlZeroMemory(KdbTranscript, sizeof(KdbTranscript));
        KdbTranscriptWrite = KdbTranscriptLength = 0;
        KdbpPrint("Transcript cleared.\n");
        KdbTranscriptEnabled = WasEnabled;
    }
    else if (_stricmp(Argv[1], "show") == 0)
    {
        WasEnabled = KdbTranscriptEnabled;
        KdbTranscriptEnabled = FALSE;
        Start = (KdbTranscriptWrite + KDB_TRANSCRIPT_SIZE - KdbTranscriptLength) %
                KDB_TRANSCRIPT_SIZE;
        First = min(KdbTranscriptLength, KDB_TRANSCRIPT_SIZE - Start);
        KdbpPager(KdbTranscript + Start, First);
        if (KdbTranscriptLength > First)
            KdbpPager(KdbTranscript, KdbTranscriptLength - First);
        KdbTranscriptEnabled = WasEnabled;
    }
    else
    {
        KdbpPrint("Usage: log [on|off|clear|show]\n");
    }
    return TRUE;
}

static BOOLEAN
KdbpCmdScript(ULONG Argc, PCHAR Argv[])
{
    static CHAR Line[KDB_MAX_COMMAND_DEPTH][1024];
    ULONG_PTR Address;
    ULONG_PTR Length;
    ULONG Offset;
    ULONG Position = 0;
    ULONG Commands = 0;
    CHAR Character;
    PCHAR Command;
    BOOLEAN Continue = TRUE;
    NTSTATUS Status;

    if (Argc != 3 ||
        !KdbpGetHexNumber(Argv[1], &Address) ||
        !KdbpGetHexNumber(Argv[2], &Length) ||
        Length == 0 || Length > 65536 ||
        Address > MAXULONG_PTR - Length)
    {
        KdbpPrint("Usage: script address length (maximum 65536 bytes)\n");
        return TRUE;
    }
    Command = Line[min(KdbCommandDepth, KDB_MAX_COMMAND_DEPTH - 1)];
    for (Offset = 0; Offset < Length; Offset++)
    {
        Status = KdbpSafeReadMemory(&Character, (PVOID)(Address + Offset), sizeof(Character));
        if (!NT_SUCCESS(Status))
        {
            KdbpPrint("script: Memory became unreadable at %p (0x%08lx).\n", (PVOID)(Address + Offset), Status);
            return TRUE;
        }
        if (Character != '\r' && Character != '\n' && Character != ANSI_NULL)
        {
            if (Position + 1 >= 1024)
            {
                KdbpPrint("script: Command at offset 0x%lx exceeds 1023 bytes.\n", Offset - Position);
                return TRUE;
            }
            Command[Position++] = Character;
            continue;
        }
        if (Position == 0)
            continue;
        Command[Position] = ANSI_NULL;
        Position = 0;
        while (isspace(*Command))
            Command++;
        if (*Command != ANSI_NULL && *Command != '#')
        {
            if (++Commands > 1024)
            {
                KdbpPrint("script: Stopped at the 1024-command safety limit.\n");
                return TRUE;
            }
            Continue = KdbpDoCommand(Command);
            if (!Continue)
                return FALSE;
        }
        Command = Line[min(KdbCommandDepth, KDB_MAX_COMMAND_DEPTH - 1)];
    }
    if (Position != 0)
    {
        Command[Position] = ANSI_NULL;
        while (isspace(*Command))
            Command++;
        if (*Command != ANSI_NULL && *Command != '#')
        {
            if (++Commands > 1024)
            {
                KdbpPrint("script: Stopped at the 1024-command safety limit.\n");
                return TRUE;
            }
            Continue = KdbpDoCommand(Command);
        }
    }
    return Continue;
}

static BOOLEAN
KdbpCmdRepeat(ULONG Argc, PCHAR Argv[])
{
    static CHAR Template[KDB_MAX_COMMAND_DEPTH][1024];
    static CHAR Execution[KDB_MAX_COMMAND_DEPTH][1024];
    ULONG_PTR Count;
    ULONG Index;
    ULONG Slot = min(KdbCommandDepth, KDB_MAX_COMMAND_DEPTH - 1);

    if (Argc < 3 || !KdbpGetHexNumber(Argv[1], &Count) ||
        Count == 0 || Count > 10000 ||
        !KdbpJoinArguments(2, Argc, Argv, Template[Slot], 1024))
    {
        KdbpPrint("Usage: repeat count command (maximum 10000)\n");
        return TRUE;
    }
    for (Index = 0; Index < Count; Index++)
    {
        RtlStringCbCopyA(Execution[Slot], 1024, Template[Slot]);
        if (!KdbpDoCommand(Execution[Slot]))
            return FALSE;
        if (KdbOutputAborted)
            break;
    }
    return TRUE;
}

static BOOLEAN
KdbpCmdSelfTest(ULONG Argc, PCHAR Argv[])
{
    const ULONG Sentinel = 0x4b444254;
    ULONG ReadBack = 0;
    ULONG_PTR Number;
    ULONG Index;
    ULONG Other;
    ULONG Failures = 0;
    NTSTATUS Status;
    ULONGLONG ExpressionResult;
    LONG ExpressionOffset;
    CHAR ExpressionError[128];

    UNREFERENCED_PARAMETER(Argv);
    if (Argc != 1)
    {
        KdbpPrint("Usage: selftest\n");
        return TRUE;
    }

    Status = KdbpSafeReadMemory(&ReadBack, (PVOID)&Sentinel, sizeof(ReadBack));
    if (!NT_SUCCESS(Status) || ReadBack != Sentinel)
    {
        KdbpPrint("selftest: guarded readable-memory test failed (0x%08lx).\n", Status);
        Failures++;
    }
    Status = KdbpSafeReadMemory(&ReadBack, (PVOID)(ULONG_PTR)1, sizeof(ReadBack));
    if (NT_SUCCESS(Status))
    {
        KdbpPrint("selftest: guarded invalid-memory test unexpectedly succeeded.\n");
        Failures++;
    }

    if (!KdbpGetHexNumber("1234", &Number) || Number != 0x1234 ||
        !KdbpGetHexNumber("0xfeed", &Number) || Number != 0xfeed ||
        KdbpGetHexNumber("1234x", &Number) || KdbpGetHexNumber("", &Number) ||
#ifdef _WIN64
        KdbpGetHexNumber("10000000000000000", &Number)
#else
        KdbpGetHexNumber("100000000", &Number)
#endif
        )
    {
        KdbpPrint("selftest: strict hexadecimal parser test failed.\n");
        Failures++;
    }

    if (KdbpRpnEvaluateExpression("0x10000000000000000", KdbCurrentTrapFrame, &ExpressionResult, &ExpressionOffset, ExpressionError))
    {
        KdbpPrint("selftest: expression overflow test unexpectedly succeeded.\n");
        Failures++;
    }

#if defined(_M_ARM64)
    {
        static const struct
        {
            PCSTR Name;
            ULONG Size;
        } RegisterViews[] =
        {
            { "v31", 16 }, { "q31", 16 }, { "d31", 8 },
            { "s31", 4 }, { "h31", 2 }, { "b31", 1 }
        };
        PVOID Storage;
        ULONG Size;

        for (Index = 0; Index < RTL_NUMBER_OF(RegisterViews); Index++)
        {
            if (!KdbpGetFpRegisterStorage(KdbCurrentTrapFrame, RegisterViews[Index].Name, &Storage, &Size) ||
                Storage != &KdbCurrentTrapFrame->V[31] ||
                Size != RegisterViews[Index].Size)
            {
                KdbpPrint("selftest: ARM64 SIMD view '%s' failed.\n", RegisterViews[Index].Name);
                Failures++;
            }
        }
    }
#endif

    for (Index = 0; Index < RTL_NUMBER_OF(KdbDebuggerCommands); Index++)
    {
        if (KdbDebuggerCommands[Index].Name == NULL)
        {
            if (KdbDebuggerCommands[Index].Syntax != NULL ||
                KdbDebuggerCommands[Index].Help == NULL ||
                KdbDebuggerCommands[Index].Fn != NULL)
            {
                KdbpPrint("selftest: malformed command category at index %lu.\n", Index);
                Failures++;
            }
            continue;
        }
        if (KdbDebuggerCommands[Index].Syntax == NULL ||
            KdbDebuggerCommands[Index].Help == NULL ||
            KdbDebuggerCommands[Index].Fn == NULL)
        {
            KdbpPrint("selftest: incomplete command '%s'.\n", KdbDebuggerCommands[Index].Name);
            Failures++;
        }
        for (Other = Index + 1; Other < RTL_NUMBER_OF(KdbDebuggerCommands); Other++)
        {
            if (KdbDebuggerCommands[Other].Name != NULL &&
                _stricmp(KdbDebuggerCommands[Index].Name, KdbDebuggerCommands[Other].Name) == 0)
            {
                KdbpPrint("selftest: duplicate command '%s'.\n", KdbDebuggerCommands[Index].Name);
                Failures++;
            }
        }
    }

    if (!KdbpDisassemblerSelfTest())
        Failures++;

    if (Failures == 0)
        KdbpPrint("KDB self-test passed: guarded reads, parser, command table, and decoder.\n");
    else
        KdbpPrint("KDB self-test failed: %lu failure(s).\n", Failures);
    return TRUE;
}

/*!\brief Sets or displays a config variables value.
 */
static BOOLEAN
KdbpCmdSet(
    ULONG Argc,
    PCHAR Argv[])
{
    LONG l;
    BOOLEAN First;
    PCHAR pend = 0;
    KDB_ENTER_CONDITION ConditionFirst = KdbDoNotEnter;
    KDB_ENTER_CONDITION ConditionLast = KdbDoNotEnter;

    static const PCHAR ExceptionNames[21] =
    {
        "ZERODEVIDE", "DEBUGTRAP", "NMI", "INT3", "OVERFLOW", "BOUND", "INVALIDOP",
        "NOMATHCOP", "DOUBLEFAULT", "RESERVED(9)", "INVALIDTSS", "SEGMENTNOTPRESENT",
        "STACKFAULT", "GPF", "PAGEFAULT", "RESERVED(15)", "MATHFAULT", "ALIGNMENTCHECK",
        "MACHINECHECK", "SIMDFAULT", "OTHERS"
    };

    if (Argc == 1)
    {
        KdbpPrint("Available settings:\n");
        KdbpPrint("  syntax [intel|at&t]\n");
        KdbpPrint("  condition [exception|*] [first|last] [never|always|kmode|umode]\n");
        KdbpPrint("  break_on_module_load [true|false]\n");
    }
    else if (strcmp(Argv[1], "syntax") == 0)
    {
        if (Argc == 2)
        {
            KdbpPrint("syntax = %s\n", KdbUseIntelSyntax ? "intel" : "at&t");
        }
        else if (Argc >= 3)
        {
            if (_stricmp(Argv[2], "intel") == 0)
                KdbUseIntelSyntax = TRUE;
            else if (_stricmp(Argv[2], "at&t") == 0)
                KdbUseIntelSyntax = FALSE;
            else
                KdbpPrint("Unknown syntax '%s'.\n", Argv[2]);
        }
    }
    else if (strcmp(Argv[1], "condition") == 0)
    {
        if (Argc == 2)
        {
            KdbpPrint("Conditions:                 (First)  (Last)\n");
            for (l = 0; l < RTL_NUMBER_OF(ExceptionNames) - 1; l++)
            {
                if (!ExceptionNames[l])
                    continue;

                if (!KdbpGetEnterCondition(l, TRUE, &ConditionFirst))
                    ASSERT(FALSE);

                if (!KdbpGetEnterCondition(l, FALSE, &ConditionLast))
                    ASSERT(FALSE);

                KdbpPrint("  #%02d  %-20s %-8s %-8s\n", l, ExceptionNames[l],
                          KDB_ENTER_CONDITION_TO_STRING(ConditionFirst),
                          KDB_ENTER_CONDITION_TO_STRING(ConditionLast));
            }

            ASSERT(l == (RTL_NUMBER_OF(ExceptionNames) - 1));
            KdbpPrint("       %-20s %-8s %-8s\n", ExceptionNames[l],
                      KDB_ENTER_CONDITION_TO_STRING(ConditionFirst),
                      KDB_ENTER_CONDITION_TO_STRING(ConditionLast));
        }
        else
        {
            if (Argc >= 5 && strcmp(Argv[2], "*") == 0) /* Allow * only when setting condition */
            {
                l = -1;
            }
            else
            {
                l = strtoul(Argv[2], &pend, 0);

                if (Argv[2] == pend)
                {
                    for (l = 0; l < RTL_NUMBER_OF(ExceptionNames); l++)
                    {
                        if (!ExceptionNames[l])
                            continue;

                        if (_stricmp(ExceptionNames[l], Argv[2]) == 0)
                            break;
                    }
                }

                if (l >= RTL_NUMBER_OF(ExceptionNames))
                {
                    KdbpPrint("Unknown exception '%s'.\n", Argv[2]);
                    return TRUE;
                }
            }

            if (Argc > 4)
            {
                if (_stricmp(Argv[3], "first") == 0)
                    First = TRUE;
                else if (_stricmp(Argv[3], "last") == 0)
                    First = FALSE;
                else
                {
                    KdbpPrint("set condition: second argument must be 'first' or 'last'\n");
                    return TRUE;
                }

                if (_stricmp(Argv[4], "never") == 0)
                    ConditionFirst = KdbDoNotEnter;
                else if (_stricmp(Argv[4], "always") == 0)
                    ConditionFirst = KdbEnterAlways;
                else if (_stricmp(Argv[4], "umode") == 0)
                    ConditionFirst = KdbEnterFromUmode;
                else if (_stricmp(Argv[4], "kmode") == 0)
                    ConditionFirst = KdbEnterFromKmode;
                else
                {
                    KdbpPrint("set condition: third argument must be 'never', 'always', 'umode' or 'kmode'\n");
                    return TRUE;
                }

                if (!KdbpSetEnterCondition(l, First, ConditionFirst))
                {
                    if (l >= 0)
                        KdbpPrint("Couldn't change condition for exception #%02d\n", l);
                    else
                        KdbpPrint("Couldn't change condition for all exceptions\n", l);
                }
            }
            else /* Argc >= 3 */
            {
                if (!KdbpGetEnterCondition(l, TRUE, &ConditionFirst))
                    ASSERT(FALSE);

                if (!KdbpGetEnterCondition(l, FALSE, &ConditionLast))
                    ASSERT(FALSE);

                if (l < (RTL_NUMBER_OF(ExceptionNames) - 1))
                {
                    KdbpPrint("Condition for exception #%02d (%s): FirstChance %s  LastChance %s\n",
                              l, ExceptionNames[l],
                              KDB_ENTER_CONDITION_TO_STRING(ConditionFirst),
                              KDB_ENTER_CONDITION_TO_STRING(ConditionLast));
                }
                else
                {
                    KdbpPrint("Condition for all other exceptions: FirstChance %s  LastChance %s\n",
                              KDB_ENTER_CONDITION_TO_STRING(ConditionFirst),
                              KDB_ENTER_CONDITION_TO_STRING(ConditionLast));
                }
            }
        }
    }
    else if (strcmp(Argv[1], "break_on_module_load") == 0)
    {
        if (Argc == 2)
            KdbpPrint("break_on_module_load = %s\n", KdbBreakOnModuleLoad ? "enabled" : "disabled");
        else if (Argc >= 3)
        {
            if (_stricmp(Argv[2], "enable") == 0 || _stricmp(Argv[2], "enabled") == 0 || _stricmp(Argv[2], "true") == 0)
                KdbBreakOnModuleLoad = TRUE;
            else if (_stricmp(Argv[2], "disable") == 0 || _stricmp(Argv[2], "disabled") == 0 || _stricmp(Argv[2], "false") == 0)
                KdbBreakOnModuleLoad = FALSE;
            else
                KdbpPrint("Unknown setting '%s'.\n", Argv[2]);
        }
    }
    else
    {
        KdbpPrint("Unknown setting '%s'.\n", Argv[1]);
    }

    return TRUE;
}

/*!\brief Displays help screen.
 */
static BOOLEAN
KdbpCmdHelp(
    ULONG Argc,
    PCHAR Argv[])
{
    ULONG i;

    if (Argc > 2)
    {
        KdbpPrint("Usage: help [command]\n");
        return TRUE;
    }

    if (Argc == 2)
    {
        for (i = 0; i < RTL_NUMBER_OF(KdbDebuggerCommands); i++)
        {
            if (KdbDebuggerCommands[i].Name != NULL &&
                _stricmp(KdbDebuggerCommands[i].Name, Argv[1]) == 0)
            {
                KdbpPrint("%s\n  %s\n", KdbDebuggerCommands[i].Syntax, KdbDebuggerCommands[i].Help);
                return TRUE;
            }
        }

        KdbpPrint("help: Unknown command '%s'.\n", Argv[1]);
        return TRUE;
    }

    KdbpPrint("Kernel debugger commands:\n");
    for (i = 0; i < RTL_NUMBER_OF(KdbDebuggerCommands); i++)
    {
        if (!KdbDebuggerCommands[i].Syntax) /* Command group */
        {
            if (i > 0)
                KdbpPrint("\n");

            KdbpPrint("\x1b[7m* %s:\x1b[0m\n", KdbDebuggerCommands[i].Help);
            continue;
        }

        KdbpPrint("  %-20s - %s\n",
                  KdbDebuggerCommands[i].Syntax,
                  KdbDebuggerCommands[i].Help);
    }

    return TRUE;
}


/*
 * memrchr(), explicitly defined, since absent in the CRT.
 * Reverse memchr()
 * Find the last occurrence of 'c' in the buffer 's' of size 'n'.
 */
void *
memrchr(const void *s, int c, size_t n)
{
    const unsigned char *cp;

    if (n != 0)
    {
        cp = (unsigned char *)s + n;
        do
        {
            if (*(--cp) == (unsigned char)c)
                return (void *)cp;
        } while (--n != 0);
    }
    return NULL;
}

/**
 * @brief   Calculate pointer position for N lines above the current position.
 *
 * Calculate pointer position for N lines above the current displaying
 * position within the given buffer. Used by KdbpPager().
 *
 * @param[in]   Buffer
 * Character buffer to operate on.
 *
 * @param[in]   BufLength
 * Size of the buffer.
 *
 * @param[in]   pCurPos
 * Current position within the buffer.
 *
 * @return  Beginning of the previous page of text.
 *
 * @note    N lines count is hardcoded to the terminal's number of rows.
 **/
static PCHAR
CountOnePageUp(
    _In_ PCCH Buffer,
    _In_ ULONG BufLength,
    _In_ PCCH pCurPos,
    _In_ const SIZE* TermSize)
{
    PCCH p;
    // p0 is initial guess of Page Start
    ULONG p0len = TermSize->cx * TermSize->cy;
    PCCH p0 = pCurPos - p0len;
    PCCH prev_p = p0, p1;
    ULONG j;

    if (pCurPos < Buffer)
        pCurPos = Buffer;
    ASSERT(pCurPos <= Buffer + BufLength);

    p = memrchr(p0, '\n', p0len);
    if (!p)
        p = p0;
    for (j = TermSize->cy; j--; )
    {
        int linesCnt;
        p1 = memrchr(p0, '\n', p-p0);
        prev_p = p;
        p = p1;
        if (!p)
        {
            p = prev_p;
            if (!p)
                p = p0;
            break;
        }
        linesCnt = (TermSize->cx+prev_p-p-2) / TermSize->cx;
        if (linesCnt > 1)
            j -= linesCnt-1;
    }

    ASSERT(p != NULL);
    ++p;
    return (PCHAR)p;
}

static VOID
KdpFilterEscapes(
    _Inout_ PSTR String)
{
    PCHAR p;
    SIZE_T i;
    size_t len;

    while ((p = strrchr(String, '\x1b'))) /* Look for escape character */
    {
        len = strlen(p);
        if (p[1] == '[')
        {
            i = 2;
            while (!isalpha(p[i++]));
            memmove(p, p + i, len + 1 - i);
        }
        else
        {
            memmove(p, p + 1, len);
        }
    }
}

/*!\brief Prints the given string with, page by page.
 *
 * \param Buffer     Characters buffer to print.
 * \param BufferLen  Buffer size.
 *
 * \note Doesn't correctly handle \\t and terminal escape sequences when calculating the
 *       number of lines required to print a single line from the Buffer in the terminal.
 *       Maximum length of buffer is limited only by memory size.
 *       Uses KdbPrintf internally.
 *
 * Note: BufLength should be greater than (KdTermSize.cx * KdTermSize.cy).
 */
static VOID
KdbpPagerInternal(
    _In_ PCHAR Buffer,
    _In_ ULONG BufLength,
    _In_ BOOLEAN DoPage)
{
    static BOOLEAN TerminalInitialized = FALSE;
    CHAR c;
    ULONG ScanCode;
    PCHAR p;
    SIZE_T i;
    LONG RowsPrintedByTerminal;

    if (BufLength == 0)
        return;

    /* Check if the user has aborted output of the current command */
    if (KdbOutputAborted)
        return;

    /* Initialize the terminal */
    if (!TerminalInitialized)
    {
        TerminalInitialized = TRUE;
        KdpInitTerminal();
    }

    /* Refresh terminal size each time when number of printed rows is 0 */
    if (KdbNumberOfRowsPrinted == 0)
    {
        KdpUpdateTerminalSize(&KdTermSize);
    }

    /* Loop through the strings */
    p = Buffer;
    while (p[0] != '\0')
    {
        if (DoPage)
        {
            if (p > Buffer + BufLength)
            {
                KdbPrintf("Dmesg: error, p > Buffer+BufLength,d=%d", p - (Buffer + BufLength));
                return;
            }
        }
        i = strcspn(p, "\n");

        if (DoPage)
        {
            /* Are we out of buffer? */
            if (p + i > Buffer + BufLength)
                break; // Leaving pager function
        }

        /* Calculate the number of lines which will be printed in
         * the terminal when outputting the current line. */
        if (i > 0)
            RowsPrintedByTerminal = (i + KdbNumberOfColsPrinted - 1) / KdTermSize.cx;
        else
            RowsPrintedByTerminal = 0;

        if (p[i] == '\n')
            RowsPrintedByTerminal++;

        //KdbPrintf("!%d!%d!%d!%d!", KdbNumberOfRowsPrinted, KdbNumberOfColsPrinted, i, RowsPrintedByTerminal);

        /* Display a prompt if we printed one screen full of text */
        if (KdTermSize.cy > 0 &&
            (LONG)(KdbNumberOfRowsPrinted + RowsPrintedByTerminal) >= KdTermSize.cy)
        {
            PCSTR Prompt;

            /* Disable the repetition of previous command with long many-page output */
            KdbRepeatLastCommand = FALSE;

            if (KdbNumberOfColsPrinted > 0)
                KdbPuts("\n");

            if (DoPage)
                Prompt = "--- Press q to abort, e/End,h/Home,u/PgUp, other key/PgDn ---";
            else
                Prompt = "--- Press q to abort, any other key to continue ---";

            KdbPuts(Prompt);
            c = KdpReadTermKey(&ScanCode);
            if (DoPage) // Show pressed key
                KdbPrintf(" '%c'/scan=%04x\n", c, ScanCode);
            else
                KdbPuts("\n");

            RowsPrintedByTerminal++;

            if (c == 'q')
            {
                KdbOutputAborted = TRUE;
                return;
            }

            if (DoPage)
            {
                if (ScanCode == KEYSC_END || c == 'e')
                {
                    PCHAR pBufEnd = Buffer + BufLength;
                    p = CountOnePageUp(Buffer, BufLength, pBufEnd, &KdTermSize);
                    i = strcspn(p, "\n");
                }
                else if (ScanCode == KEYSC_PAGEUP  ||
                         ScanCode == KEYSC_ARROWUP || c == 'u')
                {
                    p = CountOnePageUp(Buffer, BufLength, p, &KdTermSize);
                    i = strcspn(p, "\n");
                }
                else if (ScanCode == KEYSC_HOME || c == 'h')
                {
                    p = Buffer;
                    i = strcspn(p, "\n");
                }
            }

            KdbNumberOfRowsPrinted = 0;
            KdbNumberOfColsPrinted = 0;
        }

        /* Insert a NUL after the line and print only the current line */
        if (p[i] == '\n' && p[i + 1] != '\0')
        {
            c = p[i + 1];
            p[i + 1] = '\0';
        }
        else
        {
            c = '\0';
        }

        /* Remove escape sequences from the line if there is no terminal connected */
        // FIXME: Dangerous operation since we modify the source string!!
        if (!KdTermConnected)
            KdpFilterEscapes(p);

        /* Print the current line */
        KdbPuts(p);

        /* Restore not null char with saved */
        if (c != '\0')
            p[i + 1] = c;

        /* Set p to the start of the next line and
         * remember the number of printed rows/cols */
        p += i;
        if (p[0] == '\n')
        {
            p++;
            KdbNumberOfColsPrinted = 0;
        }
        else
        {
            ASSERT(p[0] == '\0');
            KdbNumberOfColsPrinted += i;
        }

        KdbNumberOfRowsPrinted += RowsPrintedByTerminal;
    }
}

/*!\brief Prints the given string with, page by page.
 *
 * \param Buffer     Characters buffer to print.
 * \param BufferLen  Buffer size.
 *
 * \note Doesn't correctly handle \\t and terminal escape sequences when calculating the
 *       number of lines required to print a single line from the Buffer in the terminal.
 *       Maximum length of buffer is limited only by memory size.
 *       Uses KdbPrintf internally.
 *
 * Note: BufLength should be greater than (KdTermSize.cx * KdTermSize.cy).
 */
VOID
KdbpPager(
    _In_ PCHAR Buffer,
    _In_ ULONG BufLength)
{
    /* Call the internal function */
    KdbpPagerInternal(Buffer, BufLength, TRUE);
}

/*!\brief Prints the given string with printf-like formatting.
 *
 * \param Format  Format of the string/arguments.
 * \param ...     Variable number of arguments matching the format specified in \a Format.
 *
 * \note Doesn't correctly handle \\t and terminal escape sequences when calculating the
 *       number of lines required to print a single line from the Buffer in the terminal.
 *       Prints maximum 4096 chars, because of its buffer size.
 */
VOID
KdbpPrint(
    _In_ PSTR Format,
    _In_ ...)
{
    static CHAR Buffer[4096];
    INT Result;
    ULONG Length;
    va_list ap;

    /* Check if the user has aborted output of the current command */
    if (KdbOutputAborted)
        return;

    /* Build the string */
    va_start(ap, Format);
    Result = _vsnprintf(Buffer, sizeof(Buffer) - 1, Format, ap);
    if (Result < 0 || Result >= sizeof(Buffer))
        Length = sizeof(Buffer) - 1;
    else
        Length = (ULONG)Result;
    Buffer[Length] = '\0';
    va_end(ap);

    /* Actually print it */
    KdbpPagerInternal(Buffer, Length, FALSE);
}

VOID
KdbpPrintUnicodeString(
    _In_ PCUNICODE_STRING String)
{
    UNICODE_STRING Snapshot;
    NTSTATUS Status;

    if (String == NULL)
    {
        KdbpPrint("<NULL>");
        return;
    }
    Status = KdbpSafeReadMemory(&Snapshot, (PVOID)String, sizeof(Snapshot));
    if (!NT_SUCCESS(Status))
    {
        KdbpPrint("<unreadable UNICODE_STRING: 0x%08lx>", Status);
        return;
    }
    KdbpPrintRemoteUnicodeString(&Snapshot);
}


BOOLEAN
NTAPI
KdbRegisterCliCallback(
    PVOID Callback,
    BOOLEAN Deregister)
{
    ULONG i;

    /* Loop all entries */
    for (i = 0; i < _countof(KdbCliCallbacks); i++)
    {
        /* Check if deregistering was requested */
        if (Deregister)
        {
            /* Check if this entry is the one that was registered */
            if (KdbCliCallbacks[i] == Callback)
            {
                /* Delete it and report success */
                KdbCliCallbacks[i] = NULL;
                return TRUE;
            }
        }
        else
        {
            /* Check if this entry is free */
            if (KdbCliCallbacks[i] == NULL)
            {
                /* Set it and and report success */
                KdbCliCallbacks[i] = Callback;
                return TRUE;
            }
        }
    }

    /* Unsuccessful */
    return FALSE;
}

/*! \brief Invokes registered CLI callbacks until one of them handled the
 *         Command.
 *
 * \param Command - Command line to parse and execute if possible.
 * \param Argc - Number of arguments in Argv
 * \param Argv - Array of strings, each of them containing one argument.
 *
 * \return TRUE, if the command was handled, FALSE if it was not handled.
 */
static
BOOLEAN
KdbpInvokeCliCallbacks(
    IN PCHAR Command,
    IN ULONG Argc,
    IN PCHAR Argv[])
{
    ULONG i;

    /* Loop all entries */
    for (i = 0; i < _countof(KdbCliCallbacks); i++)
    {
        /* Check if this entry is registered */
        if (KdbCliCallbacks[i])
        {
            /* Invoke the callback and check if it handled the command */
            if (KdbCliCallbacks[i](Command, Argc, Argv))
            {
                return TRUE;
            }
        }
    }

    /* None of the callbacks handled the command */
    return FALSE;
}


/*!\brief Parses command line and executes command if found
 *
 * \param Command    Command line to parse and execute if possible.
 *
 * \retval TRUE   Don't continue execution.
 * \retval FALSE  Continue execution (leave KDB)
 */
static BOOLEAN
KdbpDoCommand(
    IN PCHAR Command)
{
    BOOLEAN Continue = TRUE;
    SIZE_T i;
    SIZE_T CommandLength;
    PCHAR p;
    ULONG Argc;
    PKDB_COMMAND_FRAME Frame;
    PKDB_ALIAS_ENTRY Alias;
    ULONG Position;

    if (Command == NULL)
        return TRUE;
    if (KdbCommandDepth >= KDB_MAX_COMMAND_DEPTH)
    {
        KdbPrintf("Command recursion exceeded the %u-level safety limit.\n", KDB_MAX_COMMAND_DEPTH);
        return TRUE;
    }
    CommandLength = strnlen(Command, sizeof(KdbCommandFrames[0].Original));
    if (CommandLength >= sizeof(KdbCommandFrames[0].Original))
    {
        KdbPrintf("Command exceeds the 1023-byte safety limit.\n");
        return TRUE;
    }
    Frame = &KdbCommandFrames[KdbCommandDepth++];
    RtlStringCbCopyA(Frame->Original, sizeof(Frame->Original), Command);

    if (KdbTranscriptEnabled)
    {
        static const CHAR Prefix[] = "kdb:> ";
        static const CHAR Newline[] = "\n";
        KdbpCaptureOutput(Prefix, sizeof(Prefix) - 1);
        KdbpCaptureOutput(Frame->Original, (USHORT)CommandLength);
        KdbpCaptureOutput(Newline, sizeof(Newline) - 1);
    }

    Argc = 0;
    p = Command;

    for (;;)
    {
        while (*p == '\t' || *p == ' ')
            p++;

        if (*p == '\0')
            break;

        if (Argc == RTL_NUMBER_OF(Frame->Argv))
        {
            KdbPrintf("Command exceeds the 256-token safety limit.\n");
            goto Done;
        }
        i = strcspn(p, "\t ");
        Frame->Argv[Argc++] = p;
        p += i;
        if (*p == '\0')
            break;

        *p = '\0';
        p++;
    }

    if (Argc < 1)
        goto Done;

    /* Reset the pager state: number of printed rows/cols and aborted output flag */
    KdbNumberOfRowsPrinted = KdbNumberOfColsPrinted = 0;
    KdbOutputAborted = FALSE;

    Alias = KdbpFindAlias(Frame->Argv[0]);
    if (Alias != NULL)
    {
        Position = (ULONG)strlen(Alias->Command);
        if (Position >= sizeof(Frame->Expanded))
        {
            KdbPrintf("Alias '%s' is corrupt or too long.\n", Alias->Name);
            goto Done;
        }
        RtlCopyMemory(Frame->Expanded, Alias->Command, Position);
        Frame->Expanded[Position] = ANSI_NULL;
        for (i = 1; i < Argc; i++)
        {
            SIZE_T Length = strlen(Frame->Argv[i]);

            if (Position + 1 >= sizeof(Frame->Expanded) ||
                Length >= sizeof(Frame->Expanded) - Position - 1)
            {
                KdbPrintf("Expanded alias '%s' exceeds 1023 bytes.\n", Alias->Name);
                goto Done;
            }
            Frame->Expanded[Position++] = ' ';
            RtlCopyMemory(Frame->Expanded + Position, Frame->Argv[i], Length);
            Position += (ULONG)Length;
            Frame->Expanded[Position] = ANSI_NULL;
        }
        Continue = KdbpDoCommand(Frame->Expanded);
        goto Done;
    }

    for (i = 0; i < RTL_NUMBER_OF(KdbDebuggerCommands); i++)
    {
        if (!KdbDebuggerCommands[i].Name)
            continue;

        if (strcmp(KdbDebuggerCommands[i].Name, Frame->Argv[0]) == 0)
        {
            Continue = KdbDebuggerCommands[i].Fn(Argc, Frame->Argv);
            goto Done;
        }
    }

    /* Now invoke the registered callbacks */
    if (KdbpInvokeCliCallbacks(Command, Argc, Frame->Argv))
        goto Done;

    KdbPrintf("Command '%s' is unknown.\n", Frame->Original);

Done:
    KdbOutputAborted = FALSE;
    KdbCommandDepth--;
    return Continue;
}

/*!\brief KDB Main Loop.
 *
 * \param EnteredOnSingleStep  TRUE if KDB was entered on single step.
 */
VOID
KdbpCliMainLoop(
    IN BOOLEAN EnteredOnSingleStep)
{
    BOOLEAN Continue = TRUE;
    static CHAR Command[1024];
    static CHAR LastCommand[1024] = "";

// FIXME HACK: SYSREG SUPPORT CORE-19807 -- Emit a backtrace.
// TODO: Remove once SYSREG "bt" command emission is fixed!
#if 1
    KdbpDoCommand("bt");
#endif

    if (EnteredOnSingleStep)
    {
        if (!KdbSymPrintAddress((PVOID)KeGetContextPc(KdbCurrentTrapFrame), KdbCurrentTrapFrame))
            KdbPrintf("<%p>", KeGetContextPc(KdbCurrentTrapFrame));

        KdbPuts(": ");
        if (KdbpDisassemble(KeGetContextPc(KdbCurrentTrapFrame), KdbUseIntelSyntax) < 0)
            KdbPuts("<INVALID>");
        KdbPuts("\n");
    }
    else
    {
        /* Preceding this message is one of the "Entered debugger..." banners */
        // KdbPuts("\nEntered debugger\n");
        KdbPuts("\nType \"help\" for a list of commands.\n");
    }

    /* Main loop */
    while (Continue)
    {
        /*
         * Print the prompt and read a command.
         * Repeat the last one if the user pressed Enter.
         * This reduces the risk of RSI when single-stepping!
         */
        // TEMP HACK! Issue an empty string instead of duplicating "kdb:>"
        SIZE_T CmdLen = KdbPrompt(/*KdbPromptStr.Buffer*/"", Command, sizeof(Command));
        if (CmdLen == 0)
        {
            /* Nothing received but the user didn't press Enter, retry */
            continue;
        }
        else if (CmdLen > 1) // i.e. (*Command != ANSI_NULL)
        {
            /* Save this new last command */
            KdbRepeatLastCommand = TRUE;
            RtlStringCbCopyA(LastCommand, sizeof(LastCommand), Command);

            /* Remember it */
            KdbpCommandHistoryAppend(Command);
        }
        else if (KdbRepeatLastCommand)
        {
            /* The user directly pressed Enter */
            RtlStringCbCopyA(Command, sizeof(Command), LastCommand);
        }

        /* Invoke the command */
        Continue = KdbpDoCommand(Command);
    }
}

/**
 * @brief
 * Interprets the KDBinit file from the \SystemRoot\System32\drivers\etc
 * directory, that has been loaded by KdbpCliInit().
 *
 * This function is used to interpret the init file in the debugger context
 * with a trap frame set up. KdbpCliInit() enters the debugger by calling
 * DbgBreakPointWithStatus(DBG_STATUS_CONTROL_C). In turn, this will call
 * KdbEnterDebuggerException() which will finally call this function if
 * KdbInitFileBuffer is not NULL.
 **/
VOID
KdbpCliInterpretInitFile(VOID)
{
    PCHAR p1, p2;

    p1 = InterlockedExchangePointer((PVOID*)&KdbInitFileBuffer, NULL);
    if (!p1)
        return;

    /* Execute the commands in the init file */
    KdbPuts("KDB: Executing KDBinit file...\n");
    while (p1[0] != '\0')
    {
        size_t i = strcspn(p1, "\r\n");
        if (i > 0)
        {
            CHAR c = p1[i];
            p1[i] = '\0';

            /* Look for "break" command and comments */
            p2 = p1;
            while (isspace(p2[0]))
                p2++;

            if (strncmp(p2, "break", sizeof("break")-1) == 0 &&
                (p2[sizeof("break")-1] == '\0' || isspace(p2[sizeof("break")-1])))
            {
                /* The direct startup path has no trapped register context. */
                if (KdbCurrentTrapFrame == NULL)
                {
                    KdbPuts("KDB: Ignoring KDBinit break without a trapped context.\n");
                }
                else
                {
                    KdbpCliMainLoop(FALSE);
                }
            }
            else if (p2[0] != '#' && p2[0] != '\0') /* Ignore empty lines and comments */
            {
                /* Invoke the command */
                KdbpDoCommand(p1);
            }

            p1[i] = c;
        }

        p1 += i;
        while (p1[0] == '\r' || p1[0] == '\n')
            p1++;
    }
    KdbPuts("KDB: KDBinit executed\n");
}

/**
 * @brief   Called when KDB is initialized.
 *
 * Loads the KDBinit file from the \SystemRoot\System32\drivers\etc
 * directory and interprets it, by calling back into the debugger.
 **/
NTSTATUS
KdbpCliInit(VOID)
{
    NTSTATUS Status;
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING FileName;
    IO_STATUS_BLOCK Iosb;
    FILE_STANDARD_INFORMATION FileStdInfo;
    HANDLE hFile = NULL;
    ULONG FileSize;
    PCHAR FileBuffer;

    /* Don't load the KDBinit file if its buffer is already lying around */
    if (KdbInitFileBuffer)
        return STATUS_SUCCESS;

    /* Initialize the object attributes */
    RtlInitUnicodeString(&FileName, L"\\SystemRoot\\System32\\drivers\\etc\\KDBinit");
    InitializeObjectAttributes(&ObjectAttributes,
                               &FileName,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);

    /* Open the file */
    Status = ZwOpenFile(&hFile, FILE_READ_DATA | SYNCHRONIZE,
                        &ObjectAttributes, &Iosb, 0,
                        FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT |
                        FILE_NO_INTERMEDIATE_BUFFERING);
    if (!NT_SUCCESS(Status))
    {
        DPRINT("Could not open %wZ (Status 0x%lx)\n", &FileName, Status);
        return Status;
    }

    /* Get the size of the file */
    Status = ZwQueryInformationFile(hFile, &Iosb,
                                    &FileStdInfo, sizeof(FileStdInfo),
                                    FileStandardInformation);
    if (!NT_SUCCESS(Status))
    {
        ZwClose(hFile);
        DPRINT1("Could not query size of %wZ (Status 0x%lx)\n", &FileName, Status);
        return Status;
    }
    FileSize = FileStdInfo.EndOfFile.u.LowPart;

    /* Allocate memory for the file (add 1 byte for terminating NUL) */
    FileBuffer = ExAllocatePool(NonPagedPool, FileSize + 1);
    if (!FileBuffer)
    {
        ZwClose(hFile);
        DPRINT1("Could not allocate %lu bytes for KDBinit file\n", FileSize);
        return Status;
    }

    /* Load file into memory */
    Status = ZwReadFile(hFile, NULL, NULL, NULL, &Iosb,
                        FileBuffer, FileSize, NULL, NULL);
    ZwClose(hFile);

    if (!NT_SUCCESS(Status) && (Status != STATUS_END_OF_FILE))
    {
        ExFreePool(FileBuffer);
        DPRINT1("Could not read KDBinit file into memory (Status 0x%lx)\n", Status);
        return Status;
    }

    FileSize = min(FileSize, (ULONG)Iosb.Information);
    FileBuffer[FileSize] = ANSI_NULL;

    /*
     * Interpret the KDBinit file.
     *
     * Upstream triggers a breakpoint here (DbgBreakPointWithStatus) so that
     * KdbEnterDebuggerException re-enters KDB and runs KdbpCliInterpretInitFile()
     * from inside the debugger. On ARM64 SMP that round-trip is unreliable: the
     * brk #0xF000 intermittently fails to be consumed/resumed and escapes as an
     * unhandled STATUS_BREAKPOINT in this system-thread context, bugchecking
     * 0x7E (SYSTEM_THREAD_EXCEPTION_NOT_HANDLED) ~12s into boot (right after
     * "KDBinit executed"). The init file is interpreted at startup with no
     * meaningful trapped context, so run it directly instead of via the fragile
     * breakpoint. KdbpCliInterpretInitFile() takes ownership of
     * KdbInitFileBuffer (it exchanges it back to NULL).
     *
     * NOTE: a "break" directive in KDBinit (interactive stop) still needs the
     * real debugger context; it is intentionally unsupported on this path. The
     * default KDBinit only issues "set" commands, which are context-free.
     */
    InterlockedExchangePointer((PVOID*)&KdbInitFileBuffer, FileBuffer);
    KdbpCliInterpretInitFile();

    ExFreePool(FileBuffer);

    return STATUS_SUCCESS;
}


/**
 * @brief   Debug logger function.
 *
 * This function writes text strings into KdpDmesgBuffer, using it as
 * a circular buffer. KdpDmesgBuffer contents can be later (re)viewed
 * using the dmesg command. KdbDebugPrint() protects KdpDmesgBuffer
 * from simultaneous writes by use of KdpDmesgLogSpinLock.
 **/
static VOID
NTAPI
KdbDebugPrint(
    _In_ PCCH String,
    _In_ ULONG Length)
{
    BOOLEAN LockAcquired;
    KIRQL OldIrql;
    ULONG beg, end, num;

    /* Avoid recursive calling if we already are in Dmesg mode */
    if (KdbpIsInDmesgMode)
       return;

    if (KdpDmesgBuffer == NULL)
        return;

    /* Acquire the printing spinlock without waiting at raised IRQL */
    LockAcquired = KdbpAcquireLock(&KdpDmesgLogSpinLock, &OldIrql);
    if (!LockAcquired)
    {
        KdbpReleaseLock(&KdpDmesgLogSpinLock, OldIrql, LockAcquired);
        return;
    }

    beg = KdpDmesgCurrentPosition;
    /* Invariant: always_true(KdpDmesgFreeBytes == KdpDmesgBufferSize); */
    num = min(Length, KdpDmesgFreeBytes);
    if (num != 0)
    {
        end = (beg + num) % KdpDmesgBufferSize;
        if (end > beg)
        {
            RtlCopyMemory(KdpDmesgBuffer + beg, String, Length);
        }
        else
        {
            RtlCopyMemory(KdpDmesgBuffer + beg, String, KdpDmesgBufferSize - beg);
            RtlCopyMemory(KdpDmesgBuffer, String + (KdpDmesgBufferSize - beg), end);
        }
        KdpDmesgCurrentPosition = end;

        /* Counting the total bytes written */
        KdbDmesgTotalWritten += num;
    }

    /* Release the spinlock */
    KdbpReleaseLock(&KdpDmesgLogSpinLock, OldIrql, LockAcquired);

    /* Optional step(?): find out a way to notify about buffer exhaustion,
     * and possibly fall into kbd to use dmesg command: user will read
     * debug strings before they will be wiped over by next writes. */
}

/**
 * @brief   Initializes the KDBG debugger.
 *
 * @param[in]   DispatchTable
 * Pointer to the KD dispatch table.
 *
 * @param[in]   BootPhase
 * Phase of initialization.
 *
 * @return  A status value.
 * @note    Also known as "KdpKdbgInit".
 **/
NTSTATUS
NTAPI
KdbInitialize(
    _In_ PKD_DISPATCH_TABLE DispatchTable,
    _In_ ULONG BootPhase)
{
    /* Saves the different symbol-loading status across boot phases */
    static ULONG LoadSymbols = 0;

    if (BootPhase == 0)
    {
        /* Write out the functions that we support for now */
        DispatchTable->KdpPrintRoutine = KdbDebugPrint;

        /* Check if we have a command line */
        if (KeLoaderBlock && KeLoaderBlock->LoadOptions)
        {
            /* Get the KDBG Settings */
            KdbpGetCommandLineSettings(KeLoaderBlock->LoadOptions);
        }

        /* Register for BootPhase 1 initialization and as a Provider */
        DispatchTable->KdpInitRoutine = KdbInitialize;
        InsertTailList(&KdProviders, &DispatchTable->KdProvidersList);
    }
    else if (BootPhase == 1)
    {
        /* Register for later BootPhase 2 reinitialization */
        DispatchTable->KdpInitRoutine = KdbInitialize;

        /* Initialize Dmesg support */

        /* Allocate a buffer for Dmesg log buffer. +1 for terminating null,
         * see kdbp_cli.c:KdbpCmdDmesg()/2 */
        KdpDmesgBuffer = ExAllocatePoolZero(NonPagedPool,
                                            KdpDmesgBufferSize + 1,
                                            TAG_KDBG);
        /* Ignore failure if KdpDmesgBuffer is NULL */
        KdpDmesgFreeBytes = KdpDmesgBufferSize;
        KdbDmesgTotalWritten = 0;

        /* Initialize spinlock */
        KeInitializeSpinLock(&KdpDmesgLogSpinLock);
    }

    /* Initialize symbols support in BootPhase 0 and 1 */
    if (BootPhase <= 1)
    {
        LoadSymbols <<= 1;
        LoadSymbols |= KdbSymInit(BootPhase);
    }

    if (BootPhase == 1)
    {
        /* Announce ourselves */
        CHAR buffer[60];
        RtlStringCbPrintfA(buffer, sizeof(buffer),
                           "   KDBG debugger enabled - %s\r\n",
                           !(LoadSymbols & 0x2) ? "No symbols loaded" :
                           !(LoadSymbols & 0x1) ? "Kernel symbols loaded"
                                                : "Loading symbols");
        HalDisplayString(buffer);
    }

    if (BootPhase >= 2)
    {
        /* I/O is now set up for disk access: load the KDBinit file */
        NTSTATUS Status = KdbpCliInit();

        /* Schedule an I/O reinitialization if needed */
        if (Status == STATUS_OBJECT_NAME_NOT_FOUND ||
            Status == STATUS_OBJECT_PATH_NOT_FOUND)
        {
            DispatchTable->KdpInitRoutine = KdbInitialize;
        }
    }

    return STATUS_SUCCESS;
}

/* EOF */
