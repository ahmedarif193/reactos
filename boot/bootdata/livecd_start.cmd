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

goto :eof
:after_cpubench

if not exist "%SystemRoot%\system32\cmd_rostest_x64.exe" goto after_cmd_rostest_x64
echo Running cmd_rostest_x64
%SystemRoot%\system32\dbgprint.exe FEX_TEST_BEGIN cmd_rostest_x64
%SystemRoot%\system32\cmd_rostest_x64.exe
%SystemRoot%\system32\dbgprint.exe FEX_TEST_EXIT cmd_rostest_x64 %ERRORLEVEL%
%SystemRoot%\system32\dbgprint.exe FEX_TEST_END cmd_rostest_x64
:after_cmd_rostest_x64

if not exist "%SystemRoot%\system32\ntdll_apitest_x64.exe" goto after_ntdll_apitest_x64
%SystemRoot%\system32\dbgprint.exe FEX_TEST_BEGIN ntdll_apitest_x64 arm64_chpe
%SystemRoot%\system32\ntdll_apitest_x64.exe arm64_chpe
%SystemRoot%\system32\dbgprint.exe FEX_TEST_EXIT ntdll_apitest_x64 arm64_chpe %ERRORLEVEL%
%SystemRoot%\system32\dbgprint.exe FEX_TEST_END ntdll_apitest_x64 arm64_chpe
:after_ntdll_apitest_x64

if not exist "%SystemRoot%\bin\kmtest_.exe" goto :eof

pushd "%SystemRoot%\bin" || goto :eof

echo Running kmtest_ MmSelfMap
kmtest_.exe MmSelfMap

echo Running kmtest_ arm64 parity suite
kmtest_.exe HalArm64Layout
kmtest_.exe KdArm64Layout
kmtest_.exe KeArm64
kmtest_.exe KeArm64Dispatcher
kmtest_.exe KeArm64DpcIpi
kmtest_.exe KeArm64Frames
kmtest_.exe KeArm64Intrinsics
kmtest_.exe KeArm64Irql
kmtest_.exe KeArm64LoaderCache
kmtest_.exe KeArm64PcrPrcb
kmtest_.exe KeArm64Smp
kmtest_.exe KeArm64SmpChurn
kmtest_.exe KeArm64SpinLock
kmtest_.exe KeArm64ThreadProcess
kmtest_.exe RtlArm64UnwindLayout

popd
