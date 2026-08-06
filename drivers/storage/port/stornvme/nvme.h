/*
 * PROJECT:     ReactOS Storport NVMe miniport
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     NVM Express register and command layout
 * COPYRIGHT:   Copyright 2026 ReactOS Project
 *
 * Layouts follow the public NVM Express Base Specification.
 */

#pragma once

/* Controller registers, at offset 0 of BAR0. */
#define NVME_REG_CAP        0x00
#define NVME_REG_VS         0x08
#define NVME_REG_INTMS      0x0C
#define NVME_REG_INTMC      0x10
#define NVME_REG_CC         0x14
#define NVME_REG_CSTS       0x1C
#define NVME_REG_AQA        0x24
#define NVME_REG_ASQ        0x28
#define NVME_REG_ACQ        0x30
#define NVME_REG_DOORBELL   0x1000

/* CAP, read as two 32-bit halves. */
#define NVME_CAP_LOW_MQES(Low)      ((Low) & 0xFFFF)
#define NVME_CAP_LOW_CQR(Low)       (((Low) >> 16) & 1)
#define NVME_CAP_LOW_TO(Low)        (((Low) >> 24) & 0xFF)
#define NVME_CAP_HIGH_DSTRD(High)   ((High) & 0xF)
#define NVME_CAP_HIGH_CSS(High)     (((High) >> 5) & 0x1FF)
#define NVME_CAP_HIGH_MPSMIN(High)  (((High) >> 16) & 0xF)
#define NVME_CAP_HIGH_MPSMAX(High)  (((High) >> 20) & 0xF)

#define NVME_CC_EN          0x00000001
#define NVME_CC_CSS_NVM     0x00000000
#define NVME_CC_SHN_NORMAL  0x00004000
#define NVME_CC_SHN_MASK    0x0000C000
/* Entry sizes are log2: 64-byte SQE, 16-byte CQE. Both must be programmed
 * before the enable bit or the controller refuses to start. */
#define NVME_CC_IOSQES      (6 << 16)
#define NVME_CC_IOCQES      (4 << 20)

/* Doorbells: SQ y tail at 0x1000 + (2y)*(4 << DSTRD), CQ y head follows. */
#define NVME_SQ_DOORBELL(Stride, QueueId)   (NVME_REG_DOORBELL + (2 * (QueueId)) * (Stride))
#define NVME_CQ_DOORBELL(Stride, QueueId)   (NVME_REG_DOORBELL + (2 * (QueueId) + 1) * (Stride))

#define NVME_CSTS_RDY       0x00000001
#define NVME_CSTS_CFS       0x00000002
#define NVME_CSTS_SHST_MASK 0x0000000C
#define NVME_CSTS_SHST_DONE 0x00000008

/* Admin opcodes. */
#define NVME_ADMIN_DELETE_SQ    0x00
#define NVME_ADMIN_CREATE_SQ    0x01
#define NVME_ADMIN_GET_LOG_PAGE 0x02
#define NVME_ADMIN_DELETE_CQ    0x04
#define NVME_ADMIN_CREATE_CQ    0x05
#define NVME_ADMIN_IDENTIFY     0x06
#define NVME_ADMIN_ABORT        0x08
#define NVME_ADMIN_SET_FEATURES 0x09
#define NVME_ADMIN_GET_FEATURES 0x0A
#define NVME_ADMIN_ASYNC_EVENT  0x0C

/* NVM command set opcodes. */
#define NVME_NVM_FLUSH          0x00
#define NVME_NVM_WRITE          0x01
#define NVME_NVM_READ           0x02
#define NVME_NVM_COMPARE        0x05
#define NVME_NVM_WRITE_ZEROES   0x08
#define NVME_NVM_DATASET_MGMT   0x09

/* Identify CNS values. */
#define NVME_CNS_NAMESPACE      0x00
#define NVME_CNS_CONTROLLER     0x01
#define NVME_CNS_ACTIVE_NS      0x02

/* Feature identifiers. */
#define NVME_FEATURE_POWER_MANAGEMENT   0x02
#define NVME_FEATURE_TEMP_THRESHOLD     0x04
#define NVME_FEATURE_VOLATILE_WC        0x06
#define NVME_FEATURE_NUMBER_OF_QUEUES   0x07
#define NVME_FEATURE_INT_COALESCING     0x08
#define NVME_FEATURE_ASYNC_EVENT_CONFIG 0x0B
#define NVME_FEATURE_APST               0x0C
#define NVME_FEATURE_HCTM               0x10

/* Log page identifiers. */
#define NVME_LOG_ERROR          0x01
#define NVME_LOG_SMART          0x02
#define NVME_LOG_FW_SLOT        0x03

/* ONCS bits. */
#define NVME_ONCS_COMPARE       0x0001
#define NVME_ONCS_WRITE_UNC     0x0002
#define NVME_ONCS_DSM           0x0004
#define NVME_ONCS_WRITE_ZEROES  0x0008

/* OACS/OAES bits used here. */
#define NVME_OAES_NS_ATTRIBUTE  0x00000100

/* Dataset Management command attributes. */
#define NVME_DSM_ATTRIBUTE_DEALLOCATE   0x00000004

/* Async event completion DW0 decode. */
#define NVME_AEN_TYPE(Dw0)      ((Dw0) & 0x7)
#define NVME_AEN_INFO(Dw0)      (((Dw0) >> 8) & 0xFF)
#define NVME_AEN_LOG_PAGE(Dw0)  (((Dw0) >> 16) & 0xFF)
#define NVME_AEN_TYPE_ERROR     0
#define NVME_AEN_TYPE_SMART     1
#define NVME_AEN_TYPE_NOTICE    2

/* SMART/health log critical warning bits. */
#define NVME_SMART_SPARE_LOW        0x01
#define NVME_SMART_TEMPERATURE      0x02
#define NVME_SMART_RELIABILITY      0x04
#define NVME_SMART_READ_ONLY        0x08
#define NVME_SMART_BACKUP_FAILED    0x10

/* Power state descriptor flags (byte 3). */
#define NVME_PSD_FLAG_MXPS      0x01
#define NVME_PSD_FLAG_NOPS      0x02

/* APST table entry: idle transition power state and time in milliseconds. */
#define NVME_APST_ENTRY(Ps, Ms) (((ULONGLONG)(Ps) << 3) | ((ULONGLONG)(Ms) << 8))
#define NVME_APST_ENTRIES       64

/* Status code types. */
#define NVME_SCT_GENERIC        0x0
#define NVME_SCT_COMMAND        0x1
#define NVME_SCT_MEDIA          0x2

#include <pshpack1.h>

typedef struct _NVME_COMMAND
{
    ULONG CDW0;         /* opcode, fused, PSDT, command identifier */
    ULONG NSID;
    ULONG CDW2;
    ULONG CDW3;
    ULONGLONG MPTR;
    ULONGLONG PRP1;
    ULONGLONG PRP2;
    ULONG CDW10;
    ULONG CDW11;
    ULONG CDW12;
    ULONG CDW13;
    ULONG CDW14;
    ULONG CDW15;
} NVME_COMMAND, *PNVME_COMMAND;

typedef struct _NVME_COMPLETION
{
    ULONG DW0;
    ULONG DW1;
    ULONG SQHeadAndId;  /* SQ head pointer, SQ identifier */
    ULONG StatusAndId;  /* command identifier, phase tag, status field */
} NVME_COMPLETION, *PNVME_COMPLETION;

typedef struct _NVME_POWER_STATE_DESC
{
    USHORT MP;
    UCHAR Reserved2;
    UCHAR Flags;
    ULONG ENLAT;
    ULONG EXLAT;
    UCHAR RRT;
    UCHAR RRL;
    UCHAR RWT;
    UCHAR RWL;
    USHORT IDLP;
    UCHAR IPS;
    UCHAR Reserved19;
    USHORT ACTP;
    UCHAR APS;
    UCHAR Reserved23[9];
} NVME_POWER_STATE_DESC, *PNVME_POWER_STATE_DESC;

typedef struct _NVME_IDENTIFY_CONTROLLER
{
    USHORT VID;
    USHORT SSVID;
    UCHAR SN[20];
    UCHAR MN[40];
    UCHAR FR[8];
    UCHAR RAB;
    UCHAR IEEE[3];
    UCHAR CMIC;
    UCHAR MDTS;
    USHORT CNTLID;
    ULONG VER;
    ULONG RTD3R;
    ULONG RTD3E;
    ULONG OAES;
    ULONG CTRATT;
    UCHAR Reserved100[156];
    USHORT OACS;
    UCHAR ACL;
    UCHAR AERL;
    UCHAR FRMW;
    UCHAR LPA;
    UCHAR ELPE;
    UCHAR NPSS;
    UCHAR AVSCC;
    UCHAR APSTA;
    USHORT WCTEMP;
    USHORT CCTEMP;
    USHORT MTFA;
    ULONG HMPRE;
    ULONG HMMIN;
    UCHAR TNVMCAP[16];
    UCHAR UNVMCAP[16];
    ULONG RPMBS;
    USHORT EDSTT;
    UCHAR DSTO;
    UCHAR FWUG;
    USHORT KAS;
    USHORT HCTMA;
    USHORT MNTMT;
    USHORT MXTMT;
    ULONG SANICAP;
    UCHAR Reserved332[180];
    UCHAR SQES;
    UCHAR CQES;
    USHORT MAXCMD;
    ULONG NN;
    USHORT ONCS;
    USHORT FUSES;
    UCHAR FNA;
    UCHAR VWC;
    USHORT AWUN;
    USHORT AWUPF;
    UCHAR NVSCC;
    UCHAR NWPC;
    USHORT ACWU;
    UCHAR Reserved534[2];
    ULONG SGLS;
    UCHAR Reserved540[228];
    UCHAR SUBNQN[256];
    UCHAR Reserved1024[1024];
    NVME_POWER_STATE_DESC PSD[32];
    UCHAR VendorSpecific[1024];
} NVME_IDENTIFY_CONTROLLER, *PNVME_IDENTIFY_CONTROLLER;

typedef struct _NVME_LBA_FORMAT
{
    USHORT MS;
    UCHAR LBADS;
    UCHAR RP;
} NVME_LBA_FORMAT, *PNVME_LBA_FORMAT;

typedef struct _NVME_IDENTIFY_NAMESPACE
{
    ULONGLONG NSZE;
    ULONGLONG NCAP;
    ULONGLONG NUSE;
    UCHAR NSFEAT;
    UCHAR NLBAF;
    UCHAR FLBAS;
    UCHAR MC;
    UCHAR DPC;
    UCHAR DPS;
    UCHAR NMIC;
    UCHAR RESCAP;
    UCHAR FPI;
    UCHAR DLFEAT;
    USHORT NAWUN;
    USHORT NAWUPF;
    USHORT NACWU;
    USHORT NABSN;
    USHORT NABO;
    USHORT NABSPF;
    USHORT NOIOB;
    UCHAR NVMCAP[16];
    UCHAR Reserved64[40];
    UCHAR NGUID[16];
    UCHAR EUI64[8];
    NVME_LBA_FORMAT LBAF[16];
    UCHAR Reserved192[192];
    UCHAR VendorSpecific[3712];
} NVME_IDENTIFY_NAMESPACE, *PNVME_IDENTIFY_NAMESPACE;

typedef struct _NVME_SMART_LOG
{
    UCHAR CriticalWarning;
    USHORT CompositeTemperature;
    UCHAR AvailableSpare;
    UCHAR AvailableSpareThreshold;
    UCHAR PercentageUsed;
    UCHAR Reserved6[26];
    UCHAR DataUnitsRead[16];
    UCHAR DataUnitsWritten[16];
    UCHAR HostReadCommands[16];
    UCHAR HostWriteCommands[16];
    UCHAR ControllerBusyTime[16];
    UCHAR PowerCycles[16];
    UCHAR PowerOnHours[16];
    UCHAR UnsafeShutdowns[16];
    UCHAR MediaErrors[16];
    UCHAR ErrorLogEntries[16];
    ULONG WarningTemperatureTime;
    ULONG CriticalTemperatureTime;
    USHORT TemperatureSensor[8];
    UCHAR Reserved216[296];
} NVME_SMART_LOG, *PNVME_SMART_LOG;

/* Dataset management range, used for UNMAP. */
typedef struct _NVME_DSM_RANGE
{
    ULONG ContextAttributes;
    ULONG LengthInBlocks;
    ULONGLONG StartingLba;
} NVME_DSM_RANGE, *PNVME_DSM_RANGE;

#include <poppack.h>

#define NVME_COMMAND_SET_OPCODE(Command, Opcode, CommandId) \
    ((Command)->CDW0 = ((ULONG)(Opcode)) | (((ULONG)(CommandId)) << 16))

#define NVME_COMPLETION_COMMAND_ID(Completion)  ((USHORT)((Completion)->StatusAndId & 0xFFFF))
#define NVME_COMPLETION_PHASE(Completion)       (((Completion)->StatusAndId >> 16) & 1)
#define NVME_COMPLETION_STATUS(Completion)      ((USHORT)(((Completion)->StatusAndId >> 17) & 0x7FFF))
#define NVME_COMPLETION_SQ_HEAD(Completion)     ((USHORT)((Completion)->SQHeadAndId & 0xFFFF))
#define NVME_STATUS_CODE(Status)                ((UCHAR)((Status) & 0xFF))
#define NVME_STATUS_CODE_TYPE(Status)           ((UCHAR)(((Status) >> 8) & 0x7))

C_ASSERT(sizeof(NVME_COMMAND) == 64);
C_ASSERT(sizeof(NVME_COMPLETION) == 16);
C_ASSERT(sizeof(NVME_POWER_STATE_DESC) == 32);
C_ASSERT(sizeof(NVME_IDENTIFY_CONTROLLER) == PAGE_SIZE);
C_ASSERT(sizeof(NVME_IDENTIFY_NAMESPACE) == PAGE_SIZE);
C_ASSERT(sizeof(NVME_SMART_LOG) == 512);
C_ASSERT(FIELD_OFFSET(NVME_IDENTIFY_CONTROLLER, OAES) == 92);
C_ASSERT(FIELD_OFFSET(NVME_IDENTIFY_CONTROLLER, OACS) == 256);
C_ASSERT(FIELD_OFFSET(NVME_IDENTIFY_CONTROLLER, HCTMA) == 322);
C_ASSERT(FIELD_OFFSET(NVME_IDENTIFY_CONTROLLER, SQES) == 512);
C_ASSERT(FIELD_OFFSET(NVME_IDENTIFY_CONTROLLER, NN) == 516);
C_ASSERT(FIELD_OFFSET(NVME_IDENTIFY_CONTROLLER, ONCS) == 520);
C_ASSERT(FIELD_OFFSET(NVME_IDENTIFY_CONTROLLER, PSD) == 2048);
C_ASSERT(FIELD_OFFSET(NVME_IDENTIFY_NAMESPACE, LBAF) == 128);
C_ASSERT(FIELD_OFFSET(NVME_SMART_LOG, WarningTemperatureTime) == 192);
