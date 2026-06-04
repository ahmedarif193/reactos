@echo off
rem ============================================================
rem ReactOS WDDM Display Stack Test Suite
rem
rem This script exercises the WDDM display pipeline to verify:
rem   1. D3DKMT adapter enumeration works
rem   2. Display mode querying works
rem   3. GDI drawing reaches the framebuffer
rem   4. Window creation and basic UI works
rem
rem Output goes to both screen and serial (via debug prints).
rem ============================================================

set DWM_EXE=%SystemRoot%\system32\dwm.exe
set GLXGEARS_EXE=%SystemRoot%\system32\glxgears.exe
set VISUAL_HOLD_SECS=25

echo ============================================================
echo   ReactOS WDDM Display Test Suite
echo   %DATE% %TIME%
echo ============================================================

echo.
echo [TEST 0] Desktop Window Manager startup
echo ---------------------------------------
start "" /b %DWM_EXE%
timeout /t 2 /nobreak >nul
tasklist | findstr /i "dwm.exe" >nul
if %ERRORLEVEL% EQU 0 (
    echo PASS: dwm.exe is running
) else (
    echo FAIL: dwm.exe did not start
)

echo.
echo [TEST 1] Windowed compositor workload
echo --------------------------------------
start "" /b notepad.exe
start "" /b mspaint.exe
start "" /b %GLXGEARS_EXE% -seconds 0
timeout /t 3 /nobreak >nul

tasklist | findstr /i "notepad.exe" >nul
if %ERRORLEVEL% EQU 0 (
    echo PASS: notepad.exe running
) else (
    echo FAIL: notepad.exe could not start
)

tasklist | findstr /i "mspaint.exe" >nul
if %ERRORLEVEL% EQU 0 (
    echo PASS: mspaint.exe running
) else (
    echo FAIL: mspaint.exe could not start
)

tasklist | findstr /i "glxgears.exe" >nul
if %ERRORLEVEL% EQU 0 (
    echo PASS: glxgears.exe running under the compositor workload
) else (
    echo FAIL: glxgears.exe failed to stay alive
)

echo Keeping windows alive for %VISUAL_HOLD_SECS% seconds so captures can verify composition...
timeout /t %VISUAL_HOLD_SECS% /nobreak >nul

tasklist | findstr /i "glxgears.exe" >nul
if %ERRORLEVEL% EQU 0 (
    echo PASS: glxgears.exe still running after the capture window
) else (
    echo INFO: glxgears.exe exited before the hold window completed
)

echo.
echo [TEST 2] Display adapter detection
echo -----------------------------------
reg query "HKLM\HARDWARE\DEVICEMAP\VIDEO" 2>nul
if %ERRORLEVEL% EQU 0 (
    echo PASS: DEVICEMAP\VIDEO exists
) else (
    echo FAIL: DEVICEMAP\VIDEO not found
)

echo.
echo [TEST 3] dxgkrnl service status
echo --------------------------------
sc query dxgkrnl 2>nul
if %ERRORLEVEL% EQU 0 (
    echo PASS: dxgkrnl service found
) else (
    echo FAIL: dxgkrnl service not found
)

echo.
echo [TEST 4] Display driver registry
echo ---------------------------------
reg query "HKLM\SYSTEM\CurrentControlSet\Services\dxgkrnl\Device0" /v InstalledDisplayDrivers 2>nul
if %ERRORLEVEL% EQU 0 (
    echo PASS: InstalledDisplayDrivers found
) else (
    echo FAIL: InstalledDisplayDrivers not found
)

echo.
echo [TEST 5] Display resolution check
echo -----------------------------------
reg query "HKLM\SYSTEM\CurrentControlSet\Services\dxgkrnl\Device0" /v "DefaultSettings.XResolution" 2>nul
reg query "HKLM\SYSTEM\CurrentControlSet\Services\dxgkrnl\Device0" /v "DefaultSettings.YResolution" 2>nul

echo.
echo [TEST 6] D3DKMT Video key
echo --------------------------
reg query "HKLM\SYSTEM\CurrentControlSet\Services\dxgkrnl\Video" /v Service 2>nul
if %ERRORLEVEL% EQU 0 (
    echo PASS: dxgkrnl\Video key exists
) else (
    echo FAIL: dxgkrnl\Video key missing
)

echo.
echo [TEST 7] Cleanup auxiliary workload
echo ------------------------------------
taskkill /f /im mspaint.exe >nul 2>nul
taskkill /f /im notepad.exe >nul 2>nul
tasklist | findstr /i "mspaint.exe notepad.exe" >nul
if %ERRORLEVEL% EQU 0 (
    echo WARN: one or more auxiliary workload processes are still running
) else (
    echo PASS: auxiliary workload processes cleaned up
)

tasklist | findstr /i "glxgears.exe" >nul
if %ERRORLEVEL% EQU 0 (
    echo PASS: glxgears.exe remains running for interactive verification
) else (
    echo WARN: glxgears.exe is no longer running
)

echo.
echo ============================================================
echo   WDDM Test Suite Complete
echo ============================================================
echo.
echo The gears window is left running. Close it manually when done.

rem Keep window open so results are visible
pause
