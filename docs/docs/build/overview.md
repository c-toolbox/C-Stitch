---
title: 'Build from code'
sidebar_label: 'Overview'
sidebar_position: 1
---

# Build from Code

## Dependencies

- Qt 6 and KDE Frameworks 6 for the desktop UI
- SGCT source checkout at `src/sgct` or `-DCSTITCH_SGCT_SOURCE_DIR=...`
- Wuffs checkout at `D:/Google/wuffs` or `-DCSTITCH_WUFFS_ROOT=...`
- vcpkg support is enabled by default

## Configure and Build

Configure with CMake and build the `C-Stitch` target. A typical local Windows configuration uses a Qt/KF6 prefix through `CMAKE_PREFIX_PATH`, then builds the generated solution in Debug or Release.

Debug build command used during development:

```powershell
cmake --build build --config Debug --target C-Stitch
```

The SGCT node path requires OpenGL and does not include a CPU stitch fallback.
