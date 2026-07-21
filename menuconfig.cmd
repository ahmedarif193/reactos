@echo off
setlocal enabledelayedexpansion
set "_EXIT_CODE=0"

REM menuconfig.cmd - interactive ReactOS build configuration (OpenWrt/kconfig
REM style). Compiles the rosconfig host tool on first use, opens the console
REM UI to enable/disable build options, and stores the selections in the
REM untracked .rosconfig\config.cache. The generated overrides.cmake is
REM picked up by \PreLoad.cmake whenever a tree is configured
REM (configure.cmd, configure.sh or plain cmake).
REM See sdk\tools\rosconfig\README.md.

set "_ROSCONFIG_DIR=%~dp0.rosconfig"
set "_ROSCONFIG_CACHE=%_ROSCONFIG_DIR%\config.cache"
set "_ROSCONFIG_BUILD=%~dp0sdk\tools\rosconfig\build.cmd"
set "_ROSCONFIG_DEF=%~dp0sdk\cmake\rosconfig.def"
set "_ROSCONFIG_BIN=%_ROSCONFIG_DIR%\rosconfig.exe"
set "_ROSCONFIG_OVERRIDES=%_ROSCONFIG_DIR%\overrides.cmake"

if not exist "%_ROSCONFIG_BUILD%" (
    echo Error: missing %_ROSCONFIG_BUILD%
    goto quit
)

call "%_ROSCONFIG_BUILD%" "%_ROSCONFIG_DIR%" "%_ROSCONFIG_BIN%" || goto build_failed

if /I "%~1" == "--self-test" (
    if not "%~2" == "" goto usage
    "%_ROSCONFIG_BIN%" --self-test
    set "_EXIT_CODE=!ERRORLEVEL!"
    goto quit
)
if not "%~1" == "" goto usage

:run_menu
"%_ROSCONFIG_BIN%" --def "%_ROSCONFIG_DEF%" --cache "%_ROSCONFIG_CACHE%" --menu
if not "!ERRORLEVEL!" == "0" (
    set "_EXIT_CODE=!ERRORLEVEL!"
    if "!ERRORLEVEL!" == "130" echo menuconfig.cmd: cancelled; configuration was not regenerated.
    goto quit
)

REM Make sure the cache exists even if the user quit without saving, and
REM refresh the CMake fragment consumed by \PreLoad.cmake.
"%_ROSCONFIG_BIN%" --def "%_ROSCONFIG_DEF%" --cache "%_ROSCONFIG_CACHE%" --defaults
if not "!ERRORLEVEL!" == "0" (
    set "_EXIT_CODE=!ERRORLEVEL!"
    goto quit
)
"%_ROSCONFIG_BIN%" --def "%_ROSCONFIG_DEF%" --cache "%_ROSCONFIG_CACHE%" --generate "%_ROSCONFIG_OVERRIDES%"
if not "!ERRORLEVEL!" == "0" (
    set "_EXIT_CODE=!ERRORLEVEL!"
    goto quit
)

echo.
echo Configuration stored in .rosconfig\config.cache.
echo Run configure.cmd to configure a build tree with these settings
echo ^(command-line flags and -D options still take precedence^).
goto quit

:build_failed
echo Error: failed to build the rosconfig tool.
set "_EXIT_CODE=1"
goto quit

:usage
echo Usage: menuconfig.cmd [--self-test]
set "_EXIT_CODE=2"

:quit
endlocal & exit /b %_EXIT_CODE%
