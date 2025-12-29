/*
 * PROJECT:     FreeLoader UEFI Support
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     UEFI backtrace helpers (module+offset formatting) for AMD64/ARM64
 */

#include <uefildr.h>
#include <debug.h>
#include <reactos/rossym.h>
#include <ntimage.h>
#include <DevicePath.h>
#include <arch/uefi/uefisym.h>

DBG_DEFAULT_CHANNEL(WARNING);
#if DBG
/* Silence -Wunused-variable when this unit uses DbgPrint() directly */
static void __attribute__((unused)) __dbg_channel_keep(void) { (void)DbgDefaultChannel; }
#endif

extern EFI_SYSTEM_TABLE *GlobalSystemTable;
extern EFI_HANDLE GlobalImageHandle;

/* From UEFI memory init (for freeldr base/size) */
extern PVOID  OsLoaderBase;
extern SIZE_T OsLoaderSize;

typedef struct _UEFI_IMAGE_ENTRY {
    ULONG_PTR Base;
    SIZE_T    Size;
    UINTN     Index; /* enumeration index */
    CHAR*     Name;  /* optional printable name */
} UEFI_IMAGE_ENTRY;

static UEFI_IMAGE_ENTRY* gImageList = NULL;
static UINTN             gImageCount = 0;
static ULONG_PTR         gFreeldrBase = 0;
static SIZE_T            gFreeldrSize = 0;
static PROSSYM_INFO      gFreeldrRos = NULL;
static EFI_BOOT_SERVICES* gBs = NULL;
extern char __ImageBase; /* PE image base symbol */

static PVOID RosUefiAlloc(ULONG_PTR Size)
{
    if (!gBs) return NULL;
    VOID* ptr = NULL;
    if (!EFI_ERROR(gBs->AllocatePool(EfiLoaderData, (UINTN)Size, &ptr)))
        return ptr;
    return NULL;
}

static VOID RosUefiFree(PVOID Area)
{
    if (gBs && Area) gBs->FreePool(Area);
}

/* Minimal PE32+ export resolver for symbol names */
static BOOLEAN ResolveExport(ULONG_PTR ImageBase, ULONG_PTR Addr, CHAR* NameBuf, SIZE_T NameBufLen, ULONG_PTR* SymAddr)
{
    typedef struct _DOS_HDR { USHORT e_magic; USHORT e_cblp; USHORT e_cp; USHORT e_crlc; USHORT e_cparhdr; USHORT e_minalloc; USHORT e_maxalloc; USHORT e_ss; USHORT e_sp; USHORT e_csum; USHORT e_ip; USHORT e_cs; USHORT e_lfarlc; USHORT e_ovno; USHORT e_res[4]; USHORT e_oemid; USHORT e_oeminfo; USHORT e_res2[10]; LONG e_lfanew; } DOS_HDR;
    typedef struct _DATA_DIRECTORY { ULONG VirtualAddress; ULONG Size; } DATA_DIRECTORY;
    typedef struct _OPT_HDR64 { USHORT Magic; UCHAR MajorLinkerVersion; UCHAR MinorLinkerVersion; ULONG SizeOfCode; ULONG SizeOfInitializedData; ULONG SizeOfUninitializedData; ULONG AddressOfEntryPoint; ULONG BaseOfCode; ULONGLONG ImageBase; ULONG SectionAlignment; ULONG FileAlignment; USHORT MajorOSVersion; USHORT MinorOSVersion; USHORT MajorImageVersion; USHORT MinorImageVersion; USHORT MajorSubsystemVersion; USHORT MinorSubsystemVersion; ULONG Win32VersionValue; ULONG SizeOfImage; ULONG SizeOfHeaders; ULONG CheckSum; USHORT Subsystem; USHORT DllCharacteristics; ULONGLONG SizeOfStackReserve; ULONGLONG SizeOfStackCommit; ULONGLONG SizeOfHeapReserve; ULONGLONG SizeOfHeapCommit; ULONG LoaderFlags; ULONG NumberOfRvaAndSizes; DATA_DIRECTORY DataDirectory[16]; } OPT_HDR64;
    typedef struct _NT_HDRS64 { ULONG Signature; struct { USHORT Machine; USHORT NumberOfSections; ULONG TimeDateStamp; ULONG PointerToSymbolTable; ULONG NumberOfSymbols; USHORT SizeOfOptionalHeader; USHORT Characteristics; } FileHeader; OPT_HDR64 OptionalHeader; } NT_HDRS64;
    typedef struct _EXPORT_DIR { ULONG Characteristics; ULONG TimeDateStamp; USHORT MajorVersion; USHORT MinorVersion; ULONG Name; ULONG Base; ULONG NumberOfFunctions; ULONG NumberOfNames; ULONG AddressOfFunctions; ULONG AddressOfNames; ULONG AddressOfNameOrdinals; } EXPORT_DIR;

    CHAR* img = (CHAR*)ImageBase;
    DOS_HDR* dos = (DOS_HDR*)img;
    if (!dos || dos->e_magic != 0x5A4D) return FALSE; /* 'MZ' */
    NT_HDRS64* nt = (NT_HDRS64*)(img + dos->e_lfanew);
    if (!nt || nt->Signature != 0x00004550) return FALSE; /* 'PE\0\0' */
    if (nt->OptionalHeader.Magic != 0x20B) return FALSE; /* PE32+ */
    DATA_DIRECTORY expDir = nt->OptionalHeader.DataDirectory[0];
    if (expDir.VirtualAddress == 0 || expDir.Size < sizeof(EXPORT_DIR)) return FALSE;
    EXPORT_DIR* exp = (EXPORT_DIR*)(img + expDir.VirtualAddress);
    if (!exp->NumberOfFunctions) return FALSE;

    ULONG* funcRVAs = (ULONG*)(img + exp->AddressOfFunctions);
    ULONG* nameRVAs = (ULONG*)(img + exp->AddressOfNames);
    USHORT* ordinals = (USHORT*)(img + exp->AddressOfNameOrdinals);
    ULONG_PTR targetRVA = (ULONG_PTR)(Addr - ImageBase);

    ULONG bestRVA = 0; const CHAR* bestName = NULL;
    for (ULONG i = 0; i < exp->NumberOfNames; ++i)
    {
        USHORT ord = ordinals[i];
        if (ord >= exp->NumberOfFunctions) continue;
        ULONG rva = funcRVAs[ord];
        if (rva == 0 || rva > targetRVA) continue;
        if (rva >= bestRVA) { bestRVA = rva; bestName = (const CHAR*)(img + nameRVAs[i]); }
    }
    if (!bestName) return FALSE;
    SIZE_T n = 0; while (bestName[n] && n + 1 < NameBufLen) { NameBuf[n] = bestName[n]; ++n; }
    NameBuf[n] = '\0';
    if (SymAddr) *SymAddr = ImageBase + bestRVA;
    return TRUE;
}

static VOID
FormatAddressWithModule(ULONG_PTR Address)
{
    ULONG_PTR base = gFreeldrBase ? gFreeldrBase : (ULONG_PTR)OsLoaderBase;
    ULONG_PTR size = gFreeldrBase ? gFreeldrSize : (ULONG_PTR)OsLoaderSize;

    /* Early fallback: derive freeldr base/size from __ImageBase if not set yet */
    if ((base == 0 || size == 0))
    {
        ULONG_PTR ib = (ULONG_PTR)&__ImageBase;
        IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)ib;
        if (dos && dos->e_magic == IMAGE_DOS_SIGNATURE)
        {
            IMAGE_NT_HEADERS64* nt = (IMAGE_NT_HEADERS64*)((UCHAR*)ib + dos->e_lfanew);
            if (nt && nt->Signature == IMAGE_NT_SIGNATURE && nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC)
            {
                gFreeldrBase = ib;
                gFreeldrSize = nt->OptionalHeader.SizeOfImage;
                base = gFreeldrBase;
                size = gFreeldrSize;
            }
        }
    }
    ULONG_PTR end  = base + size;

    /* exported symbol resolution handled by ResolveExport() */

    if (base != 0 && size != 0 && Address >= base && Address < end)
    {
        CHAR sym[128]; ULONG_PTR symAddr = 0;

        /* If available, use .rossym for precise function names + source line */
        if (!gFreeldrRos && base && size)
        {
            /* Initialize rossym callbacks using UEFI pool allocators */
            gBs = GlobalSystemTable ? GlobalSystemTable->BootServices : NULL;
            if (gBs)
            {
                static ROSSYM_CALLBACKS cb;
                cb.AllocMemProc = RosUefiAlloc;
                cb.FreeMemProc  = RosUefiFree;
                cb.ReadFileProc = NULL;
                cb.SeekFileProc = NULL;
                cb.MemGetProc   = NULL;
                RosSymInit(&cb);
            }
            (void)RosSymCreateFromMem((PVOID)base, size, &gFreeldrRos);
        }

        if (gFreeldrRos)
        {
            ULONG line = 0; CHAR file[128] = {0}; CHAR func[128] = {0};
            if (RosSymGetAddressInformation(gFreeldrRos, (ULONG_PTR)(Address - base), &line, file, func))
            {
                if (func[0] && file[0] && line)
                    DbgPrint("    %p (freeldr!%s) %s:%lu\n", (PVOID)Address, func, file, (unsigned long)line);
                else if (func[0])
                    DbgPrint("    %p (freeldr!%s)\n", (PVOID)Address, func);
                else if (file[0] && line)
                    DbgPrint("    %p (freeldr+0x%Ix) %s:%lu\n", (PVOID)Address, (SIZE_T)(Address - base), file, (unsigned long)line);
                else
                    DbgPrint("    %p (freeldr+0x%Ix)\n", (PVOID)Address, (SIZE_T)(Address - base));
                return;
            }
        }

        /* Prefer embedded freeldr symbol table when available */
        if (FreeldrLookupEmbeddedSymbol(Address, sym, sizeof(sym), &symAddr))
        {
            LONGLONG delta = (LONGLONG)Address - (LONGLONG)symAddr;
            if (delta >= 0)
                DbgPrint("    %p (freeldr!%s+0x%llx)\n", (PVOID)Address, sym, (ULONGLONG)delta);
            else
                DbgPrint("    %p (freeldr!%s-0x%llx)\n", (PVOID)Address, sym, (ULONGLONG)(-delta));
        }
        else if (ResolveExport(base, Address, sym, sizeof(sym), &symAddr))
        {
            SIZE_T off = (SIZE_T)(Address - symAddr);
            DbgPrint("    %p (freeldr!%s+0x%Ix)\n", (PVOID)Address, sym, off);
        }
        else
        {
            DbgPrint("    %p (freeldr+0x%Ix)\n", (PVOID)Address, (SIZE_T)(Address - base));
        }
        return;
    }

        /* Search other loaded images we captured */
    for (UINTN i = 0; i < gImageCount; ++i)
    {
        ULONG_PTR mbase = gImageList[i].Base;
        ULONG_PTR mend  = mbase + (ULONG_PTR)gImageList[i].Size;
        if (Address >= mbase && Address < mend)
        {
            CHAR sym[128]; ULONG_PTR symAddr = 0;
            if (ResolveExport(mbase, Address, sym, sizeof(sym), &symAddr))
            {
                SIZE_T off = (SIZE_T)(Address - symAddr);
                if (gImageList[i].Name)
                    DbgPrint("    %p (%s!%s+0x%Ix)\n", (PVOID)Address, gImageList[i].Name, sym, off);
                else
                    DbgPrint("    %p (image#%u!%s+0x%Ix)\n", (PVOID)Address, (unsigned)gImageList[i].Index, sym, off);
            }
            else
            {
                if (gImageList[i].Name)
                    DbgPrint("    %p (%s+0x%Ix)\n", (PVOID)Address, gImageList[i].Name,
                             (SIZE_T)(Address - mbase));
                else
                    DbgPrint("    %p (image#%u+0x%Ix)\n", (PVOID)Address, (unsigned)gImageList[i].Index,
                             (SIZE_T)(Address - mbase));
            }
            return;
        }
    }

    /* Best-effort: print raw address if module not known */
    DbgPrint("    %p\n", (PVOID)Address);
}

/* Exported helper: print single address with module/symbol formatting */
VOID
UefiDbgFormatAddress(ULONG_PTR Address)
{
    FormatAddressWithModule(Address);
}

/* Print backtrace using ARM64 frame pointer chain (x29) */
static CHAR*
DupAscii(CHAR16* Src16, UINTN MaxChars, EFI_BOOT_SERVICES* Bs)
{
    if (!Src16 || MaxChars == 0) return NULL;
    /* Compute length up to MaxChars or NUL */
    UINTN len = 0;
    while (len < MaxChars && Src16[len] != 0) len++;
    CHAR* dst = NULL;
    if (EFI_ERROR(Bs->AllocatePool(EfiLoaderData, len + 1, (VOID**)&dst)))
        return NULL;
    for (UINTN i = 0; i < len; ++i)
    {
        UINTN c = (UINTN)Src16[i] & 0xFF;
        dst[i] = (CHAR)(c ? c : '?');
    }
    dst[len] = '\0';
    /* Reduce to basename: keep text after the last '\\' or '/' */
    INTN last = -1;
    for (UINTN i = 0; i < len; ++i)
    {
        if (dst[i] == '\\' || dst[i] == '/') last = (INTN)i;
    }
    if (last >= 0 && (UINTN)(last + 1) < len)
    {
        UINTN newLen = len - (UINTN)(last + 1);
        memmove(dst, dst + last + 1, newLen);
        dst[newLen] = '\0';
    }
    return dst;
}

static CHAR*
ExtractImageName(EFI_LOADED_IMAGE_PROTOCOL* Image, EFI_BOOT_SERVICES* Bs)
{
    if (!Image || !Image->FilePath) return NULL;

    EFI_DEVICE_PATH_PROTOCOL* Node = Image->FilePath;
    EFI_DEVICE_PATH_PROTOCOL* LastFileNode = NULL;

    /* Walk to find the last FILEPATH_DEVICE_PATH node */
    while (!IsDevicePathEndType(Node))
    {
        if (DevicePathType(Node) == MEDIA_DEVICE_PATH &&
            DevicePathSubType(Node) == MEDIA_FILEPATH_DP)
        {
            LastFileNode = Node;
        }
        Node = NextDevicePathNode(Node);
    }

    if (!LastFileNode)
        return NULL;

    FILEPATH_DEVICE_PATH* Fp = (FILEPATH_DEVICE_PATH*)LastFileNode;
    UINTN nodeLen = DevicePathNodeLength(LastFileNode);
    UINTN header = FIELD_OFFSET(FILEPATH_DEVICE_PATH, PathName);
    if (nodeLen <= header) return NULL;
    UINTN chars = (nodeLen - header) / sizeof(CHAR16);
    return DupAscii(Fp->PathName, chars, Bs);
}

static VOID
Arm64WalkFrames(ULONG_PTR FramePointer, ULONG_PTR StackTop, ULONG_PTR StackBottom)
{
    ULONG frames = 0;
    const ULONG max_frames = 32;

    while (frames < max_frames)
    {
        if ((FramePointer & 0xF) != 0) break;           /* 16-byte alignment */
        if (FramePointer < StackBottom || FramePointer + 16 > StackTop) break;

        ULONG_PTR* slot = (ULONG_PTR*)FramePointer;
        ULONG_PTR next_fp = slot[0];
        ULONG_PTR lr      = slot[1];

        if (lr == 0 || next_fp <= FramePointer) break;

        FormatAddressWithModule(lr);
        FramePointer = next_fp;
        frames++;
    }
}

VOID
UefiArm64PrintBacktrace(ULONG_PTR FramePointer, ULONG_PTR StackTop, ULONG_PTR StackBottom)
{
    DbgPrint("Backtrace (ARM64):\n");
    Arm64WalkFrames(FramePointer, StackTop, StackBottom);
}

VOID
UefiArm64PrintBacktraceNoHeader(ULONG_PTR FramePointer, ULONG_PTR StackTop, ULONG_PTR StackBottom)
{
    Arm64WalkFrames(FramePointer, StackTop, StackBottom);
}

/* Print backtrace using AMD64 frame pointer chain (rbp) */
VOID
UefiAmd64PrintBacktrace(ULONG_PTR Rbp, ULONG_PTR StackTop, ULONG_PTR StackBottom)
{
    ULONG frames = 0;
    const ULONG max_frames = 32;

    DbgPrint("Backtrace (AMD64):\n");

    while (frames < max_frames)
    {
        if ((Rbp & 0xF) != 0) break;                      /* 16-byte alignment */
        if (Rbp < StackBottom || Rbp + 16 > StackTop) break;

        ULONG_PTR* slot = (ULONG_PTR*)Rbp;
        ULONG_PTR next_rbp = slot[0];
        ULONG_PTR ret_addr = slot[1];

        if (ret_addr == 0 || next_rbp <= Rbp) break;

        FormatAddressWithModule(ret_addr);
        Rbp = next_rbp;
        frames++;
    }
}

/* Optional: initialize any image info. For now rely on OsLoaderBase/Size. */
VOID
UefiInitializeDebugImageInfo(VOID)
{
    if (!GlobalSystemTable || !GlobalSystemTable->BootServices)
        return;

    EFI_BOOT_SERVICES* Bs = GlobalSystemTable->BootServices;

    if (gImageList != NULL)
    {
        /* Free previous names, then the list */
        for (UINTN i = 0; i < gImageCount; ++i)
        {
            if (gImageList[i].Name)
                Bs->FreePool(gImageList[i].Name);
        }
        Bs->FreePool(gImageList);
        gImageList = NULL;
        gImageCount = 0;
    }
    EFI_GUID LoadedImageGuid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
    EFI_HANDLE* Handles = NULL;
    UINTN HandleCount = 0;

    EFI_STATUS Status = Bs->LocateHandleBuffer(ByProtocol,
                                               &LoadedImageGuid,
                                               NULL,
                                               &HandleCount,
                                               &Handles);
    if (EFI_ERROR(Status) || HandleCount == 0)
        return;

    /* Allocate local image list */
    Status = Bs->AllocatePool(EfiLoaderData,
                              HandleCount * sizeof(*gImageList),
                              (VOID**)&gImageList);
    if (EFI_ERROR(Status) || !gImageList)
        goto Done;

    gImageCount = 0;
    gFreeldrBase = 0;
    gFreeldrSize = 0;
    for (UINTN i = 0; i < HandleCount; ++i)
    {
        EFI_LOADED_IMAGE_PROTOCOL* Image = NULL;
        Status = Bs->HandleProtocol(Handles[i], &LoadedImageGuid, (VOID**)&Image);
        if (EFI_ERROR(Status) || !Image)
            continue;

        gImageList[gImageCount].Base  = (ULONG_PTR)Image->ImageBase;
        gImageList[gImageCount].Size  = (SIZE_T)Image->ImageSize;
        gImageList[gImageCount].Index = i;
        gImageList[gImageCount].Name  = ExtractImageName(Image, Bs);
        if (Handles[i] == GlobalImageHandle)
        {
            gFreeldrBase = (ULONG_PTR)Image->ImageBase;
            gFreeldrSize = (SIZE_T)Image->ImageSize;
            if (!OsLoaderBase) OsLoaderBase = Image->ImageBase;
            if (!OsLoaderSize) OsLoaderSize = Image->ImageSize;
        }
        gImageCount++;
    }

Done:
    if (Handles)
        Bs->FreePool(Handles);
}
