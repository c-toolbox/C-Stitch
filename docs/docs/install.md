---
title: 'Install C-Stitch'
sidebar_position: 2
---

# Install C-Stitch

## Windows Installer

A pre-built Windows installer is available on GitHub in the release page:

[**Download latest release**](https://github.com/c-toolbox/C-Stitch/releases)

The installer package includes the application and all required runtime dependencies for immediate use.

## Build from Source

C-Stitch can be built from source. The CMake install step can also copy Qt, KDE, SGCT scalable, and other runtime DLLs next to the executable when `CSTITCH_INSTALL_DEPENDENCIES` is enabled.

Typical local configuration:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="D:/Craft/VS2022_qt6X"
cmake --build build --config Release --target C-Stitch
```

To prepare an install tree with dependencies:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="D:/Craft/VS2022_qt6X" -DCSTITCH_INSTALL_DEPENDENCIES=ON
cmake --build build --config Release --target INSTALL
```