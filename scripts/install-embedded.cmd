@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\install-embedded.ps1" %*
set "TOOLSBOX_INSTALL_EXIT=%ERRORLEVEL%"
pause
exit /b %TOOLSBOX_INSTALL_EXIT%
