/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS kernel
 * FILE:            include/reactos/rossym.h
 * PURPOSE:         Handling of rossym symbol info
 *
 * PROGRAMMERS:     Ge van Geldorp (gvg@reactos.com)
 */

#ifndef REACTOS_ROSSYM_H_INCLUDED
#define REACTOS_ROSSYM_H_INCLUDED

#define ROSSYM_SECTION_NAME ".rossym"

#ifdef __cplusplus
extern "C" {
#endif

#include <pshpack4.h>

typedef struct _ROSSYM_HEADER {
  ULONG SymbolsOffset;
  ULONG SymbolsLength;
  ULONG StringsOffset;
  ULONG StringsLength;
} ROSSYM_HEADER, *PROSSYM_HEADER;

/* A compressed section carries this in place of ROSSYM_HEADER. The magic sits
 * where SymbolsOffset would be, and is far larger than any valid offset, so a
 * loader that predates compression rejects the section instead of misreading it. */
#define ROSSYM_COMPRESSED_MAGIC 0x43595352

typedef struct _ROSSYM_COMPRESSED_HEADER {
  ULONG Magic;
  ULONG CompressionFormat;
  ULONG SymbolsLength;
  ULONG StringsLength;
  ULONG CompressedOffset;
  ULONG CompressedLength;
} ROSSYM_COMPRESSED_HEADER, *PROSSYM_COMPRESSED_HEADER;

/* ROSSYM_ENTRY is the packed on-disk ABI. Address is an RVA, so the entry
 * remains 16 bytes for both 32-bit and 64-bit PE images. */
typedef struct _ROSSYM_ENTRY {
  ULONG Address; /* RVA */
  ULONG FunctionOffset;
  ULONG FileOffset;
  ULONG SourceLine;
} ROSSYM_ENTRY, *PROSSYM_ENTRY;

#include <poppack.h>

enum _ROSSYM_REGNAME {
    ROSSYM_X86_EAX = 0,
    ROSSYM_X86_ECX,
    ROSSYM_X86_EDX,
    ROSSYM_X86_EBX,
    ROSSYM_X86_ESP,
    ROSSYM_X86_EBP,
    ROSSYM_X86_ESI,
    ROSSYM_X86_EDI,

	ROSSYM_X64_RAX = 0,
	ROSSYM_X64_RDX,
	ROSSYM_X64_RCX,
	ROSSYM_X64_RBX,
	ROSSYM_X64_RSI,
	ROSSYM_X64_RDI,
	ROSSYM_X64_RBP,
	ROSSYM_X64_RSP,
	Rossym_X64_R8,
	ROSSYM_X64_R9,
	ROSSYM_X64_R10,
	ROSSYM_X64_R11,
	ROSSYM_X64_R12,
	ROSSYM_X64_R13,
	ROSSYM_X64_R14,
	ROSSYM_X64_R15
};

typedef struct _ROSSYM_REGISTERS {
  ULONGLONG Registers[32];
} ROSSYM_REGISTERS, *PROSSYM_REGISTERS;

typedef struct _ROSSYM_PARAMETER {
  ULONGLONG Value;
  char *ValueName;
} ROSSYM_PARAMETER, *PROSSYM_PARAMETER;

typedef enum _ROSSYM_LINEINFO_FLAGS {
  ROSSYM_LINEINFO_HAS_REGISTERS = 1
} ROSSYM_LINEINFO_FLAGS;

typedef enum _ROSSYM_LINEINFO_TYPE {
  ROSSYM_LINEINFO_UNKNOWN,
  ROSSYM_LINEINFO_NARROW_STRING,
  ROSSYM_LINEINFO_WIDE_STRING,
  ROSSYM_LINEINFO_ANSI_STRING,
  ROSSYM_LINEINFO_UNICODE_STRING,
  ROSSYM_LINEINFO_HANDLE
} ROSSYM_LINEINFO_STRINGTYPE;

typedef struct _ROSSYM_LINEINFO {
  ROSSYM_LINEINFO_FLAGS Flags;
  ULONG LineNumber;
  char *FileName;
  char *FunctionName;
  ROSSYM_REGISTERS Registers;
  ULONG NumParams;
  ROSSYM_PARAMETER Parameters[16];
} ROSSYM_LINEINFO, *PROSSYM_LINEINFO;

typedef struct _ROSSYM_AGGREGATE_MEMBER {
    PCHAR Name, Type;
    ULONG BaseOffset, Size;
    ULONG FirstBit, Bits;
    ULONG TypeId;
} ROSSYM_AGGREGATE_MEMBER, *PROSSYM_AGGREGATE_MEMBER;

typedef struct _ROSSYM_AGGREGATE {
    ULONG NumElements;
    PROSSYM_AGGREGATE_MEMBER Elements;
} ROSSYM_AGGREGATE, *PROSSYM_AGGREGATE;

typedef struct _ROSSYM_CALLBACKS {
  PVOID (*AllocMemProc)(ULONG_PTR Size);
  VOID (*FreeMemProc)(PVOID Area);
  BOOLEAN (*ReadFileProc)(PVOID FileContext, PVOID Buffer, ULONG Size);
  BOOLEAN (*SeekFileProc)(PVOID FileContext, ULONG_PTR Position);
  BOOLEAN (*MemGetProc)(PVOID FileContext, ULONG_PTR *Target, PVOID SourceMem, ULONG Size);
} ROSSYM_CALLBACKS, *PROSSYM_CALLBACKS;

#ifdef __ROS_DWARF__
typedef struct _ROSSYM_OWN_FILECONTEXT {
  BOOLEAN (*ReadFileProc)(PVOID FileContext, PVOID Buffer, ULONG Size);
  BOOLEAN (*SeekFileProc)(PVOID FileContext, ULONG_PTR Position);
} ROSSYM_OWN_FILECONTEXT, *PROSSYM_OWN_FILECONTEXT;

struct Dwarf;
typedef struct Dwarf *PROSSYM_INFO;
#else
typedef struct _ROSSYM_INFO {
  PROSSYM_ENTRY Symbols;
  ULONG SymbolsCount;
  PCHAR Strings;
  ULONG StringsLength;
} ROSSYM_INFO, *PROSSYM_INFO;
#endif

VOID RosSymInit(PROSSYM_CALLBACKS Callbacks);
#ifndef __ROS_DWARF__
VOID RosSymInitKernelMode(VOID);
#endif
VOID RosSymInitUserMode(VOID);

BOOLEAN RosSymCreateFromRaw(PVOID RawData, ULONG_PTR DataSize,
                            PROSSYM_INFO *RosSymInfo);
BOOLEAN RosSymCreateFromMem(PVOID ImageStart, ULONG_PTR ImageSize,
                            PROSSYM_INFO *RosSymInfo);
BOOLEAN RosSymCreateFromFile(PVOID FileContext, PROSSYM_INFO *RosSymInfo);
ULONG RosSymGetRawDataLength(PROSSYM_INFO RosSymInfo);
VOID RosSymGetRawData(PROSSYM_INFO RosSymInfo, PVOID RawData);
#ifdef __ROS_DWARF__
BOOLEAN RosSymGetAddressInformation(PROSSYM_INFO RosSymInfo,
                                    ULONG_PTR RelativeAddress,
                                    PROSSYM_LINEINFO RosSymLineInfo);
#else
BOOLEAN RosSymGetAddressInformation(PROSSYM_INFO RosSymInfo,
                                    ULONG_PTR RelativeAddress,
                                    ULONG *LineNumber,
                                    char *FileName,
                                    char *FunctionName);

/*
 * Bounded variant for debugger and crash-path consumers.  The original API
 * predates sized string buffers and is retained for source compatibility.
 * SymbolAddress receives the RVA of the selected line record when requested.
 */
BOOLEAN RosSymGetAddressInformationEx(PROSSYM_INFO RosSymInfo, ULONG_PTR RelativeAddress, ULONG *SymbolAddress, ULONG *LineNumber, char *FileName, ULONG FileNameLength, char *FunctionName, ULONG FunctionNameLength);
#endif
VOID RosSymFreeInfo(PROSSYM_LINEINFO RosSymLineInfo);
VOID RosSymDelete(PROSSYM_INFO RosSymInfo);
BOOLEAN
RosSymAggregate(PROSSYM_INFO RosSymInfo, PCHAR Type, PROSSYM_AGGREGATE Aggregate);
VOID RosSymFreeAggregate(PROSSYM_AGGREGATE Aggregate);

#ifdef __cplusplus
}
#endif

#endif /* REACTOS_ROSSYM_H_INCLUDED */

/* EOF */
