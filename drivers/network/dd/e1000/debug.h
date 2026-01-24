/*
 * COPYRIGHT:   See COPYING in the top level directory
 * PROJECT:     ReactOS Intel PRO/1000 Driver
 * FILE:        include/debug.h
 * PURPOSE:     Debugging support macros and traffic tracking
 * DEFINES:     DBG     - Enable debug output
 *              NASSERT - Disable assertions
 */

#pragma once
#include <reactos/debug.h>

/* ============================================================================
 * Debug Trace Levels (standard levels)
 * ============================================================================ */

/* ============================================================================
 * Rate Limiting for High-Frequency Events
 * ============================================================================ */

/* Rate limit interval in ticks (approximately 1 second at typical tick rate) */
#define DEBUG_RATE_LIMIT_INTERVAL   10000

/* Maximum messages per interval for rate-limited logging */
#define DEBUG_RATE_LIMIT_MAX        10

#ifdef ASSERT_IRQL
#undef ASSERT_IRQL
#endif
#ifdef ASSERT_IRQL_EQUAL
#undef ASSERT_IRQL_EQUAL
#endif

/* ============================================================================
 * Debug Statistics Structure
 * ============================================================================ */

typedef struct _E1000_DEBUG_STATS {
    /* Transmit statistics */
    volatile LONG64 TxAttempts;         /* Total TX attempts */
    volatile LONG64 TxSuccess;          /* Successful TX completions */
    volatile LONG64 TxFailed;           /* Failed TX attempts */
    volatile LONG64 TxBytes;            /* Total bytes transmitted */
    volatile LONG64 TxDropped;          /* Packets dropped (no resources) */
    volatile ULONG  TxRingFull;         /* Times TX ring was full */
    volatile ULONG  TxDescriptorsUsed;  /* Current descriptors in use */
    volatile ULONG  TxMaxDescriptorsUsed; /* High water mark */
    volatile ULONG  TxBatchCount;       /* Number of batch send calls */
    volatile ULONG  TxSingleCount;      /* Number of single send calls */

    /* Receive statistics */
    volatile LONG64 RxAttempts;         /* Total RX descriptor checks */
    volatile LONG64 RxSuccess;          /* Successful packet receives */
    volatile LONG64 RxFailed;           /* Failed receives (errors) */
    volatile LONG64 RxBytes;            /* Total bytes received */
    volatile LONG64 RxDropped;          /* Packets dropped */
    volatile ULONG  RxNoBuffer;         /* Receive buffer exhaustion */
    volatile ULONG  RxChecksumGood;     /* Packets with valid checksum */
    volatile ULONG  RxChecksumBad;      /* Packets with invalid checksum */
    volatile ULONG  RxChecksumNone;     /* Packets without checksum */
    volatile ULONG  RxCrcErrors;        /* CRC errors */
    volatile ULONG  RxAlignErrors;      /* Alignment errors */
    volatile ULONG  RxMultiDesc;        /* Multi-descriptor packets (unsupported) */

    /* Interrupt statistics */
    volatile LONG64 Interrupts;         /* Total interrupts */
    volatile ULONG  SpuriousInterrupts; /* Spurious interrupts (not ours) */
    volatile ULONG  TxInterrupts;       /* TX completion interrupts */
    volatile ULONG  RxInterrupts;       /* RX interrupts */
    volatile ULONG  LinkInterrupts;     /* Link status change interrupts */
    volatile ULONG  OtherInterrupts;    /* Other interrupt causes */
    volatile ULONG  UnhandledInterrupts;/* Unhandled interrupt bits */

    /* Initialization statistics */
    volatile ULONG  InitAttempts;       /* Initialization attempts */
    volatile ULONG  InitSuccess;        /* Successful initializations */
    volatile ULONG  InitFailed;         /* Failed initializations */
    volatile ULONG  ResetCount;         /* Hardware resets */

    /* Power management statistics */
    volatile ULONG  PowerTransitions;   /* Power state transitions */
    volatile ULONG  WakeEvents;         /* Wake-on-LAN events */

    /* Timestamp tracking */
    LARGE_INTEGER   LastTxTime;         /* Time of last TX */
    LARGE_INTEGER   LastRxTime;         /* Time of last RX */
    LARGE_INTEGER   LastInterruptTime;  /* Time of last interrupt */
    LARGE_INTEGER   InitTime;           /* Driver initialization time */

    /* Rate limiting state */
    LARGE_INTEGER   LastRateLimitCheck;
    ULONG           MessagesThisInterval;

} E1000_DEBUG_STATS, *PE1000_DEBUG_STATS;


/* ============================================================================
 * Debug Configuration
 * ============================================================================ */

#if DBG

/* Global debug statistics - available even in release builds for diagnostics */
extern E1000_DEBUG_STATS DebugStats;


/* ============================================================================
 * Category-Specific Debug Macros
 * ============================================================================ */

/* TX path debugging */
#define E1000_TX_DBG(_x_) \
    DPRINT _x_

/* RX path debugging */
#define E1000_RX_DBG(_x_) \
    DPRINT _x_

/* Initialization debugging */
#define E1000_INIT_DBG(_x_) \
    DPRINT _x_

/* Interrupt debugging */
#define E1000_INT_DBG(_x_) \
    DPRINT _x_

/* Power management debugging */
#define E1000_POWER_DBG(_x_) \
    DPRINT _x_

/* Checksum debugging */
#define E1000_CSUM_DBG(_x_) \
    DPRINT _x_

/* Link status debugging */
#define E1000_LINK_DBG(_x_) \
    DPRINT _x_

/* OID debugging */
#define E1000_OID_DBG(_x_) \
    DPRINT _x_

/* Descriptor ring debugging */
#define E1000_RING_DBG(_x_) \
    DPRINT _x_

/* Statistics debugging */
#define E1000_STATS_DBG(_x_) \
    DPRINT _x_

/* Hardware register debugging */
#define E1000_HW_DBG(_x_) \
    DPRINT _x_


/* ============================================================================
 * Packet Tracking Macros
 * ============================================================================ */

/* Log TX packet with sequence tracking */
#define E1000_LOG_TX_PACKET(_seq_, _len_, _desc_, _pa_) \
    E1000_TX_DBG(("TX[%llu]: len=%u desc=%u pa=0x%I64x\n", \
                  (ULONG64)(_seq_), (ULONG)(_len_), (ULONG)(_desc_), (ULONG64)(_pa_)))

/* Log TX completion */
#define E1000_LOG_TX_COMPLETE(_seq_, _status_) \
    E1000_TX_DBG(("TX[%llu]: complete status=0x%x\n", (ULONG64)(_seq_), (ULONG)(_status_)))

/* Log RX packet */
#define E1000_LOG_RX_PACKET(_seq_, _len_, _desc_, _status_, _errors_) \
    E1000_RX_DBG(("RX[%llu]: len=%u desc=%u status=0x%02x errors=0x%02x\n", \
                  (ULONG64)(_seq_), (ULONG)(_len_), (ULONG)(_desc_), \
                  (ULONG)(_status_), (ULONG)(_errors_)))


/* ============================================================================
 * Descriptor Ring State Macros
 * ============================================================================ */

/* Log TX ring state */
#define E1000_LOG_TX_RING(_adapter_) \
    E1000_RING_DBG(("TX Ring: head=%u tail=%u current=%u last=%u free=%u full=%d\n", \
                    0, 0, (_adapter_)->CurrentTxDesc, (_adapter_)->LastTxDesc, \
                    NICGetFreeTxDescriptors(_adapter_), (_adapter_)->TxFull))

/* Log RX ring state */
#define E1000_LOG_RX_RING(_head_, _tail_) \
    E1000_RING_DBG(("RX Ring: head=%u tail=%u\n", (ULONG)(_head_), (ULONG)(_tail_)))


/* ============================================================================
 * Interrupt Logging Macros
 * ============================================================================ */

/* Log interrupt cause */
#define E1000_LOG_INTERRUPT(_icr_) \
    E1000_INT_DBG(("INT: ICR=0x%08x [%s%s%s%s%s%s%s%s]\n", (ULONG)(_icr_), \
                   ((_icr_) & 0x01) ? "TXDW " : "", \
                   ((_icr_) & 0x02) ? "TXQE " : "", \
                   ((_icr_) & 0x04) ? "LSC " : "", \
                   ((_icr_) & 0x10) ? "RXDMT0 " : "", \
                   ((_icr_) & 0x40) ? "RXO " : "", \
                   ((_icr_) & 0x80) ? "RXT0 " : "", \
                   ((_icr_) & 0x1000) ? "PHYINT " : "", \
                   ((_icr_) & 0x8000) ? "TXD_LOW " : ""))


/* ============================================================================
 * Assertion Macros
 * ============================================================================ */

#define ASSERT_IRQL(x) ASSERT(KeGetCurrentIrql() <= (x))
#define ASSERT_IRQL_EQUAL(x) ASSERT(KeGetCurrentIrql() == (x))

/* TX path assertions */
#define E1000_ASSERT_TX_VALID(_packet_, _length_) \
    do { \
        ASSERT((_packet_) != NULL); \
        ASSERT((_length_) > 0); \
        ASSERT((_length_) <= MAXIMUM_FRAME_SIZE); \
    } while (0)

/* RX path assertions */
#define E1000_ASSERT_RX_VALID(_desc_) \
    do { \
        ASSERT((_desc_) != NULL); \
        ASSERT((_desc_)->Address != 0); \
    } while (0)

/* Descriptor index assertions */
#define E1000_ASSERT_TX_DESC_INDEX(_idx_) \
    ASSERT((_idx_) < NUM_TRANSMIT_DESCRIPTORS)

#define E1000_ASSERT_RX_DESC_INDEX(_idx_) \
    ASSERT((_idx_) < NUM_RECEIVE_DESCRIPTORS)


/* ============================================================================
 * Statistics Update Macros (atomic operations)
 * ============================================================================ */

#define E1000_STAT_INC(_field_) \
    InterlockedIncrement64(&DebugStats._field_)

#define E1000_STAT_ADD(_field_, _value_) \
    InterlockedExchangeAdd64(&DebugStats._field_, (LONG64)(_value_))

#define E1000_STAT_INC32(_field_) \
    InterlockedIncrement((volatile LONG*)&DebugStats._field_)


#else /* !DBG */

/* ============================================================================
 * Release Build - Disable All Debug Macros
 * ============================================================================ */

#define E1000_TX_DBG(_x_)
#define E1000_RX_DBG(_x_)
#define E1000_INIT_DBG(_x_)
#define E1000_INT_DBG(_x_)
#define E1000_POWER_DBG(_x_)
#define E1000_CSUM_DBG(_x_)
#define E1000_LINK_DBG(_x_)
#define E1000_OID_DBG(_x_)
#define E1000_RING_DBG(_x_)
#define E1000_STATS_DBG(_x_)
#define E1000_HW_DBG(_x_)

#define E1000_LOG_TX_PACKET(_seq_, _len_, _desc_, _pa_)
#define E1000_LOG_TX_COMPLETE(_seq_, _status_)
#define E1000_LOG_RX_PACKET(_seq_, _len_, _desc_, _status_, _errors_)
#define E1000_LOG_TX_RING(_adapter_)
#define E1000_LOG_RX_RING(_head_, _tail_)
#define E1000_LOG_INTERRUPT(_icr_)

#define ASSERT_IRQL(x)
#define ASSERT_IRQL_EQUAL(x)
#define E1000_ASSERT_TX_VALID(_packet_, _length_)
#define E1000_ASSERT_RX_VALID(_desc_)
#define E1000_ASSERT_TX_DESC_INDEX(_idx_)
#define E1000_ASSERT_RX_DESC_INDEX(_idx_)

#define E1000_STAT_INC(_field_)
#define E1000_STAT_ADD(_field_, _value_)
#define E1000_STAT_INC32(_field_)

#endif /* DBG */


/* ============================================================================
 * Common Macros (both debug and release builds)
 * ============================================================================ */

#ifdef assert
#undef assert
#endif
#define assert(x) ASSERT(x)
#define assert_irql(x) ASSERT_IRQL(x)

#ifdef UNIMPLEMENTED
#undef UNIMPLEMENTED
#endif
#ifdef UNIMPLEMENTED_DBGBREAK
#undef UNIMPLEMENTED_DBGBREAK
#endif

#define UNIMPLEMENTED \
    DPRINT1("UNIMPLEMENTED.\n");

#define UNIMPLEMENTED_DBGBREAK(...) \
    do { \
        DPRINT1("UNIMPLEMENTED.\n"); \
        DPRINT1("" __VA_ARGS__); \
        DbgBreakPoint(); \
    } while (0)


/* ============================================================================
 * Debug Function Prototypes
 * ============================================================================ */

/* OID to string conversion */
const char* Oid2Str(IN NDIS_OID Oid);

/* Interrupt cause to string conversion */
const char* E1000_IcrToString(IN ULONG Icr, OUT PCHAR Buffer, IN ULONG BufferSize);

/* Dump functions */
VOID E1000_DumpDriverState(IN PVOID AdapterContext);
VOID E1000_DumpStatistics(VOID);
VOID E1000_DumpTxRing(IN PVOID AdapterContext);
VOID E1000_DumpRxRing(IN PVOID AdapterContext);

/* Reset debug statistics */
VOID E1000_ResetDebugStats(VOID);

/* Initialize debug subsystem */
VOID E1000_InitDebug(VOID);
