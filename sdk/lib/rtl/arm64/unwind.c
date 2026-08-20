/*
 * PROJECT:     ReactOS Run-Time Library
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     ARM64 PE/COFF unwinding support
 * COPYRIGHT:   Unwind-code interpreter ported from the Wine project
 *              (dlls/ntdll/unwind.c), LGPL-2.1-or-later.
 */

#include <rtl.h>

#define NDEBUG
#include <debug.h>

#ifndef UNW_FLAG_EHANDLER
#define UNW_FLAG_EHANDLER 0x1
#define UNW_FLAG_UHANDLER 0x2
#endif

#ifndef CONTEXT_UNWOUND_TO_CALL
#define CONTEXT_UNWOUND_TO_CALL 0x20000000
#endif

#define ARM64_UNWIND_FLAG_MASK 0x3UL
#define ARM64_PACKED_FUNCTION_LENGTH_SHIFT 2
#define ARM64_PACKED_FUNCTION_LENGTH_MASK 0x7FFUL
#define ARM64_XDATA_FUNCTION_LENGTH_MASK 0x3FFFFUL

VOID NTAPI RtlRestoreContext(_In_ PCONTEXT ContextRecord, _In_opt_ PEXCEPTION_RECORD ExceptionRecord);
BOOLEAN NTAPI RtlIsEcCode(_In_ ULONG_PTR CodeAddress);

PRUNTIME_FUNCTION
NTAPI
RtlLookupFunctionTable(
    _In_ ULONG_PTR ControlPc,
    _Out_ PULONG_PTR ImageBase,
    _Out_ PULONG Length);

PRUNTIME_FUNCTION
NTAPI
RtlpLookupDynamicFunctionEntry(
    _In_ DWORD64 ControlPc,
    _Out_ PDWORD64 ImageBase,
    _In_opt_ PUNWIND_HISTORY_TABLE HistoryTable);

static VOID RtlpArm64CaptureNonVolatileRegisters(_Out_ DISPATCHER_CONTEXT_NONVOLREG_ARM64 *NonVolatileRegisters, _In_ PCONTEXT Context)
{
    ULONG Index;

    RtlCopyMemory(NonVolatileRegisters->GpNvRegs, &Context->X19, sizeof(NonVolatileRegisters->GpNvRegs));

    for (Index = 0; Index < NONVOL_FP_NUMREG_ARM64; Index++)
    {
        NonVolatileRegisters->FpNvRegs[Index] = Context->V[Index + 8].D[0];
    }
}

typedef struct _ARM64_RT_FUNCTION
{
    DWORD BeginAddress;
    union
    {
        DWORD UnwindData;
        struct
        {
            DWORD Flag : 2;
            DWORD FunctionLength : 11;
            DWORD RegF : 3;
            DWORD RegI : 4;
            DWORD H : 1;
            DWORD CR : 2;
            DWORD FrameSize : 9;
        };
    };
} ARM64_RT_FUNCTION, *PARM64_RT_FUNCTION;

typedef struct _ARM64_XDATA_HEADER
{
    DWORD FunctionLength : 18;
    DWORD Version : 2;
    DWORD ExceptionDataPresent : 1;
    DWORD EpilogInHeader : 1;
    DWORD EpilogCount : 5;
    DWORD CodeWords : 5;
} ARM64_XDATA_HEADER, *PARM64_XDATA_HEADER;

typedef struct _ARM64_XDATA_EXT
{
    WORD epilog;
    BYTE codes;
    BYTE reserved;
} ARM64_XDATA_EXT;

typedef struct _ARM64_XDATA_EPILOG
{
    DWORD offset : 18;
    DWORD res : 4;
    DWORD index : 10;
} ARM64_XDATA_EPILOG;

typedef struct _RTL_ARM64EC_METADATA
{
    ULONG Version;
    ULONG CodeMap;
    ULONG CodeMapCount;
    ULONG CodeRangesToEntryPoints;
    ULONG RedirectionMetadata;
    ULONG DispatchCallNoRedirect;
    ULONG DispatchRet;
    ULONG DispatchCall;
    ULONG DispatchIcall;
    ULONG DispatchIcallCfg;
    ULONG AlternateEntryPoint;
    ULONG AuxiliaryIat;
    ULONG CodeRangesToEntryPointsCount;
    ULONG RedirectionMetadataCount;
    ULONG GetX64InformationFunctionPointer;
    ULONG SetX64InformationFunctionPointer;
    ULONG ExtraRfeTable;
    ULONG ExtraRfeTableSize;
} RTL_ARM64EC_METADATA, *PRTL_ARM64EC_METADATA;

static PRUNTIME_FUNCTION
RtlpArm64GetImageFunctionTable(
    _In_ ULONG_PTR ControlPc,
    _In_ PVOID ImageBase,
    _In_ PIMAGE_NT_HEADERS NtHeaders,
    _Out_ PULONG TableSize)
{
    PIMAGE_LOAD_CONFIG_DIRECTORY LoadConfig;
    PRTL_ARM64EC_METADATA Metadata;
    ULONG LoadConfigSize;
    ULONG_PTR ImageStart, ImageEnd, Candidate;
    ULONG SizeOfImage;

    *TableSize = 0;

    if (!RtlIsEcCode(ControlPc))
        return RtlImageDirectoryEntryToData(ImageBase, TRUE, IMAGE_DIRECTORY_ENTRY_EXCEPTION, TableSize);

    LoadConfig = RtlImageDirectoryEntryToData(ImageBase, TRUE, IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG, &LoadConfigSize);
    if (!LoadConfig ||
        LoadConfigSize < RTL_SIZEOF_THROUGH_FIELD(IMAGE_LOAD_CONFIG_DIRECTORY, CHPEMetadataPointer))
    {
        return NULL;
    }

    SizeOfImage = NtHeaders->OptionalHeader.SizeOfImage;
    ImageStart = (ULONG_PTR)ImageBase;
    if (SizeOfImage < sizeof(*Metadata))
        return NULL;

    ImageEnd = ImageStart + SizeOfImage;
    Candidate = (ULONG_PTR)LoadConfig->CHPEMetadataPointer;
    if (ImageEnd < ImageStart ||
        Candidate < ImageStart ||
        Candidate > ImageEnd - sizeof(*Metadata))
    {
        return NULL;
    }

    Metadata = (PRTL_ARM64EC_METADATA)Candidate;
    if (Metadata->Version != 1 ||
        Metadata->ExtraRfeTable == 0 ||
        Metadata->ExtraRfeTableSize < sizeof(RUNTIME_FUNCTION) ||
        Metadata->ExtraRfeTableSize % sizeof(RUNTIME_FUNCTION) != 0 ||
        Metadata->ExtraRfeTable >= SizeOfImage ||
        Metadata->ExtraRfeTableSize > SizeOfImage - Metadata->ExtraRfeTable)
    {
        return NULL;
    }

    *TableSize = Metadata->ExtraRfeTableSize;
    return (PRUNTIME_FUNCTION)(ImageStart + Metadata->ExtraRfeTable);
}

ULONG
RtlpArm64FunctionLength(
    _In_ ULONG_PTR ImageBase,
    _In_ PRUNTIME_FUNCTION FunctionEntry)
{
    ULONG UnwindData;
    PULONG Xdata;

    UnwindData = FunctionEntry->UnwindData;
    if ((UnwindData & ARM64_UNWIND_FLAG_MASK) != 0)
    {
        return ((UnwindData >> ARM64_PACKED_FUNCTION_LENGTH_SHIFT) &
                ARM64_PACKED_FUNCTION_LENGTH_MASK) * sizeof(ULONG);
    }

    Xdata = (PULONG)(ImageBase + UnwindData);
    return (Xdata[0] & ARM64_XDATA_FUNCTION_LENGTH_MASK) * sizeof(ULONG);
}

PRUNTIME_FUNCTION
NTAPI
RtlLookupFunctionEntry(
    _In_ DWORD64 ControlPc,
    _Out_ PDWORD64 ImageBase,
    _Inout_opt_ PUNWIND_HISTORY_TABLE HistoryTable)
{
    PRUNTIME_FUNCTION FunctionTable, FunctionEntry;
    ULONG TableSize, TableLength;
    ULONG_PTR ControlRva;
    ULONG IndexLow, IndexHigh, IndexMid;

    FunctionTable = RtlLookupFunctionTable(ControlPc, (PULONG_PTR)ImageBase, &TableSize);
    if (FunctionTable == NULL)
        goto LookupDynamic;

    TableLength = TableSize / sizeof(*FunctionTable);
    ControlRva = (ULONG_PTR)(ControlPc - *ImageBase);

    IndexLow = 0;
    IndexHigh = TableLength;
    while (IndexHigh > IndexLow)
    {
        IndexMid = (IndexLow + IndexHigh) / 2;
        FunctionEntry = &FunctionTable[IndexMid];

        if (ControlRva < FunctionEntry->BeginAddress)
        {
            IndexHigh = IndexMid;
            continue;
        }

        if (ControlRva >= FunctionEntry->BeginAddress +
                          RtlpArm64FunctionLength((ULONG_PTR)*ImageBase, FunctionEntry))
        {
            IndexLow = IndexMid + 1;
            continue;
        }

        return FunctionEntry;
    }

LookupDynamic:
    *ImageBase = 0;
    return RtlpLookupDynamicFunctionEntry(ControlPc, ImageBase, HistoryTable);
}

static const BYTE Arm64UnwindCodeLen[256] =
{
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
    4,1,2,1,1,1,1,3,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1
};

static unsigned int
RtlpArm64SequenceLen(
    _In_ PBYTE Ptr,
    _In_ PBYTE End)
{
    unsigned int Ret = 0;

    while (Ptr < End)
    {
        if (*Ptr == 0xe4 || *Ptr == 0xe5)
            break;
        if ((*Ptr & 0xf8) != 0xe8)
            Ret++;
        Ptr += Arm64UnwindCodeLen[*Ptr];
    }
    return Ret;
}

static VOID
RtlpArm64RestoreRegs(
    _In_ int Reg,
    _In_ int Count,
    _In_ int Pos,
    _Inout_ PCONTEXT Context)
{
    int i, Offset = (Pos > 0) ? Pos : 0;

    for (i = 0; i < Count; i++)
        Context->X[Reg + i] = ((PULONG64)(ULONG_PTR)Context->Sp)[i + Offset];

    if (Pos < 0)
        Context->Sp += -8 * Pos;
}

static VOID
RtlpArm64RestoreFpRegs(
    _In_ int Reg,
    _In_ int Count,
    _In_ int Pos,
    _Inout_ PCONTEXT Context)
{
    int i, Offset = (Pos > 0) ? Pos : 0;

    for (i = 0; i < Count; i++)
        Context->V[Reg + i].Low = ((PULONG64)(ULONG_PTR)Context->Sp)[i + Offset];

    if (Pos < 0)
        Context->Sp += -8 * Pos;
}

static VOID
RtlpArm64RestoreQRegs(
    _In_ int Reg,
    _In_ int Count,
    _In_ int Pos,
    _Inout_ PCONTEXT Context)
{
    int i, Offset = (Pos > 0) ? Pos : 0;

    for (i = 0; i < Count; i++)
    {
        Context->V[Reg + i].Low = ((PULONG64)(ULONG_PTR)Context->Sp)[2 * (i + Offset)];
        Context->V[Reg + i].High = ((PULONG64)(ULONG_PTR)Context->Sp)[2 * (i + Offset) + 1];
    }

    if (Pos < 0)
        Context->Sp += -16 * Pos;
}

static VOID
RtlpArm64RestoreAnyReg(
    _In_ int Reg,
    _In_ int Count,
    _In_ int Type,
    _In_ int Pos,
    _Inout_ PCONTEXT Context)
{
    if (Reg & 0x20)
        Pos = -Pos - 1;

    switch (Type)
    {
    case 0:
        if (Count > 1 || Pos < 0)
            Pos *= 2;
        RtlpArm64RestoreRegs(Reg & 0x1f, Count, Pos, Context);
        break;
    case 1:
        if (Count > 1 || Pos < 0)
            Pos *= 2;
        RtlpArm64RestoreFpRegs(Reg & 0x1f, Count, Pos, Context);
        break;
    case 2:
        RtlpArm64RestoreQRegs(Reg & 0x1f, Count, Pos, Context);
        break;
    }
}

static VOID
RtlpArm64DoPacAuth(
    _Inout_ PCONTEXT Context)
{
    register DWORD64 x17 __asm__("x17") = Context->Lr;
    register DWORD64 x16 __asm__("x16") = Context->Sp;
    __asm__ volatile("hint 0xe" : "+r"(x17) : "r"(x16));
    Context->Lr = x17;
}

static VOID
RtlpArm64ProcessUnwindCodes(
    _In_ PBYTE Ptr,
    _In_ PBYTE End,
    _Inout_ PCONTEXT Context,
    _In_ int Skip,
    _Inout_ PBOOLEAN FinalPcFromLr)
{
    unsigned int i, val, len, save_next = 2;

    while (Ptr < End && Skip)
    {
        if (*Ptr == 0xe4)
            break;
        Ptr += Arm64UnwindCodeLen[*Ptr];
        Skip--;
    }

    while (Ptr < End)
    {
        if ((len = Arm64UnwindCodeLen[*Ptr]) > 1)
        {
            if (Ptr + len > End)
                break;
            val = Ptr[0] * 0x100 + Ptr[1];
        }
        else
            val = *Ptr;

        if (*Ptr < 0x20)
            Context->Sp += 16 * (val & 0x1f);
        else if (*Ptr < 0x40)
            RtlpArm64RestoreRegs(19, save_next, -(int)(val & 0x1f), Context);
        else if (*Ptr < 0x80)
            RtlpArm64RestoreRegs(29, 2, val & 0x3f, Context);
        else if (*Ptr < 0xc0)
            RtlpArm64RestoreRegs(29, 2, -(int)(val & 0x3f) - 1, Context);
        else if (*Ptr < 0xc8)
            Context->Sp += 16 * (val & 0x7ff);
        else if (*Ptr < 0xcc)
            RtlpArm64RestoreRegs(19 + ((val >> 6) & 0xf), save_next, val & 0x3f, Context);
        else if (*Ptr < 0xd0)
            RtlpArm64RestoreRegs(19 + ((val >> 6) & 0xf), save_next, -(int)(val & 0x3f) - 1, Context);
        else if (*Ptr < 0xd4)
            RtlpArm64RestoreRegs(19 + ((val >> 6) & 0xf), 1, val & 0x3f, Context);
        else if (*Ptr < 0xd6)
            RtlpArm64RestoreRegs(19 + ((val >> 5) & 0xf), 1, -(int)(val & 0x1f) - 1, Context);
        else if (*Ptr < 0xd8)
        {
            RtlpArm64RestoreRegs(19 + 2 * ((val >> 6) & 0x7), 1, val & 0x3f, Context);
            RtlpArm64RestoreRegs(30, 1, (val & 0x3f) + 1, Context);
        }
        else if (*Ptr < 0xda)
            RtlpArm64RestoreFpRegs(8 + ((val >> 6) & 0x7), save_next, val & 0x3f, Context);
        else if (*Ptr < 0xdc)
            RtlpArm64RestoreFpRegs(8 + ((val >> 6) & 0x7), save_next, -(int)(val & 0x3f) - 1, Context);
        else if (*Ptr < 0xde)
            RtlpArm64RestoreFpRegs(8 + ((val >> 6) & 0x7), 1, val & 0x3f, Context);
        else if (*Ptr == 0xde)
            RtlpArm64RestoreFpRegs(8 + ((val >> 5) & 0x7), 1, -(int)(val & 0x3f) - 1, Context);
        else if (*Ptr == 0xe0)
            Context->Sp += 16 * ((Ptr[1] << 16) + (Ptr[2] << 8) + Ptr[3]);
        else if (*Ptr == 0xe1)
            Context->Sp = Context->Fp;
        else if (*Ptr == 0xe2)
            Context->Sp = Context->Fp - 8 * (val & 0xff);
        else if (*Ptr == 0xe3)
            ;
        else if (*Ptr == 0xe4)
            break;
        else if (*Ptr == 0xe5)
            ;
        else if (*Ptr == 0xe6)
        {
            save_next += 2;
            Ptr += len;
            continue;
        }
        else if (*Ptr == 0xe7)
            RtlpArm64RestoreAnyReg(Ptr[1], (Ptr[1] & 0x40) ? save_next : 1,
                                   Ptr[2] >> 6, Ptr[2] & 0x3f, Context);
        else if (*Ptr == 0xe9)
        {
            Context->Pc = ((PULONG64)(ULONG_PTR)Context->Sp)[1];
            Context->Sp = ((PULONG64)(ULONG_PTR)Context->Sp)[0];
            Context->ContextFlags &= ~CONTEXT_UNWOUND_TO_CALL;
            *FinalPcFromLr = FALSE;
        }
        else if (*Ptr == 0xea)
        {
            ULONG Flags = Context->ContextFlags & ~CONTEXT_UNWOUND_TO_CALL;
            PCONTEXT SrcContext = (PCONTEXT)(ULONG_PTR)Context->Sp;

            *Context = *SrcContext;
            Context->ContextFlags = Flags | (SrcContext->ContextFlags & CONTEXT_UNWOUND_TO_CALL);
            *FinalPcFromLr = FALSE;
        }
        else if (*Ptr == 0xec)
        {
            Context->Pc = Context->Lr;
            Context->ContextFlags &= ~CONTEXT_UNWOUND_TO_CALL;
            *FinalPcFromLr = FALSE;
        }
        else if (*Ptr == 0xfc)
            RtlpArm64DoPacAuth(Context);
        else
            return;

        (VOID)i;
        save_next = 2;
        Ptr += len;
    }
}

static PVOID
RtlpArm64UnwindPacked(
    _In_ ULONG_PTR Base,
    _In_ ULONG_PTR Pc,
    _In_ PARM64_RT_FUNCTION Func,
    _Inout_ PCONTEXT Context)
{
    int i;
    unsigned int len, offset, skip = 0;
    unsigned int int_size = Func->RegI * 8, fp_size = Func->RegF * 8, h_size = Func->H * 4, regsave, local_size;
    unsigned int int_regs, fp_regs, saved_regs, local_size_regs;

    if (Func->CR == 1)
        int_size += 8;
    if (Func->RegF)
        fp_size += 8;

    regsave = ((int_size + fp_size + 8 * 8 * Func->H) + 0xf) & ~0xf;
    local_size = Func->FrameSize * 16 - regsave;

    int_regs = int_size / 8;
    fp_regs = fp_size / 8;
    saved_regs = regsave / 8;
    local_size_regs = local_size / 8;

    if (Func->Flag == 1)
    {
        offset = (unsigned int)(((Pc - Base) - Func->BeginAddress) / 4);
        if (offset < 17 || offset >= Func->FunctionLength - 15)
        {
            len = (int_size + 8) / 16 + (fp_size + 8) / 16;
            switch (Func->CR)
            {
            case 2:
                len++;
            case 3:
                len++;
                len++;
                if (local_size <= 512)
                    break;
            case 0:
            case 1:
                if (local_size)
                    len++;
                if (local_size > 4088)
                    len++;
                break;
            }
            if (offset < len + h_size)
                skip = len + h_size - offset;
            else if (offset >= Func->FunctionLength - (len + 1))
            {
                skip = offset - (Func->FunctionLength - (len + 1));
                h_size = 0;
            }
        }
    }

    if (!skip)
    {
        if (Func->CR == 3 || Func->CR == 2)
        {
            Context->Sp = Context->Fp;
            RtlpArm64RestoreRegs(29, 2, 0, Context);
        }
        Context->Sp += local_size;
        if (fp_size)
            RtlpArm64RestoreFpRegs(8, fp_regs, int_regs, Context);
        if (Func->CR == 1)
            RtlpArm64RestoreRegs(30, 1, int_regs - 1, Context);
        RtlpArm64RestoreRegs(19, Func->RegI, -(int)saved_regs, Context);
    }
    else
    {
        unsigned int pos = 0;

        switch (Func->CR)
        {
        case 3:
        case 2:
            if (pos++ >= skip)
                Context->Sp = Context->Fp;
            if (local_size <= 512)
            {
                if (pos++ >= skip)
                    RtlpArm64RestoreRegs(29, 2, -(int)local_size_regs, Context);
                break;
            }
            if (pos++ >= skip)
                RtlpArm64RestoreRegs(29, 2, 0, Context);
        case 0:
        case 1:
            if (!local_size)
                break;
            if (pos++ >= skip)
                Context->Sp += (local_size - 1) % 4088 + 1;
            if (local_size > 4088 && pos++ >= skip)
                Context->Sp += 4088;
            break;
        }

        pos += h_size;

        if (fp_size)
        {
            if (Func->RegF % 2 == 0 && pos++ >= skip)
                RtlpArm64RestoreFpRegs(8 + Func->RegF, 1, int_regs + fp_regs - 1, Context);
            for (i = (Func->RegF + 1) / 2 - 1; i >= 0; i--)
            {
                if (pos++ < skip)
                    continue;
                if (!i && !int_size)
                    RtlpArm64RestoreFpRegs(8, 2, -(int)saved_regs, Context);
                else
                    RtlpArm64RestoreFpRegs(8 + 2 * i, 2, int_regs + 2 * i, Context);
            }
        }

        if (Func->RegI % 2)
        {
            if (pos++ >= skip)
            {
                if (Func->CR == 1)
                    RtlpArm64RestoreRegs(30, 1, int_regs - 1, Context);
                RtlpArm64RestoreRegs(18 + Func->RegI, 1,
                                     (Func->RegI > 1) ? (int)(Func->RegI - 1) : -(int)saved_regs,
                                     Context);
            }
        }
        else if (Func->CR == 1)
        {
            if (pos++ >= skip)
                RtlpArm64RestoreRegs(30, 1, Func->RegI ? (int)(int_regs - 1) : -(int)saved_regs, Context);
        }

        for (i = Func->RegI / 2 - 1; i >= 0; i--)
        {
            if (pos++ < skip)
                continue;
            if (i)
                RtlpArm64RestoreRegs(19 + 2 * i, 2, 2 * i, Context);
            else
                RtlpArm64RestoreRegs(19, 2, -(int)saved_regs, Context);
        }
    }

    if (Func->CR == 2)
        RtlpArm64DoPacAuth(Context);
    return NULL;
}

static PVOID
RtlpArm64UnwindFull(
    _In_ ULONG_PTR Base,
    _In_ ULONG_PTR Pc,
    _In_ PARM64_RT_FUNCTION Func,
    _Inout_ PCONTEXT Context,
    _Out_ PVOID *HandlerData,
    _Inout_ PBOOLEAN FinalPcFromLr)
{
    PARM64_XDATA_HEADER info;
    ARM64_XDATA_EPILOG *info_epilog;
    unsigned int i, codes, epilogs, len, offset;
    PVOID data;
    PBYTE end;

    info = (PARM64_XDATA_HEADER)(ULONG_PTR)(Base + Func->UnwindData);
    data = info + 1;
    epilogs = info->EpilogCount;
    codes = info->CodeWords;
    if (!codes && !epilogs)
    {
        ARM64_XDATA_EXT *infoex = data;
        codes = infoex->codes;
        epilogs = infoex->epilog;
        data = infoex + 1;
    }
    info_epilog = data;
    if (!info->EpilogInHeader)
        data = info_epilog + epilogs;

    offset = (unsigned int)(((Pc - Base) - Func->BeginAddress) / 4);
    end = (PBYTE)data + codes * 4;

    if (offset < codes * 4)
    {
        len = RtlpArm64SequenceLen(data, end);
        if (offset < len)
        {
            RtlpArm64ProcessUnwindCodes(data, end, Context, len - offset, FinalPcFromLr);
            return NULL;
        }
    }

    if (!info->EpilogInHeader)
    {
        for (i = 0; i < epilogs; i++)
        {
            if (offset < info_epilog[i].offset)
                break;
            if (offset - info_epilog[i].offset < codes * 4 - info_epilog[i].index)
            {
                PBYTE ptr = (PBYTE)data + info_epilog[i].index;
                len = RtlpArm64SequenceLen(ptr, end);
                if (offset <= info_epilog[i].offset + len)
                {
                    RtlpArm64ProcessUnwindCodes(ptr, end, Context, offset - info_epilog[i].offset, FinalPcFromLr);
                    return NULL;
                }
            }
        }
    }
    else if (info->FunctionLength - offset <= codes * 4 - epilogs)
    {
        PBYTE ptr = (PBYTE)data + epilogs;
        len = RtlpArm64SequenceLen(ptr, end) + 1;
        if (offset >= info->FunctionLength - len)
        {
            RtlpArm64ProcessUnwindCodes(ptr, end, Context, offset - (info->FunctionLength - len), FinalPcFromLr);
            return NULL;
        }
    }

    RtlpArm64ProcessUnwindCodes(data, end, Context, 0, FinalPcFromLr);

    if (info->ExceptionDataPresent)
    {
        PDWORD handler_rva = (PDWORD)data + codes;
        *HandlerData = handler_rva + 1;
        return (PVOID)(ULONG_PTR)(Base + *handler_rva);
    }
    return NULL;
}

PEXCEPTION_ROUTINE
NTAPI
RtlVirtualUnwind(
    _In_ ULONG HandlerType,
    _In_ ULONG64 ImageBase,
    _In_ ULONG64 ControlPc,
    _In_ PRUNTIME_FUNCTION FunctionEntry,
    _Inout_ PCONTEXT Context,
    _Out_ PVOID *HandlerData,
    _Out_ PULONG64 EstablisherFrame,
    _Inout_opt_ PKNONVOLATILE_CONTEXT_POINTERS ContextPointers)
{
    PARM64_RT_FUNCTION Func = (PARM64_RT_FUNCTION)FunctionEntry;
    PVOID Handler = NULL;
    PVOID LocalHandlerData = NULL;
    BOOLEAN FinalPcFromLr = TRUE;

    (VOID)ContextPointers;

    if (HandlerData)
        *HandlerData = NULL;

    Context->ContextFlags |= CONTEXT_UNWOUND_TO_CALL;

    if (Func == NULL)
    {
        if (ControlPc == Context->Lr)
        {
            *EstablisherFrame = Context->Sp;
            Context->Pc = 0;
            return NULL;
        }
    }
    else if (Func->Flag)
        Handler = RtlpArm64UnwindPacked(ImageBase, ControlPc, Func, Context);
    else
        Handler = RtlpArm64UnwindFull(ImageBase, ControlPc, Func, Context, &LocalHandlerData, &FinalPcFromLr);

    if (FinalPcFromLr)
        Context->Pc = Context->Lr;

    *EstablisherFrame = Context->Sp;

    if (Handler != NULL && (HandlerType & (UNW_FLAG_EHANDLER | UNW_FLAG_UHANDLER)))
    {
        if (HandlerData)
            *HandlerData = LocalHandlerData;
        return (PEXCEPTION_ROUTINE)Handler;
    }

    return NULL;
}

VOID
NTAPI
RtlUnwindEx(
    _In_opt_ PVOID TargetFrame,
    _In_opt_ PVOID TargetIp,
    _In_opt_ PEXCEPTION_RECORD ExceptionRecord,
    _In_ PVOID ReturnValue,
    _In_ PCONTEXT ContextRecord,
    _In_opt_ PUNWIND_HISTORY_TABLE HistoryTable)
{
    EXCEPTION_RECORD LocalExceptionRecord;
    DISPATCHER_CONTEXT DispatcherContext;
    DISPATCHER_CONTEXT_NONVOLREG_ARM64 NonVolatileRegisters;
    PEXCEPTION_ROUTINE ExceptionRoutine;
    EXCEPTION_DISPOSITION Disposition;
    PRUNTIME_FUNCTION FunctionEntry;
    ULONG64 ImageBase, EstablisherFrame;
    CONTEXT UnwindContext, FrameContext;
    ULONG_PTR StackLow, StackHigh;
    ULONG_PTR LookupPc;
    ULONG FrameCount;
    BOOLEAN HaveTarget;

    if (ContextRecord == NULL)
        return;

    RtlCaptureContext(ContextRecord);

    if (ExceptionRecord == NULL)
    {
        RtlZeroMemory(&LocalExceptionRecord, sizeof(LocalExceptionRecord));
        LocalExceptionRecord.ExceptionCode = STATUS_UNWIND;
        LocalExceptionRecord.ExceptionAddress = (PVOID)(ULONG_PTR)ContextRecord->Pc;
        ExceptionRecord = &LocalExceptionRecord;
    }

    ExceptionRecord->ExceptionFlags = EXCEPTION_UNWINDING;
    if (TargetFrame == NULL)
        ExceptionRecord->ExceptionFlags |= EXCEPTION_EXIT_UNWIND;

    HaveTarget = (TargetFrame != NULL) || (TargetIp != NULL);
    UnwindContext = *ContextRecord;
    RtlZeroMemory(&DispatcherContext, sizeof(DispatcherContext));
    DispatcherContext.ContextRecord = ContextRecord;
    DispatcherContext.HistoryTable = HistoryTable;
    DispatcherContext.TargetPc = (ULONG64)(ULONG_PTR)TargetIp;
    DispatcherContext.NonVolatileRegisters = NonVolatileRegisters.Buffer;

    RtlpGetStackLimits(&StackLow, &StackHigh);
    if (TargetFrame != NULL)
        StackHigh = (ULONG_PTR)TargetFrame + 1;

    for (FrameCount = 0; FrameCount < 1024; FrameCount++)
    {
        if ((UnwindContext.Sp < StackLow) ||
            (UnwindContext.Sp > StackHigh) ||
            (UnwindContext.Sp & (sizeof(ULONG64) - 1)))
        {
            ExceptionRecord->ExceptionFlags |= EXCEPTION_STACK_INVALID;
            return;
        }

        LookupPc = (FrameCount == 0) ? UnwindContext.Pc : (UnwindContext.Pc - 4);
        FunctionEntry = RtlLookupFunctionEntry(LookupPc, &ImageBase, NULL);
        if (FunctionEntry == NULL)
        {
            if ((UnwindContext.Lr == 0) ||
                (UnwindContext.Lr == UnwindContext.Pc))
            {
                break;
            }

            UnwindContext.Pc = UnwindContext.Lr;
            continue;
        }

        FrameContext = UnwindContext;
        DispatcherContext.ControlPc = LookupPc;
        RtlpArm64CaptureNonVolatileRegisters(&NonVolatileRegisters, &UnwindContext);
        ExceptionRoutine = RtlVirtualUnwind(UNW_FLAG_UHANDLER,
                                            ImageBase,
                                            LookupPc,
                                            FunctionEntry,
                                            &UnwindContext,
                                            &DispatcherContext.HandlerData,
                                            &EstablisherFrame,
                                            NULL);

        if ((EstablisherFrame < StackLow) ||
            (EstablisherFrame >= StackHigh) ||
            (EstablisherFrame & (sizeof(ULONG64) - 1)))
        {
            ExceptionRecord->ExceptionFlags |= EXCEPTION_STACK_INVALID;
            return;
        }

        if (ExceptionRoutine != NULL)
        {
            if (HaveTarget && (EstablisherFrame == (ULONG64)(ULONG_PTR)TargetFrame))
                ExceptionRecord->ExceptionFlags |= EXCEPTION_TARGET_UNWIND;

            DispatcherContext.ImageBase = ImageBase;
            DispatcherContext.FunctionEntry = FunctionEntry;
            DispatcherContext.LanguageHandler = ExceptionRoutine;
            DispatcherContext.EstablisherFrame = EstablisherFrame;
            DispatcherContext.ScopeIndex = 0;

            do
            {
                Disposition = ExceptionRoutine(ExceptionRecord,
                                               (PVOID)EstablisherFrame,
                                               ContextRecord,
                                               &DispatcherContext);

                ExceptionRecord->ExceptionFlags &= ~(EXCEPTION_TARGET_UNWIND |
                                                     EXCEPTION_COLLIDED_UNWIND);

                if (Disposition == ExceptionContinueExecution)
                {
                    if (ExceptionRecord->ExceptionFlags & EXCEPTION_NONCONTINUABLE)
                        RtlRaiseStatus(STATUS_NONCONTINUABLE_EXCEPTION);
                    return;
                }

                if (Disposition == ExceptionCollidedUnwind)
                {
                    UnwindContext = *ContextRecord;
                    DispatcherContext.ContextRecord = ContextRecord;
                    EstablisherFrame = DispatcherContext.EstablisherFrame;
                    ExceptionRecord->ExceptionFlags |= EXCEPTION_COLLIDED_UNWIND;
                }
                else if (Disposition != ExceptionContinueSearch)
                {
                    RtlRaiseStatus(STATUS_INVALID_DISPOSITION);
                }
            }
            while (Disposition == ExceptionCollidedUnwind);
        }

        if (HaveTarget && (EstablisherFrame == (ULONG64)(ULONG_PTR)TargetFrame))
        {
            if (TargetIp != NULL)
                FrameContext.Pc = (ULONG64)(ULONG_PTR)TargetIp;

            FrameContext.X0 = (ULONG64)(ULONG_PTR)ReturnValue;
            *ContextRecord = FrameContext;
            RtlRestoreContext(ContextRecord, ExceptionRecord);
        }

        *ContextRecord = UnwindContext;
    }
}

VOID
NTAPI
RtlUnwind(
    _In_opt_ PVOID TargetFrame,
    _In_opt_ PVOID TargetIp,
    _In_opt_ PEXCEPTION_RECORD ExceptionRecord,
    _In_ PVOID ReturnValue)
{
    CONTEXT ContextRecord;

    RtlCaptureContext(&ContextRecord);
    RtlUnwindEx(TargetFrame,
                TargetIp,
                ExceptionRecord,
                ReturnValue,
                &ContextRecord,
                NULL);
}

/* RtlRestoreContext is implemented in arm64/context_asm.S (it must load the
 * register file and branch, which cannot be expressed in C). */

PRUNTIME_FUNCTION
NTAPI
RtlLookupFunctionTable(
    _In_ ULONG_PTR ControlPc,
    _Out_ PULONG_PTR ImageBase,
    _Out_ PULONG Length)
{
    PIMAGE_NT_HEADERS NtHeaders;
    PRUNTIME_FUNCTION FunctionTable;
    PVOID BaseAddress;

    if (!RtlPcToFileHeader((PVOID)ControlPc, &BaseAddress))
        return NULL;

    *ImageBase = (ULONG_PTR)BaseAddress;
    NtHeaders = RtlImageNtHeader(BaseAddress);
    if (!NtHeaders)
    {
        *Length = 0;
        return NULL;
    }

    FunctionTable = RtlpArm64GetImageFunctionTable(ControlPc, BaseAddress, NtHeaders, Length);
    return FunctionTable;
}
