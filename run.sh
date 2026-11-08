#!/bin/sh
# Builds (if needed) and launches Study Planner on Linux / macOS.
set -e
cd "$(dirname "$0")"

if ! command -v pkg-config >/dev/null 2>&1 || ! pkg-config --exists gtk4; then
    echo "GTK 4 development files were not found. Install them first:"
    echo
    echo "  Ubuntu / Debian:  sudo apt install build-essential pkg-config libgtk-4-dev"
    echo "  Fedora:           sudo dnf install gcc make pkgconf-pkg-config gtk4-devel"
    echo "  Arch:             sudo pacman -S base-devel gtk4"
    echo "  macOS (Homebrew): brew install gtk4 pkg-config"
    echo
    echo "Then run ./run.sh again."
    exit 1
fi

make
exec ./build/studyplanner "$@"
