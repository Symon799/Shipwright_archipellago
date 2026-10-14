@echo off
setlocal
cd /d "%~dp0..\.."
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\..\scripts\check.ps1" -Suite E2E --game-exe "%~dp0..\..\x64\Debug\soh.exe" %*
set "RESULT=%ERRORLEVEL%"
pause
exit /b %RESULT%
