@echo off
cd /d "%~dp0"
wsl.exe -d Ubuntu -- bash scripts/build_ui_preview.sh
if errorlevel 1 pause
