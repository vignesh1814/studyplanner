@echo off
rem Builds (if needed) and launches Study Planner on Windows.
rem First time? This installs everything it needs automatically.
setlocal
cd /d "%~dp0"

if not exist "C:\msys64\ucrt64\bin\gtk4-demo.exe" if not exist "C:\msys64\ucrt64\lib\pkgconfig\gtk4.pc" (
    echo GTK 4 is not installed yet - running first-time setup...
    call "%~dp0setup_windows.bat"
    if errorlevel 1 exit /b 1
)

set MSYSTEM=UCRT64
set CHERE_INVOKING=1
"C:\msys64\usr\bin\bash.exe" -lc "mingw32-make --no-print-directory"
if errorlevel 1 (
    echo.
    echo Build failed. See the messages above.
    pause
    exit /b 1
)

rem The ucrt64\bin folder holds the GTK DLLs the app needs at runtime.
set "PATH=C:\msys64\ucrt64\bin;%PATH%"
start "" "%~dp0build\studyplanner.exe" %*
