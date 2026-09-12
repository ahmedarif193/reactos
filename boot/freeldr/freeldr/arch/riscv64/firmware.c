/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     RISC-V UEFI boot-manager machine and ARC services.
 */

#include <freeldr.h>
/* Keep EDK2's base names separate from the NT SDK names. */
#define GUID EFI_BASE_GUID
#define PHYSICAL_ADDRESS EFI_BASE_PHYSICAL_ADDRESS
#define LIST_ENTRY EFI_BASE_LIST_ENTRY
#define _LIST_ENTRY _EFI_BASE_LIST_ENTRY
#undef TRUE
#undef FALSE
#undef NULL
#include <Uefi.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>
#include <Guid/FileInfo.h>
#include <Guid/GlobalVariable.h>
#undef GUID
#undef PHYSICAL_ADDRESS
#undef LIST_ENTRY
#undef _LIST_ENTRY

static EFI_SYSTEM_TABLE *SystemTable;
static EFI_FILE_PROTOCOL *BootDirectory;
static EFI_FILE_PROTOCOL *Files[MAX_FDS];
static EFI_GUID LoadedImageGuid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
static EFI_GUID FileSystemGuid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
static EFI_GUID FileInfoGuid = EFI_FILE_INFO_ID;
static EFI_GUID GlobalVariableGuid = EFI_GLOBAL_VARIABLE;
static EFI_EVENT ClockEvent;
static volatile ULONG Seconds;
static UINTN Columns, Rows;
static int PendingScanCode;
static TIMEINFO TimeInfo;
static PUCHAR PreviousScreen;

MACHVTBL MachVtbl;
UCHAR MachDefaultTextColor = 7;
CCHAR FrLdrBootPath[MAX_PATH] = "\\";
PVOID gInitRamDiskBase;
ULONG gInitRamDiskSize;

_Static_assert(sizeof(EFI_STATUS) == 8, "RV64 UEFI status width");
_Static_assert(sizeof(EFI_PHYSICAL_ADDRESS) == 8, "RV64 physical address width");
_Static_assert(sizeof(EFI_TABLE_HEADER) == 24, "UEFI table header layout");
_Static_assert(__builtin_offsetof(EFI_SYSTEM_TABLE, BootServices) == 96, "UEFI system table layout");
_Static_assert(__builtin_offsetof(EFI_BOOT_SERVICES, AllocatePool) == 64, "UEFI service table layout");

static VOID EFIAPI Tick(EFI_EVENT Event, VOID *Context)
{
    ++Seconds;
}

VOID StallExecutionProcessor(ULONG Microseconds)
{
    SystemTable->BootServices->Stall(Microseconds);
}

static VOID Idle(VOID)
{
    StallExecutionProcessor(1000);
}

PVOID MmAllocateMemoryWithType(SIZE_T Size, TYPE_OF_MEMORY Type)
{
    PVOID Memory = NULL;
    if (Size == 0 || EFI_ERROR(SystemTable->BootServices->AllocatePool(EfiLoaderData, Size, &Memory)))
        return NULL;
    return Memory;
}

PVOID MmAllocateMemory(SIZE_T Size)
{
    return MmAllocateMemoryWithType(Size, LoaderOsloaderHeap);
}

VOID MmFreeMemory(PVOID Memory)
{
    if (Memory)
        SystemTable->BootServices->FreePool(Memory);
}

PVOID FrLdrTempAlloc(SIZE_T Size, ULONG Tag)
{
    return MmAllocateMemory(Size);
}

VOID FrLdrTempFree(PVOID Memory, ULONG Tag)
{
    MmFreeMemory(Memory);
}

PVOID FrLdrHeapAlloc(SIZE_T Size, ULONG Tag)
{
    return MmAllocateMemory(Size);
}

VOID FrLdrHeapFree(PVOID Memory, ULONG Tag)
{
    MmFreeMemory(Memory);
}

static VOID PutChar(int Character)
{
    CHAR16 Text[3] = { (CHAR16)Character, 0, 0 };
    if (Character == '\n')
    {
        Text[0] = '\r';
        Text[1] = '\n';
    }
    SystemTable->ConOut->OutputString(SystemTable->ConOut, Text);
}

static BOOLEAN KbHit(VOID)
{
    return PendingScanCode || SystemTable->BootServices->CheckEvent(SystemTable->ConIn->WaitForKey) == EFI_SUCCESS;
}

static int GetCh(VOID)
{
    EFI_INPUT_KEY Key;
    int Result;
    if (PendingScanCode)
    {
        Result = PendingScanCode;
        PendingScanCode = 0;
        return Result;
    }
    if (EFI_ERROR(SystemTable->ConIn->ReadKeyStroke(SystemTable->ConIn, &Key)))
        return 0;
    if (Key.UnicodeChar)
        return Key.UnicodeChar;
    switch (Key.ScanCode)
    {
        case SCAN_ESC: return KEY_ESC;
        case SCAN_UP: PendingScanCode = KEY_UP; break;
        case SCAN_DOWN: PendingScanCode = KEY_DOWN; break;
        case SCAN_LEFT: PendingScanCode = KEY_LEFT; break;
        case SCAN_RIGHT: PendingScanCode = KEY_RIGHT; break;
        case SCAN_HOME: PendingScanCode = KEY_HOME; break;
        case SCAN_END: PendingScanCode = KEY_END; break;
        case SCAN_DELETE: PendingScanCode = KEY_DELETE; break;
        case SCAN_F1: PendingScanCode = KEY_F1; break;
        case SCAN_F2: PendingScanCode = KEY_F2; break;
        case SCAN_F3: PendingScanCode = KEY_F3; break;
        case SCAN_F4: PendingScanCode = KEY_F4; break;
        case SCAN_F5: PendingScanCode = KEY_F5; break;
        case SCAN_F6: PendingScanCode = KEY_F6; break;
        case SCAN_F7: PendingScanCode = KEY_F7; break;
        case SCAN_F8: PendingScanCode = KEY_F8; break;
        case SCAN_F9: PendingScanCode = KEY_F9; break;
        case SCAN_F10: PendingScanCode = KEY_F10; break;
        default: return 0;
    }
    return KEY_EXTENDED;
}

static VOID ClearScreen(UCHAR Attribute)
{
    SystemTable->ConOut->SetAttribute(SystemTable->ConOut, Attribute & 0x7f);
    SystemTable->ConOut->ClearScreen(SystemTable->ConOut);
    if (PreviousScreen)
        memset(PreviousScreen, 0xff, Columns * Rows * 2);
}

static VIDEODISPLAYMODE SetDisplayMode(PCSTR Mode, BOOLEAN Init)
{
    return VideoTextMode;
}

static VOID GetDisplaySize(PULONG Width, PULONG Height, PULONG Depth)
{
    *Width = Columns;
    *Height = Rows;
    *Depth = 0;
}

static ULONG GetBufferSize(VOID)
{
    return Columns * Rows * 2;
}

static VOID SetCursor(UCHAR X, UCHAR Y)
{
    SystemTable->ConOut->SetCursorPosition(SystemTable->ConOut, X, Y);
}

static VOID ShowCursor(BOOLEAN Show)
{
    SystemTable->ConOut->EnableCursor(SystemTable->ConOut, Show);
}

static CHAR16 TextCharacter(UCHAR Character)
{
    /* FreeLdr's TUI uses CP437; firmware text is Unicode. */
    switch (Character)
    {
        case 0: return ' ';
        case 0x18: return 0x2191;
        case 0x19: return 0x2193;
        case 0xb0: return 0x2591;
        case 0xb1: return 0x2592;
        case 0xb2: return 0x2593;
        case 0xb3: return 0x2502;
        case 0xc4: return 0x2500;
        case 0xda: return 0x250c;
        case 0xbf: return 0x2510;
        case 0xc0: return 0x2514;
        case 0xd9: return 0x2518;
        case 0xba: return 0x2551;
        case 0xcd: return 0x2550;
        case 0xc9: return 0x2554;
        case 0xbb: return 0x2557;
        case 0xc8: return 0x255a;
        case 0xbc: return 0x255d;
        default: return Character;
    }
}

static VOID VideoPutChar(int Character, UCHAR Attribute, unsigned X, unsigned Y)
{
    CHAR16 Text[2] = { TextCharacter(Character), 0 };
    if (X >= Columns || Y >= Rows)
        return;
    SystemTable->ConOut->SetAttribute(SystemTable->ConOut, Attribute & 0x7f);
    SystemTable->ConOut->SetCursorPosition(SystemTable->ConOut, X, Y);
    SystemTable->ConOut->OutputString(SystemTable->ConOut, Text);
}

static VOID CopyScreen(PVOID Buffer)
{
    PUCHAR Screen = Buffer;
    UINTN X, Y;
    CHAR16 Text[256];
    if (!Screen)
        return;
    for (Y = 0; Y < Rows; ++Y)
    {
        for (X = 0; X < Columns;)
        {
            UINTN Offset = (Y * Columns + X) * 2, Count = 0;
            UCHAR Attribute = Screen[Offset + 1] & 0x7f;
            if (PreviousScreen && memcmp(Screen + Offset, PreviousScreen + Offset, 2) == 0)
            {
                ++X;
                continue;
            }
            /* Leave the last cell untouched: OutputString there scrolls ConOut. */
            if (Y == Rows - 1 && X == Columns - 1)
                break;
            while (X + Count < Columns && Count < RTL_NUMBER_OF(Text) - 1)
            {
                UINTN Cell = Offset + Count * 2;
                if ((Screen[Cell + 1] & 0x7f) != Attribute || (Y == Rows - 1 && X + Count == Columns - 1))
                    break;
                Text[Count++] = TextCharacter(Screen[Cell]);
            }
            Text[Count] = 0;
            SystemTable->ConOut->SetAttribute(SystemTable->ConOut, Attribute);
            SystemTable->ConOut->SetCursorPosition(SystemTable->ConOut, X, Y);
            SystemTable->ConOut->OutputString(SystemTable->ConOut, Text);
            if (PreviousScreen)
                memcpy(PreviousScreen + Offset, Screen + Offset, Count * 2);
            X += Count;
        }
    }
}

static BOOLEAN PaletteFixed(VOID) { return TRUE; }
static VOID SyncVideo(VOID) { }
static VOID Beep(VOID) { PutChar('\a'); }

TIMEINFO *ArcGetTime(VOID)
{
    EFI_TIME Time;
    if (!EFI_ERROR(SystemTable->RuntimeServices->GetTime(&Time, NULL)))
    {
        TimeInfo.Year = Time.Year;
        TimeInfo.Month = Time.Month;
        TimeInfo.Day = Time.Day;
        TimeInfo.Hour = Time.Hour;
        TimeInfo.Minute = Time.Minute;
        TimeInfo.Second = Time.Second;
    }
    return &TimeInfo;
}

ULONG ArcGetRelativeTime(VOID) { return Seconds; }
PCCHAR FrLdrGetBootPath(VOID) { return FrLdrBootPath; }

static ARC_STATUS ArcStatus(EFI_STATUS Status)
{
    if (!EFI_ERROR(Status)) return ESUCCESS;
    if (Status == EFI_NOT_FOUND) return ENOENT;
    if (Status == EFI_OUT_OF_RESOURCES) return ENOMEM;
    if (Status == EFI_ACCESS_DENIED || Status == EFI_WRITE_PROTECTED) return EACCES;
    if (Status == EFI_INVALID_PARAMETER) return EINVAL;
    return EIO;
}

ARC_STATUS FsOpenFile(PCSTR Name, PCSTR DefaultPath, OPENMODE Mode, PULONG FileId)
{
    CHAR16 Path[MAX_PATH];
    UINTN Length = 0;
    ULONG Slot;
    EFI_STATUS Status;
    if (!BootDirectory || !Name || !FileId || Mode != OpenReadOnly)
        return EINVAL;
    if (*Name != '\\' && *Name != '/' && DefaultPath)
    {
        while (*DefaultPath && Length < RTL_NUMBER_OF(Path) - 2)
            Path[Length++] = (UCHAR)*DefaultPath++;
        if (*DefaultPath)
            return ENAMETOOLONG;
        if (Length && Path[Length - 1] != '\\')
            Path[Length++] = '\\';
    }
    while (*Name && Length < RTL_NUMBER_OF(Path) - 1)
    {
        Path[Length++] = *Name == '/' ? '\\' : (UCHAR)*Name;
        ++Name;
    }
    if (*Name)
        return ENAMETOOLONG;
    Path[Length] = 0;
    for (Slot = 0; Slot < MAX_FDS && Files[Slot]; ++Slot) { }
    if (Slot == MAX_FDS)
        return EMFILE;
    Status = BootDirectory->Open(BootDirectory, &Files[Slot], Path, EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(Status))
        return ArcStatus(Status);
    *FileId = Slot;
    return ESUCCESS;
}

ARC_STATUS ArcClose(ULONG Id)
{
    EFI_STATUS Status;
    if (Id >= MAX_FDS || !Files[Id]) return EBADF;
    Status = Files[Id]->Close(Files[Id]);
    Files[Id] = NULL;
    return ArcStatus(Status);
}

ARC_STATUS ArcRead(ULONG Id, VOID *Buffer, ULONG Length, ULONG *Count)
{
    UINTN Size = Length;
    EFI_STATUS Status;
    if (Id >= MAX_FDS || !Files[Id] || !Count) return EBADF;
    Status = Files[Id]->Read(Files[Id], &Size, Buffer);
    *Count = Size;
    return ArcStatus(Status);
}

ARC_STATUS ArcGetFileInformation(ULONG Id, FILEINFORMATION *Information)
{
    EFI_STATUS Status;
    EFI_FILE_INFO *Info;
    UINTN Size = 0;
    if (Id >= MAX_FDS || !Files[Id] || !Information) return EBADF;
    Status = Files[Id]->GetInfo(Files[Id], &FileInfoGuid, &Size, NULL);
    if (Status != EFI_BUFFER_TOO_SMALL) return ArcStatus(Status);
    Info = MmAllocateMemory(Size);
    if (!Info) return ENOMEM;
    Status = Files[Id]->GetInfo(Files[Id], &FileInfoGuid, &Size, Info);
    if (!EFI_ERROR(Status))
    {
        memset(Information, 0, sizeof(*Information));
        Information->EndingAddress.QuadPart = Info->FileSize;
    }
    MmFreeMemory(Info);
    return ArcStatus(Status);
}

BOOLEAN UefiFirmwareSetupSupported(VOID)
{
    UINT64 Supported = 0;
    UINTN Size = sizeof(Supported);
    return !EFI_ERROR(SystemTable->RuntimeServices->GetVariable(L"OsIndicationsSupported", &GlobalVariableGuid, NULL, &Size, &Supported)) &&
           (Supported & EFI_OS_INDICATIONS_BOOT_TO_FW_UI);
}

VOID UefiBootToFirmware(VOID)
{
    UINT64 Indications = 0;
    UINTN Size = sizeof(Indications);
    EFI_STATUS Status;
    if (!UefiFirmwareSetupSupported()) return;
    Status = SystemTable->RuntimeServices->GetVariable(L"OsIndications", &GlobalVariableGuid, NULL, &Size, &Indications);
    if (EFI_ERROR(Status) && Status != EFI_NOT_FOUND) return;
    Indications |= EFI_OS_INDICATIONS_BOOT_TO_FW_UI;
    Status = SystemTable->RuntimeServices->SetVariable(L"OsIndications", &GlobalVariableGuid, EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS, sizeof(Indications), &Indications);
    if (!EFI_ERROR(Status)) Reboot();
}

VOID Reboot(VOID)
{
    SystemTable->RuntimeServices->ResetSystem(EfiResetCold, EFI_SUCCESS, 0, NULL);
    for (;;) Idle();
}

VOID FrLdrCheckCpuCompatibility(VOID)
{
    /* Firmware entered RV64 code using the UEFI calling convention. The build
     * targets rv64gc with the integer (lp64) UEFI call ABI. */
}

ARC_STATUS LoadAndBootWindows(ULONG Argc, PCHAR Argv[], PCHAR Envp[])
{
    UiMessageBox("The RISC-V NT kernel handoff is not implemented.\nThis build supports the FreeLdr firmware boot manager.");
    return ENOEXEC;
}

ARC_STATUS LoadReactOSSetup(ULONG Argc, PCHAR Argv[], PCHAR Envp[])
{
    return LoadAndBootWindows(Argc, Argv, Envp);
}

UCHAR DriveMapGetBiosDriveNumber(PCSTR Name)
{
    /* Same numeric convention used by FreeLdr's ARC path editor. */
    return (UCHAR)strtoul(Name, NULL, 0);
}

ULONG DbgPrint(PCSTR Format, ...)
{
    CHAR Buffer[512];
    va_list Args;
    va_start(Args, Format);
    _vsnprintf(Buffer, sizeof(Buffer), Format, Args);
    va_end(Args);
    Buffer[sizeof(Buffer) - 1] = 0;
    for (PCSTR Text = Buffer; *Text; ++Text) PutChar(*Text);
    return 0;
}

VOID RtlAssert(PVOID Assertion, PVOID File, ULONG Line, PCHAR Message)
{
    DbgPrint("FreeLdr assertion: %s (%s:%u)\n", Assertion, File, Line);
    for (;;) Idle();
}

EFI_STATUS EFIAPI EfiEntry(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *Table)
{
    EFI_LOADED_IMAGE_PROTOCOL *Image;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *FileSystem;
    EFI_STATUS Status;
    SystemTable = Table;
    Status = Table->BootServices->HandleProtocol(ImageHandle, &LoadedImageGuid, (VOID **)&Image);
    if (EFI_ERROR(Status)) return Status;
    Status = Table->BootServices->HandleProtocol(Image->DeviceHandle, &FileSystemGuid, (VOID **)&FileSystem);
    if (EFI_ERROR(Status)) return Status;
    Status = FileSystem->OpenVolume(FileSystem, &BootDirectory);
    if (EFI_ERROR(Status)) return Status;
    Status = Table->ConOut->QueryMode(Table->ConOut, Table->ConOut->Mode->Mode, &Columns, &Rows);
    if (EFI_ERROR(Status) || Columns < 40 || Rows < 20 || Columns > 255 || Rows > 255)
    {
        BootDirectory->Close(BootDirectory);
        return EFI_UNSUPPORTED;
    }
    Status = Table->BootServices->CreateEvent(EVT_TIMER | EVT_NOTIFY_SIGNAL, TPL_CALLBACK, Tick, NULL, &ClockEvent);
    if (EFI_ERROR(Status)) goto Cleanup;
    Status = Table->BootServices->SetTimer(ClockEvent, TimerPeriodic, 10000000);
    if (EFI_ERROR(Status)) goto Cleanup;
    PreviousScreen = MmAllocateMemory(GetBufferSize());
    if (!PreviousScreen)
    {
        Status = EFI_OUT_OF_RESOURCES;
        goto Cleanup;
    }
    MachVtbl.ConsPutChar = PutChar;
    MachVtbl.ConsKbHit = KbHit;
    MachVtbl.ConsGetCh = GetCh;
    MachVtbl.VideoClearScreen = ClearScreen;
    MachVtbl.VideoSetDisplayMode = SetDisplayMode;
    MachVtbl.VideoGetDisplaySize = GetDisplaySize;
    MachVtbl.VideoGetBufferSize = GetBufferSize;
    MachVtbl.VideoSetTextCursorPosition = SetCursor;
    MachVtbl.VideoHideShowTextCursor = ShowCursor;
    MachVtbl.VideoPutChar = VideoPutChar;
    MachVtbl.VideoCopyOffScreenBufferToVRAM = CopyScreen;
    MachVtbl.VideoIsPaletteFixed = PaletteFixed;
    MachVtbl.VideoSync = SyncVideo;
    MachVtbl.Beep = Beep;
    MachVtbl.HwIdle = Idle;
    MachVtbl.GetTime = ArcGetTime;
    MachVtbl.GetRelativeTime = ArcGetRelativeTime;
    ClearScreen(7);
    LoadSettings(NULL);
    if (!UiInitialize(FALSE))
    {
        Status = EFI_DEVICE_ERROR;
        goto Cleanup;
    }
    RunLoader();
    Reboot();

Cleanup:
    MmFreeMemory(PreviousScreen);
    if (ClockEvent) Table->BootServices->CloseEvent(ClockEvent);
    BootDirectory->Close(BootDirectory);
    return Status;
}
