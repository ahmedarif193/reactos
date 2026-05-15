@echo off

if not "%1" == "" (
    start %1
    goto :eof
)

if not exist "%SystemRoot%\system32\cmd_rostest_x64.exe" goto after_cmd_rostest_x64
echo Running cmd_rostest_x64
%SystemRoot%\system32\dbgprint.exe FEX_TEST_BEGIN cmd_rostest_x64
%SystemRoot%\system32\cmd_rostest_x64.exe
%SystemRoot%\system32\dbgprint.exe FEX_TEST_EXIT cmd_rostest_x64 %ERRORLEVEL%
%SystemRoot%\system32\dbgprint.exe FEX_TEST_END cmd_rostest_x64
:after_cmd_rostest_x64

if not exist "%SystemRoot%\system32\ntdll_apitest_x64.exe" goto after_ntdll_apitest_x64
%SystemRoot%\system32\dbgprint.exe FEX_TEST_BEGIN ntdll_apitest_x64 arm64_chpe
%SystemRoot%\system32\ntdll_apitest_x64.exe arm64_chpe
%SystemRoot%\system32\dbgprint.exe FEX_TEST_EXIT ntdll_apitest_x64 arm64_chpe %ERRORLEVEL%
%SystemRoot%\system32\dbgprint.exe FEX_TEST_END ntdll_apitest_x64 arm64_chpe
:after_ntdll_apitest_x64

if not exist "%SystemRoot%\bin\kmtest_.exe" goto :eof

pushd "%SystemRoot%\bin" || goto :eof

echo Running kmtest_ MmSelfMap
kmtest_.exe MmSelfMap

popd
