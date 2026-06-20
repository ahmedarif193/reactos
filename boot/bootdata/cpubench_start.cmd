@echo off

set S=%SystemRoot%\system32
set BIN=%SystemRoot%\bin

"%S%\reg.exe" query "HKLM\SYSTEM\CurrentControlSet\Control" /v SystemStartOptions 2>nul | "%S%\findstr.exe" /i "KMTEST" >nul
if errorlevel 1 goto :eof

if not exist "%BIN%\kmtest_.exe" goto nodriver

"%S%\dbgprint.exe" KMTEST_SUITE_BEGIN arm64

"%S%\dbgprint.exe" KMTEST_BEGIN KeArm64Smp
"%BIN%\kmtest_.exe" KeArm64Smp
"%S%\dbgprint.exe" KMTEST_EXIT KeArm64Smp %ERRORLEVEL%
"%S%\dbgprint.exe" KMTEST_END KeArm64Smp

"%S%\dbgprint.exe" KMTEST_BEGIN KeArm64SmpChurn
"%BIN%\kmtest_.exe" KeArm64SmpChurn
"%S%\dbgprint.exe" KMTEST_EXIT KeArm64SmpChurn %ERRORLEVEL%
"%S%\dbgprint.exe" KMTEST_END KeArm64SmpChurn

"%S%\dbgprint.exe" KMTEST_BEGIN KeArm64SubNodeSched
"%BIN%\kmtest_.exe" KeArm64SubNodeSched
"%S%\dbgprint.exe" KMTEST_EXIT KeArm64SubNodeSched %ERRORLEVEL%
"%S%\dbgprint.exe" KMTEST_END KeArm64SubNodeSched

"%S%\dbgprint.exe" KMTEST_BEGIN KeArm64DpcIpi
"%BIN%\kmtest_.exe" KeArm64DpcIpi
"%S%\dbgprint.exe" KMTEST_EXIT KeArm64DpcIpi %ERRORLEVEL%
"%S%\dbgprint.exe" KMTEST_END KeArm64DpcIpi

"%S%\dbgprint.exe" KMTEST_BEGIN KeArm64IpiBroadcast
"%BIN%\kmtest_.exe" KeArm64IpiBroadcast
"%S%\dbgprint.exe" KMTEST_EXIT KeArm64IpiBroadcast %ERRORLEVEL%
"%S%\dbgprint.exe" KMTEST_END KeArm64IpiBroadcast

"%S%\dbgprint.exe" KMTEST_BEGIN KeArm64Irql
"%BIN%\kmtest_.exe" KeArm64Irql
"%S%\dbgprint.exe" KMTEST_EXIT KeArm64Irql %ERRORLEVEL%
"%S%\dbgprint.exe" KMTEST_END KeArm64Irql

"%S%\dbgprint.exe" KMTEST_BEGIN KeArm64SpinLock
"%BIN%\kmtest_.exe" KeArm64SpinLock
"%S%\dbgprint.exe" KMTEST_EXIT KeArm64SpinLock %ERRORLEVEL%
"%S%\dbgprint.exe" KMTEST_END KeArm64SpinLock

"%S%\dbgprint.exe" KMTEST_BEGIN KeArm64
"%BIN%\kmtest_.exe" KeArm64
"%S%\dbgprint.exe" KMTEST_EXIT KeArm64 %ERRORLEVEL%
"%S%\dbgprint.exe" KMTEST_END KeArm64

"%S%\dbgprint.exe" KMTEST_BEGIN KeArm64Dispatcher
"%BIN%\kmtest_.exe" KeArm64Dispatcher
"%S%\dbgprint.exe" KMTEST_EXIT KeArm64Dispatcher %ERRORLEVEL%
"%S%\dbgprint.exe" KMTEST_END KeArm64Dispatcher

"%S%\dbgprint.exe" KMTEST_BEGIN KeArm64Frames
"%BIN%\kmtest_.exe" KeArm64Frames
"%S%\dbgprint.exe" KMTEST_EXIT KeArm64Frames %ERRORLEVEL%
"%S%\dbgprint.exe" KMTEST_END KeArm64Frames

"%S%\dbgprint.exe" KMTEST_BEGIN KeArm64Intrinsics
"%BIN%\kmtest_.exe" KeArm64Intrinsics
"%S%\dbgprint.exe" KMTEST_EXIT KeArm64Intrinsics %ERRORLEVEL%
"%S%\dbgprint.exe" KMTEST_END KeArm64Intrinsics

"%S%\dbgprint.exe" KMTEST_BEGIN KeArm64LoaderCache
"%BIN%\kmtest_.exe" KeArm64LoaderCache
"%S%\dbgprint.exe" KMTEST_EXIT KeArm64LoaderCache %ERRORLEVEL%
"%S%\dbgprint.exe" KMTEST_END KeArm64LoaderCache

"%S%\dbgprint.exe" KMTEST_BEGIN KeArm64PcrPrcb
"%BIN%\kmtest_.exe" KeArm64PcrPrcb
"%S%\dbgprint.exe" KMTEST_EXIT KeArm64PcrPrcb %ERRORLEVEL%
"%S%\dbgprint.exe" KMTEST_END KeArm64PcrPrcb

"%S%\dbgprint.exe" KMTEST_BEGIN KeArm64ThreadProcess
"%BIN%\kmtest_.exe" KeArm64ThreadProcess
"%S%\dbgprint.exe" KMTEST_EXIT KeArm64ThreadProcess %ERRORLEVEL%
"%S%\dbgprint.exe" KMTEST_END KeArm64ThreadProcess

"%S%\dbgprint.exe" KMTEST_BEGIN HalArm64Layout
"%BIN%\kmtest_.exe" HalArm64Layout
"%S%\dbgprint.exe" KMTEST_EXIT HalArm64Layout %ERRORLEVEL%
"%S%\dbgprint.exe" KMTEST_END HalArm64Layout

"%S%\dbgprint.exe" KMTEST_BEGIN HalArm64Stage1
"%BIN%\kmtest_.exe" HalArm64Stage1
"%S%\dbgprint.exe" KMTEST_EXIT HalArm64Stage1 %ERRORLEVEL%
"%S%\dbgprint.exe" KMTEST_END HalArm64Stage1

"%S%\dbgprint.exe" KMTEST_BEGIN HalArm64Stage2
"%BIN%\kmtest_.exe" HalArm64Stage2
"%S%\dbgprint.exe" KMTEST_EXIT HalArm64Stage2 %ERRORLEVEL%
"%S%\dbgprint.exe" KMTEST_END HalArm64Stage2

"%S%\dbgprint.exe" KMTEST_BEGIN HalArm64Stage3
"%BIN%\kmtest_.exe" HalArm64Stage3
"%S%\dbgprint.exe" KMTEST_EXIT HalArm64Stage3 %ERRORLEVEL%
"%S%\dbgprint.exe" KMTEST_END HalArm64Stage3

"%S%\dbgprint.exe" KMTEST_BEGIN HalArm64Stage4
"%BIN%\kmtest_.exe" HalArm64Stage4
"%S%\dbgprint.exe" KMTEST_EXIT HalArm64Stage4 %ERRORLEVEL%
"%S%\dbgprint.exe" KMTEST_END HalArm64Stage4

"%S%\dbgprint.exe" KMTEST_BEGIN HalArm64Stage5
"%BIN%\kmtest_.exe" HalArm64Stage5
"%S%\dbgprint.exe" KMTEST_EXIT HalArm64Stage5 %ERRORLEVEL%
"%S%\dbgprint.exe" KMTEST_END HalArm64Stage5

"%S%\dbgprint.exe" KMTEST_BEGIN KdArm64Layout
"%BIN%\kmtest_.exe" KdArm64Layout
"%S%\dbgprint.exe" KMTEST_EXIT KdArm64Layout %ERRORLEVEL%
"%S%\dbgprint.exe" KMTEST_END KdArm64Layout

"%S%\dbgprint.exe" KMTEST_BEGIN RtlArm64UnwindLayout
"%BIN%\kmtest_.exe" RtlArm64UnwindLayout
"%S%\dbgprint.exe" KMTEST_EXIT RtlArm64UnwindLayout %ERRORLEVEL%
"%S%\dbgprint.exe" KMTEST_END RtlArm64UnwindLayout

"%S%\dbgprint.exe" KMTEST_SUITE_END arm64
"%S%\dbgprint.exe" BOOT_TESTS_DONE
goto :eof

:nodriver
"%S%\dbgprint.exe" KMTEST_RUNNER_MISSING
"%S%\dbgprint.exe" BOOT_TESTS_DONE
