@echo off
rem One-time setup for Windows: installs MSYS2, a C compiler and GTK 4.
setlocal
cd /d "%~dp0"

if not exist "C:\msys64\usr\bin\bash.exe" (
    echo Installing MSYS2 ^(this provides the C compiler and GTK^)...
    winget install --id MSYS2.MSYS2 -e --accept-package-agreements --accept-source-agreements
)
if not exist "C:\msys64\usr\bin\bash.exe" (
    echo.
    echo Could not install MSYS2 automatically.
    echo Please install it from https://www.msys2.org ^(use the default folder C:\msys64^)
    echo and then run this file again.
    pause
    exit /b 1
)

echo Installing GTK 4 and build tools ^(a few hundred MB, please wait^)...
"C:\msys64\usr\bin\bash.exe" -lc "pacman -S --needed --noconfirm mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-gtk4 mingw-w64-ucrt-x86_64-pkgconf mingw-w64-ucrt-x86_64-make"
if errorlevel 1 (
    echo.
    echo Package installation failed. Check your internet connection and try again.
    pause
    exit /b 1
)
echo.
echo Setup finished.
