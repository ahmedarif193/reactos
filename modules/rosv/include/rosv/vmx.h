/*
 * PROJECT:     ReactOS VMX Hypervisor Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     VMX types, VMCS field encodings, function prototypes
 * COPYRIGHT:   Copyright 2025 Ahmed Arif
 */

#pragma once

#include <rosv/rosv.h>

/* ---- VMX instruction result --------------------------------------------- */

typedef enum _VMX_RESULT {
    VmxOk = 0,             /* Success */
    VmxFailInvalid = 1,    /* CF=1 - no current VMCS */
    VmxFailValid = 2       /* ZF=1 - error number in VM-instruction error field */
} VMX_RESULT;

/* ---- VMX capability info (cached from MSRs) ----------------------------- */

typedef struct _VMX_CAPABILITIES {
    /* IA32_VMX_BASIC */
    ULONG VmcsRevisionId;
    ULONG VmcsRegionSize;
    UCHAR VmcsMemoryType;       /* 0=UC, 6=WB */
    BOOLEAN TrueCtlsAvailable;  /* Bit 55 of IA32_VMX_BASIC */

    /* Pin-based controls */
    ULONG PinBasedAllowed0;
    ULONG PinBasedAllowed1;

    /* Primary processor-based controls */
    ULONG ProcBasedAllowed0;
    ULONG ProcBasedAllowed1;

    /* Secondary processor-based controls */
    BOOLEAN SecondaryCtlsAvailable;
    ULONG ProcBased2Allowed0;
    ULONG ProcBased2Allowed1;

    /* Exit controls */
    ULONG ExitCtlsAllowed0;
    ULONG ExitCtlsAllowed1;

    /* Entry controls */
    ULONG EntryCtlsAllowed0;
    ULONG EntryCtlsAllowed1;

    /* EPT/VPID capabilities (IA32_VMX_EPT_VPID_CAP) */
    ULONG64 EptVpidCap;

    /* CR0/CR4 fixed bits */
    ULONG64 Cr0Fixed0;
    ULONG64 Cr0Fixed1;
    ULONG64 Cr4Fixed0;
    ULONG64 Cr4Fixed1;
} VMX_CAPABILITIES, *PVMX_CAPABILITIES;

/* ---- VMCS field encodings (Intel SDM Vol 3, Appendix B) ----------------- */

typedef enum _VMCS_FIELD {

    /* 16-bit control fields */
    VMCS_CTRL_VPID                          = 0x00000000,
    VMCS_CTRL_POSTED_INT_NOTIFY             = 0x00000002,
    VMCS_CTRL_EPTP_INDEX                    = 0x00000004,

    /* 16-bit guest-state fields */
    VMCS_GUEST_ES_SEL                       = 0x00000800,
    VMCS_GUEST_CS_SEL                       = 0x00000802,
    VMCS_GUEST_SS_SEL                       = 0x00000804,
    VMCS_GUEST_DS_SEL                       = 0x00000806,
    VMCS_GUEST_FS_SEL                       = 0x00000808,
    VMCS_GUEST_GS_SEL                       = 0x0000080A,
    VMCS_GUEST_LDTR_SEL                     = 0x0000080C,
    VMCS_GUEST_TR_SEL                       = 0x0000080E,
    VMCS_GUEST_INT_STATUS                   = 0x00000810,
    VMCS_GUEST_PML_INDEX                    = 0x00000812,

    /* 16-bit host-state fields */
    VMCS_HOST_ES_SEL                        = 0x00000C00,
    VMCS_HOST_CS_SEL                        = 0x00000C02,
    VMCS_HOST_SS_SEL                        = 0x00000C04,
    VMCS_HOST_DS_SEL                        = 0x00000C06,
    VMCS_HOST_FS_SEL                        = 0x00000C08,
    VMCS_HOST_GS_SEL                        = 0x00000C0A,
    VMCS_HOST_TR_SEL                        = 0x00000C0C,

    /* 64-bit control fields */
    VMCS_CTRL_IO_BITMAP_A                   = 0x00002000,
    VMCS_CTRL_IO_BITMAP_A_HI               = 0x00002001,
    VMCS_CTRL_IO_BITMAP_B                   = 0x00002002,
    VMCS_CTRL_IO_BITMAP_B_HI               = 0x00002003,
    VMCS_CTRL_MSR_BITMAP                    = 0x00002004,
    VMCS_CTRL_MSR_BITMAP_HI                = 0x00002005,
    VMCS_CTRL_EXIT_MSR_STORE               = 0x00002006,
    VMCS_CTRL_EXIT_MSR_STORE_HI            = 0x00002007,
    VMCS_CTRL_EXIT_MSR_LOAD                = 0x00002008,
    VMCS_CTRL_EXIT_MSR_LOAD_HI             = 0x00002009,
    VMCS_CTRL_ENTRY_MSR_LOAD               = 0x0000200A,
    VMCS_CTRL_ENTRY_MSR_LOAD_HI            = 0x0000200B,
    VMCS_CTRL_EXECUTIVE_VMCS_PTR           = 0x0000200C,
    VMCS_CTRL_EXECUTIVE_VMCS_PTR_HI        = 0x0000200D,
    VMCS_CTRL_PML_ADDRESS                   = 0x0000200E,
    VMCS_CTRL_PML_ADDRESS_HI               = 0x0000200F,
    VMCS_CTRL_TSC_OFFSET                    = 0x00002010,
    VMCS_CTRL_TSC_OFFSET_HI                = 0x00002011,
    VMCS_CTRL_VIRT_APIC_ADDR               = 0x00002012,
    VMCS_CTRL_VIRT_APIC_ADDR_HI            = 0x00002013,
    VMCS_CTRL_APIC_ACCESS_ADDR             = 0x00002014,
    VMCS_CTRL_APIC_ACCESS_ADDR_HI          = 0x00002015,
    VMCS_CTRL_POSTED_INT_DESC              = 0x00002016,
    VMCS_CTRL_POSTED_INT_DESC_HI           = 0x00002017,
    VMCS_CTRL_VMFUNC_CTRLS                 = 0x00002018,
    VMCS_CTRL_VMFUNC_CTRLS_HI             = 0x00002019,
    VMCS_CTRL_EPTP                          = 0x0000201A,
    VMCS_CTRL_EPTP_HI                       = 0x0000201B,
    VMCS_CTRL_EOI_EXIT0                     = 0x0000201C,
    VMCS_CTRL_EOI_EXIT0_HI                 = 0x0000201D,
    VMCS_CTRL_EOI_EXIT1                     = 0x0000201E,
    VMCS_CTRL_EOI_EXIT1_HI                 = 0x0000201F,
    VMCS_CTRL_EOI_EXIT2                     = 0x00002020,
    VMCS_CTRL_EOI_EXIT2_HI                 = 0x00002021,
    VMCS_CTRL_EPTP_LIST_ADDR               = 0x00002024,
    VMCS_CTRL_EPTP_LIST_ADDR_HI            = 0x00002025,
    VMCS_CTRL_VMREAD_BITMAP                 = 0x00002026,
    VMCS_CTRL_VMREAD_BITMAP_HI             = 0x00002027,
    VMCS_CTRL_VMWRITE_BITMAP                = 0x00002028,
    VMCS_CTRL_VMWRITE_BITMAP_HI            = 0x00002029,
    VMCS_CTRL_XSS_EXIT_BITMAP              = 0x0000202C,
    VMCS_CTRL_XSS_EXIT_BITMAP_HI           = 0x0000202D,
    VMCS_CTRL_ENCLS_EXIT_BITMAP            = 0x0000202E,
    VMCS_CTRL_ENCLS_EXIT_BITMAP_HI         = 0x0000202F,
    VMCS_CTRL_TSC_MULTIPLIER               = 0x00002032,
    VMCS_CTRL_TSC_MULTIPLIER_HI            = 0x00002033,

    /* 64-bit read-only data fields */
    VMCS_RO_GUEST_PHYS_ADDR                 = 0x00002400,
    VMCS_RO_GUEST_PHYS_ADDR_HI             = 0x00002401,

    /* 64-bit guest-state fields */
    VMCS_GUEST_VMCS_LINK_PTR                = 0x00002800,
    VMCS_GUEST_VMCS_LINK_PTR_HI            = 0x00002801,
    VMCS_GUEST_DEBUGCTL                     = 0x00002802,
    VMCS_GUEST_DEBUGCTL_HI                  = 0x00002803,
    VMCS_GUEST_PAT                          = 0x00002804,
    VMCS_GUEST_PAT_HI                       = 0x00002805,
    VMCS_GUEST_EFER                         = 0x00002806,
    VMCS_GUEST_EFER_HI                      = 0x00002807,
    VMCS_GUEST_PERF_GLOBAL_CTRL             = 0x00002808,
    VMCS_GUEST_PERF_GLOBAL_CTRL_HI         = 0x00002809,
    VMCS_GUEST_PDPTE0                       = 0x0000280A,
    VMCS_GUEST_PDPTE0_HI                    = 0x0000280B,
    VMCS_GUEST_PDPTE1                       = 0x0000280C,
    VMCS_GUEST_PDPTE1_HI                    = 0x0000280D,
    VMCS_GUEST_PDPTE2                       = 0x0000280E,
    VMCS_GUEST_PDPTE2_HI                    = 0x0000280F,
    VMCS_GUEST_PDPTE3                       = 0x00002810,
    VMCS_GUEST_PDPTE3_HI                    = 0x00002811,
    VMCS_GUEST_BNDCFGS                      = 0x00002812,
    VMCS_GUEST_BNDCFGS_HI                   = 0x00002813,

    /* 64-bit host-state fields */
    VMCS_HOST_PAT                           = 0x00002C00,
    VMCS_HOST_PAT_HI                        = 0x00002C01,
    VMCS_HOST_EFER                          = 0x00002C02,
    VMCS_HOST_EFER_HI                       = 0x00002C03,
    VMCS_HOST_PERF_GLOBAL_CTRL              = 0x00002C04,
    VMCS_HOST_PERF_GLOBAL_CTRL_HI           = 0x00002C05,

    /* 32-bit control fields */
    VMCS_CTRL_PIN_BASED                     = 0x00004000,
    VMCS_CTRL_PROC_BASED                    = 0x00004002,
    VMCS_CTRL_EXCEPTION_BITMAP              = 0x00004004,
    VMCS_CTRL_PF_ERROR_MASK                 = 0x00004006,
    VMCS_CTRL_PF_ERROR_MATCH                = 0x00004008,
    VMCS_CTRL_CR3_TARGET_COUNT              = 0x0000400A,
    VMCS_CTRL_EXIT                          = 0x0000400C,
    VMCS_CTRL_EXIT_MSR_STORE_COUNT          = 0x0000400E,
    VMCS_CTRL_EXIT_MSR_LOAD_COUNT           = 0x00004010,
    VMCS_CTRL_ENTRY                         = 0x00004012,
    VMCS_CTRL_ENTRY_MSR_LOAD_COUNT          = 0x00004014,
    VMCS_CTRL_ENTRY_INT_INFO                = 0x00004016,
    VMCS_CTRL_ENTRY_EXCEPTION_ERRCODE       = 0x00004018,
    VMCS_CTRL_ENTRY_INSTR_LENGTH            = 0x0000401A,
    VMCS_CTRL_TPR_THRESHOLD                 = 0x0000401C,
    VMCS_CTRL_PROC_BASED2                   = 0x0000401E,
    VMCS_CTRL_PLE_GAP                       = 0x00004020,
    VMCS_CTRL_PLE_WINDOW                    = 0x00004022,

    /* 32-bit read-only data fields */
    VMCS_RO_INSTR_ERROR                     = 0x00004400,
    VMCS_RO_EXIT_REASON                     = 0x00004402,
    VMCS_RO_EXIT_INT_INFO                   = 0x00004404,
    VMCS_RO_EXIT_INT_ERRCODE                = 0x00004406,
    VMCS_RO_IDT_VECTOR_INFO                 = 0x00004408,
    VMCS_RO_IDT_VECTOR_ERRCODE              = 0x0000440A,
    VMCS_RO_EXIT_INSTR_LENGTH               = 0x0000440C,
    VMCS_RO_EXIT_INSTR_INFO                 = 0x0000440E,

    /* 32-bit guest-state fields */
    VMCS_GUEST_ES_LIMIT                     = 0x00004800,
    VMCS_GUEST_CS_LIMIT                     = 0x00004802,
    VMCS_GUEST_SS_LIMIT                     = 0x00004804,
    VMCS_GUEST_DS_LIMIT                     = 0x00004806,
    VMCS_GUEST_FS_LIMIT                     = 0x00004808,
    VMCS_GUEST_GS_LIMIT                     = 0x0000480A,
    VMCS_GUEST_LDTR_LIMIT                   = 0x0000480C,
    VMCS_GUEST_TR_LIMIT                     = 0x0000480E,
    VMCS_GUEST_GDTR_LIMIT                   = 0x00004810,
    VMCS_GUEST_IDTR_LIMIT                   = 0x00004812,
    VMCS_GUEST_ES_ACCESS                    = 0x00004814,
    VMCS_GUEST_CS_ACCESS                    = 0x00004816,
    VMCS_GUEST_SS_ACCESS                    = 0x00004818,
    VMCS_GUEST_DS_ACCESS                    = 0x0000481A,
    VMCS_GUEST_FS_ACCESS                    = 0x0000481C,
    VMCS_GUEST_GS_ACCESS                    = 0x0000481E,
    VMCS_GUEST_LDTR_ACCESS                  = 0x00004820,
    VMCS_GUEST_TR_ACCESS                    = 0x00004822,
    VMCS_GUEST_INTERRUPTIBILITY             = 0x00004824,
    VMCS_GUEST_ACTIVITY                     = 0x00004826,
    VMCS_GUEST_SMBASE                       = 0x00004828,
    VMCS_GUEST_SYSENTER_CS                  = 0x0000482A,
    VMCS_GUEST_PREEMPT_TIMER                = 0x0000482E,

    /* 32-bit host-state fields */
    VMCS_HOST_SYSENTER_CS                   = 0x00004C00,

    /* Natural-width control fields */
    VMCS_CTRL_CR0_MASK                      = 0x00006000,
    VMCS_CTRL_CR4_MASK                      = 0x00006002,
    VMCS_CTRL_CR0_READ_SHADOW               = 0x00006004,
    VMCS_CTRL_CR4_READ_SHADOW               = 0x00006006,
    VMCS_CTRL_CR3_TARGET0                   = 0x00006008,
    VMCS_CTRL_CR3_TARGET1                   = 0x0000600A,
    VMCS_CTRL_CR3_TARGET2                   = 0x0000600C,
    VMCS_CTRL_CR3_TARGET3                   = 0x0000600E,

    /* Natural-width read-only data fields */
    VMCS_RO_EXIT_QUALIFICATION              = 0x00006400,
    VMCS_RO_IO_RCX                          = 0x00006402,
    VMCS_RO_IO_RSI                          = 0x00006404,
    VMCS_RO_IO_RDI                          = 0x00006406,
    VMCS_RO_IO_RIP                          = 0x00006408,
    VMCS_RO_GUEST_LINEAR_ADDR               = 0x0000640A,

    /* Natural-width guest-state fields */
    VMCS_GUEST_CR0                          = 0x00006800,
    VMCS_GUEST_CR3                          = 0x00006802,
    VMCS_GUEST_CR4                          = 0x00006804,
    VMCS_GUEST_ES_BASE                      = 0x00006806,
    VMCS_GUEST_CS_BASE                      = 0x00006808,
    VMCS_GUEST_SS_BASE                      = 0x0000680A,
    VMCS_GUEST_DS_BASE                      = 0x0000680C,
    VMCS_GUEST_FS_BASE                      = 0x0000680E,
    VMCS_GUEST_GS_BASE                      = 0x00006810,
    VMCS_GUEST_LDTR_BASE                    = 0x00006812,
    VMCS_GUEST_TR_BASE                      = 0x00006814,
    VMCS_GUEST_GDTR_BASE                    = 0x00006816,
    VMCS_GUEST_IDTR_BASE                    = 0x00006818,
    VMCS_GUEST_DR7                          = 0x0000681A,
    VMCS_GUEST_RSP                          = 0x0000681C,
    VMCS_GUEST_RIP                          = 0x0000681E,
    VMCS_GUEST_RFLAGS                       = 0x00006820,
    VMCS_GUEST_PENDING_DBG                  = 0x00006822,
    VMCS_GUEST_SYSENTER_ESP                 = 0x00006824,
    VMCS_GUEST_SYSENTER_EIP                 = 0x00006826,

    /* Natural-width host-state fields */
    VMCS_HOST_CR0                           = 0x00006C00,
    VMCS_HOST_CR3                           = 0x00006C02,
    VMCS_HOST_CR4                           = 0x00006C04,
    VMCS_HOST_FS_BASE                       = 0x00006C06,
    VMCS_HOST_GS_BASE                       = 0x00006C08,
    VMCS_HOST_TR_BASE                       = 0x00006C0A,
    VMCS_HOST_GDTR_BASE                     = 0x00006C0C,
    VMCS_HOST_IDTR_BASE                     = 0x00006C0E,
    VMCS_HOST_SYSENTER_ESP                  = 0x00006C10,
    VMCS_HOST_SYSENTER_EIP                  = 0x00006C12,
    VMCS_HOST_RSP                           = 0x00006C14,
    VMCS_HOST_RIP                           = 0x00006C16

} VMCS_FIELD;

/* ---- VM-exit reasons (Intel SDM Vol 3, Appendix C) ---------------------- */

#define VMX_EXIT_EXCEPTION_NMI          0
#define VMX_EXIT_EXTERNAL_INT           1
#define VMX_EXIT_TRIPLE_FAULT           2
#define VMX_EXIT_INIT_SIGNAL            3
#define VMX_EXIT_SIPI                   4
#define VMX_EXIT_IO_SMI                 5
#define VMX_EXIT_OTHER_SMI              6
#define VMX_EXIT_INT_WINDOW             7
#define VMX_EXIT_NMI_WINDOW             8
#define VMX_EXIT_TASK_SWITCH            9
#define VMX_EXIT_CPUID                  10
#define VMX_EXIT_GETSEC                 11
#define VMX_EXIT_HLT                    12
#define VMX_EXIT_INVD                   13
#define VMX_EXIT_INVLPG                 14
#define VMX_EXIT_RDPMC                  15
#define VMX_EXIT_RDTSC                  16
#define VMX_EXIT_RSM                    17
#define VMX_EXIT_VMCALL                 18
#define VMX_EXIT_VMCLEAR                19
#define VMX_EXIT_VMLAUNCH               20
#define VMX_EXIT_VMPTRLD                21
#define VMX_EXIT_VMPTRST                22
#define VMX_EXIT_VMREAD                 23
#define VMX_EXIT_VMRESUME               24
#define VMX_EXIT_VMWRITE                25
#define VMX_EXIT_VMXOFF                 26
#define VMX_EXIT_VMXON                  27
#define VMX_EXIT_CR_ACCESS              28
#define VMX_EXIT_DR_ACCESS              29
#define VMX_EXIT_IO                     30
#define VMX_EXIT_RDMSR                  31
#define VMX_EXIT_WRMSR                  32
#define VMX_EXIT_ENTRY_FAIL_GUEST       33
#define VMX_EXIT_ENTRY_FAIL_MSR         34
#define VMX_EXIT_MWAIT                  36
#define VMX_EXIT_MONITOR_TRAP           37
#define VMX_EXIT_MONITOR                39
#define VMX_EXIT_PAUSE                  40
#define VMX_EXIT_ENTRY_FAIL_MC          41
#define VMX_EXIT_TPR_BELOW              43
#define VMX_EXIT_APIC_ACCESS            44
#define VMX_EXIT_VIRTUALIZED_EOI        45
#define VMX_EXIT_GDTR_IDTR_ACCESS       46
#define VMX_EXIT_LDTR_TR_ACCESS         47
#define VMX_EXIT_EPT_VIOLATION          48
#define VMX_EXIT_EPT_MISCONFIG          49
#define VMX_EXIT_INVEPT                 50
#define VMX_EXIT_RDTSCP                 51
#define VMX_EXIT_PREEMPT_TIMER          52
#define VMX_EXIT_INVVPID                53
#define VMX_EXIT_WBINVD                 54
#define VMX_EXIT_XSETBV                 55
#define VMX_EXIT_APIC_WRITE             56
#define VMX_EXIT_RDRAND                 57
#define VMX_EXIT_INVPCID                58
#define VMX_EXIT_VMFUNC                 59
#define VMX_EXIT_ENCLS                  60
#define VMX_EXIT_RDSEED                 61
#define VMX_EXIT_PML_FULL               62
#define VMX_EXIT_XSAVES                 63
#define VMX_EXIT_XRSTORS               64

/* ---- Pin-based VM-execution control bits -------------------------------- */

#define VMX_PIN_EXTERNAL_INT            (1U << 0)
#define VMX_PIN_NMI_EXITING             (1U << 3)
#define VMX_PIN_VIRTUAL_NMIS            (1U << 5)
#define VMX_PIN_PREEMPT_TIMER           (1U << 6)
#define VMX_PIN_POSTED_INTS             (1U << 7)

/* ---- Primary processor-based VM-execution control bits ------------------ */

#define VMX_PROC_INT_WINDOW_EXIT        (1U << 2)
#define VMX_PROC_TSC_OFFSETTING          (1U << 3)
#define VMX_PROC_HLT_EXIT               (1U << 7)
#define VMX_PROC_INVLPG_EXIT            (1U << 9)
#define VMX_PROC_MWAIT_EXIT             (1U << 10)
#define VMX_PROC_RDPMC_EXIT             (1U << 11)
#define VMX_PROC_RDTSC_EXIT             (1U << 12)
#define VMX_PROC_CR3_LOAD_EXIT          (1U << 15)
#define VMX_PROC_CR3_STORE_EXIT         (1U << 16)
#define VMX_PROC_CR8_LOAD_EXIT          (1U << 19)
#define VMX_PROC_CR8_STORE_EXIT         (1U << 20)
#define VMX_PROC_TPR_SHADOW             (1U << 21)
#define VMX_PROC_NMI_WINDOW_EXIT        (1U << 22)
#define VMX_PROC_DR_EXIT                (1U << 23)
#define VMX_PROC_UNCOND_IO_EXIT         (1U << 24)
#define VMX_PROC_USE_IO_BITMAPS         (1U << 25)
#define VMX_PROC_MONITOR_TRAP           (1U << 27)
#define VMX_PROC_USE_MSR_BITMAPS        (1U << 28)
#define VMX_PROC_MONITOR_EXIT           (1U << 29)
#define VMX_PROC_PAUSE_EXIT             (1U << 30)
#define VMX_PROC_SECONDARY_CTLS         (1U << 31)

/* ---- Secondary processor-based VM-execution control bits ---------------- */

#define VMX_PROC2_VIRT_APIC_ACCESS      (1U << 0)
#define VMX_PROC2_EPT                   (1U << 1)
#define VMX_PROC2_DESC_TABLE_EXIT       (1U << 2)
#define VMX_PROC2_RDTSCP                (1U << 3)
#define VMX_PROC2_VIRT_X2APIC           (1U << 4)
#define VMX_PROC2_VPID                  (1U << 5)
#define VMX_PROC2_WBINVD_EXIT           (1U << 6)
#define VMX_PROC2_UNRESTRICTED_GUEST    (1U << 7)
#define VMX_PROC2_APIC_REG_VIRT         (1U << 8)
#define VMX_PROC2_VIRT_INT_DELIVERY     (1U << 9)
#define VMX_PROC2_PAUSE_LOOP_EXIT       (1U << 10)
#define VMX_PROC2_RDRAND_EXIT           (1U << 11)
#define VMX_PROC2_INVPCID               (1U << 12)
#define VMX_PROC2_VMFUNC                (1U << 13)
#define VMX_PROC2_VMCS_SHADOWING        (1U << 14)
#define VMX_PROC2_ENCLS_EXIT            (1U << 15)
#define VMX_PROC2_RDSEED_EXIT           (1U << 16)
#define VMX_PROC2_PML                   (1U << 17)
#define VMX_PROC2_EPT_VE                (1U << 18)
#define VMX_PROC2_XSAVES               (1U << 20)

/* ---- VM-exit control bits ----------------------------------------------- */

#define VMX_EXIT_SAVE_DBG_CTLS          (1U << 2)
#define VMX_EXIT_HOST_ADDR_SPACE_64     (1U << 9)
#define VMX_EXIT_LOAD_PERF_GLOBAL       (1U << 12)
#define VMX_EXIT_ACK_INT_ON_EXIT        (1U << 15)
#define VMX_EXIT_SAVE_PAT               (1U << 18)
#define VMX_EXIT_LOAD_PAT               (1U << 19)
#define VMX_EXIT_SAVE_EFER              (1U << 20)
#define VMX_EXIT_LOAD_EFER              (1U << 21)
#define VMX_EXIT_SAVE_PREEMPT_TIMER     (1U << 22)

/* ---- VM-entry control bits ---------------------------------------------- */

#define VMX_ENTRY_LOAD_DBG_CTLS         (1U << 2)
#define VMX_ENTRY_IA32E_GUEST           (1U << 9)
#define VMX_ENTRY_SMM                   (1U << 10)
#define VMX_ENTRY_DEACT_DUAL_MON        (1U << 11)
#define VMX_ENTRY_LOAD_PERF_GLOBAL      (1U << 13)
#define VMX_ENTRY_LOAD_PAT              (1U << 14)
#define VMX_ENTRY_LOAD_EFER             (1U << 15)
#define VMX_ENTRY_LOAD_BNDCFGS          (1U << 16)

/* ---- Function prototypes (vmx/) ----------------------------------------- */

/* vmx.c - VMX enable/disable and capability detection */
NTSTATUS
RosvVmxDetectCapabilities(
    _Out_ PVMX_CAPABILITIES Caps);

NTSTATUS
RosvVmxEnable(
    _In_ PVMX_CAPABILITIES Caps,
    _Out_ PHYSICAL_ADDRESS *VmxonRegionPhys,
    _Out_ PVOID *VmxonRegionVirt);

VOID
RosvVmxDisable(
    _In_ PHYSICAL_ADDRESS VmxonRegionPhys,
    _In_ PVOID VmxonRegionVirt);

/* vmcs.c - VMCS lifecycle and configuration */
NTSTATUS
RosvVmcsAlloc(
    _In_ ULONG RevisionId,
    _Out_ PHYSICAL_ADDRESS *VmcsPhys,
    _Out_ PVOID *VmcsVirt);

VOID
RosvVmcsFree(
    _In_ PHYSICAL_ADDRESS VmcsPhys,
    _In_ PVOID VmcsVirt);

NTSTATUS
RosvVmcsClear(
    _In_ PHYSICAL_ADDRESS VmcsPhys);

NTSTATUS
RosvVmcsLoad(
    _In_ PHYSICAL_ADDRESS VmcsPhys);

ULONG64
RosvVmcsRead(
    _In_ VMCS_FIELD Field);

VOID
RosvVmcsWrite(
    _In_ VMCS_FIELD Field,
    _In_ ULONG64 Value);

NTSTATUS
RosvVmcsSetupGuest(
    _In_ PROSV_VCPU Vcpu,
    _In_ PROSV_VM Vm);

NTSTATUS
RosvVmcsSetupHost(
    _In_ PROSV_VCPU Vcpu);

NTSTATUS
RosvVmcsSetupControls(
    _In_ PVMX_CAPABILITIES Caps,
    _In_ PROSV_VM Vm);

/* exit.c - VM-exit dispatcher */
BOOLEAN
RosvVmExitDispatch(
    _Inout_ PROSV_VCPU Vcpu,
    _In_ ULONG ExitReason,
    _In_ ULONG64 ExitQualification,
    _In_ ULONG64 GuestRip,
    _In_ ULONG InstructionLength);

/* vmx_asm.S - Assembly stubs */
VMX_RESULT
RosvAsmVmxOn(
    _In_ PHYSICAL_ADDRESS *VmxonRegion);

VMX_RESULT
RosvAsmVmxOff(VOID);

VMX_RESULT
RosvAsmVmClear(
    _In_ PHYSICAL_ADDRESS *VmcsRegion);

VMX_RESULT
RosvAsmVmPtrLoad(
    _In_ PHYSICAL_ADDRESS *VmcsRegion);

VMX_RESULT
RosvAsmVmRead(
    _In_ ULONG64 Field,
    _Out_ ULONG64 *Value);

VMX_RESULT
RosvAsmVmWrite(
    _In_ ULONG64 Field,
    _In_ ULONG64 Value);

VMX_RESULT
RosvAsmVmLaunch(VOID);

VMX_RESULT
RosvAsmVmResume(VOID);

VOID
RosvAsmVmExitHandler(VOID);

VOID
RosvAsmVmRun(
    _In_ PROSV_VCPU Vcpu);

VOID
RosvAsmVmRunResume(
    _In_ PROSV_VCPU Vcpu);

NTSTATUS
RosvAsmInvept(
    _In_ ULONG Type,
    _In_ ULONG64 Eptp);

/* vmx_debug.c - VMCS state dump and diagnostics */
VOID
RosvVmxDumpVmcs(VOID);

VOID
RosvVmxDumpExitReason(
    _In_ ULONG ExitReason,
    _In_ ULONG64 ExitQualification);

VOID
RosvVmxDumpFailure(VOID);

VOID
RosvDumpGuestRegisters(
    _In_ PROSV_VCPU Vcpu);

VOID
RosvDumpGuestMemory(
    _In_ PROSV_VM Vm,
    _In_ ULONG64 Gpa,
    _In_ ULONG Length);

VOID
RosvDetectTripleFault(
    _Inout_ PROSV_VCPU Vcpu,
    _Inout_ PROSV_VM Vm);

VOID
RosvDetectGuestPanic(
    _In_ PROSV_VM Vm,
    _In_ PUCHAR SerialOutput,
    _In_ ULONG Length);
