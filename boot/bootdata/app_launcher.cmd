@echo off
setlocal EnableExtensions EnableDelayedExpansion
set DBGPRINT=%SystemRoot%\system32\dbgprint.exe
set SLEEP=%SystemRoot%\system32\ping.exe
set APPDRIVE=
set /a TRIES=0

:scan
for %%D in (D E F G H I J K L M N O P Q R S T U V W X Y Z C) do (
    if /i not "%%D:" == "%SystemDrive%" (
        if exist "%%D:\launcher.cmd" (
            set APPDRIVE=%%D:
            goto found
        )
    )
)
set /a TRIES+=1
if !TRIES! GEQ 4 goto none
if not exist "%SLEEP%" goto none
"%SLEEP%" -n 3 127.0.0.1 >nul 2>nul
goto scan

:found
call :say APPLAUNCH_BEGIN !APPDRIVE!
call :shortcuts
pushd "!APPDRIVE!\"
call "!APPDRIVE!\launcher.cmd"
set RC=!ERRORLEVEL!
popd
call :say APPLAUNCH_EXIT !RC!
goto done

:none
call :say APPLAUNCH_NO_DISK

:done
call :say APPLAUNCH_DONE
endlocal
exit /b 0

:shortcuts
if not exist "!APPDRIVE!\shortcuts\" exit /b 0
set DESK=%ALLUSERSPROFILE%\Desktop
if not exist "!DESK!\" set DESK=%USERPROFILE%\Desktop
if not exist "!DESK!\" exit /b 0
for %%F in ("!APPDRIVE!\shortcuts\*") do (
    if not exist "!DESK!\%%~nxF" (
        copy /y "%%F" "!DESK!\" >nul 2>nul
        if not errorlevel 1 call :say APPLAUNCH_SHORTCUT %%~nxF
    )
)
exit /b 0

:say
if exist "%DBGPRINT%" (
    "%DBGPRINT%" %*
) else (
    echo %*
)
exit /b 0
