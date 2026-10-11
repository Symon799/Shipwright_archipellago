@echo off
setlocal
cd /d "%~dp0"
python scripts\export_map_tracker_references.py %*
if errorlevel 1 (
    echo.
    echo Export failed. Check that Python 3.10+ and x64\Debug\oot.o2r are available.
) else (
    echo.
    echo Reference files are ready in build\map_tracker_references by default.
)
pause
