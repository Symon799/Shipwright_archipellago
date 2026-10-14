@echo off
setlocal
cd /d "%~dp0..\.."
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\..\scripts\check.ps1" -Suite Integration %*
set "RESULT=%ERRORLEVEL%"
pause
exit /b %RESULT%
