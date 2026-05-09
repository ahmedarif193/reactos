@echo off

if not "%1" == "" (
    start %1
    goto :eof
)

if not exist "%SystemRoot%\bin\kmtest_.exe" goto :eof

pushd "%SystemRoot%\bin" || goto :eof

echo Running kmtest_ MmSelfMap
kmtest_.exe MmSelfMap

popd
