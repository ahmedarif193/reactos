@echo off

REM This is needed so as to avoid static expansion of environment variables
REM inside if (...) conditionals.
REM See http://stackoverflow.com/questions/305605/weird-scope-issue-in-bat-file
REM for more explanation.
REM Precisely needed for configuring Visual Studio Environment.
setlocal enabledelayedexpansion

REM Does the user need help?
if /I "%1" == "help" goto help
if /I "%1" == "/?" (
:help
    echo Help for configure script
    echo Syntax: path\to\source\configure.cmd [script-options] [Cmake-options]
    echo Available script-options: Codeblocks, Eclipse, Makefiles, clang, VSSolution,
    echo                           /gcc ^(default, RosBE GCC^), /clang ^(RosBE llvm-mingw^),
    echo                           /r or /release ^(Release build^), /d or /debug ^(default^),
    echo                           /arch=^<amd64^|i386^|arm64^> ^(default amd64^),
    echo                           menuconfig ^(interactive configuration UI; selections
    echo                           persist in .rosconfig\config.cache^)
    echo Cmake-options: -DVARIABLE:TYPE=VALUE
    goto quit
)

REM Get the source root directory
set REACTOS_SOURCE_DIR=%~dp0
set REACTOS_SOURCE_DIR=%REACTOS_SOURCE_DIR:\=/%

REM Ensure there's no spaces in the source path
echo %REACTOS_SOURCE_DIR%| find " " > NUL
if %ERRORLEVEL% == 0 (
    echo. & echo   Your source path contains at least one space.
    echo   This will cause problems with building.
    echo   Please rename your folders so there are no spaces in the source path,
    echo   or move your source to a different folder.
    goto quit
)

REM Set default generator
set CMAKE_GENERATOR="Ninja"
set CMAKE_ARCH=

REM Pre-scan args for compiler, build-type, and architecture. Last flag wins.
REM Compiler: /gcc (default, RosBE GCC), /clang (RosBE llvm-mingw).
REM Build type: /d or /debug (default), /r or /release.
REM Arch: /arch=amd64 (default), /arch=i386, /arch=arm64.
REM Note: we tokenize with `for /f "tokens=1*"` (space/tab delims) instead of
REM `for %A in (%*)` because the latter also splits on '=', breaking /arch=value.
set "_USE_LLVM_MINGW=0"
set "_BUILD_TYPE=Debug"
set "_BUILD_TYPE_FROM_USER=0"
set "_ARCH="
set "_CC_FROM_FLAG=0"
set "_BT_FROM_FLAG=0"
set "_RUN_MENUCONFIG=0"
set "_MENUCONFIG_RESTART_ARGS="
set "_PRESCAN_REMAINING=%*"

:prescan_loop
if not defined _PRESCAN_REMAINING goto prescan_done
for /f "tokens=1*" %%a in ("!_PRESCAN_REMAINING!") do (
    set "_PRE_PARAM=%%a"
    set "_PRESCAN_REMAINING=%%b"
)

if /I not "!_PRE_PARAM!" == "menuconfig" set "_MENUCONFIG_RESTART_ARGS=!_MENUCONFIG_RESTART_ARGS! !_PRE_PARAM!"

if /I "!_PRE_PARAM!" == "/clang"   (set "_USE_LLVM_MINGW=1" & set "_CC_FROM_FLAG=1")
if /I "!_PRE_PARAM!" == "/gcc"     (set "_USE_LLVM_MINGW=0" & set "_CC_FROM_FLAG=1")
if /I "!_PRE_PARAM!" == "/r"       (set "_BUILD_TYPE=Release" & set "_BT_FROM_FLAG=1")
if /I "!_PRE_PARAM!" == "/release" (set "_BUILD_TYPE=Release" & set "_BT_FROM_FLAG=1")
if /I "!_PRE_PARAM!" == "/d"       (set "_BUILD_TYPE=Debug" & set "_BT_FROM_FLAG=1")
if /I "!_PRE_PARAM!" == "/debug"   (set "_BUILD_TYPE=Debug" & set "_BT_FROM_FLAG=1")
if /I "!_PRE_PARAM!" == "menuconfig" set "_RUN_MENUCONFIG=1"
if /I "!_PRE_PARAM:~0,6!" == "/arch=" set "_ARCH=!_PRE_PARAM:~6!"
echo !_PRE_PARAM! | findstr /I "CMAKE_BUILD_TYPE" > NUL && set "_BUILD_TYPE_FROM_USER=1"

goto prescan_loop
:prescan_done

REM rosconfig (menuconfig): a small host tool keeps persistent build options
REM in the untracked .rosconfig\config.cache and turns them into a CMake
REM pre-load fragment included by \PreLoad.cmake. Cached target selections
REM below act as defaults for options not given on the command line.
set "_ROSCONFIG_DIR=%~dp0.rosconfig"
set "_ROSCONFIG_CACHE=%_ROSCONFIG_DIR%\config.cache"
set "_ROSCONFIG_BUILD=%~dp0sdk\tools\rosconfig\build.cmd"
set "_ROSCONFIG_DEF=%~dp0sdk\cmake\rosconfig.def"
set "_ROSCONFIG_BIN=%_ROSCONFIG_DIR%\rosconfig.exe"
set "_ROSCONFIG_OVERRIDES=%_ROSCONFIG_DIR%\overrides.cmake"
set "_ROSCONFIG_OK=0"
set "_ROSCONFIG_EXIT_SKIP_CONFIGURE=3"
if exist "%_ROSCONFIG_CACHE%" (
    if "%_CC_FROM_FLAG%" == "0" (
        for /f "tokens=2 delims==" %%v in ('findstr /b /c:"TOOLCHAIN=" "%_ROSCONFIG_CACHE%"') do (
            if /I "%%v" == "clang" set "_USE_LLVM_MINGW=1"
            if /I "%%v" == "gcc"   set "_USE_LLVM_MINGW=0"
        )
    )
    if "%_BT_FROM_FLAG%" == "0" if "%_BUILD_TYPE_FROM_USER%" == "0" (
        for /f "tokens=2 delims==" %%v in ('findstr /b /c:"BUILD_TYPE=" "%_ROSCONFIG_CACHE%"') do (
            if /I "%%v" == "Release" set "_BUILD_TYPE=Release"
            if /I "%%v" == "Debug"   set "_BUILD_TYPE=Debug"
        )
    )
)

REM Track whether the user explicitly specified arch via /arch=
set "_ARCH_FROM_FLAG=0"
if defined _ARCH set "_ARCH_FROM_FLAG=1"

REM Resolve effective arch: /arch flag wins; otherwise ROS_ARCH; otherwise
REM the menuconfig cache; otherwise amd64.
if not defined _ARCH if defined ROS_ARCH set "_ARCH=!ROS_ARCH!"
if not defined _ARCH if exist "%_ROSCONFIG_CACHE%" (
    for /f "tokens=2 delims==" %%v in ('findstr /b /c:"ARCH=" "%_ROSCONFIG_CACHE%"') do set "_ARCH=%%v"
)
if not defined _ARCH set "_ARCH=amd64"

REM Normalize common arch aliases.
if /I "!_ARCH!" == "x64"     set "_ARCH=amd64"
if /I "!_ARCH!" == "x86_64"  set "_ARCH=amd64"
if /I "!_ARCH!" == "x86"     set "_ARCH=i386"
if /I "!_ARCH!" == "aarch64" set "_ARCH=arm64"

set "_BUILD_TYPE_SUFFIX=debug"
if /I "%_BUILD_TYPE%" == "Release" set "_BUILD_TYPE_SUFFIX=release"

REM Auto-detect RosBE installation when no build environment is configured yet.
REM This lets configure.cmd work from a plain shell without first running
REM `rosbe enable` and without a Visual Studio Developer Command Prompt.
set "ROSBE_CMAKE_EXTRA="
if not defined ROS_ARCH if not defined VCINSTALLDIR (

    REM Locate the RosBE installation root (allow ROSBE_ROOT override).
    if not defined ROSBE_ROOT (
        set "_ROSBE_BASE=%LOCALAPPDATA%\RosBE"
        if exist "!_ROSBE_BASE!\pkgs" (
            for /f "delims=" %%D in ('dir /b /ad /o-n "!_ROSBE_BASE!\pkgs" 2^>NUL') do (
                if not defined ROSBE_ROOT if exist "!_ROSBE_BASE!\pkgs\%%D\rosbe-components.json" (
                    set "ROSBE_ROOT=!_ROSBE_BASE!\pkgs\%%D"
                )
            )
        )
    )

    if not defined ROSBE_ROOT (
        echo. & echo Error: RosBE installation not found.
        echo Looked under %%LOCALAPPDATA%%\RosBE\pkgs\^<version^>
        echo Install RosBE first ^(e.g. "winget install ReactOS.RosBE" then "rosbe install"^),
        echo or run from a Visual Studio Developer Command Prompt to build with MSVC.
        goto quit
    )

    REM Use the architecture chosen by /arch (or amd64 default).
    set "ROS_ARCH=!_ARCH!"
    set "_ROSBE_TRIPLET="
    if /I "!_ARCH!" == "amd64" set "_ROSBE_TRIPLET=x86_64-w64-mingw32"
    if /I "!_ARCH!" == "i386"  set "_ROSBE_TRIPLET=i686-w64-mingw32"
    if /I "!_ARCH!" == "arm64" set "_ROSBE_TRIPLET=aarch64-w64-mingw32"

    if not defined _ROSBE_TRIPLET (
        echo. & echo Error: Unsupported architecture for RosBE: !_ARCH!
        echo Supported: amd64 ^(default^), i386, arm64.
        goto quit
    )

    if not exist "!ROSBE_ROOT!\mingw-gcc\!_ROSBE_TRIPLET!\bin\!_ROSBE_TRIPLET!-gcc.exe" (
        echo. & echo Error: RosBE found at !ROSBE_ROOT!
        echo but the !_ARCH! GCC toolchain is missing
        echo ^(expected !ROSBE_ROOT!\mingw-gcc\!_ROSBE_TRIPLET!\bin\!_ROSBE_TRIPLET!-gcc.exe^).
        echo Re-run "rosbe install" to repair the toolchain bundle.
        goto quit
    )

    REM Resolve dynamic version directories shipped inside the RosBE bundle.
    set "_ROSBE_CMAKE_BIN="
    set "_ROSBE_NINJA_DIR="
    set "_ROSBE_WFB_DIR="
    set "_ROSBE_LLVM_BIN="
    for /d %%D in ("!ROSBE_ROOT!\cmake-*")          do set "_ROSBE_CMAKE_BIN=%%D\bin"
    for /d %%D in ("!ROSBE_ROOT!\ninja-*")          do set "_ROSBE_NINJA_DIR=%%D"
    for /d %%D in ("!ROSBE_ROOT!\win_flex_bison-*") do set "_ROSBE_WFB_DIR=%%D"
    if exist "!ROSBE_ROOT!\llvm-mingw\bin"          set "_ROSBE_LLVM_BIN=!ROSBE_ROOT!\llvm-mingw\bin"

    set "_ROSBE_GCC_BIN=!ROSBE_ROOT!\mingw-gcc\!_ROSBE_TRIPLET!\bin"

    REM Force the toolchain prefix to match the RosBE binary layout.
    REM toolchain-gcc.cmake leaves the prefix empty for i386 on Win32 hosts and
    REM has no branch for arm64, so we set it explicitly here.
    set "ROSBE_CMAKE_EXTRA=-DMINGW_TOOLCHAIN_PREFIX:STRING=!_ROSBE_TRIPLET!-"

    if "!_USE_LLVM_MINGW!" == "1" (
        if not defined _ROSBE_LLVM_BIN (
            echo. & echo Error: /clang requested but llvm-mingw is missing under !ROSBE_ROOT!\llvm-mingw\bin.
            echo Re-run "rosbe install" to repair the toolchain bundle.
            goto quit
        )
        if not exist "!_ROSBE_LLVM_BIN!\clang.exe" (
            echo. & echo Error: /clang requested but clang.exe is missing under !_ROSBE_LLVM_BIN!.
            goto quit
        )
        set "PATH=!_ROSBE_LLVM_BIN!;!_ROSBE_CMAKE_BIN!;!_ROSBE_NINJA_DIR!;!_ROSBE_WFB_DIR!;!PATH!"
        set "ROSBE_CMAKE_EXTRA=!ROSBE_CMAKE_EXTRA! -DREACTOS_CLANG_LLVM_MINGW_ROOT:PATH=!ROSBE_ROOT!\llvm-mingw"
        echo Auto-detected RosBE at !ROSBE_ROOT! ^(/clang: llvm-mingw, !_ARCH!^)
    ) else (
        set "PATH=!_ROSBE_GCC_BIN!;!_ROSBE_CMAKE_BIN!;!_ROSBE_NINJA_DIR!;!_ROSBE_WFB_DIR!;!_ROSBE_LLVM_BIN!;!PATH!"
        echo Auto-detected RosBE at !ROSBE_ROOT! ^(/gcc: mingw-gcc, !_ARCH!^)
    )
)

REM Detect presence of cmake
cmd /c cmake --version 2>&1 | find "cmake version" > NUL || goto cmake_notfound

REM Detect build environment (MinGW, VS, WDK, ...)
if defined ROS_ARCH (
    REM /arch=<x> overrides an externally-set ROS_ARCH.
    if "%_ARCH_FROM_FLAG%" == "1" set "ROS_ARCH=%_ARCH%"
    echo Detected RosBE for !ROS_ARCH!
    set BUILD_ENVIRONMENT=MinGW
    set ARCH=!ROS_ARCH!
    if "%_USE_LLVM_MINGW%" == "1" (
        set MINGW_TOOCHAIN_FILE=toolchain-clang.cmake
        set "OUTPUT_LABEL=Clang"
    ) else (
        set MINGW_TOOCHAIN_FILE=toolchain-gcc.cmake
        set "OUTPUT_LABEL=MinGW"
    )

) else if defined VCINSTALLDIR (
    REM VS command prompt does not put this in environment vars
    cl 2>&1 | find "x86" > NUL && set ARCH=i386
    cl 2>&1 | find "x64" > NUL && set ARCH=amd64
    cl 2>&1 | find "ARM" > NUL && set ARCH=arm
    cl 2>&1 | find "ARM64" > NUL && set ARCH=arm64
    cl 2>&1 | find "19.00." > NUL && set VS_VERSION=14
    cl 2>&1 | findstr /R /c:"19\.1.\." > NUL && set VS_VERSION=15
    cl 2>&1 | findstr /R /c:"19\.2.\." > NUL && set VS_VERSION=16
    cl 2>&1 | findstr /R /c:"19\.3.\." > NUL && set VS_VERSION=17
    cl 2>&1 | findstr /R /c:"19\.4.\." > NUL && set VS_VERSION=17
    cl 2>&1 | findstr /R /c:"19\.5.\." > NUL && set VS_VERSION=18
    if not defined VS_VERSION (
        echo Error: Visual Studio version too old ^(before 14 ^(2015^)^) or version detection failed.
        goto quit
    )
    set BUILD_ENVIRONMENT=VS
    set VS_SOLUTION=0
    echo Detected Visual Studio Environment !BUILD_ENVIRONMENT!!VS_VERSION!-!ARCH!
) else (
    echo Error: Unable to detect build environment. Configure script failure.
    goto quit
)

REM Checkpoint
if not defined ARCH (
    echo Unknown build architecture.
    goto quit
)

set USE_CLANG_CL=0

REM rosconfig: build the host configuration tool (used by menuconfig and to
REM apply cached selections). Failure to build it is not fatal.
if exist "%_ROSCONFIG_BUILD%" (
    call "%_ROSCONFIG_BUILD%" "%_ROSCONFIG_DIR%" "%_ROSCONFIG_BIN%"
    if !ERRORLEVEL! == 0 (
        set "_ROSCONFIG_OK=1"
    ) else (
        echo Warning: could not build the rosconfig tool; menuconfig selections will not be applied.
    )
) else (
    echo Warning: rosconfig build helper is missing; menuconfig selections will not be applied.
)

if "%_RUN_MENUCONFIG%" == "1" (
    if not "!_ROSCONFIG_OK!" == "1" (
        echo Error: menuconfig requested but the rosconfig tool could not be built.
        goto quit
    )
    "%_ROSCONFIG_BIN%" --def "%_ROSCONFIG_DEF%" --cache "%_ROSCONFIG_CACHE%" --menu --ask-configure
    set "_ROSCONFIG_MENU_STATUS=!ERRORLEVEL!"
    if "!_ROSCONFIG_MENU_STATUS!" == "!_ROSCONFIG_EXIT_SKIP_CONFIGURE!" (
        echo configure.cmd: configuration was not started.
        endlocal & exit /b 0
    )
    if not "!_ROSCONFIG_MENU_STATUS!" == "0" (
        if "!_ROSCONFIG_MENU_STATUS!" == "130" echo configure.cmd: menuconfig cancelled; build configuration was not started.
        endlocal & exit /b !_ROSCONFIG_MENU_STATUS!
    )
    echo.
    echo Configuration saved to .rosconfig\config.cache.
    echo Continuing configuration with the selected target and toolchain.
    call "%~f0" !_MENUCONFIG_RESTART_ARGS!
    set "_ROSCONFIG_RESTART_STATUS=!ERRORLEVEL!"
    endlocal & exit /b !_ROSCONFIG_RESTART_STATUS!
)

if "!_ROSCONFIG_OK!" == "1" (
    REM Seed the cache with defaults on first use; merge newly added options.
    "%_ROSCONFIG_BIN%" --def "%_ROSCONFIG_DEF%" --cache "%_ROSCONFIG_CACHE%" --defaults
    if not !ERRORLEVEL! == 0 set "_ROSCONFIG_OK=0"
)

REM Parse command line parameters
set CMAKE_PARAMS=
set REMAINING=%*
:repeat

    REM Extract a parameter without removing '='
    for /f "tokens=1*" %%a in ("%REMAINING%") do (
        set "PARAM=%%a"
        set REMAINING=%%b
    )

    if "%BUILD_ENVIRONMENT%" == "MinGW" (
        if /I "!PARAM!" == "Codeblocks" (
            set CMAKE_GENERATOR="CodeBlocks - MinGW Makefiles"
        ) else if /I "!PARAM!" == "Eclipse" (
            set CMAKE_GENERATOR="Eclipse CDT4 - MinGW Makefiles"
        ) else if /I "!PARAM!" == "Makefiles" (
            set CMAKE_GENERATOR="MinGW Makefiles"
        ) else if /I "!PARAM!" == "Ninja" (
            set CMAKE_GENERATOR="Ninja"
        ) else if /I "!PARAM!" == "VSSolution" (
            echo. & echo Error: Creation of VS Solution files is not supported in a MinGW environment.
            echo Please run this command in a [Developer] Command Prompt for Visual Studio.
            goto quit
        ) else if /I "!PARAM!" == "/clang" (
            REM Already handled by the pre-scan above.
        ) else if /I "!PARAM!" == "/gcc" (
            REM Already handled by the pre-scan above.
        ) else if /I "!PARAM!" == "/r" (
            REM Already handled by the pre-scan above.
        ) else if /I "!PARAM!" == "/release" (
            REM Already handled by the pre-scan above.
        ) else if /I "!PARAM!" == "/d" (
            REM Already handled by the pre-scan above.
        ) else if /I "!PARAM!" == "/debug" (
            REM Already handled by the pre-scan above.
        ) else if /I "!PARAM:~0,6!" == "/arch=" (
            REM Already handled by the pre-scan above.
        ) else if /I "!PARAM!" == "menuconfig" (
            REM Already handled by the pre-scan above.
        ) else if /I "!PARAM:~0,2!" == "-D" (
            REM User is passing a switch to CMake
            set "CMAKE_PARAMS=%CMAKE_PARAMS% !PARAM!"
        ) else (
            echo. & echo   Warning: Unrecognized switch "!PARAM!" & echo.
        )
    ) else (
        if /I "!PARAM!" == "CodeBlocks" (
            set CMAKE_GENERATOR="CodeBlocks - NMake Makefiles"
        ) else if /I "!PARAM!" == "Eclipse" (
            set CMAKE_GENERATOR="Eclipse CDT4 - NMake Makefiles"
        ) else if /I "!PARAM!" == "Makefiles" (
            set CMAKE_GENERATOR="NMake Makefiles"
        ) else if /I "!PARAM!" == "Ninja" (
            set CMAKE_GENERATOR="Ninja"
        ) else if /I "!PARAM!" == "clang" (
            set USE_CLANG_CL=1
        ) else if /I "!PARAM!" == "/clang" (
            REM Treat /clang in VS mode as an alias for clang-cl.
            set USE_CLANG_CL=1
        ) else if /I "!PARAM!" == "/gcc" (
            echo. & echo   Warning: /gcc is not applicable in a Visual Studio environment; using MSVC.
        ) else if /I "!PARAM!" == "/r" (
            REM Already handled by the pre-scan above.
        ) else if /I "!PARAM!" == "/release" (
            REM Already handled by the pre-scan above.
        ) else if /I "!PARAM!" == "/d" (
            REM Already handled by the pre-scan above.
        ) else if /I "!PARAM!" == "/debug" (
            REM Already handled by the pre-scan above.
        ) else if /I "!PARAM:~0,6!" == "/arch=" (
            echo. & echo   Warning: /arch is not applicable in a Visual Studio environment; using cl-detected architecture.
        ) else if /I "!PARAM!" == "menuconfig" (
            REM Already handled by the pre-scan above.
        ) else if /I "!PARAM!" == "VSSolution" (
            set VS_SOLUTION=1
            REM explicitly set VS version for project generator
            for /f "tokens=1*" %%a in ("%REMAINING%") do (
                set PARAM=%%a
                set REMAINING2=%%b
            )
            if /I "!PARAM!" == "-VS_VER" (
                for /f "tokens=1*" %%a in ("!REMAINING2!") do (
                    set VS_VERSION=%%a
                    set REMAINING=%%b
                )
                echo Visual Studio Environment set to !BUILD_ENVIRONMENT!!VS_VERSION!-!ARCH!
            )
            set CMAKE_GENERATOR="Visual Studio !VS_VERSION!"
            if "!ARCH!" == "i386" (
                set CMAKE_ARCH=-A Win32
            ) else if "!ARCH!" == "amd64" (
                set CMAKE_ARCH=-A x64
            ) else if "!ARCH!" == "arm" (
                set CMAKE_ARCH=-A ARM
            ) else if "!ARCH!" == "arm64" (
                set CMAKE_ARCH=-A ARM64
            )
        ) else if /I "!PARAM:~0,2!" == "-D" (
            REM User is passing a switch to CMake
            set "CMAKE_PARAMS=%CMAKE_PARAMS% !PARAM!"
        ) else (
            echo. & echo   Warning: Unrecognized switch "!PARAM!" & echo.
        )
    )

    REM Go to next parameter
    if defined REMAINING goto repeat

REM Inject -DCMAKE_BUILD_TYPE if the user did not pass one explicitly.
if "%_BUILD_TYPE_FROM_USER%" == "0" (
    set "CMAKE_PARAMS=-DCMAKE_BUILD_TYPE:STRING=%_BUILD_TYPE% %CMAKE_PARAMS%"
)

REM rosconfig: turn cached selections into a CMake pre-load fragment picked
REM up by \PreLoad.cmake. Explicit -D arguments still take precedence.
set "_ROSCONFIG_CCACHE=-DENABLE_CCACHE:BOOL=0"
if "!_ROSCONFIG_OK!" == "1" (
    set "_ROSCONFIG_TC=gcc"
    if "%_USE_LLVM_MINGW%" == "1" set "_ROSCONFIG_TC=clang"
    if "%BUILD_ENVIRONMENT%" == "VS" set "_ROSCONFIG_TC=msvc"
    "%_ROSCONFIG_BIN%" --def "%_ROSCONFIG_DEF%" --cache "%_ROSCONFIG_CACHE%" --generate "%_ROSCONFIG_OVERRIDES%" --override "ARCH=%ARCH%" --override "TOOLCHAIN=!_ROSCONFIG_TC!" --override "BUILD_TYPE=%_BUILD_TYPE%"
    if !ERRORLEVEL! == 0 (
        REM ENABLE_CCACHE is now controlled by the overrides file.
        set "_ROSCONFIG_CCACHE="
    ) else (
        if exist "%_ROSCONFIG_OVERRIDES%" del /q "%_ROSCONFIG_OVERRIDES%"
    )
) else (
    if exist "%_ROSCONFIG_OVERRIDES%" del /q "%_ROSCONFIG_OVERRIDES%"
)

REM Inform the user about the default build
if "!CMAKE_GENERATOR!" == "Ninja" (
    echo This script defaults to Ninja. Type "configure help" for alternative options.
)

REM Display information
echo Configuring a new ReactOS build on:
(for /f "delims=" %%x in ('ver') do @echo %%x) & echo.

REM Create directories
if defined OUTPUT_LABEL (
    set REACTOS_OUTPUT_PATH=output-%OUTPUT_LABEL%-%ARCH%-%_BUILD_TYPE_SUFFIX%
) else (
    set REACTOS_OUTPUT_PATH=output-%BUILD_ENVIRONMENT%-%ARCH%-%_BUILD_TYPE_SUFFIX%
)

if "%VS_SOLUTION%" == "1" (
    set REACTOS_OUTPUT_PATH=%REACTOS_OUTPUT_PATH%-sln
)

if "%REACTOS_SOURCE_DIR%" == "%CD:\=/%/" (
    set CD_SAME_AS_SOURCE=1
    echo Creating directories in %REACTOS_OUTPUT_PATH%

    if not exist %REACTOS_OUTPUT_PATH% (
        mkdir %REACTOS_OUTPUT_PATH%
    )
    cd %REACTOS_OUTPUT_PATH%
)

if "%VS_SOLUTION%" == "1" (
    if exist build.ninja (
        echo. & echo Error: This directory has already been configured for ninja.
        echo An output folder configured for ninja can't be reconfigured for VSSolution.
        echo Use an empty folder or delete the contents of this folder, then try again.
        goto quit
    )
) else if exist REACTOS.sln (
    echo. & echo Error: This directory has already been configured for Visual Studio.
    echo An output folder configured for VSSolution can't be reconfigured for ninja.
    echo Use an empty folder or delete the contents of this folder, then try again. & echo.
    goto quit
)


if EXIST CMakeCache.txt (
    del /q CMakeCache.txt
)

if "%BUILD_ENVIRONMENT%" == "MinGW" (
    cmake -G %CMAKE_GENERATOR% %_ROSCONFIG_CCACHE% -DCMAKE_TOOLCHAIN_FILE:FILEPATH=%MINGW_TOOCHAIN_FILE% -DARCH:STRING=%ARCH% %ROSBE_CMAKE_EXTRA% %BUILD_TOOLS_FLAG% %CMAKE_PARAMS% "%REACTOS_SOURCE_DIR%"
) else if %USE_CLANG_CL% == 1 (
    cmake -G %CMAKE_GENERATOR% -DCMAKE_TOOLCHAIN_FILE:FILEPATH=toolchain-msvc.cmake -DARCH:STRING=%ARCH% %BUILD_TOOLS_FLAG% -DUSE_CLANG_CL:BOOL=1 %CMAKE_PARAMS% "%REACTOS_SOURCE_DIR%"
) else (
    cmake -G %CMAKE_GENERATOR% %CMAKE_ARCH% -DCMAKE_TOOLCHAIN_FILE:FILEPATH=toolchain-msvc.cmake -DARCH:STRING=%ARCH% %BUILD_TOOLS_FLAG% %CMAKE_PARAMS% "%REACTOS_SOURCE_DIR%"
)

if %ERRORLEVEL% NEQ 0 (
    goto quit
)

if "%CD_SAME_AS_SOURCE%" == "1" (
    set ENDV= from %REACTOS_OUTPUT_PATH%
)

if "%VS_SOLUTION%" == "1" (
    set ENDV= You can now use msbuild or open REACTOS.sln%ENDV%
) else (
    set ENDV= Execute appropriate build commands ^(e.g. ninja, make, nmake, etc.^)%ENDV%
)

echo. & echo Configure script complete^^!%ENDV%

goto quit

:cmake_notfound
echo Unable to find cmake. If it is installed, check your PATH variable.

:quit
endlocal
exit /b
