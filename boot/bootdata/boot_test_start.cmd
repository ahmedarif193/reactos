@echo off
rem Architecture- and media-neutral boot-test dispatcher.
setlocal EnableExtensions EnableDelayedExpansion

set S=%SystemRoot%\system32
set BIN=%SystemRoot%\bin
set OPTIONS=%TEMP%\boot-test-options.txt
set KMTEST_LIST=%TEMP%\kmtests-all.txt
set KMTEST_LOG=%TEMP%\kmtest-current.log
set WIFI_LOG=%TEMP%\wifi-scan.log
set NETWORK_CONFIG=%~dp0boot_test_network.cfg
set RUN_ROSAUTOTEST=0
set RUN_CPUBENCH=0
set RUN_ETHBENCH=0
set RUN_KMTEST=0
set RUN_RPI5_WIFI=0
set ETHBENCH_PEER=10.42.0.1
set ETHBENCH_SECONDS=30
set WIFI_SSID=
set WIFI_KEY_FILE=
set BOOT_TEST_SELECTED=0
set BOOT_TEST_FAILURES=0

"%S%\dbgprint.exe" BOOT_TESTS_DISPATCHER_BEGIN
"%S%\reg.exe" query "HKLM\SYSTEM\CurrentControlSet\Control" /v SystemStartOptions > "%OPTIONS%" 2>nul
if errorlevel 1 goto no_options
set OPTS_TEXT=
for /f "usebackq delims=" %%L in ("%OPTIONS%") do set OPTS_TEXT=!OPTS_TEXT! %%L
if not "!OPTS_TEXT:ROSAUTOTEST=!" == "!OPTS_TEXT!" set RUN_ROSAUTOTEST=1
if not "!OPTS_TEXT:CPUBENCH=!" == "!OPTS_TEXT!" set RUN_CPUBENCH=1
if not "!OPTS_TEXT:ETHBENCH=!" == "!OPTS_TEXT!" set RUN_ETHBENCH=1
if not "!OPTS_TEXT:KMTEST=!" == "!OPTS_TEXT!" set RUN_KMTEST=1
if not "!OPTS_TEXT:RPI5WIFITEST=!" == "!OPTS_TEXT!" set RUN_RPI5_WIFI=1
if "!RUN_ROSAUTOTEST!!RUN_CPUBENCH!!RUN_ETHBENCH!!RUN_KMTEST!!RUN_RPI5_WIFI!" == "00000" goto disabled

if not "!RUN_ETHBENCH!!RUN_RPI5_WIFI!" == "00" call :load_network_config

del /q "%OPTIONS%" 2>nul
"%S%\dbgprint.exe" BOOT_TESTS_BEGIN

if "!RUN_ETHBENCH!" == "1" call :run_ethbench
if "!RUN_RPI5_WIFI!" == "1" call :run_rpi5_wifi
if "!RUN_CPUBENCH!" == "1" call :run_cpubench
if "!RUN_KMTEST!" == "1" call :run_kmtests
if "!RUN_ROSAUTOTEST!" == "1" call :run_rosautotest

"%S%\dbgprint.exe" BOOT_TESTS_END selected=!BOOT_TEST_SELECTED! failures=!BOOT_TEST_FAILURES!
if not "!BOOT_TEST_FAILURES!" == "0" goto tests_failed
"%S%\dbgprint.exe" BOOT_TESTS_DONE
endlocal
exit /b 0

:tests_failed
"%S%\dbgprint.exe" BOOT_TESTS_FAILED
"%S%\dbgprint.exe" BOOT_TESTS_DONE
endlocal
exit /b 1

:no_options
"%S%\dbgprint.exe" BOOT_TESTS_NO_START_OPTIONS
endlocal
exit /b 0

:disabled
"%S%\dbgprint.exe" BOOT_TESTS_DISABLED
del /q "%OPTIONS%" 2>nul
endlocal
exit /b 0

:load_network_config
if not exist "%NETWORK_CONFIG%" exit /b 0
rem Optional local configuration keys:
rem ETHBENCH_PEER, WIFI_SSID, and WIFI_KEY_FILE.
rem WIFI_KEY_FILE keeps the passphrase out of this script and the boot options.
for /f "usebackq eol=# tokens=1,* delims==" %%K in ("%NETWORK_CONFIG%") do (
    if /i "%%K" == "ETHBENCH_PEER" set ETHBENCH_PEER=%%L
    if /i "%%K" == "WIFI_SSID" set WIFI_SSID=%%L
    if /i "%%K" == "WIFI_KEY_FILE" set WIFI_KEY_FILE=%%L
)
exit /b 0

:run_ethbench
set /a BOOT_TEST_SELECTED+=1
set ETHBENCH_EXIT=0
if not exist "%S%\ethbench.exe" goto ethbench_missing
"%S%\dbgprint.exe" ETHBENCH_LOOPBACK_BEGIN directions=tx,rx seconds=10 threads=1
"%S%\ethbench.exe" loop-tx 127.0.0.1 5203 10 1
if errorlevel 1 goto ethbench_loopback_failed
"%S%\ethbench.exe" loop-rx 127.0.0.1 5203 10 1
if errorlevel 1 goto ethbench_loopback_failed
"%S%\dbgprint.exe" ETHBENCH_LOOPBACK_END

"%S%\dbgprint.exe" ETHBENCH_BEGIN scope=rp1gem directions=rx,tx seconds=!ETHBENCH_SECONDS! threads=1
call :run_remote_benchmark ETHBENCH
if not "!NETWORK_BENCH_EXIT!" == "0" goto ethbench_failed
"%S%\dbgprint.exe" ETHBENCH_END status=passed
goto ethbench_finished

:ethbench_loopback_failed
"%S%\dbgprint.exe" ETHBENCH_LOOPBACK_FAILED
goto ethbench_failed

:ethbench_missing
"%S%\dbgprint.exe" ETHBENCH_TOOL_MISSING

:ethbench_failed
"%S%\dbgprint.exe" ETHBENCH_END status=failed
set ETHBENCH_EXIT=1

:ethbench_finished
if not "!ETHBENCH_EXIT!" == "0" set /a BOOT_TEST_FAILURES+=1
"%S%\dbgprint.exe" ETHBENCH_EXIT !ETHBENCH_EXIT!
exit /b 0

:run_rpi5_wifi
set /a BOOT_TEST_SELECTED+=1
set WIFI_EXIT=0
"%S%\dbgprint.exe" RPI5_WIFI_BEGIN
if not exist "%S%\wlanscan.exe" goto wifi_missing
"%S%\ping.exe" -n 6 127.0.0.1 >nul
"%S%\wlanscan.exe" > "%WIFI_LOG%" 2>&1
set WIFI_EXIT=!ERRORLEVEL!
type "%WIFI_LOG%"
"%S%\findstr.exe" /i /c:"No wireless interfaces present" /c:"failed:" "%WIFI_LOG%" >nul 2>nul
if not errorlevel 1 set WIFI_EXIT=1
del /q "%WIFI_LOG%" 2>nul
if not "!WIFI_EXIT!" == "0" goto wifi_finished

if "!WIFI_SSID!" == "" (
    "%S%\dbgprint.exe" RPI5_WIFI_CONNECT_SKIPPED no-config
    goto wifi_finished
)
if "!WIFI_KEY_FILE!" == "" goto wifi_config_incomplete
if not exist "!WIFI_KEY_FILE!" goto wifi_key_missing
if not exist "%S%\wlanconn.exe" goto wifi_missing

"%S%\dbgprint.exe" RPI5_WIFI_CONNECT_BEGIN
"%S%\wlanconn.exe" "!WIFI_SSID!" --key-file "!WIFI_KEY_FILE!"
set WIFI_EXIT=!ERRORLEVEL!
"%S%\dbgprint.exe" RPI5_WIFI_CONNECT_EXIT !WIFI_EXIT!
if not "!WIFI_EXIT!" == "0" goto wifi_finished

"%S%\ping.exe" -n 14 127.0.0.1 >nul
"%S%\ipconfig.exe" /all
if not exist "%S%\ethbench.exe" goto wifi_missing
"%S%\dbgprint.exe" RPI5_WIFI_BENCH_BEGIN directions=rx,tx seconds=!ETHBENCH_SECONDS! threads=1
call :run_remote_benchmark RPI5_WIFI
if not "!NETWORK_BENCH_EXIT!" == "0" set WIFI_EXIT=1
"%S%\dbgprint.exe" RPI5_WIFI_BENCH_END status=!NETWORK_BENCH_EXIT!
goto wifi_finished

:wifi_config_incomplete
"%S%\dbgprint.exe" RPI5_WIFI_CONFIG_INCOMPLETE
set WIFI_EXIT=1
goto wifi_finished

:wifi_key_missing
"%S%\dbgprint.exe" RPI5_WIFI_KEY_FILE_MISSING
set WIFI_EXIT=1
goto wifi_finished

:wifi_missing
"%S%\dbgprint.exe" RPI5_WIFI_TOOL_MISSING
set WIFI_EXIT=1

:wifi_finished
if not "!WIFI_EXIT!" == "0" set /a BOOT_TEST_FAILURES+=1
"%S%\dbgprint.exe" RPI5_WIFI_EXIT !WIFI_EXIT!
"%S%\dbgprint.exe" RPI5_WIFI_END
exit /b 0

:run_remote_benchmark
set NETWORK_LABEL=%~1
set NETWORK_BENCH_EXIT=0
"%S%\dbgprint.exe" !NETWORK_LABEL!_RX_BEGIN
"%S%\ethbench.exe" rx !ETHBENCH_PEER! 5202 !ETHBENCH_SECONDS! 1
set NETWORK_RX_EXIT=!ERRORLEVEL!
"%S%\dbgprint.exe" !NETWORK_LABEL!_RX_EXIT !NETWORK_RX_EXIT!
"%S%\dbgprint.exe" !NETWORK_LABEL!_TX_BEGIN
"%S%\ethbench.exe" tx !ETHBENCH_PEER! 5202 !ETHBENCH_SECONDS! 1
set NETWORK_TX_EXIT=!ERRORLEVEL!
"%S%\dbgprint.exe" !NETWORK_LABEL!_TX_EXIT !NETWORK_TX_EXIT!
if not "!NETWORK_TX_EXIT!!NETWORK_RX_EXIT!" == "00" set NETWORK_BENCH_EXIT=1
exit /b 0

:run_cpubench
set /a BOOT_TEST_SELECTED+=1
"%S%\dbgprint.exe" CPUBENCH_BEGIN
if not exist "%S%\cpubench.exe" goto cpubench_missing
"%S%\cpubench.exe"
set CPUBENCH_EXIT=!ERRORLEVEL!
goto cpubench_finished

:cpubench_missing
"%S%\dbgprint.exe" CPUBENCH_MISSING
set CPUBENCH_EXIT=1

:cpubench_finished
if not "!CPUBENCH_EXIT!" == "0" set /a BOOT_TEST_FAILURES+=1
"%S%\dbgprint.exe" CPUBENCH_EXIT !CPUBENCH_EXIT!
"%S%\dbgprint.exe" CPUBENCH_END
exit /b 0

:run_kmtests
set /a BOOT_TEST_SELECTED+=1
set /a KMTEST_COUNT=0
set /a KMTEST_FAILURES=0
set /a KMTEST_SKIPPED=0
"%S%\dbgprint.exe" KMTEST_SUITE_BEGIN all
if not exist "%BIN%\kmtest.exe" goto kmtest_missing
if not exist "%BIN%\kmtest_drv.sys" goto kmtest_missing

"%BIN%\kmtest.exe" --list-all > "%KMTEST_LIST%" 2>&1
set KMTEST_LIST_EXIT=!ERRORLEVEL!
if not "!KMTEST_LIST_EXIT!" == "0" goto kmtest_list_failed

rem --list-all indents each name; tokens=* removes that indentation.
for /f "usebackq skip=1 tokens=*" %%T in ("%KMTEST_LIST%") do (
    if not "%%T" == "" (
        set /a KMTEST_COUNT+=1
        set KMTEST_RUN=1
        set KMTEST_SKIP_REASON=
        if /i "%%T" == "ExHardErrorInteractive" (
            set KMTEST_RUN=0
            set KMTEST_SKIP_REASON=manual-ui
        )
        if /i "%%T" == "Example" (
            set KMTEST_RUN=0
            set KMTEST_SKIP_REASON=framework-negative-selftest
        )
        if /i "%%T" == "ExPools" (
            set KMTEST_RUN=0
            set KMTEST_SKIP_REASON=manual-only
        )
        if "!KMTEST_RUN!" == "0" (
            set /a KMTEST_SKIPPED+=1
            "%S%\dbgprint.exe" KMTEST_SKIP %%T !KMTEST_SKIP_REASON!
        ) else (
            "%S%\dbgprint.exe" KMTEST_BEGIN %%T
            "%BIN%\kmtest.exe" %%T > "!KMTEST_LOG!" 2>&1
            set KMTEST_EXIT=!ERRORLEVEL!
            "%S%\findstr.exe" /c:"Test failed:" "!KMTEST_LOG!" >nul 2>nul
            if not errorlevel 1 set KMTEST_EXIT=1
            type "!KMTEST_LOG!"
            del /q "!KMTEST_LOG!" 2>nul
            if not "!KMTEST_EXIT!" == "0" set /a KMTEST_FAILURES+=1
            "%S%\dbgprint.exe" KMTEST_EXIT %%T !KMTEST_EXIT!
            "%S%\dbgprint.exe" KMTEST_END %%T
        )
    )
)

del /q "%KMTEST_LIST%" 2>nul
goto kmtest_finished

:kmtest_list_failed
del /q "%KMTEST_LIST%" 2>nul
"%S%\dbgprint.exe" KMTEST_LIST_FAILED !KMTEST_LIST_EXIT!
set /a KMTEST_FAILURES+=1
goto kmtest_finished

:kmtest_missing
"%S%\dbgprint.exe" KMTEST_RUNNER_MISSING
set /a KMTEST_FAILURES+=1

:kmtest_finished
set /a BOOT_TEST_FAILURES+=KMTEST_FAILURES
"%S%\dbgprint.exe" KMTEST_SUITE_EXIT !KMTEST_FAILURES!
"%S%\dbgprint.exe" KMTEST_SUITE_END all count=!KMTEST_COUNT! failures=!KMTEST_FAILURES! skipped=!KMTEST_SKIPPED!
exit /b 0

:run_rosautotest
set /a BOOT_TEST_SELECTED+=1
"%S%\dbgprint.exe" ROSAUTOTEST_BEGIN
if not exist "%S%\rosautotest.exe" goto rosautotest_missing
pushd "%BIN%"
if errorlevel 1 goto rosautotest_missing
"%S%\rosautotest.exe" /r /n
set ROSAUTOTEST_EXIT=!ERRORLEVEL!
popd
goto rosautotest_finished

:rosautotest_missing
"%S%\dbgprint.exe" ROSAUTOTEST_MISSING
set ROSAUTOTEST_EXIT=1

:rosautotest_finished
if not "!ROSAUTOTEST_EXIT!" == "0" set /a BOOT_TEST_FAILURES+=1
"%S%\dbgprint.exe" ROSAUTOTEST_EXIT !ROSAUTOTEST_EXIT!
"%S%\dbgprint.exe" ROSAUTOTEST_END
exit /b 0
