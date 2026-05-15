@echo off

if not "%1" == "" (
    start %1
    goto :eof
)

if exist "%SystemRoot%\system32\cmd_rostest_x64.exe" (
    echo Running cmd_rostest_x64
    %SystemRoot%\system32\cmd_rostest_x64.exe
)

if exist "%SystemRoot%\system32\ntdll_apitest_x64.exe" (
    %SystemRoot%\system32\dbgprint.exe FEX_TEST_BEGIN ntdll_apitest_x64 arm64_chpe
    %SystemRoot%\system32\ntdll_apitest_x64.exe arm64_chpe
    %SystemRoot%\system32\dbgprint.exe FEX_TEST_EXIT ntdll_apitest_x64 arm64_chpe %ERRORLEVEL%
    %SystemRoot%\system32\dbgprint.exe FEX_TEST_END ntdll_apitest_x64 arm64_chpe
)

if not exist "%SystemRoot%\bin\kmtest_.exe" goto :eof

pushd "%SystemRoot%\bin" || goto :eof

echo Running kmtest_ MmSelfMap
kmtest_.exe MmSelfMap

popd
