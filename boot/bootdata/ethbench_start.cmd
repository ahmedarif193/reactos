@echo off
%SystemRoot%\system32\dbgprint.exe ETHBENCH_BEGIN
%SystemRoot%\system32\ipconfig.exe
%SystemRoot%\system32\ping.exe -n 3 192.168.223.1
%SystemRoot%\system32\dbgprint.exe ETHBENCH_TX
%SystemRoot%\system32\ethbench.exe tx 192.168.223.1 5201 5
%SystemRoot%\system32\dbgprint.exe ETHBENCH_RX
%SystemRoot%\system32\ethbench.exe rx 192.168.223.1 5201 5
%SystemRoot%\system32\dbgprint.exe ETHBENCH_END
