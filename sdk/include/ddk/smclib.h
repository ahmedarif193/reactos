/*
 * PROJECT:     LiberNT DDK
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Smart card driver library interface
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _SMCLIB_
#define _SMCLIB_

#if DBG || DEBUG
#undef DEBUG
#define DEBUG 1
#undef DBG
#define DBG 1
#endif

#ifdef __cplusplus
extern "C" {
#endif

#include "smcnt.h"
#include "winsmcrd.h"

#ifndef DRIVER_NAME
#define DRIVER_NAME "SMCLIB"
#endif

#define SMCLIB_VERSION          0x150
#define SMCLIB_VERSION_REQUIRED 0x100

#if DEBUG
#define DEBUG_IOCTL     ((ULONG) 0x00000001)
#define DEBUG_ATR       ((ULONG) 0x00000002)
#define DEBUG_PROTOCOL  ((ULONG) 0x00000004)
#define DEBUG_DRIVER    ((ULONG) 0x00000008)
#define DEBUG_TRACE     ((ULONG) 0x00000010)
#define DEBUG_ERROR     ((ULONG) 0x00000020)
#define DEBUG_INFO      DEBUG_ERROR
#define DEBUG_PERF      ((ULONG) 0x10000000)
#define DEBUG_T1_TEST   ((ULONG) 0x40000000)
#define DEBUG_BREAK     ((ULONG) 0x80000000)
#define DEBUG_ALL       ((ULONG) 0x0000FFFF)
#endif

#if DEBUG
#define SmartcardDebug(LEVEL, STRING) \
        { \
            if ((LEVEL) & (DEBUG_ERROR | SmartcardGetDebugLevel())) \
                DbgPrint STRING; \
            if (SmartcardGetDebugLevel() & DEBUG_BREAK) \
                NT_ASSERT(FALSE); \
        }
#else
#define SmartcardDebug(LEVEL, STRING)
#endif

#define AccessUnsafeData(Irql) \
    KeAcquireSpinLock(&SmartcardExtension->OsData->SpinLock, (Irql));
#define EndAccessUnsafeData(Irql) \
    KeReleaseSpinLock(&SmartcardExtension->OsData->SpinLock, (Irql));

#ifndef SMART_CARD_READER_GUID_DEFINED
#define SMART_CARD_READER_GUID_DEFINED
#include <initguid.h>
DEFINE_GUID(SmartCardReaderGuid, 0x50DD5230, 0xBA8A, 0x11D1, 0xBF,0x5D,0x00,0x00,0xF8,0x05,0xF5,0x30);
#endif

#define RDF_CARD_POWER      0
#define RDF_TRANSMIT        1
#define RDF_CARD_EJECT      2
#define RDF_READER_SWALLOW  3
#define RDF_CARD_TRACKING   4
#define RDF_SET_PROTOCOL    5
#define RDF_DEBUG_LEVEL     6
#define RDF_CARD_CONFISCATE 7
#define RDF_IOCTL_VENDOR    8
#define RDF_ATR_PARSE       9

#define MIN_BUFFER_SIZE 288

typedef union _LENGTH {
    struct {
        ULONG l0;
    } l;
    struct {
        UCHAR b0;
        UCHAR b1;
        UCHAR b2;
        UCHAR b3;
    } b;
} LENGTH, *PLENGTH;

#define MAXIMUM_ATR_CODES   4
#define MAXIMUM_ATR_LENGTH  33

typedef struct _T0_DATA {
    ULONG Lc;
    ULONG Le;
} T0_DATA, *PT0_DATA;

#define T1_INIT             0
#define T1_START            1
#define T1_I_BLOCK          2
#define T1_R_BLOCK          3
#define T1_RESTART          4

#define T1_RESYNCH_REQUEST  0xC0
#define T1_RESYNCH_RESPONSE 0xE0
#define T1_IFS_REQUEST      0xC1
#define T1_IFS_RESPONSE     0xE1
#define T1_ABORT_REQUEST    0xC2
#define T1_ABORT_RESPONSE   0xE2
#define T1_WTX_REQUEST      0xC3
#define T1_WTX_RESPONSE     0xE3
#define T1_VPP_ERROR        0xE4

#define T1_IFSD             254
#define T1_IFSD_DEFAULT     32

#define T1_MAX_RETRIES      2

#define T1_MORE_DATA        0x20

#define T1_ERROR_CHKSUM     1
#define T1_ERROR_OTHER      2

#define T1_CRC_CHECK        1

#define T1_CWI_DEFAULT      13

#define T1_BWI_DEFAULT      4

typedef struct _T1_DATA {
    UCHAR IFSC;
    UCHAR IFSD;
    ULONG BytesReceived;
    ULONG BytesSent;
    ULONG BytesToSend;
    UCHAR LastError;
    BOOLEAN MoreData;
    UCHAR NAD;
    ULONG OriginalState;
    UCHAR Resend;
    UCHAR Resynch;
    UCHAR RSN;
    UCHAR SSN;
    ULONG State;
    UCHAR Wtx;
    PUCHAR ReplyData;
    BOOLEAN WaitForReply;
    UCHAR InfBytesSent;
#ifndef _WIN64
    UCHAR Reserved[
        10 -
        sizeof(PUCHAR) -
        sizeof(BOOLEAN) -
        sizeof(UCHAR)];
#endif
} T1_DATA, *PT1_DATA;

typedef struct _T1_BLOCK_FRAME {
    UCHAR Nad;
    UCHAR Pcb;
    UCHAR Len;
    PUCHAR Inf;
} T1_BLOCK_FRAME, *PT1_BLOCK_FRAME;

typedef struct _SMARTCARD_REQUEST {
    PUCHAR Buffer;
    ULONG BufferSize;
    ULONG BufferLength;
} SMARTCARD_REQUEST, *PSMARTCARD_REQUEST;

typedef struct _SMARTCARD_REPLY {
    PUCHAR Buffer;
    ULONG BufferSize;
    ULONG BufferLength;
} SMARTCARD_REPLY, *PSMARTCARD_REPLY;

typedef struct _CLOCK_RATE_CONVERSION {
    const ULONG F;
    const ULONG fs;
} CLOCK_RATE_CONVERSION, *PCLOCK_RATE_CONVERSION;

typedef struct _BIT_RATE_ADJUSTMENT {
    const ULONG DNumerator;
    const ULONG DDivisor;
} BIT_RATE_ADJUSTMENT, *PBIT_RATE_ADJUSTMENT;

#ifdef _ISO_TABLES_
#define MHZ * 1000000l

static CLOCK_RATE_CONVERSION ClockRateConversion[] = {
    { 372,  4 MHZ   },
    { 372,  5 MHZ   },
    { 558,  6 MHZ   },
    { 744,  8 MHZ   },
    { 1116, 12 MHZ  },
    { 1488, 16 MHZ  },
    { 1860, 20 MHZ  },
    { 0,    0       },
    { 0,    0       },
    { 512,  5 MHZ   },
    { 768,  7500000 },
    { 1024, 10 MHZ  },
    { 1536, 15 MHZ  },
    { 2048, 20 MHZ  },
    { 0,    0       },
    { 0,    0       }
};

#undef MHZ

static BIT_RATE_ADJUSTMENT BitRateAdjustment[] = {
    { 0,    0   },
    { 1,    1   },
    { 2,    1   },
    { 4,    1   },
    { 8,    1   },
    { 16,   1   },
    { 32,   1   },
    { 64,   1   },
    { 12,   1   },
    { 20,   1   },
    { 0,    0   },
    { 0,    0   },
    { 0,    0   },
    { 0,    0   },
    { 0,    0   },
    { 0,    0   }
};
#endif

#if defined (DEBUG) && defined (SMCLIB_NT)
typedef struct _PERF_INFO {
    ULONG NumTransmissions;
    ULONG BytesSent;
    ULONG BytesReceived;
    LARGE_INTEGER IoTickCount;
    LARGE_INTEGER TickStart;
    LARGE_INTEGER TickEnd;
} PERF_INFO, *PPERF_INFO;
#endif

typedef struct _PTS_DATA {
#define PTS_TYPE_DEFAULT 0x00
#define PTS_TYPE_OPTIMAL 0x01
#define PTS_TYPE_USER    0x02
    UCHAR Type;
    UCHAR Fl;
    UCHAR Dl;
    ULONG CLKFrequency;
    ULONG DataRate;
    UCHAR StopBits;
} PTS_DATA, *PPTS_DATA;

typedef struct _SCARD_CARD_CAPABILITIES {
    BOOLEAN InversConvention;
    ULONG etu;
    struct {
        UCHAR Buffer[64];
        UCHAR Length;
    } ATR;
    struct {
        UCHAR Buffer[16];
        UCHAR Length;
    } HistoricalChars;
    PCLOCK_RATE_CONVERSION ClockRateConversion;
    PBIT_RATE_ADJUSTMENT BitRateAdjustment;
    UCHAR Fl;
    UCHAR Dl;
    UCHAR II;
    UCHAR P;
    UCHAR N;
    ULONG GT;
    struct {
        ULONG Supported;
        ULONG Selected;
    } Protocol;
    struct {
        UCHAR WI;
        ULONG WT;
    } T0;
    struct {
        UCHAR IFSC;
        UCHAR CWI;
        UCHAR BWI;
        UCHAR EDC;
        ULONG CWT;
        ULONG BWT;
        ULONG BGT;
    } T1;
    PTS_DATA PtsData;
    UCHAR Reserved[100 - sizeof(PTS_DATA)];
} SCARD_CARD_CAPABILITIES, *PSCARD_CARD_CAPABILITIES;

typedef struct _SCARD_READER_CAPABILITIES {
    ULONG SupportedProtocols;
    ULONG Reserved;
    ULONG ReaderType;
    ULONG MechProperties;
    ULONG CurrentState;
    ULONG Channel;
    struct {
        ULONG Default;
        ULONG Max;
    } CLKFrequency;
    struct {
        ULONG Default;
        ULONG Max;
    } DataRate;
    ULONG MaxIFSD;
    ULONG PowerMgmtSupport;
    ULONG CardConfiscated;
    struct _DataRatesSupported {
        PULONG List;
        UCHAR Entries;
    } DataRatesSupported;
    struct _CLKFrequenciesSupported {
        PULONG List;
        UCHAR Entries;
    } CLKFrequenciesSupported;
    UCHAR Reserved1[
        100 -
        sizeof(ULONG) -
        sizeof(struct _DataRatesSupported) -
        sizeof(struct _CLKFrequenciesSupported)
        ];
} SCARD_READER_CAPABILITIES, *PSCARD_READER_CAPABILITIES;

typedef struct _VENDOR_ATTR {
    struct {
        USHORT Length;
        UCHAR Buffer[MAXIMUM_ATTR_STRING_LENGTH];
    } VendorName;
    struct {
        USHORT Length;
        UCHAR Buffer[MAXIMUM_ATTR_STRING_LENGTH];
    } IfdType;
    ULONG UnitNo;
    struct {
        USHORT BuildNumber;
        UCHAR VersionMinor;
        UCHAR VersionMajor;
    } IfdVersion;
    struct {
        USHORT Length;
        UCHAR Buffer[MAXIMUM_ATTR_STRING_LENGTH];
    } IfdSerialNo;
    ULONG Reserved[25];
} VENDOR_ATTR, *PVENDOR_ATTR;

typedef struct _READER_EXTENSION *PREADER_EXTENSION;

typedef struct _OS_DEP_DATA *POS_DEP_DATA;

typedef struct _SMARTCARD_EXTENSION *PSMARTCARD_EXTENSION;

typedef struct _SMARTCARD_EXTENSION {
    ULONG Version;
    VENDOR_ATTR VendorAttr;
    NTSTATUS (NTAPI *ReaderFunction[16])(PSMARTCARD_EXTENSION);
    SCARD_CARD_CAPABILITIES CardCapabilities;
    ULONG LastError;
    struct {
        PULONG Information;
        PUCHAR RequestBuffer;
        ULONG RequestBufferLength;
        PUCHAR ReplyBuffer;
        ULONG ReplyBufferLength;
    } IoRequest;
    ULONG MajorIoControlCode;
    ULONG MinorIoControlCode;
    POS_DEP_DATA OsData;
    SCARD_READER_CAPABILITIES ReaderCapabilities;
    PREADER_EXTENSION ReaderExtension;
    SMARTCARD_REPLY SmartcardReply;
    SMARTCARD_REQUEST SmartcardRequest;
    T0_DATA T0;
    T1_DATA T1;
#if defined (DEBUG) && defined (SMCLIB_NT)
    PPERF_INFO PerfInfo;
#endif
    ULONG Reserved[
        25
#if defined (DEBUG) && defined (SMCLIB_NT)
        - sizeof(PPERF_INFO)
#endif
        ];
} SMARTCARD_EXTENSION, *PSMARTCARD_EXTENSION;

#ifndef _SMCLIBSYSTEM_
#define SMCLIBAPI _declspec(dllimport)
#else
#define SMCLIBAPI
#endif

VOID
SMCLIBAPI
NTAPI
SmartcardLogError(
    PVOID Object,
    LONG ErrorCode,
    PUNICODE_STRING Insertion,
    ULONG DumpWord
    );

NTSTATUS
SMCLIBAPI
NTAPI
SmartcardDeviceControl(
    PSMARTCARD_EXTENSION SmartcardExtension,
    PIRP Irp
    );

VOID
SMCLIBAPI
NTAPI
SmartcardInitializeCardCapabilities(
    PSMARTCARD_EXTENSION SmartcardExtension
    );

NTSTATUS
SMCLIBAPI
NTAPI
SmartcardInitialize(
    PSMARTCARD_EXTENSION SmartcardExtension
    );

VOID
SMCLIBAPI
NTAPI
SmartcardCompleteCardTracking(
    PSMARTCARD_EXTENSION SmartcardExtension
    );

VOID
SMCLIBAPI
NTAPI
SmartcardExit(
    PSMARTCARD_EXTENSION SmartcardExtension
    );

NTSTATUS
SMCLIBAPI
NTAPI
SmartcardUpdateCardCapabilities(
    PSMARTCARD_EXTENSION SmartcardExtension
    );

NTSTATUS
SMCLIBAPI
NTAPI
SmartcardRawRequest(
    PSMARTCARD_EXTENSION SmartcardExtension
    );

NTSTATUS
SMCLIBAPI
NTAPI
SmartcardT0Request(
    PSMARTCARD_EXTENSION SmartcardExtension
    );

NTSTATUS
SMCLIBAPI
NTAPI
SmartcardT1Request(
    PSMARTCARD_EXTENSION SmartcardExtension
    );

NTSTATUS
SMCLIBAPI
NTAPI
SmartcardRawReply(
    PSMARTCARD_EXTENSION SmartcardExtension
    );

NTSTATUS
SMCLIBAPI
NTAPI
SmartcardT0Reply(
    PSMARTCARD_EXTENSION SmartcardExtension
    );

NTSTATUS
SMCLIBAPI
NTAPI
SmartcardT1Reply(
    PSMARTCARD_EXTENSION SmartcardExtension
    );

VOID
SMCLIBAPI
NTAPI
SmartcardInvertData(
    PUCHAR Buffer,
    ULONG Length
    );

NTSTATUS
SMCLIBAPI
NTAPI
SmartcardCreateLink(
    IN OUT PUNICODE_STRING LinkName,
    IN PUNICODE_STRING DeviceName
    );

ULONG
SMCLIBAPI
NTAPI
SmartcardGetDebugLevel(
    void
    );

void
SMCLIBAPI
NTAPI
SmartcardSetDebugLevel(
    ULONG Level
    );

NTSTATUS
NTAPI
SmartcardAcquireRemoveLock(
    IN PSMARTCARD_EXTENSION SmartcardExtension
    );

NTSTATUS
NTAPI
SmartcardAcquireRemoveLockWithTag(
    IN PSMARTCARD_EXTENSION SmartcardExtension,
    IN ULONG Tag
    );

VOID
NTAPI
SmartcardReleaseRemoveLock(
    IN PSMARTCARD_EXTENSION SmartcardExtension
    );

VOID
NTAPI
SmartcardReleaseRemoveLockWithTag(
    IN PSMARTCARD_EXTENSION SmartcardExtension,
    IN ULONG Tag
    );

VOID
NTAPI
SmartcardReleaseRemoveLockAndWait(
    IN PSMARTCARD_EXTENSION SmartcardExtension
    );

#ifdef __cplusplus
}
#endif

#endif
