@echo off
setlocal
pushd "%~dp0"
if errorlevel 1 exit /b 1
wsl.exe -d Ubuntu -- bash scripts/build_from_windows.sh
set "moonlightBuildExit=%errorlevel%"
popd
exit /b %moonlightBuildExit%
