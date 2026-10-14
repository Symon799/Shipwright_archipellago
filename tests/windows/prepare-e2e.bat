@echo off
setlocal
cd /d "%~dp0..\.."
set "CMAKE=C:\Program Files\CMake\bin\cmake.exe"
if not exist "%CMAKE%" set "CMAKE=cmake.exe"
if not defined BUILD_JOBS set "BUILD_JOBS=2"
if not defined VCTOOLS_VERSION set "VCTOOLS_VERSION=14.43.34808"
if not exist "build\x64-v143\CMakeCache.txt" (
    echo Prepare the game build first with compile-debug.bat at the repository root.
    set "RESULT=2"
    goto :done
)
echo Enabling opt-in Debug quality scenarios...
"%CMAKE%" -S . -B build/x64-v143 -DSOH_ENABLE_QUALITY_SCENARIOS=ON -DFETCHCONTENT_FULLY_DISCONNECTED=ON
if errorlevel 1 goto :failed
"%CMAKE%" --build build/x64-v143 --config Debug --target soh --parallel %BUILD_JOBS% -- /p:VCToolsVersion=%VCTOOLS_VERSION% /p:CL_MPCount=%BUILD_JOBS%
if errorlevel 1 goto :failed
echo Ready. Run tests\windows\check-e2e.bat.
set "RESULT=0"
goto :done
:failed
echo E2E preparation failed.
set "RESULT=1"
:done
pause
exit /b %RESULT%
