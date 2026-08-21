@echo off
setlocal enabledelayedexpansion
set "_EXIT_CODE=0"

REM menuconfig.cmd - interactive ReactOS build configuration (OpenWrt/kconfig
REM style). Compiles the rosconfig host tool on first use, opens the console
REM UI to enable/disable build options, and stores selections in one output
REM tree. The generated overrides.cmake is picked up by \PreLoad.cmake.
REM See sdk\tools\rosconfig\README.md.

set "_ROSCONFIG_DIR=%~dp0.rosconfig"
set "_ROSCONFIG_BUILD=%~dp0sdk\tools\rosconfig\build.cmd"
set "_ROSCONFIG_DEF=%~dp0sdk\cmake\rosconfig.def"
set "_ROSCONFIG_BIN=%_ROSCONFIG_DIR%\rosconfig.exe"

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
for %%I in ("%~dp0.") do set "_SOURCE_DIR=%%~fI"
if "%~1" == "" (
    for %%I in ("%CD%") do (
        set "_BUILD_DIR=%%~fI"
        set "_START_NAME=%%~nxI"
    )
    if /I not "!_START_NAME:~0,7!" == "output-" set "_BUILD_DIR=!_SOURCE_DIR!\output-MinGW-amd64-debug"
) else if /I "%~1" == "--build-dir" (
    if "%~2" == "" goto usage
    if not "%~3" == "" goto usage
    for %%I in ("%~2") do set "_BUILD_DIR=%%~fI"
) else (
    goto usage
)

for %%I in ("!_BUILD_DIR!") do set "_BUILD_DIR=%%~fI"
if /I "!_BUILD_DIR!" == "!_SOURCE_DIR!" (
    echo Error: run from an output directory or pass --build-dir output-^<toolchain^>-^<arch^>-^<type^>.
    set "_EXIT_CODE=1"
    goto quit
)

set "_ROSCONFIG_STATE_DIR=!_BUILD_DIR!\.rosconfig"
set "_ROSCONFIG_CACHE=!_ROSCONFIG_STATE_DIR!\config.cache"
set "_ROSCONFIG_OVERRIDES=!_ROSCONFIG_STATE_DIR!\overrides.cmake"
set "_CMAKE_CACHE=!_BUILD_DIR!\CMakeCache.txt"

set "_ARCH="
set "_BUILD_TYPE="
set "_TOOLCHAIN="
set "_TOOLCHAIN_FILE="
set "_INFERRED_ARCH="
set "_INFERRED_BUILD_TYPE="
set "_INFERRED_TOOLCHAIN="
for %%I in ("!_BUILD_DIR!") do set "_BUILD_NAME=%%~nxI"
for /f "tokens=1-4 delims=-" %%a in ("!_BUILD_NAME!") do (
    if /I "%%a" == "output" (
        if /I "%%b" == "Clang" set "_INFERRED_TOOLCHAIN=clang"
        if /I "%%b" == "GCC" set "_INFERRED_TOOLCHAIN=gcc"
        if /I "%%b" == "MinGW" set "_INFERRED_TOOLCHAIN=gcc"
        if /I "%%b" == "VS" set "_INFERRED_TOOLCHAIN=msvc"
        if /I "%%b" == "MSVC" set "_INFERRED_TOOLCHAIN=msvc"
        if /I "%%c" == "amd64" set "_INFERRED_ARCH=amd64"
        if /I "%%c" == "i386" set "_INFERRED_ARCH=i386"
        if /I "%%c" == "arm64" set "_INFERRED_ARCH=arm64"
        if /I "%%c" == "arm" set "_INFERRED_ARCH=arm"
        if /I "%%d" == "debug" set "_INFERRED_BUILD_TYPE=Debug"
        if /I "%%d" == "release" set "_INFERRED_BUILD_TYPE=Release"
    )
)
if exist "!_CMAKE_CACHE!" (
    for /f "tokens=2 delims==" %%v in ('findstr /b /c:"ARCH:" "!_CMAKE_CACHE!"') do set "_ARCH=%%v"
    for /f "tokens=2 delims==" %%v in ('findstr /b /c:"CMAKE_BUILD_TYPE:" "!_CMAKE_CACHE!"') do set "_BUILD_TYPE=%%v"
    for /f "tokens=2 delims==" %%v in ('findstr /b /c:"CMAKE_TOOLCHAIN_FILE:" "!_CMAKE_CACHE!"') do set "_TOOLCHAIN_FILE=%%v"
)
if not defined _ARCH if exist "!_ROSCONFIG_CACHE!" for /f "tokens=2 delims==" %%v in ('findstr /b /c:"ARCH=" "!_ROSCONFIG_CACHE!"') do set "_ARCH=%%v"
if not defined _BUILD_TYPE if exist "!_ROSCONFIG_CACHE!" for /f "tokens=2 delims==" %%v in ('findstr /b /c:"BUILD_TYPE=" "!_ROSCONFIG_CACHE!"') do set "_BUILD_TYPE=%%v"
echo !_TOOLCHAIN_FILE! | findstr /I /C:"toolchain-clang.cmake" > NUL && set "_TOOLCHAIN=clang"
echo !_TOOLCHAIN_FILE! | findstr /I /C:"toolchain-gcc.cmake" > NUL && set "_TOOLCHAIN=gcc"
echo !_TOOLCHAIN_FILE! | findstr /I /C:"toolchain-msvc.cmake" > NUL && set "_TOOLCHAIN=msvc"
if not defined _TOOLCHAIN if exist "!_ROSCONFIG_CACHE!" for /f "tokens=2 delims==" %%v in ('findstr /b /c:"TOOLCHAIN=" "!_ROSCONFIG_CACHE!"') do set "_TOOLCHAIN=%%v"
if not defined _ARCH set "_ARCH=!_INFERRED_ARCH!"
if not defined _BUILD_TYPE set "_BUILD_TYPE=!_INFERRED_BUILD_TYPE!"
if not defined _TOOLCHAIN set "_TOOLCHAIN=!_INFERRED_TOOLCHAIN!"

if not defined _ARCH (
    echo Error: cannot determine a target architecture for !_BUILD_DIR!.
    set "_EXIT_CODE=1"
    goto quit
)
if not defined _BUILD_TYPE (
    echo Error: cannot determine a build type for !_BUILD_DIR!.
    set "_EXIT_CODE=1"
    goto quit
)
if not defined _TOOLCHAIN (
    echo Error: cannot determine a toolchain for !_BUILD_DIR!.
    set "_EXIT_CODE=1"
    goto quit
)

if not exist "!_BUILD_DIR!" (
    echo Creating output configuration directory: !_BUILD_DIR!
    mkdir "!_BUILD_DIR!"
)
if not exist "!_BUILD_DIR!" (
    echo Error: could not create output directory: !_BUILD_DIR!
    set "_EXIT_CODE=1"
    goto quit
)
if not exist "!_ROSCONFIG_STATE_DIR!" mkdir "!_ROSCONFIG_STATE_DIR!"
"%_ROSCONFIG_BIN%" --def "%_ROSCONFIG_DEF%" --cache "!_ROSCONFIG_CACHE!" --defaults --set "ARCH=!_ARCH!" --set "TOOLCHAIN=!_TOOLCHAIN!" --set "BUILD_TYPE=!_BUILD_TYPE!"
if not "!ERRORLEVEL!" == "0" (
    set "_EXIT_CODE=!ERRORLEVEL!"
    goto quit
)

:run_menu
"%_ROSCONFIG_BIN%" --def "%_ROSCONFIG_DEF%" --cache "!_ROSCONFIG_CACHE!" --menu
if not "!ERRORLEVEL!" == "0" (
    set "_EXIT_CODE=!ERRORLEVEL!"
    if "!ERRORLEVEL!" == "130" echo menuconfig.cmd: cancelled; configuration was not regenerated.
    goto quit
)

REM Preserve the identity encoded by the selected output directory.
"%_ROSCONFIG_BIN%" --def "%_ROSCONFIG_DEF%" --cache "!_ROSCONFIG_CACHE!" --defaults --set "ARCH=!_ARCH!" --set "TOOLCHAIN=!_TOOLCHAIN!" --set "BUILD_TYPE=!_BUILD_TYPE!"
if not "!ERRORLEVEL!" == "0" (
    set "_EXIT_CODE=!ERRORLEVEL!"
    goto quit
)
"%_ROSCONFIG_BIN%" --def "%_ROSCONFIG_DEF%" --cache "!_ROSCONFIG_CACHE!" --generate "!_ROSCONFIG_OVERRIDES!"
if not "!ERRORLEVEL!" == "0" (
    set "_EXIT_CODE=!ERRORLEVEL!"
    goto quit
)

echo.
echo Configuration stored in !_ROSCONFIG_CACHE!.
echo Re-run configure.cmd for this output tree to apply these settings
echo ^(command-line flags and -D options still take precedence^).
goto quit

:build_failed
echo Error: failed to build the rosconfig tool.
set "_EXIT_CODE=1"
goto quit

:usage
echo Usage: menuconfig.cmd [--build-dir ^<output-directory^>] [--self-test]
set "_EXIT_CODE=2"

:quit
endlocal & exit /b %_EXIT_CODE%
