@echo off
setlocal enabledelayedexpansion

REM Incremental host build for rosconfig. Each translation unit has its own
REM object and source stamp, so only changed code is recompiled.

if "%~2" == "" (
    echo usage: build.cmd ^<build-directory^> ^<output-binary^> 1>&2
    exit /b 2
)

set "_SRC_DIR=%~dp0"
set "_BUILD_DIR=%~1"
set "_OUTPUT=%~2"
set "_HEADER=%_SRC_DIR%rosconfig.h"
set "_HEADER_STAMP=%_BUILD_DIR%\rosconfig.h.stamp"
set "_COMPILER_STAMP=%_BUILD_DIR%\rosconfig.compiler"
set "_LINK_PENDING=%_BUILD_DIR%\rosconfig.link.pending"

for %%S in (rosconfig rosconfig_util rosconfig_model rosconfig_ui rosconfig_selftest) do (
    if not exist "%_SRC_DIR%%%S.c" (
        echo rosconfig: missing %_SRC_DIR%%%S.c 1>&2
        exit /b 1
    )
)
if not exist "%_HEADER%" (
    echo rosconfig: missing %_HEADER% 1>&2
    exit /b 1
)
if not exist "%_BUILD_DIR%" mkdir "%_BUILD_DIR%"

set "_CC="
set "_CC_KIND="
where gcc >NUL 2>&1 && set "_CC=gcc" && set "_CC_KIND=gnu"
if not defined _CC where x86_64-w64-mingw32-gcc >NUL 2>&1 && set "_CC=x86_64-w64-mingw32-gcc" && set "_CC_KIND=gnu"
if not defined _CC where i686-w64-mingw32-gcc >NUL 2>&1 && set "_CC=i686-w64-mingw32-gcc" && set "_CC_KIND=gnu"
if not defined _CC where aarch64-w64-mingw32-gcc >NUL 2>&1 && set "_CC=aarch64-w64-mingw32-gcc" && set "_CC_KIND=gnu"
if not defined _CC where clang >NUL 2>&1 && set "_CC=clang" && set "_CC_KIND=gnu"

if not defined _CC (
    if not defined ROSBE_ROOT (
        set "_ROSBE_BASE=%LOCALAPPDATA%\RosBE"
        if exist "!_ROSBE_BASE!\pkgs" (
            for /f "delims=" %%D in ('dir /b /ad /o-n "!_ROSBE_BASE!\pkgs" 2^>NUL') do (
                if not defined ROSBE_ROOT if exist "!_ROSBE_BASE!\pkgs\%%D\rosbe-components.json" set "ROSBE_ROOT=!_ROSBE_BASE!\pkgs\%%D"
            )
        )
    )
    if defined ROSBE_ROOT (
        for %%T in (x86_64-w64-mingw32 i686-w64-mingw32 aarch64-w64-mingw32) do (
            if not defined _CC if exist "!ROSBE_ROOT!\mingw-gcc\%%T\bin\%%T-gcc.exe" (
                set "_CC=!ROSBE_ROOT!\mingw-gcc\%%T\bin\%%T-gcc.exe"
                set "_CC_KIND=gnu"
            )
        )
    )
)
if not defined _CC where cl >NUL 2>&1 && set "_CC=cl" && set "_CC_KIND=msvc"
if not defined _CC (
    if exist "%_OUTPUT%" (
        "%_OUTPUT%" --self-test >NUL 2>&1
        if not errorlevel 1 (
            echo rosconfig: no host C compiler found; using tested cached %_OUTPUT%
            exit /b 0
        )
    )
    echo rosconfig: no host C compiler found ^(gcc, clang or cl^) 1>&2
    exit /b 2
)

set "_HEADER_CHANGED=1"
if exist "%_HEADER_STAMP%" fc /b "%_HEADER%" "%_HEADER_STAMP%" >NUL 2>&1 && set "_HEADER_CHANGED=0"
set "_COMPILER_CHANGED=1"
set "_COMPILER_SIGNATURE=%_CC_KIND%:%_CC%"
if exist "%_COMPILER_STAMP%" (
    set "_OLD_COMPILER="
    set /p "_OLD_COMPILER=" < "%_COMPILER_STAMP%"
    if "!_OLD_COMPILER!" == "!_COMPILER_SIGNATURE!" set "_COMPILER_CHANGED=0"
)
set "_NEED_LINK=0"

call :compile rosconfig || exit /b 1
call :compile rosconfig_util || exit /b 1
call :compile rosconfig_model || exit /b 1
call :compile rosconfig_ui || exit /b 1
call :compile rosconfig_selftest || exit /b 1

if "!_HEADER_CHANGED!" == "1" copy /y "%_HEADER%" "%_HEADER_STAMP%" >NUL
if not exist "%_OUTPUT%" set "_NEED_LINK=1"
if exist "!_LINK_PENDING!" set "_NEED_LINK=1"
if "!_NEED_LINK!" == "0" exit /b 0

echo   LINK    %~nx2
set "_TEMP_OUTPUT=%_OUTPUT%.tmp.exe"
if exist "!_TEMP_OUTPUT!" del /q "!_TEMP_OUTPUT!"
if "%_CC_KIND%" == "gnu" (
    "%_CC%" -o "!_TEMP_OUTPUT!" "%_BUILD_DIR%\rosconfig.obj" "%_BUILD_DIR%\rosconfig_util.obj" "%_BUILD_DIR%\rosconfig_model.obj" "%_BUILD_DIR%\rosconfig_ui.obj" "%_BUILD_DIR%\rosconfig_selftest.obj" || exit /b 1
) else (
    cl /nologo "/Fe!_TEMP_OUTPUT!" "%_BUILD_DIR%\rosconfig.obj" "%_BUILD_DIR%\rosconfig_util.obj" "%_BUILD_DIR%\rosconfig_model.obj" "%_BUILD_DIR%\rosconfig_ui.obj" "%_BUILD_DIR%\rosconfig_selftest.obj" || exit /b 1
)
move /y "!_TEMP_OUTPUT!" "%_OUTPUT%" >NUL
> "%_COMPILER_STAMP%" echo !_COMPILER_SIGNATURE!
if exist "!_LINK_PENDING!" del /q "!_LINK_PENDING!"
exit /b 0

:compile
set "_NAME=%~1"
set "_SOURCE=%_SRC_DIR%!_NAME!.c"
set "_OBJECT=%_BUILD_DIR%\!_NAME!.obj"
set "_STAMP=%_BUILD_DIR%\!_NAME!.c.stamp"
set "_NEED_OBJECT=%_HEADER_CHANGED%"
if "!_COMPILER_CHANGED!" == "1" set "_NEED_OBJECT=1"
if not exist "!_OBJECT!" set "_NEED_OBJECT=1"
if not exist "!_STAMP!" (
    set "_NEED_OBJECT=1"
) else (
    fc /b "!_SOURCE!" "!_STAMP!" >NUL 2>&1 || set "_NEED_OBJECT=1"
)
if "!_NEED_OBJECT!" == "0" exit /b 0

echo   CC      !_NAME!.c
set "_TEMP_OBJECT=%_BUILD_DIR%\!_NAME!.tmp.obj"
if exist "!_TEMP_OBJECT!" del /q "!_TEMP_OBJECT!"
if "%_CC_KIND%" == "gnu" (
    "%_CC%" -O2 -I"%_SRC_DIR%" -c "!_SOURCE!" -o "!_TEMP_OBJECT!" || exit /b 1
) else (
    cl /nologo /O2 /I"%_SRC_DIR%" /c "/Fo!_TEMP_OBJECT!" "!_SOURCE!" || exit /b 1
)
move /y "!_TEMP_OBJECT!" "!_OBJECT!" >NUL
> "!_LINK_PENDING!" echo pending
copy /y "!_SOURCE!" "!_STAMP!" >NUL
set "_NEED_LINK=1"
exit /b 0
