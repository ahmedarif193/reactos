@echo off

set S=%SystemRoot%\system32
set BIN=%SystemRoot%\bin

cd /d "%BIN%"

"%S%\dbgprint.exe" SUITE_BEGIN win32k

rem GDI apitests (PATCOPY, EngBitBlt, NtGdiGetPixel, GdiGetClipBox)
if not exist "%BIN%\gdi32_apitest.exe" goto after_gdi32_apitest
"%S%\dbgprint.exe" BEGIN gdi32_apitest_GetPixel
"%S%\dbgprint.exe" --process "%BIN%\gdi32_apitest.exe GetPixel 2>&1"
"%S%\dbgprint.exe" EXIT gdi32_apitest_GetPixel %ERRORLEVEL%
"%S%\dbgprint.exe" END gdi32_apitest_GetPixel
"%S%\dbgprint.exe" BEGIN gdi32_apitest_GetClipBox
"%S%\dbgprint.exe" --process "%BIN%\gdi32_apitest.exe GetClipBox 2>&1"
"%S%\dbgprint.exe" EXIT gdi32_apitest_GetClipBox %ERRORLEVEL%
"%S%\dbgprint.exe" END gdi32_apitest_GetClipBox
"%S%\dbgprint.exe" BEGIN gdi32_apitest_PatBlt
"%S%\dbgprint.exe" --process "%BIN%\gdi32_apitest.exe PatBlt 2>&1"
"%S%\dbgprint.exe" EXIT gdi32_apitest_PatBlt %ERRORLEVEL%
"%S%\dbgprint.exe" END gdi32_apitest_PatBlt
"%S%\dbgprint.exe" BEGIN gdi32_apitest_StretchBlt
"%S%\dbgprint.exe" --process "%BIN%\gdi32_apitest.exe StretchBlt 2>&1"
"%S%\dbgprint.exe" EXIT gdi32_apitest_StretchBlt %ERRORLEVEL%
"%S%\dbgprint.exe" END gdi32_apitest_StretchBlt
"%S%\dbgprint.exe" BEGIN gdi32_apitest_MaskBlt
"%S%\dbgprint.exe" --process "%BIN%\gdi32_apitest.exe MaskBlt 2>&1"
"%S%\dbgprint.exe" EXIT gdi32_apitest_MaskBlt %ERRORLEVEL%
"%S%\dbgprint.exe" END gdi32_apitest_MaskBlt
:after_gdi32_apitest

rem GDI Wine tests (blit, clip, DC, same-device-lock)
if not exist "%BIN%\gdi32_winetest.exe" goto after_gdi32_winetest
"%S%\dbgprint.exe" BEGIN gdi32_winetest_bitmap
"%S%\dbgprint.exe" --process "%BIN%\gdi32_winetest.exe bitmap 2>&1"
"%S%\dbgprint.exe" EXIT gdi32_winetest_bitmap %ERRORLEVEL%
"%S%\dbgprint.exe" END gdi32_winetest_bitmap
"%S%\dbgprint.exe" BEGIN gdi32_winetest_dc
"%S%\dbgprint.exe" --process "%BIN%\gdi32_winetest.exe dc 2>&1"
"%S%\dbgprint.exe" EXIT gdi32_winetest_dc %ERRORLEVEL%
"%S%\dbgprint.exe" END gdi32_winetest_dc
"%S%\dbgprint.exe" BEGIN gdi32_winetest_clipping
"%S%\dbgprint.exe" --process "%BIN%\gdi32_winetest.exe clipping 2>&1"
"%S%\dbgprint.exe" EXIT gdi32_winetest_clipping %ERRORLEVEL%
"%S%\dbgprint.exe" END gdi32_winetest_clipping
"%S%\dbgprint.exe" BEGIN gdi32_winetest_dib
"%S%\dbgprint.exe" --process "%BIN%\gdi32_winetest.exe dib 2>&1"
"%S%\dbgprint.exe" EXIT gdi32_winetest_dib %ERRORLEVEL%
"%S%\dbgprint.exe" END gdi32_winetest_dib
:after_gdi32_winetest

rem win32k syscall apitests (BitBlt, visible region)
if not exist "%BIN%\win32u_apitest.exe" goto after_win32u_apitest
"%S%\dbgprint.exe" BEGIN win32u_apitest_NtGdiBitBlt
"%S%\dbgprint.exe" --process "%BIN%\win32u_apitest.exe NtGdiBitBlt 2>&1"
"%S%\dbgprint.exe" EXIT win32u_apitest_NtGdiBitBlt %ERRORLEVEL%
"%S%\dbgprint.exe" END win32u_apitest_NtGdiBitBlt
"%S%\dbgprint.exe" BEGIN win32u_apitest_NtGdiGetRandomRgn
"%S%\dbgprint.exe" --process "%BIN%\win32u_apitest.exe NtGdiGetRandomRgn 2>&1"
"%S%\dbgprint.exe" EXIT win32u_apitest_NtGdiGetRandomRgn %ERRORLEVEL%
"%S%\dbgprint.exe" END win32u_apitest_NtGdiGetRandomRgn
:after_win32u_apitest

rem USER apitests (window DC visible region, scroll)
if not exist "%BIN%\user32_apitest.exe" goto after_user32_apitest
"%S%\dbgprint.exe" BEGIN user32_apitest_GetDCEx
"%S%\dbgprint.exe" --process "%BIN%\user32_apitest.exe GetDCEx 2>&1"
"%S%\dbgprint.exe" EXIT user32_apitest_GetDCEx %ERRORLEVEL%
"%S%\dbgprint.exe" END user32_apitest_GetDCEx
"%S%\dbgprint.exe" BEGIN user32_apitest_ScrollDC
"%S%\dbgprint.exe" --process "%BIN%\user32_apitest.exe ScrollDC 2>&1"
"%S%\dbgprint.exe" EXIT user32_apitest_ScrollDC %ERRORLEVEL%
"%S%\dbgprint.exe" END user32_apitest_ScrollDC
"%S%\dbgprint.exe" BEGIN user32_apitest_ScrollWindowEx
"%S%\dbgprint.exe" --process "%BIN%\user32_apitest.exe ScrollWindowEx 2>&1"
"%S%\dbgprint.exe" EXIT user32_apitest_ScrollWindowEx %ERRORLEVEL%
"%S%\dbgprint.exe" END user32_apitest_ScrollWindowEx
"%S%\dbgprint.exe" BEGIN user32_apitest_RedrawWindow
"%S%\dbgprint.exe" --process "%BIN%\user32_apitest.exe RedrawWindow 2>&1"
"%S%\dbgprint.exe" EXIT user32_apitest_RedrawWindow %ERRORLEVEL%
"%S%\dbgprint.exe" END user32_apitest_RedrawWindow
:after_user32_apitest

rem USER Wine tests (DCE/visregion, DrawFocusRect)
if not exist "%BIN%\user32_winetest.exe" goto after_user32_winetest
"%S%\dbgprint.exe" BEGIN user32_winetest_dce
"%S%\dbgprint.exe" --process "%BIN%\user32_winetest.exe dce 2>&1"
"%S%\dbgprint.exe" EXIT user32_winetest_dce %ERRORLEVEL%
"%S%\dbgprint.exe" END user32_winetest_dce
"%S%\dbgprint.exe" BEGIN user32_winetest_uitools
"%S%\dbgprint.exe" --process "%BIN%\user32_winetest.exe uitools 2>&1"
"%S%\dbgprint.exe" EXIT user32_winetest_uitools %ERRORLEVEL%
"%S%\dbgprint.exe" END user32_winetest_uitools
:after_user32_winetest

rem Common control Wine tests (LISTVIEW_KillFocus)
if not exist "%BIN%\comctl32_winetest.exe" goto after_comctl32_winetest
"%S%\dbgprint.exe" BEGIN comctl32_winetest_listview
"%S%\dbgprint.exe" --process "%BIN%\comctl32_winetest.exe listview 2>&1"
"%S%\dbgprint.exe" EXIT comctl32_winetest_listview %ERRORLEVEL%
"%S%\dbgprint.exe" END comctl32_winetest_listview
:after_comctl32_winetest

"%S%\dbgprint.exe" SUITE_END win32k
"%S%\dbgprint.exe" BOOT_TESTS_DONE
