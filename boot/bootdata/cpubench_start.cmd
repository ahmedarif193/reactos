@echo off

set S=%SystemRoot%\system32
set BIN=%SystemRoot%\bin

if "%1" == "httpboot" goto run_tests
"%S%\reg.exe" query "HKLM\SYSTEM\CurrentControlSet\Control" /v SystemStartOptions 2>nul | "%S%\findstr.exe" /i "KMTEST" >nul
if errorlevel 1 goto :eof

:run_tests
if not exist "%BIN%\kmtest.exe" goto nodriver
if not exist "%BIN%\kmtest_drv.sys" goto nodriver

set KMTEST_FAILED=0
"%S%\dbgprint.exe" KMTEST_SUITE_BEGIN arm64_nt10

rem Begin with narrow layout and architectural probes. Every boundary is tagged
rem independently so the runner reports all failures before the final status.
call :run HalArm64Stage1
call :run HalArm64Stage2
call :run HalArm64Stage3
call :run HalArm64Stage4
call :run HalArm64Stage5
call :run HalArm64Layout
call :run HalArm64Ipi
call :run KdArm64Layout
call :run KeArm64Frames
call :run RtlArm64UnwindLayout
call :run KeArm64PcrPrcb
call :run KeArm64ThreadProcess
call :run KeArm64LoaderCache

call :run KeArm64
call :run KeArm64Dispatcher
call :run KeQueue
call :run KeArm64Intrinsics
call :run KeArm64Irql
call :run KeArm64SpinLock
call :run KeArm64Smp
call :run KeArm64DpcIpi
call :run KeArm64IpiBroadcast
call :run KeArm64SmpChurn
call :run KeArm64SubNodeSched
call :run KeArm64Smt
call :run KeArm64Numa

rem These tests encode behavior measured against the local Windows 11 ARM64
rem reference kernel, PDB, and type dumps.
call :run Win11NewKM
call :run MmWin11KM
call :run MmMdlWin11KM
call :run MmPoolWin11KM
call :run MmSectionWin11KM
call :run MmPteWin11KM
call :run MmSelfMap

if not "%KMTEST_FAILED%" == "0" goto failed
"%S%\dbgprint.exe" KMTEST_SUITE_END arm64_nt10
"%S%\dbgprint.exe" BOOT_TESTS_DONE
goto :eof

:run
"%S%\dbgprint.exe" KMTEST_BEGIN %1
"%BIN%\kmtest.exe" %1
set KMTEST_RC=%ERRORLEVEL%
"%S%\dbgprint.exe" KMTEST_EXIT %1 %KMTEST_RC%
"%S%\dbgprint.exe" KMTEST_END %1
if not "%KMTEST_RC%" == "0" set KMTEST_FAILED=1
goto :eof

:failed
"%S%\dbgprint.exe" KMTEST_SUITE_FAILED arm64_nt10
"%S%\dbgprint.exe" BOOT_TESTS_FAILED
goto :eof

:nodriver
"%S%\dbgprint.exe" KMTEST_RUNNER_MISSING
"%S%\dbgprint.exe" BOOT_TESTS_FAILED
