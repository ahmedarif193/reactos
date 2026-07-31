@echo off

if not "%1" == "" (
    start %1
    goto :eof
)

set S=%SystemRoot%\system32

"%S%\reg.exe" query "HKLM\SYSTEM\CurrentControlSet\Control" /v SystemStartOptions 2>nul | "%S%\findstr.exe" /i "SMPDIAG" >nul
if errorlevel 1 goto normal_start

if not exist "%S%\taskmgr11.exe" goto taskmgr_missing
"%S%\dbgprint.exe" TASKMGR11_BOOT_START
start "" "%S%\taskmgr11.exe" /smpdiag
goto taskmgr_started
:taskmgr_missing
"%S%\dbgprint.exe" TASKMGR11_BOOT_MISSING
:taskmgr_started

"%S%\ping.exe" -n 4 127.0.0.1 >nul

if not exist "%S%\cpubench.exe" goto cpubench_missing
"%S%\dbgprint.exe" CPUBENCH_GUI_TEST_BEGIN
start "" /min /wait "%S%\cpubench.exe"
"%S%\dbgprint.exe" CPUBENCH_GUI_TEST_END
goto cpubench_done
:cpubench_missing
"%S%\dbgprint.exe" CPUBENCH_GUI_TEST_MISSING
:cpubench_done

"%S%\ping.exe" -n 3 127.0.0.1 >nul
"%S%\dbgprint.exe" BOOT_TESTS_DONE
goto :eof

:normal_start
%SystemRoot%\system32\dbgprint.exe WIFISCAN_BEGIN
%SystemRoot%\system32\wlanscan.exe
%SystemRoot%\system32\dbgprint.exe WIFISCAN_END
%SystemRoot%\system32\dbgprint.exe BOOT_TESTS_DONE

if not exist "%~dp0wifi_test.cmd" goto wifi_test_done
%SystemRoot%\system32\dbgprint.exe WIFI_TEST_BEGIN
call "%~dp0wifi_test.cmd"
if errorlevel 1 goto wifi_test_failed
%SystemRoot%\system32\dbgprint.exe WIFI_TEST_PASS
goto wifi_test_done
:wifi_test_failed
%SystemRoot%\system32\dbgprint.exe WIFI_TEST_FAIL
:wifi_test_done

if exist "%~dp0cpubench_start.cmd" call "%~dp0cpubench_start.cmd" httpboot
goto :eof
