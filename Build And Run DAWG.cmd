@echo off
setlocal
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\Run-Dawg.ps1"
if errorlevel 1 (
    echo.
    echo DAWG did not build or start. Read the message above, then press any key to close this window.
    pause >nul
)
endlocal
