@echo off
setlocal
cd /d "%~dp0"

set "CONFIG=Release"
set "BUILD_DIR=build\x64-v143"
set "CMAKE=C:\Program Files\CMake\bin\cmake.exe"
set "VCTOOLS_VERSION=14.43.34808"
set "BUILD_JOBS=16"
set "PACK_DIR=release-assets"
set "PACK_FILE=%PACK_DIR%\SoH_MapRessource_v1.1.4.zip"
set "PACK_URL=https://github.com/Symon799/SoH_Map_Ressource_Maker/releases/download/SoH_MapRessource_v1.1.4/SoH_MapRessource_v1.1.4.zip"
set "PACK_SHA256=ED4BC2C15C53DE5E13178BC916F443D47746857F52904C0138AF6B306BDFE16F"

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

echo Building SoH in %CONFIG% configuration...
"%CMAKE%" --build "%BUILD_DIR%" --config %CONFIG% --target soh --parallel %BUILD_JOBS% -- /p:VCToolsVersion=%VCTOOLS_VERSION%
if errorlevel 1 goto :failed

if not exist "%PACK_FILE%" (
    echo Downloading the Map Tracker resource pack...
    if not exist "%PACK_DIR%" mkdir "%PACK_DIR%"
    powershell -NoProfile -ExecutionPolicy Bypass -Command "Invoke-WebRequest -Uri '%PACK_URL%' -OutFile '%PACK_FILE%'"
    if errorlevel 1 goto :failed
)

for /f "delims=" %%H in ('powershell -NoProfile -ExecutionPolicy Bypass -Command "(Get-FileHash -Algorithm SHA256 -LiteralPath '%PACK_FILE%').Hash"') do set "PACK_HASH=%%H"
if /i not "%PACK_HASH%"=="%PACK_SHA256%" (
    echo The Map Tracker resource pack hash is invalid.
    goto :failed
)

echo Creating the native Windows package...
pushd "%BUILD_DIR%"
"C:\Program Files\CMake\bin\cpack.exe" -G ZIP
if errorlevel 1 (
    popd
    goto :failed
)
popd

for /f "delims=" %%P in ('powershell -NoProfile -ExecutionPolicy Bypass -Command "(Get-ChildItem -LiteralPath '_packages' -Filter '*.zip' | Where-Object { $_.Name -ne 'SoH_Arch_MapTracker_Windows.zip' -and $_.Name -ne 'SoH_AP_Map_Windows.zip' } | Sort-Object LastWriteTime -Descending | Select-Object -First 1).FullName"') do set "NATIVE_PACKAGE=%%P"
if not defined NATIVE_PACKAGE (
    echo No native CPack package was found.
    goto :failed
)

if not exist "_packages" mkdir "_packages"
python scripts\package_map_tracker_release.py ^
  --platform windows ^
  --package "%NATIVE_PACKAGE%" ^
  --output "_packages\SoH_AP_Map_Windows.zip" ^
  --config "packaging\shipofharkinian.json" ^
  --map-pack "%PACK_FILE%" ^
  --readme "README.md"
if errorlevel 1 goto :failed

echo.
echo Windows release package created:
echo _packages\SoH_AP_Map_Windows.zip
pause
exit /b 0

:cmake_missing
echo CMake was not found. Install CMake or add it to PATH.

:failed
echo.
echo Release packaging failed.
pause
exit /b 1
