@echo off
setlocal
cd /d "%~dp0"

powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "& 'C:\Program Files\CMake\bin\cmake.exe' --build '.\build' --config Release; " ^
  "Set-Location '.\build'; " ^
  "& 'C:\Program Files\CMake\bin\cpack.exe' -G ZIP"

if errorlevel 1 (
  echo.
  echo Build/pack a echoue.
  pause
  exit /b 1
)

echo.
echo OK.
pause