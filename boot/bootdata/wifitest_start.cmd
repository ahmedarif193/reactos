@echo off
ping -n 6 127.0.0.1 >nul
%SystemRoot%\system32\dbgprint.exe WIFITEST_SCAN
%SystemRoot%\system32\wlanscan.exe
%SystemRoot%\system32\dbgprint.exe WIFITEST_CONNECT
%SystemRoot%\system32\wlanconn.exe SFR_EBFF 8rmhuivgmsd8r9ptfza1
%SystemRoot%\system32\dbgprint.exe WIFITEST_DHCPWAIT
ping -n 14 127.0.0.1 >nul
%SystemRoot%\system32\dbgprint.exe WIFITEST_ETHBENCH_RX
%SystemRoot%\system32\ethbench.exe rx 192.168.1.244 5201 12
%SystemRoot%\system32\dbgprint.exe WIFITEST_ETHBENCH_RX4
%SystemRoot%\system32\ethbench.exe rx 192.168.1.244 5201 12 4
%SystemRoot%\system32\dbgprint.exe WIFITEST_ETHBENCH_TX
%SystemRoot%\system32\ethbench.exe tx 192.168.1.244 5201 12
%SystemRoot%\system32\dbgprint.exe WIFITEST_ETHBENCH_UDP
%SystemRoot%\system32\ethbench.exe udp 192.168.1.244 5201 10
%SystemRoot%\system32\dbgprint.exe WIFITEST_DONE
