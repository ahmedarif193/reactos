/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            ntoskrnl/arch/arm64/ke/boot.c
 * PURPOSE:         Kernel entry stubs for ARM64
 */

#include <ntoskrnl.h>
#include <ntstrsafe.h>
#define NDEBUG
#include <debug.h>

typedef struct _ARM64_EARLY_GPRS
{
    UINT64 X0;
    UINT64 X1;
    UINT64 X2;
    UINT64 X3;
    UINT64 Sp;
} ARM64_EARLY_GPRS, *PARM64_EARLY_GPRS;

VOID
KiInitializeSystem(_Inout_ PLOADER_PARAMETER_BLOCK LoaderBlock);

VOID
KiArm64EarlyVectorHandler(_In_ UINT64 VectorId,
                          _In_ UINT64 ExceptionSyndrome,
                          _In_ UINT64 FaultAddress,
                          _In_opt_ PARM64_EARLY_GPRS Registers);

/* PL011 defaults mirror the QEMU virt platform; the loader will override them
 * whenever it exposes a concrete UART configuration. */
#define ARM64_EARLY_UART_PHYS_BASE  0x09000000ULL
#define ARM64_PL011_DR              0x00
#define ARM64_PL011_FR              0x18
#define ARM64_PL011_FR_TXFF         (1u << 5)

#define ARM64_KSEG0_BASE            0xFFFF800000000000ULL
/* Memory attribute indices in MAIR_EL1 */
#define ARM64_MEM_ATTR_DEVICE_nGnRnE 0x0ULL
#define ARM64_MEM_ATTR_NORMAL_WB     0x4ULL
#define ARM64_PTE_TYPE_BLOCK        0x1ULL
#define ARM64_PTE_TYPE_TABLE        0x3ULL
#define ARM64_PTE_BLOCK_ATTR_INDEX_SHIFT 2
#define ARM64_PTE_BLOCK_INNER_SHARE (3ULL << 8)
#define ARM64_PTE_BLOCK_AF          (1ULL << 10)
#define ARM64_PTE_BLOCK_PXN         (1ULL << 53)
#define ARM64_PTE_BLOCK_UXN         (1ULL << 54)
#define ARM64_PTE_TABLE_NSTABLE     (1ULL << 63)
#define ARM64_IDENTITY_MIN_BYTES    (512ULL << 20) /* 512 MB */
#define ARM64_L1_BLOCK_SHIFT        30
#define ARM64_L2_BLOCK_SHIFT        21
#define ARM64_L2_BLOCK_SIZE         (1ULL << ARM64_L2_BLOCK_SHIFT)
#define ARM64_L1_MAX_ENTRIES        512

#define ARM64_IDENTITY_L0_ENTRIES   512
#define ARM64_IDENTITY_L1_ENTRIES   512
#define ARM64_IDENTITY_L2_ENTRIES   (512 * 512)

/* Backing storage for identity tables; pointers are aligned at runtime. */
static UINT64 KiArm64IdentityL0Backing[ARM64_IDENTITY_L0_ENTRIES +
                                       (PAGE_SIZE / sizeof(UINT64))];
static UINT64 KiArm64IdentityL1Backing[ARM64_IDENTITY_L1_ENTRIES +
                                       (PAGE_SIZE / sizeof(UINT64))];
static UINT64 KiArm64IdentityL2Backing[ARM64_IDENTITY_L2_ENTRIES +
                                       (PAGE_SIZE / sizeof(UINT64))];

extern const UINT64 KiArm64EarlyVectorTable[];

static UINT64 *KiArm64IdentityL0;
static UINT64 *KiArm64IdentityL1;
static UINT64 (*KiArm64IdentityL2)[512];

typedef struct _ARM64_EARLY_TRAP_STATE
{
    ARM64_EARLY_GPRS Registers;
    UINT64 VectorId;
    UINT64 ExceptionSyndrome;
    UINT64 FaultAddress;
    UINT64 Elr;
    UINT64 Spsr;
} ARM64_EARLY_TRAP_STATE, *PARM64_EARLY_TRAP_STATE;

static ARM64_EARLY_TRAP_STATE KiArm64LastTrapState;
static BOOLEAN KiArm64TrapStateValid = FALSE;

static const PCSTR KiArm64EsrClassNames[64] =
{
    [0x00] = "Unknown",
    [0x01] = "WFI/WFE trap",
    [0x03] = "CP15 RT trap",
    [0x04] = "CP15 R trap",
    [0x05] = "CP15 W trap",
    [0x07] = "FP/SIMD access",
    [0x08] = "MCRR/MRRC trap",
    [0x0C] = "SVE access",
    [0x11] = "SVC in AArch32",
    [0x12] = "HVC in AArch32",
    [0x13] = "SMC in AArch32",
    [0x15] = "SVC in AArch64",
    [0x16] = "HVC in AArch64",
    [0x17] = "SMC in AArch64",
    [0x18] = "MSR/MRS trap",
    [0x1C] = "Instruction abort (lower EL)",
    [0x1D] = "Instruction abort (same EL)",
    [0x20] = "Data abort (lower EL)",
    [0x21] = "Data abort (same EL)",
    [0x22] = "SP alignment fault",
    [0x24] = "FP exception",
    [0x26] = "FP exception (AArch64)",
    [0x2F] = "SError interrupt",
};

static const PCSTR KiArm64VectorNames[16] =
{
    [0]  = "Sync SP0",
    [1]  = "IRQ SP0",
    [2]  = "FIQ SP0",
    [3]  = "SError SP0",
    [4]  = "Sync SPx",
    [5]  = "IRQ SPx",
    [6]  = "FIQ SPx",
    [7]  = "SError SPx",
    [8]  = "Sync lower A64",
    [9]  = "IRQ lower A64",
    [10] = "FIQ lower A64",
    [11] = "SError lower A64",
    [12] = "Sync lower A32",
    [13] = "IRQ lower A32",
    [14] = "FIQ lower A32",
    [15] = "SError lower A32",
};

typedef struct _ARM64_BOOT_CONTEXT
{
    PLOADER_PARAMETER_BLOCK LoaderBlock;
    BOOLEAN MmuEnabled;
    UINT64 SctlrEl1;
    UINT64 TcrEl1;
    UINT64 Ttbr0El1;
    UINT64 Ttbr1El1;
    UINT64 MairEl1;
    UINT64 UartPhysicalBase;
} ARM64_BOOT_CONTEXT, *PARM64_BOOT_CONTEXT;

static volatile ULONG *KiArm64BootUartBase = (volatile ULONG *)ARM64_EARLY_UART_PHYS_BASE;
static BOOLEAN KiArm64BootSerialReady = FALSE;

/* Boot stack (CPU0): mirror amd64 boot stack handoff behavior */
UCHAR DECLSPEC_ALIGN(16) KiArm64P0BootStackData[KERNEL_STACK_SIZE] = {0};
PVOID KiArm64P0BootStack = &KiArm64P0BootStackData[KERNEL_STACK_SIZE];

/* Assembly helper that switches SP then branches into the C wrapper */
DECLSPEC_NORETURN VOID KiArm64SwitchToBootStack(ULONG_PTR InitialStack,
                                                PLOADER_PARAMETER_BLOCK LoaderBlock);
CODE_SEG("INIT") DECLSPEC_NORETURN VOID NTAPI KiArm64SystemStartupBootStack(_Inout_ PLOADER_PARAMETER_BLOCK LoaderBlock);

static __inline VOID KiArm64VectorUartPutc(char Ch)
{
    volatile ULONG *Uart = (volatile ULONG *)(ULONG_PTR)ARM64_EARLY_UART_PHYS_BASE;
    if (!Uart) return;
    /* Bound the wait to avoid wedging on a stuck UART */
    for (ULONG spins = 0x10000;
         (Uart[ARM64_PL011_FR / sizeof(ULONG)] & ARM64_PL011_FR_TXFF) && spins != 0;
         --spins)
    {
        __asm__ __volatile__("wfi");
    }
    Uart[ARM64_PL011_DR / sizeof(ULONG)] = (ULONG)(unsigned char)Ch;
}

static __inline VOID KiArm64VectorUartPuts(const char *S)
{
    if (!S) return;
    while (*S)
    {
        if (*S == '\n') KiArm64VectorUartPutc('\r');
        KiArm64VectorUartPutc(*S++);
    }
}

static __inline VOID KiArm64VectorUartPutHex64(ULONGLONG V)
{
    static const char H[] = "0123456789ABCDEF";
    for (int i = 15; i >= 0; --i)
    {
        ULONG shift = (ULONG)i * 4;
        KiArm64VectorUartPutc(H[(V >> shift) & 0xFULL]);
    }
}

CODE_SEG("INIT")
static VOID
KiArm64EmitEntryMarker(VOID)
{
    volatile ULONG *Uart = (volatile ULONG *)(ULONG_PTR)ARM64_EARLY_UART_PHYS_BASE;
    ULONG Wait = 0x100000;

    if (!Uart)
        return;

    while ((Uart[ARM64_PL011_FR / sizeof(ULONG)] & ARM64_PL011_FR_TXFF) && --Wait)
        ;

    Uart[ARM64_PL011_DR / sizeof(ULONG)] = (ULONG)'K';
}

CODE_SEG("INIT")
static UINT64
KiArm64VirtualToPhysical(_In_ UINT64 Virtual);

CODE_SEG("INIT")
static UINT64
KiArm64AlignUp(_In_ UINT64 Value,
               _In_ UINT64 Alignment);

CODE_SEG("INIT")
static BOOLEAN
KiArm64IsMappableMemoryType(_In_ TYPE_OF_MEMORY MemoryType);

CODE_SEG("INIT")
static VOID
KiArm64EnsureL1Entry(_In_ UINT64 Index);

CODE_SEG("INIT")
static VOID
KiArm64MapIdentityRange(_In_ UINT64 PhysicalStart,
                        _In_ UINT64 PhysicalEnd,
                        _In_ UINT64 Attributes);

CODE_SEG("INIT")
static DECLSPEC_NORETURN VOID
KiArm64FatalHalt(VOID);

CODE_SEG("INIT")
static SIZE_T
KiArm64BootStringLength(_In_z_ PCSTR Text)
{
    SIZE_T Length = 0;

    if (!Text)
        return 0;

    while (Text[Length] != '\0')
    {
        Length++;
    }

    return Length;
}

CODE_SEG("INIT")
static PCSTR
KiArm64DescribeEsr(_In_ UINT64 ExceptionSyndrome)
{
    ULONG Class = (ULONG)((ExceptionSyndrome >> 26) & 0x3FULL);

    if (Class < RTL_NUMBER_OF(KiArm64EsrClassNames) &&
        KiArm64EsrClassNames[Class] != NULL)
    {
        return KiArm64EsrClassNames[Class];
    }

    return "Unknown";
}

CODE_SEG("INIT")
static BOOLEAN
KiArm64IsMappableMemoryType(_In_ TYPE_OF_MEMORY MemoryType)
{
    switch (MemoryType)
    {
        case LoaderFree:
        case LoaderLoadedProgram:
        case LoaderFirmwareTemporary:
        case LoaderFirmwarePermanent:
        case LoaderOsloaderHeap:
        case LoaderOsloaderStack:
        case LoaderSystemCode:
        case LoaderHalCode:
        case LoaderBootDriver:
        case LoaderConsoleInDriver:
        case LoaderConsoleOutDriver:
        case LoaderStartupDpcStack:
        case LoaderStartupKernelStack:
        case LoaderStartupPanicStack:
        case LoaderStartupPcrPage:
        case LoaderStartupPdrPage:
        case LoaderRegistryData:
        case LoaderMemoryData:
        case LoaderNlsData:
        case LoaderSpecialMemory:
        case LoaderBBTMemory:
        case LoaderXIPRom:
        case LoaderHALCachedMemory:
        case LoaderLargePageFiller:
        case LoaderErrorLogMemory:
            return TRUE;
        default:
            return FALSE;
    }
}

CODE_SEG("INIT")
static __inline__ UINT64
KiArm64ReadSctlrEl1(VOID)
{
    UINT64 Value;
    __asm__ __volatile__("mrs %0, sctlr_el1" : "=r"(Value));
    return Value;
}

CODE_SEG("INIT")
static __inline__ UINT64
KiArm64ReadTcrEl1(VOID)
{
    UINT64 Value;
    __asm__ __volatile__("mrs %0, tcr_el1" : "=r"(Value));
    return Value;
}

CODE_SEG("INIT")
static __inline__ UINT64
KiArm64ReadTtbr0El1(VOID)
{
    UINT64 Value;
    __asm__ __volatile__("mrs %0, ttbr0_el1" : "=r"(Value));
    return Value;
}

CODE_SEG("INIT")
static __inline__ UINT64
KiArm64ReadTtbr1El1(VOID)
{
    UINT64 Value;
    __asm__ __volatile__("mrs %0, ttbr1_el1" : "=r"(Value));
    return Value;
}

CODE_SEG("INIT")
static __inline__ UINT64
KiArm64ReadMairEl1(VOID)
{
    UINT64 Value;
    __asm__ __volatile__("mrs %0, mair_el1" : "=r"(Value));
    return Value;
}

CODE_SEG("INIT")
static VOID
KiArm64BootSerialWaitTxFifo(VOID)
{
    if (!KiArm64BootSerialReady)
        return;
    /* Bound the wait to prevent indefinite spin if UART is wedged */
    for (ULONG spins = 0x100000;
         (KiArm64BootUartBase[ARM64_PL011_FR / sizeof(ULONG)] & ARM64_PL011_FR_TXFF) && spins != 0;
         --spins)
    {
        /* busy-wait */
    }
}

CODE_SEG("INIT")
static VOID
KiArm64BootSerialPutChar(_In_ CHAR Character)
{
    if (!KiArm64BootSerialReady)
        return;

    if (Character == '\n')
    {
        KiArm64BootSerialWaitTxFifo();
        KiArm64BootUartBase[ARM64_PL011_DR / sizeof(ULONG)] = '\r';
    }

    KiArm64BootSerialWaitTxFifo();
    KiArm64BootUartBase[ARM64_PL011_DR / sizeof(ULONG)] = (ULONG)Character;
}

CODE_SEG("INIT")
static VOID
KiArm64BootSerialWrite(_In_reads_bytes_opt_(Count) const CHAR *Buffer,
                       _In_ SIZE_T Count)
{
    if (!Buffer || !Count)
        return;

    while (Count--)
    {
        KiArm64BootSerialPutChar(*Buffer++);
    }
}

CODE_SEG("INIT")
static VOID
KiArm64BootSerialWriteLine(_In_z_ PCSTR Text)
{
    if (!Text)
        return;

    while (*Text)
    {
        KiArm64BootSerialPutChar(*Text++);
    }

    KiArm64BootSerialPutChar('\n');
}

CODE_SEG("INIT")
static VOID
KiArm64BootSerialInitialize(_Inout_ PARM64_BOOT_CONTEXT BootContext)
{
    ULONGLONG UartBase = ARM64_EARLY_UART_PHYS_BASE;
    PLOADER_PARAMETER_BLOCK LoaderBlock = BootContext->LoaderBlock;

    if (LoaderBlock && LoaderBlock->Extension && LoaderBlock->Extension->HeadlessLoaderBlock)
    {
        PHEADLESS_LOADER_BLOCK Headless = LoaderBlock->Extension->HeadlessLoaderBlock;

        if (Headless->PortAddress && Headless->IsMMIODevice)
        {
            UartBase = (ULONGLONG)(ULONG_PTR)Headless->PortAddress;
        }
        /* If PortAddress is not an MMIO device, keep default MMIO UART on ARM64 */
    }

    KiArm64BootUartBase = (volatile ULONG *)(ULONG_PTR)UartBase;
    BootContext->UartPhysicalBase = KiArm64VirtualToPhysical(UartBase);
    KiArm64BootSerialReady = TRUE;

    {
        CHAR Buffer[96];
        if (NT_SUCCESS(RtlStringCbPrintfA(Buffer,
                                          sizeof(Buffer),
                                          "[arm64] Boot serial console enabled @0x%llx",
                                          UartBase)))
        {
            KiArm64BootSerialWrite(Buffer, KiArm64BootStringLength(Buffer));
            KiArm64BootSerialPutChar('\n');
        }
        else
        {
            KiArm64BootSerialWriteLine("[arm64] Boot serial console enabled");
        }
    }
}

CODE_SEG("INIT")
static VOID
KiArm64CaptureMmuState(_Out_ PARM64_BOOT_CONTEXT BootContext)
{
    BootContext->SctlrEl1 = KiArm64ReadSctlrEl1();
    BootContext->TcrEl1 = KiArm64ReadTcrEl1();
    BootContext->Ttbr0El1 = KiArm64ReadTtbr0El1();
    BootContext->Ttbr1El1 = KiArm64ReadTtbr1El1();
    BootContext->MairEl1 = KiArm64ReadMairEl1();
    BootContext->MmuEnabled = (BootContext->SctlrEl1 & 1ULL) != 0;
}

CODE_SEG("INIT")
static VOID
KiArm64DumpBootContext(_In_ PARM64_BOOT_CONTEXT BootContext)
{
    CHAR Buffer[128];
    SIZE_T Length;

    Length = 0;
    if (NT_SUCCESS(RtlStringCbPrintfA(Buffer,
                                      sizeof(Buffer),
                                      "[arm64] sctlr=0x%llx tcr=0x%llx mair=0x%llx\n",
                                      BootContext->SctlrEl1,
                                      BootContext->TcrEl1,
                                      BootContext->MairEl1)))
    {
        Length = KiArm64BootStringLength(Buffer);
        KiArm64BootSerialWrite(Buffer, Length);
    }

    if (NT_SUCCESS(RtlStringCbPrintfA(Buffer,
                                      sizeof(Buffer),
                                      "[arm64] ttbr0=0x%llx ttbr1=0x%llx mmu=%s\n",
                                      BootContext->Ttbr0El1,
                                      BootContext->Ttbr1El1,
                                      BootContext->MmuEnabled ? "on" : "off")))
    {
        Length = KiArm64BootStringLength(Buffer);
        KiArm64BootSerialWrite(Buffer, Length);
    }

    if (BootContext->UartPhysicalBase &&
        NT_SUCCESS(RtlStringCbPrintfA(Buffer,
                                      sizeof(Buffer),
                                      "[arm64] uart=0x%llx\n",
                                      BootContext->UartPhysicalBase)))
    {
        Length = KiArm64BootStringLength(Buffer);
        KiArm64BootSerialWrite(Buffer, Length);
    }
}

VOID
KiArm64BootStageLog(_In_z_ PCSTR Stage)
{
    /*
     * WORKAROUND/FIXME: Keep a universal stage logger callable even when
     * INIT text is discarded. Write directly to the PL011 UART when the
     * early console is initialized, and also emit to DbgPrintEx so the
     * message appears in the normal kernel debug stream later.
     */
    if (Stage)
    {
        if (KiArm64BootSerialReady)
        {
            /* Minimal inline send to avoid relying on INIT-only helpers */
            const CHAR *p = Stage;
            while (*p)
            {
                /* Wait until TX FIFO has room */
                for (ULONG spins = 0x100000;
                     (KiArm64BootUartBase[ARM64_PL011_FR / sizeof(ULONG)] & ARM64_PL011_FR_TXFF) && spins != 0;
                     --spins) { /* busy-wait */ }

                if (*p == '\n')
                {
                    /* Write CR before LF */
                    for (ULONG spins = 0x100000;
                         (KiArm64BootUartBase[ARM64_PL011_FR / sizeof(ULONG)] & ARM64_PL011_FR_TXFF) && spins != 0;
                         --spins) { /* busy-wait */ }
                    KiArm64BootUartBase[ARM64_PL011_DR / sizeof(ULONG)] = '\r';
                }

                KiArm64BootUartBase[ARM64_PL011_DR / sizeof(ULONG)] = (ULONG)(*p++);
            }

            /* Append newline */
            for (ULONG spins = 0x100000;
                 (KiArm64BootUartBase[ARM64_PL011_FR / sizeof(ULONG)] & ARM64_PL011_FR_TXFF) && spins != 0;
                 --spins) { /* busy-wait */ }
            KiArm64BootUartBase[ARM64_PL011_DR / sizeof(ULONG)] = (ULONG)'\n';
        }

        /* Also mirror to kernel debug stream when available */
        DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_INFO_LEVEL, "%s\n", Stage);
    }
}

CODE_SEG("INIT")
static VOID
KiArm64EnsureL1Entry(_In_ UINT64 Index)
{
    if (Index >= ARM64_L1_MAX_ENTRIES)
        return;

    if ((KiArm64IdentityL1[Index] & ARM64_PTE_TYPE_TABLE) == 0)
    {
        RtlZeroMemory(KiArm64IdentityL2[Index], sizeof(KiArm64IdentityL2[Index]));

        UINT64 L2Physical = KiArm64VirtualToPhysical((ULONG_PTR)&KiArm64IdentityL2[Index][0]);
        KiArm64IdentityL1[Index] = (L2Physical & ~((UINT64)PAGE_SIZE - 1ULL)) |
                                   ARM64_PTE_TYPE_TABLE |
                                   ARM64_PTE_TABLE_NSTABLE;
    }
}

CODE_SEG("INIT")
static VOID
KiArm64MapIdentityRange(_In_ UINT64 PhysicalStart,
                        _In_ UINT64 PhysicalEnd,
                        _In_ UINT64 Attributes)
{
    const UINT64 PhysicalLimit = 512ULL << ARM64_L1_BLOCK_SHIFT;

    if (PhysicalStart >= PhysicalLimit)
        return;

    if (PhysicalEnd > PhysicalLimit)
        PhysicalEnd = PhysicalLimit;

    if (PhysicalEnd <= PhysicalStart)
        return;

    PhysicalStart &= ~(ARM64_L2_BLOCK_SIZE - 1ULL);
    PhysicalEnd = KiArm64AlignUp(PhysicalEnd, ARM64_L2_BLOCK_SIZE);

    while (PhysicalStart < PhysicalEnd)
    {
        UINT64 L1Index = PhysicalStart >> ARM64_L1_BLOCK_SHIFT;
        UINT64 L2Index = (PhysicalStart >> ARM64_L2_BLOCK_SHIFT) & 0x1FFULL;

        KiArm64EnsureL1Entry(L1Index);
        KiArm64IdentityL2[L1Index][L2Index] = PhysicalStart | Attributes;

        PhysicalStart += ARM64_L2_BLOCK_SIZE;
    }
}

DECLSPEC_NORETURN
CODE_SEG("INIT")
static VOID
KiArm64FatalHalt(VOID)
{
    __asm__ __volatile__("msr daifset, #0xf" ::: "memory");
    for (;;) {
        __asm__ __volatile__("wfi" ::: "memory");
    }
}

CODE_SEG("INIT")
 VOID
KiArm64EarlyVectorHandler(_In_ UINT64 VectorId,
                          _In_ UINT64 ExceptionSyndrome,
                          _In_ UINT64 FaultAddress,
                          _In_opt_ PARM64_EARLY_GPRS Registers)
{
    PCSTR VectorName = (VectorId < RTL_NUMBER_OF(KiArm64VectorNames) &&
                        KiArm64VectorNames[VectorId]) ? KiArm64VectorNames[VectorId]
                                                       : "Unknown";
    PCSTR EsrDesc = KiArm64DescribeEsr(ExceptionSyndrome);
    ULONG Iss = (ULONG)(ExceptionSyndrome & 0x1FFFFFFULL);
    UINT64 Spsr, Elr;
    ARM64_EARLY_GPRS LocalRegisters = {0};
    BOOLEAN ShouldLog;

    __asm__ __volatile__("mrs %0, spsr_el1" : "=r"(Spsr));
    __asm__ __volatile__("mrs %0, elr_el1"  : "=r"(Elr));

    if (Registers)
    {
        LocalRegisters = *Registers;
    }

    KiArm64LastTrapState.Registers = LocalRegisters;
    KiArm64LastTrapState.VectorId = VectorId;
    KiArm64LastTrapState.ExceptionSyndrome = ExceptionSyndrome;
    KiArm64LastTrapState.FaultAddress = FaultAddress;
    KiArm64LastTrapState.Elr = Elr;
    KiArm64LastTrapState.Spsr = Spsr;
    KiArm64TrapStateValid = TRUE;

    ShouldLog = TRUE;

    if (ShouldLog)
    {
        KiArm64VectorUartPuts("[vector] id=");
        KiArm64VectorUartPutHex64(VectorId);
        KiArm64VectorUartPuts(" esr=");
        KiArm64VectorUartPutHex64(ExceptionSyndrome);
        KiArm64VectorUartPuts(" far=");
        KiArm64VectorUartPutHex64(FaultAddress);
        KiArm64VectorUartPuts(" elr=");
        KiArm64VectorUartPutHex64(Elr);
        KiArm64VectorUartPuts(" x0=");
        KiArm64VectorUartPutHex64(LocalRegisters.X0);
        KiArm64VectorUartPuts(" x30=");
        KiArm64VectorUartPutHex64(LocalRegisters.Sp); /* placeholder */
        KiArm64VectorUartPuts("\n");
    }

    if (ShouldLog)
    {
        DbgPrint("[arm64] vector %llu (%s) esr=0x%llx (%s) iss=0x%lx far=0x%llx elr=0x%llx spsr=0x%llx x0=0x%llx x1=0x%llx x2=0x%llx x3=0x%llx sp=0x%llx\n",
                 VectorId,
                 VectorName,
                 ExceptionSyndrome,
                 EsrDesc,
                 Iss,
                 FaultAddress,
                 Elr,
                 Spsr,
                 LocalRegisters.X0,
                 LocalRegisters.X1,
                 LocalRegisters.X2,
                 LocalRegisters.X3,
                 LocalRegisters.Sp);
    }

    /*
     * If final vectors are installed, return and let the permanent
     * vector path handle this exception/interrupt. Only halt when
     * still in early bring-up before KeInitExceptions has run.
     */
    if (KiArm64FinalVectorsInstalled)
    {
        return;
    }

    /* Early boot only: halt so the log stays visible */
    KiArm64FatalHalt();
}

CODE_SEG("INIT")
static UINT64
KiArm64VirtualToPhysical(_In_ UINT64 Virtual)
{
    if (Virtual >= ARM64_KSEG0_BASE)
        return Virtual - ARM64_KSEG0_BASE;

    return Virtual;
}

CODE_SEG("INIT")
static UINT64
KiArm64EnsureMairNormalWb(_In_ UINT64 CurrentMair)
{
    const UINT64 AttributeMask = 0xFFULL << (ARM64_MEM_ATTR_NORMAL_WB * 8);

    if ((CurrentMair & AttributeMask) == AttributeMask)
        return CurrentMair;

    UINT64 Updated = (CurrentMair & ~AttributeMask) | (0xFFULL << (ARM64_MEM_ATTR_NORMAL_WB * 8));
    __asm__ __volatile__("msr mair_el1, %0" :: "r"(Updated));
    __asm__ __volatile__("isb");
    return Updated;
}

CODE_SEG("INIT")
static UINT64
KiArm64EnsureMairDeviceNgnrne(_In_ UINT64 CurrentMair)
{
    /* Ensure MAIR attr index 0 encodes Device-nGnRnE (0x00) */
    const UINT64 AttributeMask = 0xFFULL << (ARM64_MEM_ATTR_DEVICE_nGnRnE * 8);
    UINT64 Updated = (CurrentMair & ~AttributeMask) | (0x00ULL << (ARM64_MEM_ATTR_DEVICE_nGnRnE * 8));
    if (Updated != CurrentMair)
    {
        __asm__ __volatile__("msr mair_el1, %0" :: "r"(Updated));
        __asm__ __volatile__("isb");
    }
    return Updated;
}

CODE_SEG("INIT")
static UINT64
KiArm64AlignUp(_In_ UINT64 Value,
               _In_ UINT64 Alignment)
{
    return (Value + (Alignment - 1)) & ~(Alignment - 1);
}

CODE_SEG("INIT")
static VOID
KiArm64InitIdentityMapStorage(VOID)
{
    if (KiArm64IdentityL0 && KiArm64IdentityL1 && KiArm64IdentityL2)
        return;

    KiArm64IdentityL0 = (UINT64 *)KiArm64AlignUp((UINT64)KiArm64IdentityL0Backing,
                                                 PAGE_SIZE);
    KiArm64IdentityL1 = (UINT64 *)KiArm64AlignUp((UINT64)KiArm64IdentityL1Backing,
                                                 PAGE_SIZE);
    KiArm64IdentityL2 = (UINT64 (*)[512])KiArm64AlignUp((UINT64)KiArm64IdentityL2Backing,
                                                        PAGE_SIZE);

    ASSERT(((ULONG_PTR)KiArm64IdentityL0 & (PAGE_SIZE - 1)) == 0);
    ASSERT(((ULONG_PTR)KiArm64IdentityL1 & (PAGE_SIZE - 1)) == 0);
    ASSERT(((ULONG_PTR)KiArm64IdentityL2 & (PAGE_SIZE - 1)) == 0);
}

CODE_SEG("INIT")
static VOID
KiArm64InstallEarlyExceptionVectors(VOID)
{
    ULONG_PTR PreVbar = 0;
    __asm__ __volatile__("mrs %0, vbar_el1" : "=r"(PreVbar));
    /* Pre-KD: avoid KdpDprintf, use boot UART logging only */
    {
        CHAR Buf[96];
        if (NT_SUCCESS(RtlStringCbPrintfA(Buf, sizeof(Buf),
                                          "[arm64] EarlyVectors: previous VBAR = %p",
                                          (PVOID)PreVbar)))
        {
            KiArm64BootSerialWrite(Buf, KiArm64BootStringLength(Buf));
            KiArm64BootSerialPutChar('\n');
        }
    }

    __asm__ __volatile__("msr vbar_el1, %0" :: "r"((ULONG_PTR)&KiArm64EarlyVectorTable));
    __asm__ __volatile__("isb");
    {
        CHAR Buf[96];
        if (NT_SUCCESS(RtlStringCbPrintfA(Buf, sizeof(Buf),
                                          "[arm64] EarlyVectors: installed VBAR = %p",
                                          (PVOID)(ULONG_PTR)&KiArm64EarlyVectorTable)))
        {
            KiArm64BootSerialWrite(Buf, KiArm64BootStringLength(Buf));
            KiArm64BootSerialPutChar('\n');
        }
    }
    KiArm64BootStageLog("[arm64] early exception vectors installed");
}

CODE_SEG("INIT")
static VOID
KiArm64EnsureIdentityMapping(_Inout_ PARM64_BOOT_CONTEXT BootContext)
{
    UINT64 HighestPhysical = 0;
    UINT64 BlockAttributes;
    UINT64 TablePhysical;
    UINT64 Ttbr0Physical;
    UINT64 UpdatedMair;
    PLOADER_PARAMETER_BLOCK LoaderBlock;
    UINT64 ExtraAddresses[16] = {0};
    ULONG ExtraCount = 0;

    LoaderBlock = BootContext->LoaderBlock;

    if (LoaderBlock)
    {
        UINT64 Candidate;

        Candidate = KiArm64VirtualToPhysical(LoaderBlock->KernelStack);
        if (Candidate > 0 && ExtraCount < RTL_NUMBER_OF(ExtraAddresses))
            ExtraAddresses[ExtraCount++] = Candidate;
        if (Candidate > HighestPhysical) HighestPhysical = Candidate;

        Candidate = KiArm64VirtualToPhysical(LoaderBlock->Prcb);
        if (Candidate > 0 && ExtraCount < RTL_NUMBER_OF(ExtraAddresses))
            ExtraAddresses[ExtraCount++] = Candidate;
        if (Candidate > HighestPhysical) HighestPhysical = Candidate;

        Candidate = KiArm64VirtualToPhysical(LoaderBlock->Thread);
        if (Candidate > 0 && ExtraCount < RTL_NUMBER_OF(ExtraAddresses))
            ExtraAddresses[ExtraCount++] = Candidate;
        if (Candidate > HighestPhysical) HighestPhysical = Candidate;

        Candidate = KiArm64VirtualToPhysical((ULONG_PTR)LoaderBlock);
        if (Candidate > 0 && ExtraCount < RTL_NUMBER_OF(ExtraAddresses))
            ExtraAddresses[ExtraCount++] = Candidate;
        if (Candidate > HighestPhysical) HighestPhysical = Candidate;

        Candidate = KiArm64VirtualToPhysical(LoaderBlock->u.Arm64.PanicStack);
        if (Candidate > 0 && ExtraCount < RTL_NUMBER_OF(ExtraAddresses))
            ExtraAddresses[ExtraCount++] = Candidate;
        if (Candidate > HighestPhysical) HighestPhysical = Candidate;

        Candidate = KiArm64VirtualToPhysical(LoaderBlock->u.Arm64.InterruptStack);
        if (Candidate > 0 && ExtraCount < RTL_NUMBER_OF(ExtraAddresses))
            ExtraAddresses[ExtraCount++] = Candidate;
        if (Candidate > HighestPhysical) HighestPhysical = Candidate;

        Candidate = KiArm64VirtualToPhysical(LoaderBlock->u.Arm64.PcrPage);
        if (Candidate > 0 && ExtraCount < RTL_NUMBER_OF(ExtraAddresses))
            ExtraAddresses[ExtraCount++] = Candidate;
        if (Candidate > HighestPhysical) HighestPhysical = Candidate;

        for (PLIST_ENTRY Entry = LoaderBlock->MemoryDescriptorListHead.Flink;
             Entry != &LoaderBlock->MemoryDescriptorListHead;
             Entry = Entry->Flink)
        {
            PMEMORY_ALLOCATION_DESCRIPTOR Descriptor =
                CONTAINING_RECORD(Entry, MEMORY_ALLOCATION_DESCRIPTOR, ListEntry);

            UINT64 RangeStart = (UINT64)Descriptor->BasePage << PAGE_SHIFT;
            UINT64 RangeEnd = RangeStart + ((UINT64)Descriptor->PageCount << PAGE_SHIFT);

            if (RangeEnd > HighestPhysical)
                HighestPhysical = RangeEnd;
        }
    }

    {
        UINT64 Candidate;

        Candidate = KiArm64VirtualToPhysical((ULONG_PTR)KiSystemStartup);
        if (Candidate > 0 && ExtraCount < RTL_NUMBER_OF(ExtraAddresses))
            ExtraAddresses[ExtraCount++] = Candidate;
        if (Candidate > HighestPhysical) HighestPhysical = Candidate;

        Candidate = KiArm64VirtualToPhysical((ULONG_PTR)KiArm64IdentityL0);
        if (Candidate > 0 && ExtraCount < RTL_NUMBER_OF(ExtraAddresses))
            ExtraAddresses[ExtraCount++] = Candidate;
        if (Candidate > HighestPhysical) HighestPhysical = Candidate;

        Candidate = KiArm64VirtualToPhysical((ULONG_PTR)KiArm64IdentityL1);
        if (Candidate > 0 && ExtraCount < RTL_NUMBER_OF(ExtraAddresses))
            ExtraAddresses[ExtraCount++] = Candidate;
        if (Candidate > HighestPhysical) HighestPhysical = Candidate;
    }

    KiArm64InitIdentityMapStorage();

    RtlZeroMemory(KiArm64IdentityL0, ARM64_IDENTITY_L0_ENTRIES * sizeof(UINT64));
    RtlZeroMemory(KiArm64IdentityL1, ARM64_IDENTITY_L1_ENTRIES * sizeof(UINT64));
    RtlZeroMemory(KiArm64IdentityL2,
                  ARM64_IDENTITY_L2_ENTRIES * sizeof(UINT64));

    BlockAttributes = ARM64_PTE_TYPE_BLOCK |
                      (ARM64_MEM_ATTR_NORMAL_WB << ARM64_PTE_BLOCK_ATTR_INDEX_SHIFT) |
                      ARM64_PTE_BLOCK_INNER_SHARE |
                      ARM64_PTE_BLOCK_AF |
                      ARM64_PTE_BLOCK_UXN |
                      ARM64_PTE_BLOCK_PXN;

    TablePhysical = KiArm64VirtualToPhysical((ULONG_PTR)KiArm64IdentityL1);
    KiArm64IdentityL0[0] = (TablePhysical & ~((UINT64)PAGE_SIZE - 1ULL)) | ARM64_PTE_TYPE_TABLE;

    if (LoaderBlock)
    {
        for (PLIST_ENTRY Entry = LoaderBlock->MemoryDescriptorListHead.Flink;
             Entry != &LoaderBlock->MemoryDescriptorListHead;
             Entry = Entry->Flink)
        {
            PMEMORY_ALLOCATION_DESCRIPTOR Descriptor =
                CONTAINING_RECORD(Entry, MEMORY_ALLOCATION_DESCRIPTOR, ListEntry);

            if (!KiArm64IsMappableMemoryType(Descriptor->MemoryType))
                continue;

            UINT64 RangeStart = (UINT64)Descriptor->BasePage << PAGE_SHIFT;
            UINT64 RangeEnd = RangeStart + ((UINT64)Descriptor->PageCount << PAGE_SHIFT);

            if (RangeEnd > RangeStart)
                KiArm64MapIdentityRange(RangeStart, RangeEnd, BlockAttributes);
        }
    }

    for (ULONG Index = 0; Index < RTL_NUMBER_OF(ExtraAddresses); ++Index)
    {
        UINT64 Address = ExtraAddresses[Index];
        if (Address == 0)
            continue;

        UINT64 Start = Address & ~(ARM64_L2_BLOCK_SIZE - 1ULL);
        UINT64 End = Start + ARM64_L2_BLOCK_SIZE;
        KiArm64MapIdentityRange(Start, End, BlockAttributes);
    }

    if ((KiArm64IdentityL1[0] & ARM64_PTE_TYPE_TABLE) == 0)
    {
        KiArm64MapIdentityRange(0, ARM64_IDENTITY_MIN_BYTES, BlockAttributes);
    }

    if (KiArm64BootSerialReady)
    {
        CHAR MapBuf[128];
        UINT64 L0Entry = KiArm64IdentityL0[0];
        UINT64 L1Entry0 = KiArm64IdentityL1[0];
        UINT64 UartPte = 0;

        if (BootContext->UartPhysicalBase != 0)
        {
            UINT64 UartStart = BootContext->UartPhysicalBase & ~(ARM64_L2_BLOCK_SIZE - 1ULL);
            UINT64 L1Index = UartStart >> ARM64_L1_BLOCK_SHIFT;
            UINT64 L2Index = (UartStart >> ARM64_L2_BLOCK_SHIFT) & 0x1FFULL;

            if (L1Index < ARM64_L1_MAX_ENTRIES)
            {
                UartPte = KiArm64IdentityL2[L1Index][L2Index];
            }
        }

        if (NT_SUCCESS(RtlStringCbPrintfA(MapBuf,
                                          sizeof(MapBuf),
                                          "[arm64] identity: L0[0]=0x%llx L1[0]=0x%llx UartPte=0x%llx",
                                          L0Entry,
                                          L1Entry0,
                                          UartPte)))
        {
            KiArm64BootSerialWrite(MapBuf, KiArm64BootStringLength(MapBuf));
            KiArm64BootSerialPutChar('\n');
        }
    }

    if (HighestPhysical != 0 && KiArm64BootSerialReady)
    {
        CHAR Coverage[96];
        if (NT_SUCCESS(RtlStringCbPrintfA(Coverage,
                                          sizeof(Coverage),
                                          "[arm64] identity top=0x%llx",
                                          HighestPhysical)))
        {
            KiArm64BootSerialWrite(Coverage, KiArm64BootStringLength(Coverage));
            KiArm64BootSerialPutChar('\n');
        }
    }

    KiArm64BootStageLog("[arm64] identity: before MAIR");

    /* Program MAIR for Normal-WB and Device-nGnRnE attributes */
    UpdatedMair = KiArm64EnsureMairNormalWb(BootContext->MairEl1);
    UpdatedMair = KiArm64EnsureMairDeviceNgnrne(UpdatedMair);
    BootContext->MairEl1 = UpdatedMair;

    KiArm64BootStageLog("[arm64] identity: after MAIR");

    /* Identity map the UART MMIO page as Device-nGnRnE */
    if (BootContext->UartPhysicalBase != 0)
    {
        UINT64 UartStart = BootContext->UartPhysicalBase & ~(ARM64_L2_BLOCK_SIZE - 1ULL);
        UINT64 UartEnd = UartStart + ARM64_L2_BLOCK_SIZE;
        UINT64 DeviceBlockAttrs = ARM64_PTE_TYPE_BLOCK |
                                  (ARM64_MEM_ATTR_DEVICE_nGnRnE << ARM64_PTE_BLOCK_ATTR_INDEX_SHIFT) |
                                  ARM64_PTE_BLOCK_AF |
                                  ARM64_PTE_BLOCK_UXN |
                                  ARM64_PTE_BLOCK_PXN;
        KiArm64MapIdentityRange(UartStart, UartEnd, DeviceBlockAttrs);
    }

    KiArm64BootStageLog("[arm64] identity: after UART map");

    /* Map common GIC MMIO (QEMU virt: 0x0800_0000..0x0802_0000) */
    {
        const UINT64 GicStart = 0x08000000ULL & ~(ARM64_L2_BLOCK_SIZE - 1ULL);
        const UINT64 GicEnd   = GicStart + (2 * ARM64_L2_BLOCK_SIZE);
        UINT64 DeviceBlockAttrs = ARM64_PTE_TYPE_BLOCK |
                                  (ARM64_MEM_ATTR_DEVICE_nGnRnE << ARM64_PTE_BLOCK_ATTR_INDEX_SHIFT) |
                                  ARM64_PTE_BLOCK_AF |
                                  ARM64_PTE_BLOCK_UXN |
                                  ARM64_PTE_BLOCK_PXN;
        KiArm64MapIdentityRange(GicStart, GicEnd, DeviceBlockAttrs);
    }

    KiArm64BootStageLog("[arm64] identity: after GIC map");

    Ttbr0Physical = KiArm64VirtualToPhysical((ULONG_PTR)KiArm64IdentityL0);

    if (KiArm64BootSerialReady)
    {
        CHAR Buf[96];
        if (NT_SUCCESS(RtlStringCbPrintfA(Buf,
                                          sizeof(Buf),
                                          "[arm64] identity: ttbr0 phys=0x%llx",
                                          Ttbr0Physical)))
        {
            KiArm64BootSerialWrite(Buf, KiArm64BootStringLength(Buf));
            KiArm64BootSerialPutChar('\n');
        }
    }

    KiArm64BootStageLog("[arm64] identity: before TTBR0 switch");

    __asm__ __volatile__("dsb ishst" ::: "memory");
    __asm__ __volatile__("msr ttbr0_el1, %0" :: "r"(Ttbr0Physical));
    __asm__ __volatile__("dsb ish" ::: "memory");
    __asm__ __volatile__("tlbi vmalle1is" ::: "memory");
    __asm__ __volatile__("dsb ish" ::: "memory");
    __asm__ __volatile__("isb");

    KiArm64BootStageLog("[arm64] identity: after TTBR0 switch");

    BootContext->Ttbr0El1 = Ttbr0Physical;
    BootContext->MmuEnabled = TRUE;

    KiArm64BootStageLog("[arm64] identity mapping initialised");
}

CODE_SEG("INIT")
static VOID
KiArm64HandoverToPhase1(_Inout_ ARM64_BOOT_CONTEXT *BootContext)
{
    UNREFERENCED_PARAMETER(BootContext);

    KiArm64BootStageLog("[arm64] entering KiInitializeSystem");
    KiInitializeSystem(BootContext->LoaderBlock);
}

DECLSPEC_NORETURN
CODE_SEG("INIT")
VOID
NTAPI
KiSystemStartup(_Inout_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    ARM64_BOOT_CONTEXT BootContext = {0};

    /* Keep KiArm64HandoverToPhase1 reachable for GCC/MinGW (unused warning). */
    if (0)
    {
        KiArm64HandoverToPhase1(&BootContext);
    }

    KiArm64EmitEntryMarker();

    BootContext.LoaderBlock = LoaderBlock;
    KiArm64BootSerialInitialize(&BootContext);

    KiArm64BootStageLog("[arm64] KiSystemStartup entered");

    KiArm64CaptureMmuState(&BootContext);
    KiArm64DumpBootContext(&BootContext);

    KiArm64InstallEarlyExceptionVectors();

    KiArm64EnsureIdentityMapping(&BootContext);

    /* Switch to a clean boot stack before entering KiInitializeSystem */
    LoaderBlock->KernelStack = (ULONG_PTR)KiArm64P0BootStack;
    if (LoaderBlock->KernelStack < ARM64_KSEG0_BASE)
    {
        LoaderBlock->KernelStack += ARM64_KSEG0_BASE;
    }
    {
        CHAR Stage[160];
        if (NT_SUCCESS(RtlStringCbPrintfA(Stage,
                                          sizeof(Stage),
                                          "[arm64] bootstack: raw=%p adjusted=%p",
                                          KiArm64P0BootStack,
                                          (PVOID)LoaderBlock->KernelStack)))
        {
            KiArm64BootStageLog(Stage);
        }
    }

    {
        ULONG_PTR InitialStack = LoaderBlock->KernelStack;
        KiArm64BootStageLog("[arm64] switching to boot stack");
        KiArm64SwitchToBootStack(InitialStack, LoaderBlock);
    }

    /* Not reached */
    KiArm64BootStageLog("[arm64] KiSwitchToBootStack returned unexpectedly");
    KiArm64FatalHalt();
}
#define ARM64_PL011_BASE 0x09000000ULL
#define ARM64_PL011_DR   0x00
#define ARM64_PL011_FR   0x18
#define ARM64_PL011_FR_TXFF (1u << 5)
static __inline VOID KiArm64VectorUartPutc(char Ch);
static __inline VOID KiArm64VectorUartPuts(const char *S);
static __inline VOID KiArm64VectorUartPutHex64(ULONGLONG V);

CODE_SEG("INIT")
DECLSPEC_NORETURN VOID NTAPI
KiArm64SystemStartupBootStack(_Inout_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    /*
     * ARM64 Boot Stack Initialization
     *
     * This function is called on the clean boot stack before entering the main
     * kernel initialization. It must perform critical early initialization that
     * other subsystems depend on, similar to amd64's KiSystemStartupBootStack.
     *
     * Key responsibilities:
     * 1. Initialize pool lookaside list pointers in the PRCB
     * 2. Set up any architecture-specific state needed for early boot
     * 3. Hand off to the main kernel initialization
     */

    /* Declare the pool lookaside initialization function from ex/lookas.c */
    extern VOID NTAPI ExInitPoolLookasidePointers(VOID);

    KiArm64BootStageLog("[arm64] KiSystemStartupBootStack: initializing pool lookaside pointers");

    /*
     * CRITICAL: Initialize pool lookaside list pointers BEFORE calling KiInitializeSystem.
     *
     * The PRCB contains per-CPU pointers to lookaside lists that are used by the pool
     * allocator (ExAllocatePoolWithTag/ExFreePoolWithTag). These must be initialized
     * before any pool allocations occur, otherwise the allocator will dereference
     * NULL or uninitialized pointers when trying to use the lookaside lists.
     *
     * On amd64, this is done in KiSystemStartupBootStack before KiInitializeKernel.
     * We must do the same on ARM64 to avoid crashes in RtlInterlockedPopEntrySList
     * when ExAllocatePoolWithTag tries to pop from an uninitialized lookaside list.
     */
    ExInitPoolLookasidePointers();

    KiArm64BootStageLog("[arm64] KiSystemStartupBootStack: entering KiInitializeSystem");
    KiInitializeSystem(LoaderBlock);
    KiArm64BootStageLog("[arm64] KiSystemStartupBootStack: KiInitializeSystem returned unexpectedly");
    KiArm64FatalHalt();
}
