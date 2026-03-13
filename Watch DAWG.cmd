@echo off
setlocal
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\Watch-Dawg.ps1"
if errorlevel 1 (
    echo.
    echo DAWG watch mode stopped because of an error. Read the message above, then press any key to close this window.
    pause >nul
)
endlocal
