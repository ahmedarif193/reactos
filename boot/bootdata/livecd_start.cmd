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

if not exist "%SystemRoot%\system32\notepad_x64.exe" goto after_notepad_x64
echo Launching notepad_x64
%SystemRoot%\system32\dbgprint.exe FEX_TEST_BEGIN notepad_x64
start "" "%SystemRoot%\system32\notepad_x64.exe"
%SystemRoot%\system32\dbgprint.exe FEX_TEST_LAUNCH notepad_x64 %ERRORLEVEL%
%SystemRoot%\system32\dbgprint.exe FEX_TEST_END notepad_x64
:after_notepad_x64

if not exist "%SystemRoot%\system32\calc_x64.exe" goto after_calc_x64
echo Launching calc_x64
%SystemRoot%\system32\dbgprint.exe FEX_TEST_BEGIN calc_x64
start "" "%SystemRoot%\system32\calc_x64.exe"
%SystemRoot%\system32\dbgprint.exe FEX_TEST_LAUNCH calc_x64 %ERRORLEVEL%
%SystemRoot%\system32\dbgprint.exe FEX_TEST_END calc_x64
:after_calc_x64

if not exist "%SystemRoot%\bin\kmtest_.exe" goto :eof

pushd "%SystemRoot%\bin" || goto :eof

echo Running kmtest_ MmSelfMap
%SystemRoot%\system32\dbgprint.exe Running kmtest_ MmSelfMap
kmtest_.exe MmSelfMap
%SystemRoot%\system32\dbgprint.exe kmtest_ MmSelfMap exit %ERRORLEVEL%

echo Running kmtest_ KeArm64
%SystemRoot%\system32\dbgprint.exe Running kmtest_ KeArm64
kmtest_.exe KeArm64
%SystemRoot%\system32\dbgprint.exe kmtest_ KeArm64 exit %ERRORLEVEL%

popd
