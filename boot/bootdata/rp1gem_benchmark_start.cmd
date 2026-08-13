@echo off

set S=%SystemRoot%\system32
set ETHBENCH_PEER=10.42.0.1
set ETHBENCH_SECONDS=30
set ETHBENCH_STATUS=failed

"%S%\dbgprint.exe" ETHBENCH_LOOPBACK_BEGIN directions=tx,rx seconds=10 threads=1
"%S%\ethbench.exe" loop-tx 127.0.0.1 5203 10 1
if errorlevel 1 goto loopback_failed
"%S%\ethbench.exe" loop-rx 127.0.0.1 5203 10 1
if errorlevel 1 goto loopback_failed
"%S%\dbgprint.exe" ETHBENCH_LOOPBACK_END

"%S%\dbgprint.exe" ETHBENCH_BEGIN scope=rp1gem directions=rx,tx seconds=%ETHBENCH_SECONDS% threads=1
"%S%\dbgprint.exe" ETHBENCH_RX_BEGIN
"%S%\ethbench.exe" rx %ETHBENCH_PEER% 5202 %ETHBENCH_SECONDS% 1
if errorlevel 1 goto rx_failed
"%S%\dbgprint.exe" ETHBENCH_RX_END
"%S%\dbgprint.exe" ETHBENCH_TX_BEGIN
"%S%\ethbench.exe" tx %ETHBENCH_PEER% 5202 %ETHBENCH_SECONDS% 1
if errorlevel 1 goto tx_failed
"%S%\dbgprint.exe" ETHBENCH_TX_END
"%S%\dbgprint.exe" ETHBENCH_END status=passed
set ETHBENCH_STATUS=passed
goto :eof

:rx_failed
"%S%\dbgprint.exe" ETHBENCH_RX_FAILED
goto failed

:tx_failed
"%S%\dbgprint.exe" ETHBENCH_TX_FAILED
goto failed

:loopback_failed
"%S%\dbgprint.exe" ETHBENCH_LOOPBACK_FAILED
goto failed

:failed
"%S%\dbgprint.exe" ETHBENCH_END status=failed
goto :eof
