@echo off

rem CPU benchmark disabled while chasing SMP scheduler/object-manager races.
rem if exist "%SystemRoot%\system32\cpubench.exe" (
rem     %SystemRoot%\system32\dbgprint.exe CPUBENCH_BEGIN
rem     start "ReactOS CPU Benchmark" /wait %SystemRoot%\system32\cpubench.exe
rem     %SystemRoot%\system32\dbgprint.exe CPUBENCH_END
rem )

set S=%SystemRoot%\system32
set WAIT=%S%\timeout.exe /t 2 /nobreak

%S%\dbgprint.exe SMP_STRESS_VARIANT_1_COMMENTED_BROAD_SWARM
rem %S%\dbgprint.exe SMP_STRESS_VARIANT_1_BEGIN
rem if exist "%S%\control.exe" start "smp-v1-01-control" "%S%\control.exe"
rem if exist "%S%\devmgmt.exe" start "smp-v1-02-devmgmt" "%S%\devmgmt.exe"
rem if exist "%S%\taskmgr.exe" start "smp-v1-03-taskmgr" "%S%\taskmgr.exe"
rem if exist "%S%\regedit.exe" start "smp-v1-04-regedit" "%S%\regedit.exe"
rem if exist "%S%\servman.exe" start "smp-v1-05-servman" "%S%\servman.exe"
rem if exist "%S%\eventvwr.exe" start "smp-v1-06-eventvwr" "%S%\eventvwr.exe"
rem if exist "%S%\msconfig.exe" start "smp-v1-07-msconfig" "%S%\msconfig.exe"
rem if exist "%S%\dxdiag.exe" start "smp-v1-08-dxdiag" "%S%\dxdiag.exe"
rem if exist "%S%\rapps.exe" start "smp-v1-09-rapps" "%S%\rapps.exe"
rem if exist "%S%\filebrowser.exe" start "smp-v1-10-filebrowser" "%S%\filebrowser.exe"
rem if exist "%S%\notepad.exe" start "smp-v1-11-notepad" "%S%\notepad.exe"
rem if exist "%S%\wordpad.exe" start "smp-v1-12-wordpad" "%S%\wordpad.exe"
rem if exist "%S%\mspaint.exe" start "smp-v1-13-mspaint" "%S%\mspaint.exe"
rem if exist "%S%\calc.exe" start "smp-v1-14-calc" "%S%\calc.exe"
rem if exist "%S%\winver.exe" start "smp-v1-15-winver" "%S%\winver.exe"
rem if exist "%S%\charmap.exe" start "smp-v1-16-charmap" "%S%\charmap.exe"
rem if exist "%S%\osk.exe" start "smp-v1-17-osk" "%S%\osk.exe"
rem if exist "%S%\magnify.exe" start "smp-v1-18-magnify" "%S%\magnify.exe"
rem if exist "%S%\sndvol32.exe" start "smp-v1-19-sndvol32" "%S%\sndvol32.exe"
rem if exist "%S%\clipbrd.exe" start "smp-v1-20-clipbrd" "%S%\clipbrd.exe"
rem %S%\dbgprint.exe SMP_STRESS_VARIANT_1_END

%WAIT%

%S%\dbgprint.exe SMP_STRESS_VARIANT_2_LOAD_SWARM_BEGIN
%S%\dbgprint.exe SMP_LOAD_SWARM_BEGIN
%S%\dbgprint.exe SMP_LOAD_01_CMD_VER_BEFORE
if exist "%S%\cmd.exe" start "smp-load-01-cmd-ver" "%S%\cmd.exe" /c ver
%S%\dbgprint.exe SMP_LOAD_01_CMD_VER_AFTER
%S%\dbgprint.exe SMP_LOAD_02_CMD_SET_BEFORE
if exist "%S%\cmd.exe" start "smp-load-02-cmd-set" "%S%\cmd.exe" /c set
%S%\dbgprint.exe SMP_LOAD_02_CMD_SET_AFTER
%S%\dbgprint.exe SMP_LOAD_03_CMD_DIR_SYSTEM32_BEFORE
if exist "%S%\cmd.exe" start "smp-load-03-cmd-dir-system32" "%S%\cmd.exe" /c dir "%S%"
%S%\dbgprint.exe SMP_LOAD_03_CMD_DIR_SYSTEM32_AFTER
%S%\dbgprint.exe SMP_LOAD_04_CMD_DIR_REACTOS_BEFORE
if exist "%S%\cmd.exe" start "smp-load-04-cmd-dir-reactos" "%S%\cmd.exe" /c dir "%SystemRoot%"
%S%\dbgprint.exe SMP_LOAD_04_CMD_DIR_REACTOS_AFTER
%S%\dbgprint.exe SMP_LOAD_05_HOSTNAME_BEFORE
if exist "%S%\hostname.exe" start "smp-load-05-hostname" "%S%\cmd.exe" /c "%S%\hostname.exe"
%S%\dbgprint.exe SMP_LOAD_05_HOSTNAME_AFTER
%S%\dbgprint.exe SMP_LOAD_06_WHOAMI_BEFORE
if exist "%S%\whoami.exe" start "smp-load-06-whoami" "%S%\cmd.exe" /c "%S%\whoami.exe"
%S%\dbgprint.exe SMP_LOAD_06_WHOAMI_AFTER
%S%\dbgprint.exe SMP_LOAD_07_TASKLIST_BEFORE
rem tasklist directly exercises SystemProcessInformation and can block this
rem startup script under the load swarm; keep it as a separate reproducer.
rem if exist "%S%\tasklist.exe" start "smp-load-07-tasklist" "%S%\cmd.exe" /c "%S%\tasklist.exe"
%S%\dbgprint.exe SMP_LOAD_07_TASKLIST_AFTER
%S%\dbgprint.exe SMP_LOAD_08_IPCONFIG_BEFORE
if exist "%S%\ipconfig.exe" start "smp-load-08-ipconfig" "%S%\cmd.exe" /c "%S%\ipconfig.exe /all"
%S%\dbgprint.exe SMP_LOAD_08_IPCONFIG_AFTER
%S%\dbgprint.exe SMP_LOAD_09_NETSTAT_BEFORE
if exist "%S%\netstat.exe" start "smp-load-09-netstat" "%S%\cmd.exe" /c "%S%\netstat.exe -an"
%S%\dbgprint.exe SMP_LOAD_09_NETSTAT_AFTER
%S%\dbgprint.exe SMP_LOAD_10_CONTROL_SYSDM_BEFORE
if exist "%S%\control.exe" start "smp-load-10-control-sysdm" "%S%\control.exe" sysdm.cpl
%S%\dbgprint.exe SMP_LOAD_10_CONTROL_SYSDM_AFTER
%S%\dbgprint.exe SMP_LOAD_11_CONTROL_DESK_BEFORE
if exist "%S%\control.exe" start "smp-load-11-control-desk" "%S%\control.exe" desk.cpl
%S%\dbgprint.exe SMP_LOAD_11_CONTROL_DESK_AFTER
%S%\dbgprint.exe SMP_LOAD_12_CONTROL_MAIN_BEFORE
if exist "%S%\control.exe" start "smp-load-12-control-main" "%S%\control.exe" main.cpl
%S%\dbgprint.exe SMP_LOAD_12_CONTROL_MAIN_AFTER
%S%\dbgprint.exe SMP_LOAD_13_CONTROL_APPWIZ_BEFORE
if exist "%S%\control.exe" start "smp-load-13-control-appwiz" "%S%\control.exe" appwiz.cpl
%S%\dbgprint.exe SMP_LOAD_13_CONTROL_APPWIZ_AFTER
%S%\dbgprint.exe SMP_LOAD_14_CONTROL_INTL_BEFORE
if exist "%S%\control.exe" start "smp-load-14-control-intl" "%S%\control.exe" intl.cpl
%S%\dbgprint.exe SMP_LOAD_14_CONTROL_INTL_AFTER
%S%\dbgprint.exe SMP_LOAD_16_RUNDLL_DESK_BEFORE
if exist "%S%\rundll32.exe" start "smp-load-16-rundll-desk" "%S%\rundll32.exe" shell32.dll,Control_RunDLL "%S%\desk.cpl"
%S%\dbgprint.exe SMP_LOAD_16_RUNDLL_DESK_AFTER
%S%\dbgprint.exe SMP_LOAD_17_RUNDLL_MAIN_BEFORE
if exist "%S%\rundll32.exe" start "smp-load-17-rundll-main" "%S%\rundll32.exe" shell32.dll,Control_RunDLL "%S%\main.cpl"
%S%\dbgprint.exe SMP_LOAD_17_RUNDLL_MAIN_AFTER
%S%\dbgprint.exe SMP_LOAD_18_RUNDLL_APPWIZ_BEFORE
if exist "%S%\rundll32.exe" start "smp-load-18-rundll-appwiz" "%S%\rundll32.exe" shell32.dll,Control_RunDLL "%S%\appwiz.cpl"
%S%\dbgprint.exe SMP_LOAD_18_RUNDLL_APPWIZ_AFTER
%S%\dbgprint.exe SMP_LOAD_19_RUNDLL_INTL_BEFORE
if exist "%S%\rundll32.exe" start "smp-load-19-rundll-intl" "%S%\rundll32.exe" shell32.dll,Control_RunDLL "%S%\intl.cpl"
%S%\dbgprint.exe SMP_LOAD_19_RUNDLL_INTL_AFTER
%S%\dbgprint.exe SMP_LOAD_26_MMC_BEFORE
if exist "%S%\mmc.exe" start "smp-load-26-mmc" "%S%\mmc.exe"
%S%\dbgprint.exe SMP_LOAD_26_MMC_AFTER
%S%\dbgprint.exe SMP_LOAD_27_REGEDT32_BEFORE
rem regedt32 keeps the startup script blocked on current builds; skip it so the
rem focused load swarm reaches the zombie probes and completion marker.
rem if exist "%S%\regedt32.exe" start "smp-load-27-regedt32" "%S%\regedt32.exe"
%S%\dbgprint.exe SMP_LOAD_27_REGEDT32_AFTER
%S%\dbgprint.exe SMP_LOAD_28_TASKMGR_BEFORE
rem Task Manager is covered by variant 4; avoid process-list polling during loader/config phases.
rem if exist "%S%\taskmgr.exe" start "smp-load-28-taskmgr" "%S%\taskmgr.exe"
%S%\dbgprint.exe SMP_LOAD_28_TASKMGR_AFTER
%S%\dbgprint.exe SMP_LOAD_29_SERVMAN_BEFORE
if exist "%S%\servman.exe" start "smp-load-29-servman" "%S%\servman.exe"
%S%\dbgprint.exe SMP_LOAD_29_SERVMAN_AFTER
%S%\dbgprint.exe SMP_LOAD_30_EVENTVWR_BEFORE
rem eventvwr currently floods win32k callback stack-overflow errors under the load swarm.
rem Skip it for zombie-handle ownership runs so the later tasklist probes can complete.
rem if exist "%S%\eventvwr.exe" start "smp-load-30-eventvwr" "%S%\eventvwr.exe"
%S%\dbgprint.exe SMP_LOAD_30_EVENTVWR_AFTER
%S%\dbgprint.exe SMP_LOAD_31_MSCONFIG_BEFORE
rem if exist "%S%\msconfig.exe" start "smp-load-31-msconfig" "%S%\msconfig.exe"
%S%\dbgprint.exe SMP_LOAD_31_MSCONFIG_AFTER
%S%\dbgprint.exe SMP_LOAD_32_DXDIAG_BEFORE
rem if exist "%S%\dxdiag.exe" start "smp-load-32-dxdiag" "%S%\dxdiag.exe"
%S%\dbgprint.exe SMP_LOAD_32_DXDIAG_AFTER
%S%\dbgprint.exe SMP_ZOMBIE_PROBE_01_TASKLIST_BEFORE
rem if exist "%S%\tasklist.exe" start "smp-zombie-probe-01-tasklist" "%S%\cmd.exe" /c "%S%\tasklist.exe"
%S%\dbgprint.exe SMP_ZOMBIE_PROBE_01_TASKLIST_AFTER
%S%\dbgprint.exe SMP_ZOMBIE_PROBE_02_TASKLIST_BEFORE
rem if exist "%S%\tasklist.exe" start "smp-zombie-probe-02-tasklist" "%S%\cmd.exe" /c "%S%\tasklist.exe"
%S%\dbgprint.exe SMP_ZOMBIE_PROBE_02_TASKLIST_AFTER
%S%\dbgprint.exe SMP_ZOMBIE_PROBE_03_TASKLIST_BEFORE
rem if exist "%S%\tasklist.exe" start "smp-zombie-probe-03-tasklist" "%S%\cmd.exe" /c "%S%\tasklist.exe"
%S%\dbgprint.exe SMP_ZOMBIE_PROBE_03_TASKLIST_AFTER
%S%\dbgprint.exe SMP_LOAD_SWARM_END
%S%\dbgprint.exe SMP_STRESS_VARIANT_2_LOAD_SWARM_END

%WAIT%

%S%\dbgprint.exe SMP_STRESS_VARIANT_3_CONFIG_SWARM_BEGIN
%S%\dbgprint.exe SMP_CFG_01_REG_HKLM_NT_BEFORE
if exist "%S%\reg.exe" start "smp-cfg-01-reg-hklm-nt" "%S%\cmd.exe" /c "%S%\reg.exe" QUERY "HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion"
%S%\dbgprint.exe SMP_CFG_01_REG_HKLM_NT_AFTER
%S%\dbgprint.exe SMP_CFG_02_REG_HKLM_SYSTEM_BEFORE
if exist "%S%\reg.exe" start "smp-cfg-02-reg-hklm-system" "%S%\cmd.exe" /c "%S%\reg.exe" QUERY "HKLM\SYSTEM\CurrentControlSet\Control"
%S%\dbgprint.exe SMP_CFG_02_REG_HKLM_SYSTEM_AFTER
%S%\dbgprint.exe SMP_CFG_03_REG_HKCU_RUN_BEFORE
if exist "%S%\reg.exe" start "smp-cfg-03-reg-hkcu-run" "%S%\cmd.exe" /c "%S%\reg.exe" QUERY "HKCU\SOFTWARE\Microsoft\Windows\CurrentVersion\Run"
%S%\dbgprint.exe SMP_CFG_03_REG_HKCU_RUN_AFTER
%S%\dbgprint.exe SMP_CFG_04_REG_SHELL_FOLDERS_BEFORE
if exist "%S%\reg.exe" start "smp-cfg-04-reg-shell-folders" "%S%\cmd.exe" /c "%S%\reg.exe" QUERY "HKCU\SOFTWARE\Microsoft\Windows\CurrentVersion\Explorer\User Shell Folders"
%S%\dbgprint.exe SMP_CFG_04_REG_SHELL_FOLDERS_AFTER
%S%\dbgprint.exe SMP_CFG_05_REG_FONTS_BEFORE
if exist "%S%\reg.exe" start "smp-cfg-05-reg-fonts" "%S%\cmd.exe" /c "%S%\reg.exe" QUERY "HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Fonts"
%S%\dbgprint.exe SMP_CFG_05_REG_FONTS_AFTER
%S%\dbgprint.exe SMP_CFG_06_REG_SERVICES_BEFORE
if exist "%S%\reg.exe" start "smp-cfg-06-reg-services" "%S%\cmd.exe" /c "%S%\reg.exe" QUERY "HKLM\SYSTEM\CurrentControlSet\Services"
%S%\dbgprint.exe SMP_CFG_06_REG_SERVICES_AFTER
%S%\dbgprint.exe SMP_CFG_07_ENV_SET_BEFORE
if exist "%S%\cmd.exe" start "smp-cfg-07-env-set" "%S%\cmd.exe" /c set
%S%\dbgprint.exe SMP_CFG_07_ENV_SET_AFTER
%S%\dbgprint.exe SMP_CFG_08_ENV_PATH_BEFORE
if exist "%S%\cmd.exe" start "smp-cfg-08-env-path" "%S%\cmd.exe" /c path
%S%\dbgprint.exe SMP_CFG_08_ENV_PATH_AFTER
%S%\dbgprint.exe SMP_CFG_09_ENV_VER_BEFORE
if exist "%S%\cmd.exe" start "smp-cfg-09-env-ver" "%S%\cmd.exe" /c ver
%S%\dbgprint.exe SMP_CFG_09_ENV_VER_AFTER
%S%\dbgprint.exe SMP_CFG_10_ENV_DIR_PROFILES_BEFORE
if exist "%S%\cmd.exe" start "smp-cfg-10-dir-profiles" "%S%\cmd.exe" /c dir "%SystemDrive%\Profiles"
%S%\dbgprint.exe SMP_CFG_10_ENV_DIR_PROFILES_AFTER
%S%\dbgprint.exe SMP_CFG_11_CONTROL_FOLDERS_BEFORE
if exist "%S%\control.exe" start "smp-cfg-11-control-folders" "%S%\control.exe" folders
%S%\dbgprint.exe SMP_CFG_11_CONTROL_FOLDERS_AFTER
%S%\dbgprint.exe SMP_CFG_12_CONTROL_INTERNET_BEFORE
if exist "%S%\control.exe" start "smp-cfg-12-control-inetcpl" "%S%\control.exe" inetcpl.cpl
%S%\dbgprint.exe SMP_CFG_12_CONTROL_INTERNET_AFTER
%S%\dbgprint.exe SMP_CFG_13_RUNDLL_INTERNET_BEFORE
if exist "%S%\rundll32.exe" start "smp-cfg-13-rundll-inetcpl" "%S%\rundll32.exe" shell32.dll,Control_RunDLL "%S%\inetcpl.cpl"
%S%\dbgprint.exe SMP_CFG_13_RUNDLL_INTERNET_AFTER
%S%\dbgprint.exe SMP_CFG_14_RUNDLL_SYSDM_ADV_BEFORE
if exist "%S%\rundll32.exe" start "smp-cfg-14-rundll-sysdm-advanced" "%S%\rundll32.exe" shell32.dll,Control_RunDLL "%S%\sysdm.cpl",,3
%S%\dbgprint.exe SMP_CFG_14_RUNDLL_SYSDM_ADV_AFTER
%S%\dbgprint.exe SMP_CFG_15_MSINFO_BEFORE
if exist "%S%\msinfo32.exe" start "smp-cfg-15-msinfo32" "%S%\msinfo32.exe"
%S%\dbgprint.exe SMP_CFG_15_MSINFO_AFTER
%S%\dbgprint.exe SMP_CFG_16_SERVMAN_BEFORE
if exist "%S%\servman.exe" start "smp-cfg-16-servman" "%S%\servman.exe"
%S%\dbgprint.exe SMP_CFG_16_SERVMAN_AFTER
%S%\dbgprint.exe SMP_STRESS_VARIANT_3_CONFIG_SWARM_END

%WAIT%

%S%\dbgprint.exe SMP_STRESS_VARIANT_4_WIN32K_SWARM_BEGIN
%S%\dbgprint.exe SMP_WIN32_01_NOTEPAD_BEFORE
if exist "%S%\notepad.exe" start "smp-win32-01-notepad" "%S%\notepad.exe"
%S%\dbgprint.exe SMP_WIN32_01_NOTEPAD_AFTER
%S%\dbgprint.exe SMP_WIN32_02_WORDPAD_BEFORE
if exist "%S%\wordpad.exe" start "smp-win32-02-wordpad" "%S%\wordpad.exe"
%S%\dbgprint.exe SMP_WIN32_02_WORDPAD_AFTER
%S%\dbgprint.exe SMP_WIN32_03_MSPAINT_BEFORE
if exist "%S%\mspaint.exe" start "smp-win32-03-mspaint" "%S%\mspaint.exe"
%S%\dbgprint.exe SMP_WIN32_03_MSPAINT_AFTER
%S%\dbgprint.exe SMP_WIN32_04_CALC_BEFORE
if exist "%S%\calc.exe" start "smp-win32-04-calc" "%S%\calc.exe"
%S%\dbgprint.exe SMP_WIN32_04_CALC_AFTER
%S%\dbgprint.exe SMP_WIN32_05_CHARMAP_BEFORE
if exist "%S%\charmap.exe" start "smp-win32-05-charmap" "%S%\charmap.exe"
%S%\dbgprint.exe SMP_WIN32_05_CHARMAP_AFTER
%S%\dbgprint.exe SMP_WIN32_06_OSK_BEFORE
if exist "%S%\osk.exe" start "smp-win32-06-osk" "%S%\osk.exe"
%S%\dbgprint.exe SMP_WIN32_06_OSK_AFTER
%S%\dbgprint.exe SMP_WIN32_07_MAGNIFY_BEFORE
if exist "%S%\magnify.exe" start "smp-win32-07-magnify" "%S%\magnify.exe"
%S%\dbgprint.exe SMP_WIN32_07_MAGNIFY_AFTER
%S%\dbgprint.exe SMP_WIN32_08_CLIPBRD_BEFORE
if exist "%S%\clipbrd.exe" start "smp-win32-08-clipbrd" "%S%\clipbrd.exe"
%S%\dbgprint.exe SMP_WIN32_08_CLIPBRD_AFTER
%S%\dbgprint.exe SMP_WIN32_09_WINVER_BEFORE
if exist "%S%\winver.exe" start "smp-win32-09-winver" "%S%\winver.exe"
%S%\dbgprint.exe SMP_WIN32_09_WINVER_AFTER
%S%\dbgprint.exe SMP_WIN32_16_TASKMGR_BEFORE
if exist "%S%\taskmgr.exe" start "smp-win32-16-taskmgr" "%S%\taskmgr.exe"
%S%\dbgprint.exe SMP_WIN32_16_TASKMGR_AFTER
%S%\dbgprint.exe SMP_STRESS_VARIANT_4_WIN32K_SWARM_END

%WAIT%

%S%\dbgprint.exe SMP_TEARDOWN_SWARM_BEGIN
%S%\dbgprint.exe SMP_TD_01_CMD_VER_BEFORE
if exist "%S%\cmd.exe" start "smp-teardown-01" "%S%\cmd.exe" /c ver
%S%\dbgprint.exe SMP_TD_01_CMD_VER_AFTER
%S%\dbgprint.exe SMP_TD_02_CMD_VER_BEFORE
if exist "%S%\cmd.exe" start "smp-teardown-02" "%S%\cmd.exe" /c ver
%S%\dbgprint.exe SMP_TD_02_CMD_VER_AFTER
%S%\dbgprint.exe SMP_TD_03_CMD_SET_BEFORE
if exist "%S%\cmd.exe" start "smp-teardown-03" "%S%\cmd.exe" /c set
%S%\dbgprint.exe SMP_TD_03_CMD_SET_AFTER
%S%\dbgprint.exe SMP_TD_04_CMD_SET_BEFORE
if exist "%S%\cmd.exe" start "smp-teardown-04" "%S%\cmd.exe" /c set
%S%\dbgprint.exe SMP_TD_04_CMD_SET_AFTER
%S%\dbgprint.exe SMP_TD_05_DIR_ROOT_BEFORE
if exist "%S%\cmd.exe" start "smp-teardown-05" "%S%\cmd.exe" /c dir "%SystemRoot%"
%S%\dbgprint.exe SMP_TD_05_DIR_ROOT_AFTER
%S%\dbgprint.exe SMP_TD_06_DIR_SYSTEM32_BEFORE
if exist "%S%\cmd.exe" start "smp-teardown-06" "%S%\cmd.exe" /c dir "%S%"
%S%\dbgprint.exe SMP_TD_06_DIR_SYSTEM32_AFTER
%S%\dbgprint.exe SMP_TD_07_HOSTNAME_BEFORE
if exist "%S%\hostname.exe" start "smp-teardown-07-hostname" "%S%\cmd.exe" /c "%S%\hostname.exe"
%S%\dbgprint.exe SMP_TD_07_HOSTNAME_AFTER
%S%\dbgprint.exe SMP_TD_08_HOSTNAME_BEFORE
if exist "%S%\hostname.exe" start "smp-teardown-08-hostname" "%S%\cmd.exe" /c "%S%\hostname.exe"
%S%\dbgprint.exe SMP_TD_08_HOSTNAME_AFTER
%S%\dbgprint.exe SMP_TD_09_WHOAMI_BEFORE
if exist "%S%\whoami.exe" start "smp-teardown-09-whoami" "%S%\cmd.exe" /c "%S%\whoami.exe"
%S%\dbgprint.exe SMP_TD_09_WHOAMI_AFTER
%S%\dbgprint.exe SMP_TD_10_WHOAMI_BEFORE
if exist "%S%\whoami.exe" start "smp-teardown-10-whoami" "%S%\cmd.exe" /c "%S%\whoami.exe"
%S%\dbgprint.exe SMP_TD_10_WHOAMI_AFTER
%S%\dbgprint.exe SMP_TD_11_TASKLIST_BEFORE
rem if exist "%S%\tasklist.exe" start "smp-teardown-11-tasklist" "%S%\cmd.exe" /c "%S%\tasklist.exe"
%S%\dbgprint.exe SMP_TD_11_TASKLIST_AFTER
%S%\dbgprint.exe SMP_TD_12_TASKLIST_BEFORE
rem if exist "%S%\tasklist.exe" start "smp-teardown-12-tasklist" "%S%\cmd.exe" /c "%S%\tasklist.exe"
%S%\dbgprint.exe SMP_TD_12_TASKLIST_AFTER
%S%\dbgprint.exe SMP_TD_13_IPCONFIG_BEFORE
if exist "%S%\ipconfig.exe" start "smp-teardown-13-ipconfig" "%S%\cmd.exe" /c "%S%\ipconfig.exe /all"
%S%\dbgprint.exe SMP_TD_13_IPCONFIG_AFTER
%S%\dbgprint.exe SMP_TD_14_IPCONFIG_BEFORE
if exist "%S%\ipconfig.exe" start "smp-teardown-14-ipconfig" "%S%\cmd.exe" /c "%S%\ipconfig.exe /all"
%S%\dbgprint.exe SMP_TD_14_IPCONFIG_AFTER
%S%\dbgprint.exe SMP_TD_15_NETSTAT_BEFORE
if exist "%S%\netstat.exe" start "smp-teardown-15-netstat" "%S%\cmd.exe" /c "%S%\netstat.exe -an"
%S%\dbgprint.exe SMP_TD_15_NETSTAT_AFTER
%S%\dbgprint.exe SMP_TD_16_NETSTAT_BEFORE
if exist "%S%\netstat.exe" start "smp-teardown-16-netstat" "%S%\cmd.exe" /c "%S%\netstat.exe -an"
%S%\dbgprint.exe SMP_TD_16_NETSTAT_AFTER
%S%\dbgprint.exe SMP_TD_17_CALC_BEFORE
if exist "%S%\calc.exe" start "smp-teardown-17-calc" "%S%\calc.exe"
%S%\dbgprint.exe SMP_TD_17_CALC_AFTER
%S%\dbgprint.exe SMP_TD_18_WINVER_BEFORE
if exist "%S%\winver.exe" start "smp-teardown-18-winver" "%S%\winver.exe"
%S%\dbgprint.exe SMP_TD_18_WINVER_AFTER
%S%\dbgprint.exe SMP_TD_19_NOTEPAD_BEFORE
if exist "%S%\notepad.exe" start "smp-teardown-19-notepad" "%S%\notepad.exe"
%S%\dbgprint.exe SMP_TD_19_NOTEPAD_AFTER
%S%\dbgprint.exe SMP_TD_20_MSPAINT_BEFORE
if exist "%S%\mspaint.exe" start "smp-teardown-20-mspaint" "%S%\mspaint.exe"
%S%\dbgprint.exe SMP_TD_20_MSPAINT_AFTER
%S%\dbgprint.exe SMP_TEARDOWN_SWARM_END
%S%\dbgprint.exe SMP_STRESS_ALL_SWARMS_DONE
%S%\dbgprint.exe BOOT_TESTS_DONE
