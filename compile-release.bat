@echo off
setlocal
cd /d "%~dp0"

set "CONFIG=Release"
set "BUILD_DIR=build\x64-v143"
set "CMAKE=C:\Program Files\CMake\bin\cmake.exe"
if not defined VCTOOLS_VERSION set "VCTOOLS_VERSION=14.43.34808"
if not defined BUILD_JOBS set "BUILD_JOBS=2"

if not exist "%CMAKE%" (
    where cmake.exe >nul 2>&1
    if errorlevel 1 goto :cmake_missing
    set "CMAKE=cmake.exe"
)

if not exist "%BUILD_DIR%\CMakeCache.txt" (
    echo CMake configuration is missing. Configuring the project...
    "%CMAKE%" -S . -B "%BUILD_DIR%" -G "Visual Studio 17 2022" -T "v143,version=%VCTOOLS_VERSION%" -A x64 -DBUILD_REMOTE_CONTROL=1
    if errorlevel 1 goto :failed
)

echo Refreshing soh.o2r from the current source assets...
"%CMAKE%" --build "%BUILD_DIR%" --config %CONFIG% --target GenerateSohOtr --parallel %BUILD_JOBS% -- /p:VCToolsVersion=%VCTOOLS_VERSION% /p:CL_MPCount=%BUILD_JOBS%
if errorlevel 1 goto :failed

echo Building soh in %CONFIG% configuration...
"%CMAKE%" --build "%BUILD_DIR%" --config %CONFIG% --target soh --parallel %BUILD_JOBS% -- /p:VCToolsVersion=%VCTOOLS_VERSION% /p:CL_MPCount=%BUILD_JOBS%
if errorlevel 1 goto :failed

echo.
echo Release build completed: x64\Release\soh.exe
pause
exit /b 0

:cmake_missing
echo CMake was not found. Install CMake or add it to PATH.
goto :failed

:failed
echo.
echo The Release build failed.
pause
exit /b 1
