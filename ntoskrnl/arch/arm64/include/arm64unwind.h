/*
 * PROJECT:     ReactOS Kernel ARM64
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     ARM64 PE/COFF exception unwinding (Microsoft ABI)
 *
 * References:
 *   https://learn.microsoft.com/en-us/cpp/build/arm64-exception-handling
 */

#pragma once

/*
 * ARM64 .pdata / RUNTIME_FUNCTION layout
 *
 * Each entry is 8 bytes:
 *   DWORD BeginAddress;
 *   DWORD UnwindData;   // packed flags + data, or RVA to .xdata
 *
 * The 2-bit Flag in UnwindData:
 *   00 = remaining bits are RVA to .xdata record
 *   01 = packed unwind data, single prolog+epilog at function start/end
 *   10 = packed unwind data, no prolog/epilog (separated function fragments)
 *   11 = reserved
 */
#define ARM64_UNWIND_FLAG_MASK                 0x3UL

/* Packed format bitfields (when Flag != 0) */
#define ARM64_PACKED_FUNCTION_LENGTH_SHIFT     2
#define ARM64_PACKED_FUNCTION_LENGTH_MASK      0x7FFUL
#define ARM64_PACKED_REGF_SHIFT                13
#define ARM64_PACKED_REGF_MASK                 0x7UL
#define ARM64_PACKED_REGI_SHIFT                16
#define ARM64_PACKED_REGI_MASK                 0xFUL
#define ARM64_PACKED_HOME_PARAMS               (1UL << 20)
#define ARM64_PACKED_CR_SHIFT                  21
#define ARM64_PACKED_CR_MASK                   0x3UL
#define ARM64_PACKED_FRAMESZ_SHIFT             23
#define ARM64_PACKED_FRAMESZ_MASK              0x1FFUL

/* CR values */
#define ARM64_CR_UNCHAINED                     0
#define ARM64_CR_UNCHAINED_SAVED_LR            1
#define ARM64_CR_CHAINED_PAC                   2
#define ARM64_CR_CHAINED                       3

/*
 * ARM64 .xdata record header (first word)
 *
 * Bits:
 *   0-17   FunctionLength (total bytes / 4)
 *   18-19  Vers (must be 0)
 *   20     X  (exception data present)
 *   21     E  (single epilog packed into header)
 *   22-26  Epilog Count (if E=0) or Epilog Start Index (if E=1)
 *   27-31  Code Words
 *
 * If both Epilog Count and Code Words are 0, a second header word follows:
 *   0-15   Extended Epilog Count
 *   16-23  Extended Code Words
 *   24-31  reserved
 */
#define ARM64_XDATA_FUNCTION_LENGTH_MASK       0x3FFFFUL
#define ARM64_XDATA_VERS_SHIFT                 18
#define ARM64_XDATA_EXCEPTION_DATA             (1UL << 20)
#define ARM64_XDATA_EPILOGUE_PACKED            (1UL << 21)
#define ARM64_XDATA_EPILOGUE_COUNT_SHIFT       22
#define ARM64_XDATA_EPILOGUE_COUNT_MASK        0x1FUL
#define ARM64_XDATA_CODE_WORDS_SHIFT           27
#define ARM64_XDATA_CODE_WORDS_MASK            0x1FUL

/*
 * ARM64 Unwind Opcode identifiers.
 * These are NOT the same as the code bytes - they are internal
 * identifiers used during unwind code processing. Each code byte's
 * first byte maps to one of these identifiers.
 */
#define ARM64_UWOP_ALLOC_S                     0
#define ARM64_UWOP_SAVE_R19R20_X               1
#define ARM64_UWOP_SAVE_FPLR                   2
#define ARM64_UWOP_SAVE_FPLR_X                 3
#define ARM64_UWOP_ALLOC_M                     4
#define ARM64_UWOP_SAVE_REGP                   5
#define ARM64_UWOP_SAVE_REGP_X                 6
#define ARM64_UWOP_SAVE_REG                    7
#define ARM64_UWOP_SAVE_REG_X                  8
#define ARM64_UWOP_SAVE_LRPAIR                 9
#define ARM64_UWOP_SAVE_FREGP                  10
#define ARM64_UWOP_SAVE_FREGP_X                11
#define ARM64_UWOP_SAVE_FREG                   12
#define ARM64_UWOP_SAVE_FREG_X                 13
#define ARM64_UWOP_ALLOC_Z                     14
#define ARM64_UWOP_ALLOC_L                     15
#define ARM64_UWOP_SET_FP                      16
#define ARM64_UWOP_ADD_FP                      17
#define ARM64_UWOP_NOP                         18
#define ARM64_UWOP_END                         19
#define ARM64_UWOP_END_C                       20
#define ARM64_UWOP_SAVE_NEXT                   21
#define ARM64_UWOP_SAVE_ANY_XREG               22
#define ARM64_UWOP_SAVE_ANY_DREG               23
#define ARM64_UWOP_SAVE_ANY_QREG               24
#define ARM64_UWOP_SAVE_ZREG                   25
#define ARM64_UWOP_SAVE_PREG                   26
#define ARM64_UWOP_PAC_SIGN_LR                 27
#define ARM64_UWOP_CUSTOM_TRAP_FRAME           28
#define ARM64_UWOP_CUSTOM_MACHINE_FRAME        29
#define ARM64_UWOP_CUSTOM_CONTEXT              30
#define ARM64_UWOP_CUSTOM_EC_CONTEXT           31
#define ARM64_UWOP_CUSTOM_CLEAR_UNWOUND        32

#define ARM64_UWOP_RESERVED                    0xFF

/*
 * ARM64 Unwind Code structure.
 *
 * Unlike AMD64 (fixed 2-byte UNWIND_CODE), ARM64 codes are variable-length
 * (1-4 bytes). We store them as a sequence of bytes and decode on the fly.
 *
 * Each code's first byte determines both the opcode and the total code size.
 * This table maps first-byte patterns to {opcode_id, total_bytes}.
 *
 * First-byte layout:
 *   Bits 7-5 | Bits 4-0              | Meaning
 *   ---------|-----------------------|---------
 *   000      | xxxxx (size*16 < 512) | alloc_s
 *   001      | zzzzz (offset*8)      | save_r19r20_x
 *   01       | zzzzzz (offset*8)     | save_fplr
 *   10       | zzzzzz (offset*8)     | save_fplr_x
 *   11000    | xxx xxxxxxxx          | alloc_m
 *   11001    | 0xx xxzzzzzz          | save_regp
 *   11001    | 1xx xxzzzzzz          | save_regp_x
 *   11010    | 0xx xxzzzzzz          | save_reg
 *   11010    | 10x xxxzzzzz          | save_reg_x
 *   11010    | 11x xxzzzzzz          | save_lrpair
 *   1101100  | x xxzzzzzz            | save_fregp
 *   1101101  | x xxzzzzzz            | save_fregp_x
 *   1101110  | x xxzzzzzz            | save_freg
 *   11011110 | xxxzzzzz              | save_freg_x
 *   11011111 | zzzzzzzz              | alloc_z
 *   11100000 | (next 3 bytes)        | alloc_l
 *   11100001 |                       | set_fp
 *   11100010 | xxxxxxxx              | add_fp
 *   11100011 |                       | nop
 *   11100100 |                       | end
 *   11100101 |                       | end_c
 *   11100110 |                       | save_next
 *   11100111 | (2 more bytes)        | save_any_*
 *   11101000 |                       | custom trap frame
 *   11101001 |                       | custom machine frame
 *   11101010 |                       | custom context
 *   11101011 |                       | custom EC context
 *   11101100 |                       | custom clear unwound-to-call
 *   11111100 |                       | pac_sign_lr
 */

/*
 * ARM64 unwind code decode result.
 */
typedef struct _ARM64_UNWIND_CODE {
    UCHAR OpcodeId;     /* ARM64_UWOP_* identifier */
    UCHAR TotalBytes;   /* total size of this code in bytes (1-4) */
    UCHAR Byte0;        /* first byte of code */
    UCHAR Byte1;        /* second byte (valid for TotalBytes >= 2) */
    UCHAR Byte2;        /* third byte (valid for TotalBytes >= 3) */
    UCHAR Byte3;        /* fourth byte (valid for TotalBytes == 4) */
} ARM64_UNWIND_CODE, *PARM64_UNWIND_CODE;

/*
 * ARM64 .xdata record header parse result.
 */
typedef struct _ARM64_XDATA_INFO {
    ULONG FunctionLength;       /* total function bytes */
    ULONG CodeWords;            /* number of 32-bit words of unwind codes */
    ULONG EpilogCount;          /* number of epilog scopes */
    ULONG EpilogStartIndex;     /* if E=1, index of single epilog's first code */
    BOOLEAN HasExceptionData;   /* X bit */
    BOOLEAN EpilogPacked;       /* E bit */
    ULONG HeaderWords;          /* 1 or 2 */
    PULONG Xdata;               /* pointer to start of xdata (header word 0) */
    PULONG EpilogScopes;        /* pointer to epilog scope array */
    PUCHAR UnwindCodes;         /* pointer to unwind code bytes */
} ARM64_XDATA_INFO, *PARM64_XDATA_INFO;

/*
 * Packed format decode result.
 */
typedef struct _ARM64_PACKED_INFO {
    ULONG FunctionLength;       /* total function bytes */
    ULONG FrameSize;            /* total stack allocation */
    ULONG RegI;                 /* number of saved INT regs */
    ULONG RegF;                 /* number of saved FP regs */
    BOOLEAN HomesParams;        /* H bit */
    ULONG CR;                   /* frame chain type */
} ARM64_PACKED_INFO, *PARM64_PACKED_INFO;

/*
 * Declarations for the ARM64 RTL unwind interface.
 * Actual types (CONTEXT, PRUNTIME_FUNCTION, PEXCEPTION_ROUTINE,
 * DISPATCHER_CONTEXT, etc.) are provided by the including context. */
