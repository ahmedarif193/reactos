@echo off
rem ============================================================
rem ReactOS WDDM Regression Test Suite — Run All
rem Runs every WDDM-adjacent apitest suite through rosautotest so stdout is
rem mirrored to DbgPrint and appears in the serial log.
rem ============================================================

set ROSAUTOTEST=%SystemRoot%\system32\rosautotest.exe
set DBGPRINT=%SystemRoot%\system32\dbgprint.exe
set FAIL=0

echo ============================================================
echo   ReactOS WDDM Regression Test Suite
echo   %DATE% %TIME%
echo ============================================================
%DBGPRINT% WDDM_APITEST_BEGIN all

echo.
echo === kmtest WdmNt81Abi (Win8.1 WDM/DDK export ABI) ===
echo.
%DBGPRINT% WDDM_APITEST_SUITE_BEGIN kmtest.WdmNt81Abi
%ROSAUTOTEST% /n kmtest WdmNt81Abi
set SUITE_RESULT=%ERRORLEVEL%
%DBGPRINT% WDDM_APITEST_SUITE_END kmtest.WdmNt81Abi errorlevel %SUITE_RESULT%
if not "%SUITE_RESULT%"=="0" set FAIL=1

echo.
echo === kmtest WdmExRundown (WDM rundown behavior) ===
echo.
%DBGPRINT% WDDM_APITEST_SUITE_BEGIN kmtest.WdmExRundown
%ROSAUTOTEST% /n kmtest WdmExRundown
set SUITE_RESULT=%ERRORLEVEL%
%DBGPRINT% WDDM_APITEST_SUITE_END kmtest.WdmExRundown errorlevel %SUITE_RESULT%
if not "%SUITE_RESULT%"=="0" set FAIL=1

echo.
echo === kmtest WdmIoRemoveLock (WDM remove-lock behavior) ===
echo.
%DBGPRINT% WDDM_APITEST_SUITE_BEGIN kmtest.WdmIoRemoveLock
%ROSAUTOTEST% /n kmtest WdmIoRemoveLock
set SUITE_RESULT=%ERRORLEVEL%
%DBGPRINT% WDDM_APITEST_SUITE_END kmtest.WdmIoRemoveLock errorlevel %SUITE_RESULT%
if not "%SUITE_RESULT%"=="0" set FAIL=1

echo.
echo === kmtest WdmIoShareAccess (WDM share-access behavior) ===
echo.
%DBGPRINT% WDDM_APITEST_SUITE_BEGIN kmtest.WdmIoShareAccess
%ROSAUTOTEST% /n kmtest WdmIoShareAccess
set SUITE_RESULT=%ERRORLEVEL%
%DBGPRINT% WDDM_APITEST_SUITE_END kmtest.WdmIoShareAccess errorlevel %SUITE_RESULT%
if not "%SUITE_RESULT%"=="0" set FAIL=1

echo.
echo === display_apitest (display stack classifier) ===
echo.
%DBGPRINT% WDDM_APITEST_SUITE_BEGIN display
%ROSAUTOTEST% /n display
set SUITE_RESULT=%ERRORLEVEL%
%DBGPRINT% WDDM_APITEST_SUITE_END display errorlevel %SUITE_RESULT%
if not "%SUITE_RESULT%"=="0" set FAIL=1

echo.
echo === user32_apitest CursorMoveVisual (cursor move visual restore/present) ===
echo.
%DBGPRINT% WDDM_APITEST_SUITE_BEGIN user32.CursorMoveVisual
%DBGPRINT% --process "%SystemRoot%\bin\user32_apitest.exe CursorMoveVisual"
set SUITE_RESULT=%ERRORLEVEL%
%DBGPRINT% WDDM_APITEST_SUITE_END user32.CursorMoveVisual errorlevel %SUITE_RESULT%
if not "%SUITE_RESULT%"=="0" set FAIL=1

echo.
echo === dxgkddi_unittest (direct dxgkrnl miniport callback chain) ===
echo.
%DBGPRINT% WDDM_APITEST_SUITE_BEGIN dxgkddi
%ROSAUTOTEST% /n dxgkddi
set SUITE_RESULT=%ERRORLEVEL%
%DBGPRINT% WDDM_APITEST_SUITE_END dxgkddi errorlevel %SUITE_RESULT%
if not "%SUITE_RESULT%"=="0" set FAIL=1

echo.
echo === watchdog_test (TDR/watchdog/dxgkrnl exports and stress) ===
echo.
%DBGPRINT% WDDM_APITEST_SUITE_BEGIN watchdog
%DBGPRINT% --process "%SystemRoot%\bin\watchdog_test.exe"
set SUITE_RESULT=%ERRORLEVEL%
%DBGPRINT% WDDM_APITEST_SUITE_END watchdog errorlevel %SUITE_RESULT%
if not "%SUITE_RESULT%"=="0" set FAIL=1

echo.
echo === dxgkrnl_apitest (kernel IOCTL chain) ===
echo.
%DBGPRINT% WDDM_APITEST_SUITE_BEGIN dxgkrnl
%ROSAUTOTEST% /n dxgkrnl
set SUITE_RESULT=%ERRORLEVEL%
%DBGPRINT% WDDM_APITEST_SUITE_END dxgkrnl errorlevel %SUITE_RESULT%
if not "%SUITE_RESULT%"=="0" set FAIL=1

echo.
echo === d3dkmt_apitest (gdi32 D3DKMT API) ===
echo.
%DBGPRINT% WDDM_APITEST_SUITE_BEGIN d3dkmt
%ROSAUTOTEST% /n d3dkmt
set SUITE_RESULT=%ERRORLEVEL%
%DBGPRINT% WDDM_APITEST_SUITE_END d3dkmt errorlevel %SUITE_RESULT%
if not "%SUITE_RESULT%"=="0" set FAIL=1

echo.
echo === dwm_apitest (DWM composition) ===
echo.
%DBGPRINT% WDDM_APITEST_SUITE_BEGIN dwm
%ROSAUTOTEST% /n dwm
set SUITE_RESULT=%ERRORLEVEL%
%DBGPRINT% WDDM_APITEST_SUITE_END dwm errorlevel %SUITE_RESULT%
if not "%SUITE_RESULT%"=="0" set FAIL=1

echo.
echo === dxgi_apitest (DXGI factory/output/swapchain) ===
echo.
%DBGPRINT% WDDM_APITEST_SUITE_BEGIN dxgi
%ROSAUTOTEST% /n dxgi
set SUITE_RESULT=%ERRORLEVEL%
%DBGPRINT% WDDM_APITEST_SUITE_END dxgi errorlevel %SUITE_RESULT%
if not "%SUITE_RESULT%"=="0" set FAIL=1

echo.
echo === opengl32_apitest (WGL game fallback) ===
echo.
%DBGPRINT% WDDM_APITEST_SUITE_BEGIN opengl32
%ROSAUTOTEST% /n opengl32 wgl_smoke
set SUITE_RESULT=%ERRORLEVEL%
%DBGPRINT% WDDM_APITEST_SUITE_END opengl32 errorlevel %SUITE_RESULT%
if not "%SUITE_RESULT%"=="0" set FAIL=1

echo.
echo ============================================================
echo   RESULTS: see serial DbgPrint output for per-test pass/fail/skip totals
echo ============================================================

if "%FAIL%"=="0" (
    echo STATUS: PASS
    %DBGPRINT% WDDM_APITEST_END PASS
    %DBGPRINT% WDDM_APITEST_REQUEST_SHUTDOWN PASS
    %SystemRoot%\system32\shutdown.exe /s /t 0 /f
    exit /b 0
    goto :eof
)

echo STATUS: FAIL
%DBGPRINT% WDDM_APITEST_END FAIL
%DBGPRINT% WDDM_APITEST_REQUEST_SHUTDOWN FAIL
%SystemRoot%\system32\shutdown.exe /s /t 0 /f
exit /b 1
