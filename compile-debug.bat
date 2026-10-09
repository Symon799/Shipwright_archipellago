@echo off
setlocal
cd /d "%~dp0"

set "CONFIG=Debug"
set "BUILD_DIR=build\x64-v143"
set "CMAKE=C:\Program Files\CMake\bin\cmake.exe"
set "VCTOOLS_VERSION=14.43.34808"
set "BUILD_JOBS=16"

if not exist "%CMAKE%" (
    where cmake.exe >nul 2>&1
    if errorlevel 1 goto :cmake_missing
    set "CMAKE=cmake.exe"
)

if not exist "%BUILD_DIR%\CMakeCache.txt" (
    echo CMake configuration is missing. Configuring the project...
    "%CMAKE%" -S . -B "%BUILD_DIR%" -G "Visual Studio 17 2022" -A x64 -DBUILD_REMOTE_CONTROL=1
    if errorlevel 1 goto :failed
)

if not exist "x64\%CONFIG%\soh.o2r" (
    echo soh.o2r is missing. Generating resources...
    "%CMAKE%" --build "%BUILD_DIR%" --config %CONFIG% --target GenerateSohOtr --parallel %BUILD_JOBS% -- /p:VCToolsVersion=%VCTOOLS_VERSION%
    if errorlevel 1 goto :failed
)

echo Building soh in %CONFIG% configuration...
"%CMAKE%" --build "%BUILD_DIR%" --config %CONFIG% --target soh --parallel %BUILD_JOBS% -- /p:VCToolsVersion=%VCTOOLS_VERSION%
if errorlevel 1 goto :failed

echo.
echo Debug build completed: x64\Debug\soh.exe
pause
exit /b 0

:cmake_missing
echo CMake was not found. Install CMake or add it to PATH.
goto :failed

:failed
echo.
echo The Debug build failed.
pause
exit /b 1
