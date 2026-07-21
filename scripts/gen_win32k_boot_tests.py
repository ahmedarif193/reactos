#!/usr/bin/env python3
"""Generate the win32k/GDI/USER boot auto-test scripts.

Writes boot/bootdata/livecd_start.cmd and boot/bootdata/cpubench_start.cmd
identically. Each subtest gets its own BEGIN/EXIT/END marker via
dbgprint --process, so a crash in one subtest is isolated and the suite keeps
going. dbgprint --process streams child output to the debug log, which keeps
the serial-stall timer alive without a heartbeat. The harness
(scripts/vm_monitor.py) keys completion off BOOT_TESTS_DONE.

Edit the GROUPS table below when test subtests change, then rerun:
    python3 scripts/gen_win32k_boot_tests.py
Subtest names come from each apitest's testlist.c and from each winetest's
START_TEST source-file stems.
"""
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BOOTDATA_DIR = os.path.join(ROOT, "boot", "bootdata")
TARGETS = ("livecd_start.cmd", "cpubench_start.cmd")
SUITE = "win32k"

# (rem header, exe target, [subtests]) in run order: GDI first, then win32kss/USER.
GROUPS = [
    ("GDI apitests", "gdi32_apitest", [
        "AddFontMemResourceEx", "AddFontResource", "AddFontResourceEx", "BeginPath",
        "CombineRgn", "CombineTransform", "CreateBitmap", "CreateBitmapIndirect",
        "CreateCompatibleDC", "CreateDIBitmap", "CreateDIBPatternBrush", "CreateFont",
        "CreateFontIndirect", "CreateIconIndirect", "CreatePen", "CreateRectRgn",
        "DPtoLP", "EngAcquireSemaphore", "EngCreateSemaphore", "EngReleaseSemaphore",
        "EnumFontFamilies", "ExcludeClipRect", "ExtCreatePen", "ExtCreateRegion",
        "FrameRgn", "GdiConvertBitmap", "GdiConvertBrush", "GdiConvertDC",
        "GdiConvertFont", "GdiConvertPalette", "GdiConvertRegion", "GdiDeleteLocalDC",
        "GdiGetCharDimensions", "GdiGetLocalBrush", "GdiGetLocalDC", "GdiReleaseLocalDC",
        "GdiSetAttrs", "GetCharWidth", "GetClipBox", "GetClipRgn", "GetCurrentObject",
        "GetDIBColorTable", "GetDIBits", "GetFontResourceInfoW", "GetGlyphIndices",
        "GetGlyphOutline", "GetPixel", "GetObject", "GetRandomRgn", "GetStockObject",
        "GetTextExtentExPoint", "GetTextMetrics", "GetTextFace", "LPtoDP", "MaskBlt",
        "NtGdiAddFontResource", "OffsetClipRgn", "OffsetRgn", "PaintRgn", "PatBlt",
        "Rectangle", "RealizePalette", "SelectObject", "SetBoundsRect", "SetBrushOrgEx",
        "SetDCPenColor", "SetDIBits", "SetDIBitsToDevice", "SetLayout", "SetMapMode",
        "SetPixel", "SetSysColors", "SetWindowExtEx", "SetWorldTransform", "StretchBlt",
        "TextTransform",
    ]),
    ("GDI Wine tests", "gdi32_winetest", [
        "bitmap", "brush", "clipping", "dc", "dib", "driver", "font", "gdiobj",
        "generated", "icm", "mapping", "metafile", "palette", "path", "pen",
    ]),
    ("GDI+ Wine tests", "gdiplus_winetest", [
        "brush", "customlinecap", "font", "graphics", "graphicspath", "guid", "image",
        "matrix", "metafile", "pathiterator", "pen", "region", "stringformat",
    ]),
    ("GDI font shell-extension apitests", "fontext_apitest", [
        "GetDisplayNameOf", "shellext",
    ]),
    ("DCI manager apitests", "dciman32_apitest", [
        "DCICreatePrimary",
    ]),
    ("win32k syscall apitests", "win32u_apitest", [
        "NtGdiDdCreateDirectDrawObject", "NtGdiDdDeleteDirectDrawObject",
        "NtGdiDdQueryDirectDrawObject", "NtGdiArcInternal", "NtGdiBitBlt",
        "NtGdiCombineRgn", "NtGdiCreateBitmap", "NtGdiCreateCompatibleBitmap",
        "NtGdiCreateCompatibleDC", "NtGdiCreateDIBSection", "NtGdiDeleteObjectApp",
        "NtGdiDoPalette", "NtGdiEngCreatePalette", "NtGdiEnumFontOpen",
        "NtGdiExcludeClipRect", "NtGdiExtSelectClipRgn", "NtGdiExtTextOutW",
        "NtGdiFlushUserBatch", "NtGdiGetBitmapBits", "NtGdiGetDIBitsInternal",
        "NtGdiGetFontResourceInfoInternalW", "NtGdiGetRandomRgn", "NtGdiGetStockObject",
        "NtGdiIntersectClipRect", "NtGdiLineTo", "NtGdiOffsetClipRgn",
        "NtGdiPolyPolyDraw", "NtGdiRestoreDC", "NtGdiSaveDC", "NtGdiSelectBitmap",
        "NtGdiSelectBrush", "NtGdiSelectFont", "NtGdiSelectPen", "NtGdiSetBitmapBits",
        "NtGdiSetDIBitsToDeviceInternal", "NtGdiSetPixel", "NtGdiTransformPoints",
        "NtUserCallHwnd", "NtUserCallHwndLock", "NtUserCallHwndOpt", "NtUserCallHwndParam",
        "NtUserCallHwndParamLock", "NtUserCallNoParam", "NtUserCallOneParam",
        "NtUserConvertMemHandle", "NtUserCountClipboardFormats",
        "NtUserCreateAcceleratorTable", "NtUserCreateWindowEx", "NtUserEnumDisplayMonitors",
        "NtUserEnumDisplaySettings", "NtUserFindExistingCursorIcon", "NtUserGetAsyncKeyState",
        "NtUserGetClassInfo", "NtUserGetCursorInfo", "NtUserGetIconInfo",
        "NtUserGetKeyboardLayoutName", "NtUserGetThreadState", "NtUserGetTitleBarInfo",
        "NtUserProcessConnect", "NtUserRedrawWindow", "NtUserScrollDC",
        "NtUserSelectPalette", "NtUserSetTimer", "NtUserSystemParametersInfo",
        "NtUserToUnicodeEx", "NtUserUpdatePerUserSystemParameters",
    ]),
    ("USER apitests", "user32_apitest", [
        "AttachThreadInput", "CharFuncs", "CloseWindow", "CopyImage", "CreateDialog",
        "CreateIconFromResourceEx", "CreateWindowEx", "DeferWindowPos", "DestroyCursorIcon",
        "DM_REPOSITION", "DrawIconEx", "DrawText", "desktop", "EmptyClipboard",
        "EnumDisplaySettings", "GetClassInfo", "GetDCEx", "GetIconInfo", "GetKeyState",
        "GetMessageTime", "GetPeekMessage", "GetSetWindowInt", "GetSystemMetrics",
        "GetUserObjectInformation", "GetWindowPlacement", "GW_ENABLEDPOPUP",
        "InitializeLpkHooks", "KbdLayout", "keybd_event", "LoadImage", "LoadImageGCC",
        "LookupIconIdFromDirectoryEx", "MenuUI", "MessageStateAnalyzer", "NextDlgItem",
        "PrivateExtractIcons", "RealGetWindowClass", "RedrawWindow", "RegisterHotKey",
        "RegisterClassEx", "ScrollBarRedraw", "ScrollBarWndExtra", "ScrollDC",
        "ScrollWindowEx", "SendMessageTimeout", "SetActiveWindow", "SetCursorPos",
        "SetFocus", "SetParent", "SetProp", "SetScrollInfo", "SetScrollRange",
        "SetWindowPlacement", "ShowWindow", "SwitchToThisWindow", "SystemMenu",
        "SystemParametersInfo", "TrackMouseEvent", "TrackPopupMenuEx", "VirtualKey",
        "WndProc", "wsprintfApi",
    ]),
    ("USER dynamic apitests", "user32_dynamic_apitest", [
        "load",
    ]),
    ("USER Wine tests", "user32_winetest", [
        "broadcast", "class", "clipboard", "combo", "cursoricon", "dce", "dde", "dialog",
        "edit", "generated", "input", "listbox", "menu", "monitor", "msg", "scroll",
        "static", "sysparams", "testdll", "text", "uitools", "win", "winstation",
        "wsprintf",
    ]),
]


def build():
    L = []
    L.append("@echo off")
    L.append("")
    L.append("set S=%SystemRoot%\\system32")
    L.append("set BIN=%SystemRoot%\\bin")
    L.append("")
    L.append('cd /d "%BIN%" || goto nodir')
    L.append("")
    L.append('"%%S%%\\dbgprint.exe" SUITE_BEGIN %s' % SUITE)
    for header, exe, tests in GROUPS:
        L.append("")
        L.append("rem " + header)
        names = " ".join(tests)
        L.append('if exist "%%BIN%%\\%s.exe" for %%%%T in (%s) do call :runtest %s %%%%T' % (exe, names, exe))
    L.append("")
    L.append('"%%S%%\\dbgprint.exe" SUITE_END %s' % SUITE)
    L.append('"%S%\\dbgprint.exe" BOOT_TESTS_DONE')
    L.append("goto :eof")
    L.append("")
    L.append(":runtest")
    L.append("set NAME=%1_%2")
    L.append('"%S%\\dbgprint.exe" BEGIN %NAME%')
    L.append('"%S%\\dbgprint.exe" --process "%BIN%\\%1.exe %2 2>&1"')
    L.append("set ERROR=%ERRORLEVEL%")
    L.append('"%S%\\dbgprint.exe" EXIT %NAME% %ERROR%')
    L.append('"%S%\\dbgprint.exe" END %NAME%')
    L.append("goto :eof")
    L.append("")
    L.append(":nodir")
    L.append('"%S%\\dbgprint.exe" MISSING bin')
    L.append('"%S%\\dbgprint.exe" BOOT_TESTS_DONE')
    L.append("goto :eof")
    L.append("")
    return "\r\n".join(L)


def main():
    content = build()
    total = sum(len(t) for _, _, t in GROUPS)
    for name in TARGETS:
        path = os.path.join(BOOTDATA_DIR, name)
        with open(path, "w", encoding="ascii", newline="") as f:
            f.write(content)
        print("wrote", path)
    print("groups:", len(GROUPS), "subtests:", total)


if __name__ == "__main__":
    main()
