/*
 * PROJECT:     ReactOS Client/Server Runtime SubSystem
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     WoW64 CSR API message layouts
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "srv.h"

#include <wingdi.h>
#include <wincon.h>
#include <wincon_undoc.h>
#include <reactos/subsys/csr/csrwow64.h>
#include <reactos/subsys/win/basemsg.h>
#include <reactos/subsys/win/conmsg.h>
#include <reactos/subsys/win/winmsg.h>

#define NDEBUG
#include <debug.h>

typedef enum _CSR_WOW64_FIELD_TYPE
{
    CsrWow64Data,
    CsrWow64Handle,
    CsrWow64Pointer
} CSR_WOW64_FIELD_TYPE;

typedef struct _CSR_WOW64_FIELD
{
    USHORT Offset;
    USHORT Size;
    CSR_WOW64_FIELD_TYPE Type;
} CSR_WOW64_FIELD;

typedef struct _CSR_WOW64_MESSAGE
{
    const CSR_WOW64_FIELD *Fields;
    ULONG FieldCount;
    ULONG Size;
} CSR_WOW64_MESSAGE;

#define CSR_WOW64_DATA(Type, First, Last) \
    { FIELD_OFFSET(Type, First), FIELD_OFFSET(Type, Last) + RTL_FIELD_SIZE(Type, Last) - FIELD_OFFSET(Type, First), CsrWow64Data }
#define CSR_WOW64_HANDLE(Type, Field) { FIELD_OFFSET(Type, Field), sizeof(ULONG_PTR), CsrWow64Handle }
#define CSR_WOW64_POINTER(Type, Field) { FIELD_OFFSET(Type, Field), sizeof(ULONG_PTR), CsrWow64Pointer }
#define CSR_WOW64_MESSAGE(Type, Fields) { Fields, RTL_NUMBER_OF(Fields), sizeof(Type) }

static const CSR_WOW64_FIELD CsrpWow64OpenConsole[] =
{
    CSR_WOW64_HANDLE(CONSOLE_OPENCONSOLE, ConsoleHandle),
    CSR_WOW64_DATA(CONSOLE_OPENCONSOLE, HandleType, ShareMode),
    CSR_WOW64_HANDLE(CONSOLE_OPENCONSOLE, Handle),
};

static const CSR_WOW64_FIELD CsrpWow64GetConsoleInput[] =
{
    CSR_WOW64_HANDLE(CONSOLE_GETINPUT, ConsoleHandle),
    CSR_WOW64_HANDLE(CONSOLE_GETINPUT, InputHandle),
    CSR_WOW64_DATA(CONSOLE_GETINPUT, RecordStaticBuffer, RecordStaticBuffer),
    CSR_WOW64_POINTER(CONSOLE_GETINPUT, RecordBufPtr),
    CSR_WOW64_DATA(CONSOLE_GETINPUT, NumRecords, Unicode),
};

static const CSR_WOW64_FIELD CsrpWow64WriteConsoleInput[] =
{
    CSR_WOW64_HANDLE(CONSOLE_WRITEINPUT, ConsoleHandle),
    CSR_WOW64_HANDLE(CONSOLE_WRITEINPUT, InputHandle),
    CSR_WOW64_DATA(CONSOLE_WRITEINPUT, RecordStaticBuffer, RecordStaticBuffer),
    CSR_WOW64_POINTER(CONSOLE_WRITEINPUT, RecordBufPtr),
    CSR_WOW64_DATA(CONSOLE_WRITEINPUT, NumRecords, AppendToEnd),
};

static const CSR_WOW64_FIELD CsrpWow64ReadConsoleOutput[] =
{
    CSR_WOW64_HANDLE(CONSOLE_READOUTPUT, ConsoleHandle),
    CSR_WOW64_HANDLE(CONSOLE_READOUTPUT, OutputHandle),
    CSR_WOW64_DATA(CONSOLE_READOUTPUT, StaticBuffer, StaticBuffer),
    CSR_WOW64_POINTER(CONSOLE_READOUTPUT, CharInfo),
    CSR_WOW64_DATA(CONSOLE_READOUTPUT, ReadRegion, Unicode),
};

static const CSR_WOW64_FIELD CsrpWow64WriteConsoleOutput[] =
{
    CSR_WOW64_HANDLE(CONSOLE_WRITEOUTPUT, ConsoleHandle),
    CSR_WOW64_HANDLE(CONSOLE_WRITEOUTPUT, OutputHandle),
    CSR_WOW64_DATA(CONSOLE_WRITEOUTPUT, StaticBuffer, StaticBuffer),
    CSR_WOW64_POINTER(CONSOLE_WRITEOUTPUT, CharInfo),
    CSR_WOW64_DATA(CONSOLE_WRITEOUTPUT, WriteRegion, UseVirtualMemory),
};

static const CSR_WOW64_FIELD CsrpWow64ReadConsoleOutputString[] =
{
    CSR_WOW64_HANDLE(CONSOLE_READOUTPUTCODE, ConsoleHandle),
    CSR_WOW64_HANDLE(CONSOLE_READOUTPUTCODE, OutputHandle),
    CSR_WOW64_DATA(CONSOLE_READOUTPUTCODE, Coord, CodeStaticBuffer),
    CSR_WOW64_POINTER(CONSOLE_READOUTPUTCODE, pCode),
    CSR_WOW64_DATA(CONSOLE_READOUTPUTCODE, NumCodes, NumCodes),
};

static const CSR_WOW64_FIELD CsrpWow64WriteConsoleOutputString[] =
{
    CSR_WOW64_HANDLE(CONSOLE_WRITEOUTPUTCODE, ConsoleHandle),
    CSR_WOW64_HANDLE(CONSOLE_WRITEOUTPUTCODE, OutputHandle),
    CSR_WOW64_DATA(CONSOLE_WRITEOUTPUTCODE, Coord, CodeStaticBuffer),
    CSR_WOW64_POINTER(CONSOLE_WRITEOUTPUTCODE, pCode),
    CSR_WOW64_DATA(CONSOLE_WRITEOUTPUTCODE, NumCodes, NumCodes),
};

static const CSR_WOW64_FIELD CsrpWow64FillConsoleOutput[] =
{
    CSR_WOW64_HANDLE(CONSOLE_FILLOUTPUTCODE, ConsoleHandle),
    CSR_WOW64_HANDLE(CONSOLE_FILLOUTPUTCODE, OutputHandle),
    CSR_WOW64_DATA(CONSOLE_FILLOUTPUTCODE, WriteCoord, NumCodes),
};

static const CSR_WOW64_FIELD CsrpWow64GetMode[] =
{
    CSR_WOW64_HANDLE(CONSOLE_GETSETCONSOLEMODE, ConsoleHandle),
    CSR_WOW64_HANDLE(CONSOLE_GETSETCONSOLEMODE, Handle),
    CSR_WOW64_DATA(CONSOLE_GETSETCONSOLEMODE, Mode, Mode),
};

static const CSR_WOW64_FIELD CsrpWow64GetNumberOfFonts[] =
{
    CSR_WOW64_HANDLE(CONSOLE_GETNUMFONTS, ConsoleHandle),
    CSR_WOW64_DATA(CONSOLE_GETNUMFONTS, NumFonts, NumFonts),
};

static const CSR_WOW64_FIELD CsrpWow64GetNumberOfInputEvents[] =
{
    CSR_WOW64_HANDLE(CONSOLE_GETNUMINPUTEVENTS, ConsoleHandle),
    CSR_WOW64_HANDLE(CONSOLE_GETNUMINPUTEVENTS, InputHandle),
    CSR_WOW64_DATA(CONSOLE_GETNUMINPUTEVENTS, NumberOfEvents, NumberOfEvents),
};

static const CSR_WOW64_FIELD CsrpWow64GetScreenBufferInfo[] =
{
    CSR_WOW64_HANDLE(CONSOLE_GETSCREENBUFFERINFO, ConsoleHandle),
    CSR_WOW64_HANDLE(CONSOLE_GETSCREENBUFFERINFO, OutputHandle),
    CSR_WOW64_DATA(CONSOLE_GETSCREENBUFFERINFO, ScreenBufferSize, MaximumViewSize),
};

static const CSR_WOW64_FIELD CsrpWow64GetCursorInfo[] =
{
    CSR_WOW64_HANDLE(CONSOLE_GETSETCURSORINFO, ConsoleHandle),
    CSR_WOW64_HANDLE(CONSOLE_GETSETCURSORINFO, OutputHandle),
    CSR_WOW64_DATA(CONSOLE_GETSETCURSORINFO, Info, Info),
};

static const CSR_WOW64_FIELD CsrpWow64GetMouseInfo[] =
{
    CSR_WOW64_HANDLE(CONSOLE_GETMOUSEINFO, ConsoleHandle),
    CSR_WOW64_DATA(CONSOLE_GETMOUSEINFO, NumButtons, NumButtons),
};

static const CSR_WOW64_FIELD CsrpWow64GetFontInfo[] =
{
    CSR_WOW64_HANDLE(CONSOLE_GETFONTINFO, ConsoleHandle),
    CSR_WOW64_HANDLE(CONSOLE_GETFONTINFO, OutputHandle),
    CSR_WOW64_DATA(CONSOLE_GETFONTINFO, MaximumWindow, MaximumWindow),
    CSR_WOW64_POINTER(CONSOLE_GETFONTINFO, FontInfo),
    CSR_WOW64_DATA(CONSOLE_GETFONTINFO, NumFonts, NumFonts),
};

static const CSR_WOW64_FIELD CsrpWow64GetFontSize[] =
{
    CSR_WOW64_HANDLE(CONSOLE_GETFONTSIZE, ConsoleHandle),
    CSR_WOW64_HANDLE(CONSOLE_GETFONTSIZE, OutputHandle),
    CSR_WOW64_DATA(CONSOLE_GETFONTSIZE, FontIndex, FontSize),
};

static const CSR_WOW64_FIELD CsrpWow64GetCurrentFont[] =
{
    CSR_WOW64_HANDLE(CONSOLE_GETCURRENTFONT, ConsoleHandle),
    CSR_WOW64_HANDLE(CONSOLE_GETCURRENTFONT, OutputHandle),
    CSR_WOW64_DATA(CONSOLE_GETCURRENTFONT, MaximumWindow, FaceName),
};

static const CSR_WOW64_FIELD CsrpWow64SetActiveScreenBuffer[] =
{
    CSR_WOW64_HANDLE(CONSOLE_SETACTIVESCREENBUFFER, ConsoleHandle),
    CSR_WOW64_HANDLE(CONSOLE_SETACTIVESCREENBUFFER, OutputHandle),
};

static const CSR_WOW64_FIELD CsrpWow64FlushInputBuffer[] =
{
    CSR_WOW64_HANDLE(CONSOLE_FLUSHINPUTBUFFER, ConsoleHandle),
    CSR_WOW64_HANDLE(CONSOLE_FLUSHINPUTBUFFER, InputHandle),
};

static const CSR_WOW64_FIELD CsrpWow64GetLargestWindowSize[] =
{
    CSR_WOW64_HANDLE(CONSOLE_GETLARGESTWINDOWSIZE, ConsoleHandle),
    CSR_WOW64_HANDLE(CONSOLE_GETLARGESTWINDOWSIZE, OutputHandle),
    CSR_WOW64_DATA(CONSOLE_GETLARGESTWINDOWSIZE, Size, Size),
};

static const CSR_WOW64_FIELD CsrpWow64SetScreenBufferSize[] =
{
    CSR_WOW64_HANDLE(CONSOLE_SETSCREENBUFFERSIZE, ConsoleHandle),
    CSR_WOW64_HANDLE(CONSOLE_SETSCREENBUFFERSIZE, OutputHandle),
    CSR_WOW64_DATA(CONSOLE_SETSCREENBUFFERSIZE, Size, Size),
};

static const CSR_WOW64_FIELD CsrpWow64SetCursorPosition[] =
{
    CSR_WOW64_HANDLE(CONSOLE_SETCURSORPOSITION, ConsoleHandle),
    CSR_WOW64_HANDLE(CONSOLE_SETCURSORPOSITION, OutputHandle),
    CSR_WOW64_DATA(CONSOLE_SETCURSORPOSITION, Position, Position),
};

static const CSR_WOW64_FIELD CsrpWow64SetWindowInfo[] =
{
    CSR_WOW64_HANDLE(CONSOLE_SETWINDOWINFO, ConsoleHandle),
    CSR_WOW64_HANDLE(CONSOLE_SETWINDOWINFO, OutputHandle),
    CSR_WOW64_DATA(CONSOLE_SETWINDOWINFO, Absolute, WindowRect),
};

static const CSR_WOW64_FIELD CsrpWow64ScrollScreenBuffer[] =
{
    CSR_WOW64_HANDLE(CONSOLE_SCROLLSCREENBUFFER, ConsoleHandle),
    CSR_WOW64_HANDLE(CONSOLE_SCROLLSCREENBUFFER, OutputHandle),
    CSR_WOW64_DATA(CONSOLE_SCROLLSCREENBUFFER, ScrollRectangle, Unicode),
};

static const CSR_WOW64_FIELD CsrpWow64SetTextAttribute[] =
{
    CSR_WOW64_HANDLE(CONSOLE_SETTEXTATTRIB, ConsoleHandle),
    CSR_WOW64_HANDLE(CONSOLE_SETTEXTATTRIB, OutputHandle),
    CSR_WOW64_DATA(CONSOLE_SETTEXTATTRIB, Attributes, Attributes),
};

static const CSR_WOW64_FIELD CsrpWow64SetFont[] =
{
    CSR_WOW64_HANDLE(CONSOLE_SETFONT, ConsoleHandle),
    CSR_WOW64_HANDLE(CONSOLE_SETFONT, OutputHandle),
    CSR_WOW64_DATA(CONSOLE_SETFONT, FontIndex, FontIndex),
};

static const CSR_WOW64_FIELD CsrpWow64SetIcon[] =
{
    CSR_WOW64_HANDLE(CONSOLE_SETICON, ConsoleHandle),
    CSR_WOW64_HANDLE(CONSOLE_SETICON, IconHandle),
};

static const CSR_WOW64_FIELD CsrpWow64ReadConsole[] =
{
    CSR_WOW64_HANDLE(CONSOLE_READCONSOLE, ConsoleHandle),
    CSR_WOW64_HANDLE(CONSOLE_READCONSOLE, InputHandle),
    CSR_WOW64_DATA(CONSOLE_READCONSOLE, ExeLength, StaticBuffer),
    CSR_WOW64_POINTER(CONSOLE_READCONSOLE, Buffer),
    CSR_WOW64_DATA(CONSOLE_READCONSOLE, NumBytes, Unicode),
};

static const CSR_WOW64_FIELD CsrpWow64WriteConsole[] =
{
    CSR_WOW64_HANDLE(CONSOLE_WRITECONSOLE, ConsoleHandle),
    CSR_WOW64_HANDLE(CONSOLE_WRITECONSOLE, OutputHandle),
    CSR_WOW64_DATA(CONSOLE_WRITECONSOLE, StaticBuffer, StaticBuffer),
    CSR_WOW64_POINTER(CONSOLE_WRITECONSOLE, Buffer),
    CSR_WOW64_DATA(CONSOLE_WRITECONSOLE, NumBytes, Reserved2),
};

static const CSR_WOW64_FIELD CsrpWow64DuplicateHandle[] =
{
    CSR_WOW64_HANDLE(CONSOLE_DUPLICATEHANDLE, ConsoleHandle),
    CSR_WOW64_HANDLE(CONSOLE_DUPLICATEHANDLE, SourceHandle),
    CSR_WOW64_DATA(CONSOLE_DUPLICATEHANDLE, DesiredAccess, Options),
    CSR_WOW64_HANDLE(CONSOLE_DUPLICATEHANDLE, TargetHandle),
};

static const CSR_WOW64_FIELD CsrpWow64GetHandleInformation[] =
{
    CSR_WOW64_HANDLE(CONSOLE_GETHANDLEINFO, ConsoleHandle),
    CSR_WOW64_HANDLE(CONSOLE_GETHANDLEINFO, Handle),
    CSR_WOW64_DATA(CONSOLE_GETHANDLEINFO, Flags, Flags),
};

static const CSR_WOW64_FIELD CsrpWow64SetHandleInformation[] =
{
    CSR_WOW64_HANDLE(CONSOLE_SETHANDLEINFO, ConsoleHandle),
    CSR_WOW64_HANDLE(CONSOLE_SETHANDLEINFO, Handle),
    CSR_WOW64_DATA(CONSOLE_SETHANDLEINFO, Mask, Flags),
};

static const CSR_WOW64_FIELD CsrpWow64CloseHandle[] =
{
    CSR_WOW64_HANDLE(CONSOLE_CLOSEHANDLE, ConsoleHandle),
    CSR_WOW64_HANDLE(CONSOLE_CLOSEHANDLE, Handle),
};

static const CSR_WOW64_FIELD CsrpWow64VerifyIoHandle[] =
{
    CSR_WOW64_DATA(CONSOLE_VERIFYHANDLE, IsValid, IsValid),
    CSR_WOW64_HANDLE(CONSOLE_VERIFYHANDLE, ConsoleHandle),
    CSR_WOW64_HANDLE(CONSOLE_VERIFYHANDLE, Handle),
};

static const CSR_WOW64_FIELD CsrpWow64Alloc[] =
{
    CSR_WOW64_POINTER(CONSOLE_ALLOCCONSOLE, ConsoleStartInfo),
    CSR_WOW64_DATA(CONSOLE_ALLOCCONSOLE, TitleLength, TitleLength),
    CSR_WOW64_POINTER(CONSOLE_ALLOCCONSOLE, ConsoleTitle),
    CSR_WOW64_DATA(CONSOLE_ALLOCCONSOLE, DesktopLength, DesktopLength),
    CSR_WOW64_POINTER(CONSOLE_ALLOCCONSOLE, Desktop),
    CSR_WOW64_DATA(CONSOLE_ALLOCCONSOLE, AppNameLength, AppNameLength),
    CSR_WOW64_POINTER(CONSOLE_ALLOCCONSOLE, AppName),
    CSR_WOW64_DATA(CONSOLE_ALLOCCONSOLE, CurDirLength, CurDirLength),
    CSR_WOW64_POINTER(CONSOLE_ALLOCCONSOLE, CurDir),
    CSR_WOW64_POINTER(CONSOLE_ALLOCCONSOLE, CtrlRoutine),
    CSR_WOW64_POINTER(CONSOLE_ALLOCCONSOLE, PropRoutine),
};

static const CSR_WOW64_FIELD CsrpWow64Free[] =
{
    CSR_WOW64_HANDLE(CONSOLE_FREECONSOLE, ConsoleHandle),
};

static const CSR_WOW64_FIELD CsrpWow64GetTitle[] =
{
    CSR_WOW64_HANDLE(CONSOLE_GETSETCONSOLETITLE, ConsoleHandle),
    CSR_WOW64_DATA(CONSOLE_GETSETCONSOLETITLE, Length, Length),
    CSR_WOW64_POINTER(CONSOLE_GETSETCONSOLETITLE, Title),
    CSR_WOW64_DATA(CONSOLE_GETSETCONSOLETITLE, Unicode, Unicode),
};

static const CSR_WOW64_FIELD CsrpWow64CreateScreenBuffer[] =
{
    CSR_WOW64_HANDLE(CONSOLE_CREATESCREENBUFFER, ConsoleHandle),
    CSR_WOW64_DATA(CONSOLE_CREATESCREENBUFFER, DesiredAccess, GraphicsBufferInfo.dwBitMapInfoLength),
    CSR_WOW64_POINTER(CONSOLE_CREATESCREENBUFFER, GraphicsBufferInfo.lpBitMapInfo),
    CSR_WOW64_DATA(CONSOLE_CREATESCREENBUFFER, GraphicsBufferInfo.dwUsage, GraphicsBufferInfo.dwUsage),
    CSR_WOW64_HANDLE(CONSOLE_CREATESCREENBUFFER, GraphicsBufferInfo.hMutex),
    CSR_WOW64_POINTER(CONSOLE_CREATESCREENBUFFER, GraphicsBufferInfo.lpBitMap),
    CSR_WOW64_HANDLE(CONSOLE_CREATESCREENBUFFER, hMutex),
    CSR_WOW64_POINTER(CONSOLE_CREATESCREENBUFFER, lpBitMap),
    CSR_WOW64_HANDLE(CONSOLE_CREATESCREENBUFFER, OutputHandle),
};

static const CSR_WOW64_FIELD CsrpWow64InvalidateBitMapRect[] =
{
    CSR_WOW64_HANDLE(CONSOLE_INVALIDATEDIBITS, ConsoleHandle),
    CSR_WOW64_HANDLE(CONSOLE_INVALIDATEDIBITS, OutputHandle),
    CSR_WOW64_DATA(CONSOLE_INVALIDATEDIBITS, Region, Region),
};

static const CSR_WOW64_FIELD CsrpWow64SetCursor[] =
{
    CSR_WOW64_HANDLE(CONSOLE_SETCURSOR, ConsoleHandle),
    CSR_WOW64_HANDLE(CONSOLE_SETCURSOR, OutputHandle),
    CSR_WOW64_HANDLE(CONSOLE_SETCURSOR, CursorHandle),
};

static const CSR_WOW64_FIELD CsrpWow64ShowCursor[] =
{
    CSR_WOW64_HANDLE(CONSOLE_SHOWCURSOR, ConsoleHandle),
    CSR_WOW64_HANDLE(CONSOLE_SHOWCURSOR, OutputHandle),
    CSR_WOW64_DATA(CONSOLE_SHOWCURSOR, Show, RefCount),
};

static const CSR_WOW64_FIELD CsrpWow64MenuControl[] =
{
    CSR_WOW64_HANDLE(CONSOLE_MENUCONTROL, ConsoleHandle),
    CSR_WOW64_HANDLE(CONSOLE_MENUCONTROL, OutputHandle),
    CSR_WOW64_DATA(CONSOLE_MENUCONTROL, CmdIdLow, CmdIdHigh),
    CSR_WOW64_HANDLE(CONSOLE_MENUCONTROL, MenuHandle),
};

static const CSR_WOW64_FIELD CsrpWow64SetPalette[] =
{
    CSR_WOW64_HANDLE(CONSOLE_SETPALETTE, ConsoleHandle),
    CSR_WOW64_HANDLE(CONSOLE_SETPALETTE, OutputHandle),
    CSR_WOW64_HANDLE(CONSOLE_SETPALETTE, PaletteHandle),
    CSR_WOW64_DATA(CONSOLE_SETPALETTE, Usage, Usage),
};

static const CSR_WOW64_FIELD CsrpWow64SetDisplayMode[] =
{
    CSR_WOW64_HANDLE(CONSOLE_SETDISPLAYMODE, ConsoleHandle),
    CSR_WOW64_HANDLE(CONSOLE_SETDISPLAYMODE, OutputHandle),
    CSR_WOW64_DATA(CONSOLE_SETDISPLAYMODE, DisplayMode, NewSBDim),
    CSR_WOW64_HANDLE(CONSOLE_SETDISPLAYMODE, EventHandle),
};

static const CSR_WOW64_FIELD CsrpWow64RegisterVDM[] =
{
    CSR_WOW64_HANDLE(CONSOLE_REGISTERVDM, ConsoleHandle),
    CSR_WOW64_DATA(CONSOLE_REGISTERVDM, RegisterFlags, RegisterFlags),
    CSR_WOW64_HANDLE(CONSOLE_REGISTERVDM, StartHardwareEvent),
    CSR_WOW64_HANDLE(CONSOLE_REGISTERVDM, EndHardwareEvent),
    CSR_WOW64_HANDLE(CONSOLE_REGISTERVDM, ErrorHardwareEvent),
    CSR_WOW64_DATA(CONSOLE_REGISTERVDM, UnusedVar, VideoStateLength),
    CSR_WOW64_POINTER(CONSOLE_REGISTERVDM, VideoState),
    CSR_WOW64_POINTER(CONSOLE_REGISTERVDM, UnusedBuffer),
    CSR_WOW64_DATA(CONSOLE_REGISTERVDM, UnusedBufferLength, VDMBufferSize),
    CSR_WOW64_POINTER(CONSOLE_REGISTERVDM, VDMBuffer),
};

static const CSR_WOW64_FIELD CsrpWow64GetHardwareState[] =
{
    CSR_WOW64_HANDLE(CONSOLE_GETSETHWSTATE, ConsoleHandle),
    CSR_WOW64_HANDLE(CONSOLE_GETSETHWSTATE, OutputHandle),
    CSR_WOW64_DATA(CONSOLE_GETSETHWSTATE, Flags, State),
};

static const CSR_WOW64_FIELD CsrpWow64GetDisplayMode[] =
{
    CSR_WOW64_HANDLE(CONSOLE_GETDISPLAYMODE, ConsoleHandle),
    CSR_WOW64_DATA(CONSOLE_GETDISPLAYMODE, DisplayMode, DisplayMode),
};

static const CSR_WOW64_FIELD CsrpWow64AddAlias[] =
{
    CSR_WOW64_HANDLE(CONSOLE_ADDGETALIAS, ConsoleHandle),
    CSR_WOW64_DATA(CONSOLE_ADDGETALIAS, SourceLength, ExeLength),
    CSR_WOW64_POINTER(CONSOLE_ADDGETALIAS, Source),
    CSR_WOW64_POINTER(CONSOLE_ADDGETALIAS, Target),
    CSR_WOW64_POINTER(CONSOLE_ADDGETALIAS, ExeName),
    CSR_WOW64_DATA(CONSOLE_ADDGETALIAS, Unicode, Unicode2),
};

static const CSR_WOW64_FIELD CsrpWow64GetAliasesLength[] =
{
    CSR_WOW64_HANDLE(CONSOLE_GETALLALIASESLENGTH, ConsoleHandle),
    CSR_WOW64_DATA(CONSOLE_GETALLALIASESLENGTH, ExeLength, ExeLength),
    CSR_WOW64_POINTER(CONSOLE_GETALLALIASESLENGTH, ExeName),
    CSR_WOW64_DATA(CONSOLE_GETALLALIASESLENGTH, Length, Unicode2),
};

static const CSR_WOW64_FIELD CsrpWow64GetAliasExesLength[] =
{
    CSR_WOW64_HANDLE(CONSOLE_GETALIASESEXESLENGTH, ConsoleHandle),
    CSR_WOW64_DATA(CONSOLE_GETALIASESEXESLENGTH, Length, Unicode),
};

static const CSR_WOW64_FIELD CsrpWow64GetAliases[] =
{
    CSR_WOW64_HANDLE(CONSOLE_GETALLALIASES, ConsoleHandle),
    CSR_WOW64_DATA(CONSOLE_GETALLALIASES, ExeLength, ExeLength),
    CSR_WOW64_POINTER(CONSOLE_GETALLALIASES, ExeName),
    CSR_WOW64_DATA(CONSOLE_GETALLALIASES, Unicode, AliasesBufferLength),
    CSR_WOW64_POINTER(CONSOLE_GETALLALIASES, AliasesBuffer),
};

static const CSR_WOW64_FIELD CsrpWow64GetAliasExes[] =
{
    CSR_WOW64_HANDLE(CONSOLE_GETALIASESEXES, ConsoleHandle),
    CSR_WOW64_DATA(CONSOLE_GETALIASESEXES, Length, Length),
    CSR_WOW64_POINTER(CONSOLE_GETALIASESEXES, ExeNames),
    CSR_WOW64_DATA(CONSOLE_GETALIASESEXES, Unicode, Unicode),
};

static const CSR_WOW64_FIELD CsrpWow64ExpungeCommandHistory[] =
{
    CSR_WOW64_HANDLE(CONSOLE_EXPUNGECOMMANDHISTORY, ConsoleHandle),
    CSR_WOW64_DATA(CONSOLE_EXPUNGECOMMANDHISTORY, ExeLength, ExeLength),
    CSR_WOW64_POINTER(CONSOLE_EXPUNGECOMMANDHISTORY, ExeName),
    CSR_WOW64_DATA(CONSOLE_EXPUNGECOMMANDHISTORY, Unicode, Unicode2),
};

static const CSR_WOW64_FIELD CsrpWow64SetNumberOfCommands[] =
{
    CSR_WOW64_HANDLE(CONSOLE_SETHISTORYNUMBERCOMMANDS, ConsoleHandle),
    CSR_WOW64_DATA(CONSOLE_SETHISTORYNUMBERCOMMANDS, NumCommands, ExeLength),
    CSR_WOW64_POINTER(CONSOLE_SETHISTORYNUMBERCOMMANDS, ExeName),
    CSR_WOW64_DATA(CONSOLE_SETHISTORYNUMBERCOMMANDS, Unicode, Unicode2),
};

static const CSR_WOW64_FIELD CsrpWow64GetCommandHistoryLength[] =
{
    CSR_WOW64_HANDLE(CONSOLE_GETCOMMANDHISTORYLENGTH, ConsoleHandle),
    CSR_WOW64_DATA(CONSOLE_GETCOMMANDHISTORYLENGTH, HistoryLength, ExeLength),
    CSR_WOW64_POINTER(CONSOLE_GETCOMMANDHISTORYLENGTH, ExeName),
    CSR_WOW64_DATA(CONSOLE_GETCOMMANDHISTORYLENGTH, Unicode, Unicode2),
};

static const CSR_WOW64_FIELD CsrpWow64GetCommandHistory[] =
{
    CSR_WOW64_HANDLE(CONSOLE_GETCOMMANDHISTORY, ConsoleHandle),
    CSR_WOW64_DATA(CONSOLE_GETCOMMANDHISTORY, HistoryLength, HistoryLength),
    CSR_WOW64_POINTER(CONSOLE_GETCOMMANDHISTORY, History),
    CSR_WOW64_DATA(CONSOLE_GETCOMMANDHISTORY, ExeLength, ExeLength),
    CSR_WOW64_POINTER(CONSOLE_GETCOMMANDHISTORY, ExeName),
    CSR_WOW64_DATA(CONSOLE_GETCOMMANDHISTORY, Unicode, Unicode2),
};

static const CSR_WOW64_FIELD CsrpWow64SetCommandHistoryMode[] =
{
    CSR_WOW64_HANDLE(CONSOLE_SETHISTORYMODE, ConsoleHandle),
    CSR_WOW64_DATA(CONSOLE_SETHISTORYMODE, Mode, Mode),
};

static const CSR_WOW64_FIELD CsrpWow64GetCP[] =
{
    CSR_WOW64_HANDLE(CONSOLE_GETINPUTOUTPUTCP, ConsoleHandle),
    CSR_WOW64_DATA(CONSOLE_GETINPUTOUTPUTCP, CodePage, OutputCP),
};

static const CSR_WOW64_FIELD CsrpWow64SetCP[] =
{
    CSR_WOW64_HANDLE(CONSOLE_SETINPUTOUTPUTCP, ConsoleHandle),
    CSR_WOW64_DATA(CONSOLE_SETINPUTOUTPUTCP, CodePage, OutputCP),
    CSR_WOW64_HANDLE(CONSOLE_SETINPUTOUTPUTCP, EventHandle),
};

static const CSR_WOW64_FIELD CsrpWow64SetMenuClose[] =
{
    CSR_WOW64_HANDLE(CONSOLE_SETMENUCLOSE, ConsoleHandle),
    CSR_WOW64_DATA(CONSOLE_SETMENUCLOSE, Enable, Enable),
};

static const CSR_WOW64_FIELD CsrpWow64NotifyLastClose[] =
{
    CSR_WOW64_HANDLE(CONSOLE_NOTIFYLASTCLOSE, ConsoleHandle),
};

static const CSR_WOW64_FIELD CsrpWow64GenerateCtrlEvent[] =
{
    CSR_WOW64_HANDLE(CONSOLE_GENERATECTRLEVENT, ConsoleHandle),
    CSR_WOW64_DATA(CONSOLE_GENERATECTRLEVENT, CtrlEvent, ProcessGroupId),
};

static const CSR_WOW64_FIELD CsrpWow64GetKeyboardLayoutName[] =
{
    CSR_WOW64_HANDLE(CONSOLE_GETKBDLAYOUTNAME, ConsoleHandle),
    CSR_WOW64_DATA(CONSOLE_GETKBDLAYOUTNAME, LayoutBuffer, Ansi),
};

static const CSR_WOW64_FIELD CsrpWow64GetConsoleWindow[] =
{
    CSR_WOW64_HANDLE(CONSOLE_GETWINDOW, ConsoleHandle),
    CSR_WOW64_HANDLE(CONSOLE_GETWINDOW, WindowHandle),
};

static const CSR_WOW64_FIELD CsrpWow64GetLangId[] =
{
    CSR_WOW64_HANDLE(CONSOLE_GETLANGID, ConsoleHandle),
    CSR_WOW64_DATA(CONSOLE_GETLANGID, LangId, LangId),
};

static const CSR_WOW64_FIELD CsrpWow64Attach[] =
{
    CSR_WOW64_DATA(CONSOLE_ATTACHCONSOLE, ProcessId, ProcessId),
    CSR_WOW64_POINTER(CONSOLE_ATTACHCONSOLE, ConsoleStartInfo),
    CSR_WOW64_POINTER(CONSOLE_ATTACHCONSOLE, CtrlRoutine),
    CSR_WOW64_POINTER(CONSOLE_ATTACHCONSOLE, PropRoutine),
};

static const CSR_WOW64_FIELD CsrpWow64GetSelectionInfo[] =
{
    CSR_WOW64_HANDLE(CONSOLE_GETSELECTIONINFO, ConsoleHandle),
    CSR_WOW64_DATA(CONSOLE_GETSELECTIONINFO, Info, Info),
};

static const CSR_WOW64_FIELD CsrpWow64GetProcessList[] =
{
    CSR_WOW64_HANDLE(CONSOLE_GETPROCESSLIST, ConsoleHandle),
    CSR_WOW64_DATA(CONSOLE_GETPROCESSLIST, ProcessCount, ProcessCount),
    CSR_WOW64_POINTER(CONSOLE_GETPROCESSLIST, ProcessIdsList),
};

static const CSR_WOW64_FIELD CsrpWow64GetHistory[] =
{
    CSR_WOW64_DATA(CONSOLE_GETSETHISTORYINFO, HistoryBufferSize, dwFlags),
};

static const CSR_WOW64_FIELD CsrpWow64GetScreenBufferInfoEx[] =
{
    CSR_WOW64_HANDLE(CONSOLE_GETSCREENBUFFERINFOEX, ConsoleHandle),
    CSR_WOW64_HANDLE(CONSOLE_GETSCREENBUFFERINFOEX, OutputHandle),
    CSR_WOW64_DATA(CONSOLE_GETSCREENBUFFERINFOEX, ScreenBufferSize, ColorTable),
};

static const CSR_WOW64_MESSAGE CsrpWow64ConsoleMessages[ConsolepMaxApiNumber - CONSRV_FIRST_API_NUMBER] =
{
    [ConsolepOpenConsole - CONSRV_FIRST_API_NUMBER] = CSR_WOW64_MESSAGE(CONSOLE_OPENCONSOLE, CsrpWow64OpenConsole),
    [ConsolepGetConsoleInput - CONSRV_FIRST_API_NUMBER] = CSR_WOW64_MESSAGE(CONSOLE_GETINPUT, CsrpWow64GetConsoleInput),
    [ConsolepWriteConsoleInput - CONSRV_FIRST_API_NUMBER] = CSR_WOW64_MESSAGE(CONSOLE_WRITEINPUT, CsrpWow64WriteConsoleInput),
    [ConsolepReadConsoleOutput - CONSRV_FIRST_API_NUMBER] = CSR_WOW64_MESSAGE(CONSOLE_READOUTPUT, CsrpWow64ReadConsoleOutput),
    [ConsolepWriteConsoleOutput - CONSRV_FIRST_API_NUMBER] = CSR_WOW64_MESSAGE(CONSOLE_WRITEOUTPUT, CsrpWow64WriteConsoleOutput),
    [ConsolepReadConsoleOutputString - CONSRV_FIRST_API_NUMBER] = CSR_WOW64_MESSAGE(CONSOLE_READOUTPUTCODE, CsrpWow64ReadConsoleOutputString),
    [ConsolepWriteConsoleOutputString - CONSRV_FIRST_API_NUMBER] = CSR_WOW64_MESSAGE(CONSOLE_WRITEOUTPUTCODE, CsrpWow64WriteConsoleOutputString),
    [ConsolepFillConsoleOutput - CONSRV_FIRST_API_NUMBER] = CSR_WOW64_MESSAGE(CONSOLE_FILLOUTPUTCODE, CsrpWow64FillConsoleOutput),
    [ConsolepGetMode - CONSRV_FIRST_API_NUMBER] = CSR_WOW64_MESSAGE(CONSOLE_GETSETCONSOLEMODE, CsrpWow64GetMode),
    [ConsolepGetNumberOfFonts - CONSRV_FIRST_API_NUMBER] = CSR_WOW64_MESSAGE(CONSOLE_GETNUMFONTS, CsrpWow64GetNumberOfFonts),
    [ConsolepGetNumberOfInputEvents - CONSRV_FIRST_API_NUMBER] = CSR_WOW64_MESSAGE(CONSOLE_GETNUMINPUTEVENTS, CsrpWow64GetNumberOfInputEvents),
    [ConsolepGetScreenBufferInfo - CONSRV_FIRST_API_NUMBER] = CSR_WOW64_MESSAGE(CONSOLE_GETSCREENBUFFERINFO, CsrpWow64GetScreenBufferInfo),
    [ConsolepGetCursorInfo - CONSRV_FIRST_API_NUMBER] = CSR_WOW64_MESSAGE(CONSOLE_GETSETCURSORINFO, CsrpWow64GetCursorInfo),
    [ConsolepGetMouseInfo - CONSRV_FIRST_API_NUMBER] = CSR_WOW64_MESSAGE(CONSOLE_GETMOUSEINFO, CsrpWow64GetMouseInfo),
    [ConsolepGetFontInfo - CONSRV_FIRST_API_NUMBER] = CSR_WOW64_MESSAGE(CONSOLE_GETFONTINFO, CsrpWow64GetFontInfo),
    [ConsolepGetFontSize - CONSRV_FIRST_API_NUMBER] = CSR_WOW64_MESSAGE(CONSOLE_GETFONTSIZE, CsrpWow64GetFontSize),
    [ConsolepGetCurrentFont - CONSRV_FIRST_API_NUMBER] = CSR_WOW64_MESSAGE(CONSOLE_GETCURRENTFONT, CsrpWow64GetCurrentFont),
    [ConsolepSetMode - CONSRV_FIRST_API_NUMBER] = CSR_WOW64_MESSAGE(CONSOLE_GETSETCONSOLEMODE, CsrpWow64GetMode),
    [ConsolepSetActiveScreenBuffer - CONSRV_FIRST_API_NUMBER] = CSR_WOW64_MESSAGE(CONSOLE_SETACTIVESCREENBUFFER, CsrpWow64SetActiveScreenBuffer),
    [ConsolepFlushInputBuffer - CONSRV_FIRST_API_NUMBER] = CSR_WOW64_MESSAGE(CONSOLE_FLUSHINPUTBUFFER, CsrpWow64FlushInputBuffer),
    [ConsolepGetLargestWindowSize - CONSRV_FIRST_API_NUMBER] = CSR_WOW64_MESSAGE(CONSOLE_GETLARGESTWINDOWSIZE, CsrpWow64GetLargestWindowSize),
    [ConsolepSetScreenBufferSize - CONSRV_FIRST_API_NUMBER] = CSR_WOW64_MESSAGE(CONSOLE_SETSCREENBUFFERSIZE, CsrpWow64SetScreenBufferSize),
    [ConsolepSetCursorPosition - CONSRV_FIRST_API_NUMBER] = CSR_WOW64_MESSAGE(CONSOLE_SETCURSORPOSITION, CsrpWow64SetCursorPosition),
    [ConsolepSetCursorInfo - CONSRV_FIRST_API_NUMBER] = CSR_WOW64_MESSAGE(CONSOLE_GETSETCURSORINFO, CsrpWow64GetCursorInfo),
    [ConsolepSetWindowInfo - CONSRV_FIRST_API_NUMBER] = CSR_WOW64_MESSAGE(CONSOLE_SETWINDOWINFO, CsrpWow64SetWindowInfo),
    [ConsolepScrollScreenBuffer - CONSRV_FIRST_API_NUMBER] = CSR_WOW64_MESSAGE(CONSOLE_SCROLLSCREENBUFFER, CsrpWow64ScrollScreenBuffer),
    [ConsolepSetTextAttribute - CONSRV_FIRST_API_NUMBER] = CSR_WOW64_MESSAGE(CONSOLE_SETTEXTATTRIB, CsrpWow64SetTextAttribute),
    [ConsolepSetFont - CONSRV_FIRST_API_NUMBER] = CSR_WOW64_MESSAGE(CONSOLE_SETFONT, CsrpWow64SetFont),
    [ConsolepSetIcon - CONSRV_FIRST_API_NUMBER] = CSR_WOW64_MESSAGE(CONSOLE_SETICON, CsrpWow64SetIcon),
    [ConsolepReadConsole - CONSRV_FIRST_API_NUMBER] = CSR_WOW64_MESSAGE(CONSOLE_READCONSOLE, CsrpWow64ReadConsole),
    [ConsolepWriteConsole - CONSRV_FIRST_API_NUMBER] = CSR_WOW64_MESSAGE(CONSOLE_WRITECONSOLE, CsrpWow64WriteConsole),
    [ConsolepDuplicateHandle - CONSRV_FIRST_API_NUMBER] = CSR_WOW64_MESSAGE(CONSOLE_DUPLICATEHANDLE, CsrpWow64DuplicateHandle),
    [ConsolepGetHandleInformation - CONSRV_FIRST_API_NUMBER] = CSR_WOW64_MESSAGE(CONSOLE_GETHANDLEINFO, CsrpWow64GetHandleInformation),
    [ConsolepSetHandleInformation - CONSRV_FIRST_API_NUMBER] = CSR_WOW64_MESSAGE(CONSOLE_SETHANDLEINFO, CsrpWow64SetHandleInformation),
    [ConsolepCloseHandle - CONSRV_FIRST_API_NUMBER] = CSR_WOW64_MESSAGE(CONSOLE_CLOSEHANDLE, CsrpWow64CloseHandle),
    [ConsolepVerifyIoHandle - CONSRV_FIRST_API_NUMBER] = CSR_WOW64_MESSAGE(CONSOLE_VERIFYHANDLE, CsrpWow64VerifyIoHandle),
    [ConsolepAlloc - CONSRV_FIRST_API_NUMBER] = CSR_WOW64_MESSAGE(CONSOLE_ALLOCCONSOLE, CsrpWow64Alloc),
    [ConsolepFree - CONSRV_FIRST_API_NUMBER] = CSR_WOW64_MESSAGE(CONSOLE_FREECONSOLE, CsrpWow64Free),
    [ConsolepGetTitle - CONSRV_FIRST_API_NUMBER] = CSR_WOW64_MESSAGE(CONSOLE_GETSETCONSOLETITLE, CsrpWow64GetTitle),
    [ConsolepSetTitle - CONSRV_FIRST_API_NUMBER] = CSR_WOW64_MESSAGE(CONSOLE_GETSETCONSOLETITLE, CsrpWow64GetTitle),
    [ConsolepCreateScreenBuffer - CONSRV_FIRST_API_NUMBER] = CSR_WOW64_MESSAGE(CONSOLE_CREATESCREENBUFFER, CsrpWow64CreateScreenBuffer),
    [ConsolepInvalidateBitMapRect - CONSRV_FIRST_API_NUMBER] = CSR_WOW64_MESSAGE(CONSOLE_INVALIDATEDIBITS, CsrpWow64InvalidateBitMapRect),
    [ConsolepSetCursor - CONSRV_FIRST_API_NUMBER] = CSR_WOW64_MESSAGE(CONSOLE_SETCURSOR, CsrpWow64SetCursor),
    [ConsolepShowCursor - CONSRV_FIRST_API_NUMBER] = CSR_WOW64_MESSAGE(CONSOLE_SHOWCURSOR, CsrpWow64ShowCursor),
    [ConsolepMenuControl - CONSRV_FIRST_API_NUMBER] = CSR_WOW64_MESSAGE(CONSOLE_MENUCONTROL, CsrpWow64MenuControl),
    [ConsolepSetPalette - CONSRV_FIRST_API_NUMBER] = CSR_WOW64_MESSAGE(CONSOLE_SETPALETTE, CsrpWow64SetPalette),
    [ConsolepSetDisplayMode - CONSRV_FIRST_API_NUMBER] = CSR_WOW64_MESSAGE(CONSOLE_SETDISPLAYMODE, CsrpWow64SetDisplayMode),
    [ConsolepRegisterVDM - CONSRV_FIRST_API_NUMBER] = CSR_WOW64_MESSAGE(CONSOLE_REGISTERVDM, CsrpWow64RegisterVDM),
    [ConsolepGetHardwareState - CONSRV_FIRST_API_NUMBER] = CSR_WOW64_MESSAGE(CONSOLE_GETSETHWSTATE, CsrpWow64GetHardwareState),
    [ConsolepSetHardwareState - CONSRV_FIRST_API_NUMBER] = CSR_WOW64_MESSAGE(CONSOLE_GETSETHWSTATE, CsrpWow64GetHardwareState),
    [ConsolepGetDisplayMode - CONSRV_FIRST_API_NUMBER] = CSR_WOW64_MESSAGE(CONSOLE_GETDISPLAYMODE, CsrpWow64GetDisplayMode),
    [ConsolepAddAlias - CONSRV_FIRST_API_NUMBER] = CSR_WOW64_MESSAGE(CONSOLE_ADDGETALIAS, CsrpWow64AddAlias),
    [ConsolepGetAlias - CONSRV_FIRST_API_NUMBER] = CSR_WOW64_MESSAGE(CONSOLE_ADDGETALIAS, CsrpWow64AddAlias),
    [ConsolepGetAliasesLength - CONSRV_FIRST_API_NUMBER] = CSR_WOW64_MESSAGE(CONSOLE_GETALLALIASESLENGTH, CsrpWow64GetAliasesLength),
    [ConsolepGetAliasExesLength - CONSRV_FIRST_API_NUMBER] = CSR_WOW64_MESSAGE(CONSOLE_GETALIASESEXESLENGTH, CsrpWow64GetAliasExesLength),
    [ConsolepGetAliases - CONSRV_FIRST_API_NUMBER] = CSR_WOW64_MESSAGE(CONSOLE_GETALLALIASES, CsrpWow64GetAliases),
    [ConsolepGetAliasExes - CONSRV_FIRST_API_NUMBER] = CSR_WOW64_MESSAGE(CONSOLE_GETALIASESEXES, CsrpWow64GetAliasExes),
    [ConsolepExpungeCommandHistory - CONSRV_FIRST_API_NUMBER] = CSR_WOW64_MESSAGE(CONSOLE_EXPUNGECOMMANDHISTORY, CsrpWow64ExpungeCommandHistory),
    [ConsolepSetNumberOfCommands - CONSRV_FIRST_API_NUMBER] = CSR_WOW64_MESSAGE(CONSOLE_SETHISTORYNUMBERCOMMANDS, CsrpWow64SetNumberOfCommands),
    [ConsolepGetCommandHistoryLength - CONSRV_FIRST_API_NUMBER] = CSR_WOW64_MESSAGE(CONSOLE_GETCOMMANDHISTORYLENGTH, CsrpWow64GetCommandHistoryLength),
    [ConsolepGetCommandHistory - CONSRV_FIRST_API_NUMBER] = CSR_WOW64_MESSAGE(CONSOLE_GETCOMMANDHISTORY, CsrpWow64GetCommandHistory),
    [ConsolepSetCommandHistoryMode - CONSRV_FIRST_API_NUMBER] = CSR_WOW64_MESSAGE(CONSOLE_SETHISTORYMODE, CsrpWow64SetCommandHistoryMode),
    [ConsolepGetCP - CONSRV_FIRST_API_NUMBER] = CSR_WOW64_MESSAGE(CONSOLE_GETINPUTOUTPUTCP, CsrpWow64GetCP),
    [ConsolepSetCP - CONSRV_FIRST_API_NUMBER] = CSR_WOW64_MESSAGE(CONSOLE_SETINPUTOUTPUTCP, CsrpWow64SetCP),
    [ConsolepSetMenuClose - CONSRV_FIRST_API_NUMBER] = CSR_WOW64_MESSAGE(CONSOLE_SETMENUCLOSE, CsrpWow64SetMenuClose),
    [ConsolepNotifyLastClose - CONSRV_FIRST_API_NUMBER] = CSR_WOW64_MESSAGE(CONSOLE_NOTIFYLASTCLOSE, CsrpWow64NotifyLastClose),
    [ConsolepGenerateCtrlEvent - CONSRV_FIRST_API_NUMBER] = CSR_WOW64_MESSAGE(CONSOLE_GENERATECTRLEVENT, CsrpWow64GenerateCtrlEvent),
    [ConsolepGetKeyboardLayoutName - CONSRV_FIRST_API_NUMBER] = CSR_WOW64_MESSAGE(CONSOLE_GETKBDLAYOUTNAME, CsrpWow64GetKeyboardLayoutName),
    [ConsolepGetConsoleWindow - CONSRV_FIRST_API_NUMBER] = CSR_WOW64_MESSAGE(CONSOLE_GETWINDOW, CsrpWow64GetConsoleWindow),
    [ConsolepGetLangId - CONSRV_FIRST_API_NUMBER] = CSR_WOW64_MESSAGE(CONSOLE_GETLANGID, CsrpWow64GetLangId),
    [ConsolepAttach - CONSRV_FIRST_API_NUMBER] = CSR_WOW64_MESSAGE(CONSOLE_ATTACHCONSOLE, CsrpWow64Attach),
    [ConsolepGetSelectionInfo - CONSRV_FIRST_API_NUMBER] = CSR_WOW64_MESSAGE(CONSOLE_GETSELECTIONINFO, CsrpWow64GetSelectionInfo),
    [ConsolepGetProcessList - CONSRV_FIRST_API_NUMBER] = CSR_WOW64_MESSAGE(CONSOLE_GETPROCESSLIST, CsrpWow64GetProcessList),
    [ConsolepGetHistory - CONSRV_FIRST_API_NUMBER] = CSR_WOW64_MESSAGE(CONSOLE_GETSETHISTORYINFO, CsrpWow64GetHistory),
    [ConsolepSetHistory - CONSRV_FIRST_API_NUMBER] = CSR_WOW64_MESSAGE(CONSOLE_GETSETHISTORYINFO, CsrpWow64GetHistory),
    [ConsolepGetScreenBufferInfoEx - CONSRV_FIRST_API_NUMBER] = CSR_WOW64_MESSAGE(CONSOLE_GETSCREENBUFFERINFOEX, CsrpWow64GetScreenBufferInfoEx),
    [ConsolepSetScreenBufferInfoEx - CONSRV_FIRST_API_NUMBER] = CSR_WOW64_MESSAGE(CONSOLE_GETSCREENBUFFERINFOEX, CsrpWow64GetScreenBufferInfoEx),
};

static const CSR_WOW64_FIELD CsrpWow64DefineDosDevice[] =
{
    CSR_WOW64_DATA(BASE_DEFINE_DOS_DEVICE, Flags, Flags),
    CSR_WOW64_DATA(BASE_DEFINE_DOS_DEVICE, DeviceName.Length, DeviceName.MaximumLength),
    CSR_WOW64_POINTER(BASE_DEFINE_DOS_DEVICE, DeviceName.Buffer),
    CSR_WOW64_DATA(BASE_DEFINE_DOS_DEVICE, TargetPath.Length, TargetPath.MaximumLength),
    CSR_WOW64_POINTER(BASE_DEFINE_DOS_DEVICE, TargetPath.Buffer),
};

static const CSR_WOW64_MESSAGE CsrpWow64BaseMessages[BasepMaxApiNumber - BASESRV_FIRST_API_NUMBER] =
{
    [BasepDefineDosDevice - BASESRV_FIRST_API_NUMBER] = CSR_WOW64_MESSAGE(BASE_DEFINE_DOS_DEVICE, CsrpWow64DefineDosDevice),
};

static const CSR_WOW64_FIELD CsrpWow64EndTask[] =
{
    CSR_WOW64_DATA(USER_END_TASK, LastError, LastError),
    CSR_WOW64_HANDLE(USER_END_TASK, WndHandle),
    CSR_WOW64_DATA(USER_END_TASK, Force, Success),
};

static const CSR_WOW64_FIELD CsrpWow64GetThreadConsoleDesktop[] =
{
    CSR_WOW64_POINTER(USER_GET_THREAD_CONSOLE_DESKTOP, ThreadId),
    CSR_WOW64_HANDLE(USER_GET_THREAD_CONSOLE_DESKTOP, ConsoleDesktop),
};

static const CSR_WOW64_MESSAGE CsrpWow64UserMessages[UserpMaxApiNumber - USERSRV_FIRST_API_NUMBER] =
{
    [UserpEndTask - USERSRV_FIRST_API_NUMBER] = CSR_WOW64_MESSAGE(USER_END_TASK, CsrpWow64EndTask),
    [UserpGetThreadConsoleDesktop - USERSRV_FIRST_API_NUMBER] = CSR_WOW64_MESSAGE(USER_GET_THREAD_CONSOLE_DESKTOP, CsrpWow64GetThreadConsoleDesktop),
};

static const CSR_WOW64_MESSAGE *
CsrpWow64GetMessage(IN PCSR_API_MESSAGE ApiMessage)
{
    ULONG ApiId = CSR_API_NUMBER_TO_API_ID(ApiMessage->ApiNumber);
    const CSR_WOW64_MESSAGE *Messages;
    ULONG Count;

    switch (CSR_API_NUMBER_TO_SERVER_ID(ApiMessage->ApiNumber))
    {
        case BASESRV_SERVERDLL_INDEX:
            Messages = CsrpWow64BaseMessages;
            Count = RTL_NUMBER_OF(CsrpWow64BaseMessages);
            ApiId -= BASESRV_FIRST_API_NUMBER;
            break;

        case CONSRV_SERVERDLL_INDEX:
            Messages = CsrpWow64ConsoleMessages;
            Count = RTL_NUMBER_OF(CsrpWow64ConsoleMessages);
            ApiId -= CONSRV_FIRST_API_NUMBER;
            break;

        case USERSRV_SERVERDLL_INDEX:
            Messages = CsrpWow64UserMessages;
            Count = RTL_NUMBER_OF(CsrpWow64UserMessages);
            ApiId -= USERSRV_FIRST_API_NUMBER;
            break;

        default:
            return NULL;
    }

    if (ApiId >= Count || !Messages[ApiId].Fields) return NULL;
    return &Messages[ApiId];
}

static ULONG
CsrpWow64GetOffset32(IN const CSR_WOW64_FIELD *Field, IN OUT PULONG End32)
{
    ULONG Offset32 = (Field->Type == CsrWow64Data) ? *End32 : ALIGN_UP_BY(*End32, sizeof(ULONG));

    *End32 = Offset32 + ((Field->Type == CsrWow64Data) ? Field->Size : sizeof(ULONG));
    return Offset32;
}

static ULONG
CsrpWow64ConvertMessage(IN const CSR_WOW64_MESSAGE *Message, IN PUCHAR Data64, IN PUCHAR Data32, IN BOOLEAN ToServer)
{
    ULONG Index, End32 = 0;

    for (Index = 0; Index < Message->FieldCount; ++Index)
    {
        const CSR_WOW64_FIELD *Field = &Message->Fields[Index];
        PUCHAR Field32 = Data32 + CsrpWow64GetOffset32(Field, &End32);
        PUCHAR Field64 = Data64 + Field->Offset;

        if (Field->Type == CsrWow64Data)
        {
            if (ToServer) RtlCopyMemory(Field64, Field32, Field->Size);
            else RtlCopyMemory(Field32, Field64, Field->Size);
        }
        else if (!ToServer)
        {
            *(PULONG)Field32 = (ULONG)*(PULONG_PTR)Field64;
        }
        else if (Field->Type == CsrWow64Handle)
        {
            *(PULONG_PTR)Field64 = (ULONG_PTR)(LONG_PTR)*(PLONG)Field32;
        }
        else
        {
            *(PULONG_PTR)Field64 = *(PULONG)Field32;
        }
    }
    return ALIGN_UP_BY(End32, sizeof(ULONG));
}

VOID
NTAPI
CsrWow64MessageToServer(IN OUT PCSR_API_MESSAGE ApiMessage)
{
    const CSR_WOW64_MESSAGE *Message = CsrpWow64GetMessage(ApiMessage);
    UCHAR Data32[RTL_FIELD_SIZE(CSR_API_MESSAGE32, Data)];

    if (!Message) return;
    RtlCopyMemory(Data32, &ApiMessage->Data, sizeof(Data32));
    RtlZeroMemory(&ApiMessage->Data, Message->Size);
    CsrpWow64ConvertMessage(Message, (PUCHAR)&ApiMessage->Data, Data32, TRUE);
    ApiMessage->Header.u1.s1.TotalLength = (CSHORT)(FIELD_OFFSET(CSR_API_MESSAGE, Data) + Message->Size);
    ApiMessage->Header.u1.s1.DataLength = ApiMessage->Header.u1.s1.TotalLength - sizeof(PORT_MESSAGE);
}

VOID
NTAPI
CsrWow64MessageToClient(IN OUT PCSR_API_MESSAGE ApiMessage)
{
    const CSR_WOW64_MESSAGE *Message = CsrpWow64GetMessage(ApiMessage);
    UCHAR Data64[RTL_FIELD_SIZE(CSR_API_MESSAGE, Data)];
    ULONG Size32;

    if (!Message) return;
    RtlCopyMemory(Data64, &ApiMessage->Data, Message->Size);
    Size32 = CsrpWow64ConvertMessage(Message, Data64, (PUCHAR)&ApiMessage->Data, FALSE);
    ApiMessage->Header.u1.s1.TotalLength = (CSHORT)(FIELD_OFFSET(CSR_API_MESSAGE, Data) + Size32);
    ApiMessage->Header.u1.s1.DataLength = ApiMessage->Header.u1.s1.TotalLength - sizeof(PORT_MESSAGE);
}

ULONG_PTR
NTAPI
CsrWow64GetMessagePointerOffset(IN PCSR_API_MESSAGE ApiMessage, IN ULONG Offset32)
{
    const CSR_WOW64_MESSAGE *Message = CsrpWow64GetMessage(ApiMessage);
    ULONG Index, End32 = 0;

    if (!Message) return MAXULONG_PTR;
    for (Index = 0; Index < Message->FieldCount; ++Index)
    {
        const CSR_WOW64_FIELD *Field = &Message->Fields[Index];
        ULONG FieldOffset32 = CsrpWow64GetOffset32(Field, &End32);

        if (Field->Type == CsrWow64Pointer && FIELD_OFFSET(CSR_API_MESSAGE32, Data) + FieldOffset32 == Offset32)
            return FIELD_OFFSET(CSR_API_MESSAGE, Data) + Field->Offset;
    }
    return MAXULONG_PTR;
}
