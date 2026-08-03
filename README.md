# C-Stitch

C-Stitch is a Qt/QML desktop application for stitching cubemap image sequences into fisheye dome frames. It is designed for practical batch work: choose the cubemap faces, verify the inputs, set the output range and encoding options, then launch the renderer from the same interface.

## Cubemap Input

The application accepts four mono cubemap faces in left, right, top, and bottom order. Stereo jobs use a second set of four faces for the right eye, and can render both eyes or just the selected left or right eye. Image sequences are detected from numbered filenames such as `Frame_0000.png`, and C-Stitch can adopt the detected start and stop frame range automatically.

Cubemap folders can be arranged with short face directory names:

- `LL`, `LR`, `LT`, `LB` for the left-eye or mono left/right/top/bottom faces
- `RL`, `RR`, `RT`, `RB` for the right-eye left/right/top/bottom faces

When automatic face detection is enabled, selecting one numbered image from this layout fills the matching sibling face paths that contain the same filename and indexes the sequence range. If all right-eye face folders are present, stereo mode is enabled automatically. The detection option is available in the main window and can be stored as a startup default in Preferences.

## Rendering

C-Stitch launches its renderer in node mode with the settings selected in the UI. The node decodes input images, uploads cubemap faces to SGCT/OpenGL textures, renders the fisheye output, reads the framebuffer, and writes PNG, JPEG, or TGA frames. Mono jobs use no active eye separation. Stereo eye jobs use off-axis fisheye rendering by shifting the fisheye projection by the signed eye separation relative to the dome diameter.

Important controls include:

- stereo output eye selection, fisheye field of view, dome diameter, eye separation, output resolution, cubemap texture resolution, MSAA, FXAA, and cubic filtering
- per-face rotation for left, right, top, and bottom faces
- input sequence start/stop, output start, step size, and texture loading retry behavior
- PNG compression, JPEG quality, output format, loading threads, and encoding threads

## Dependencies

- Qt 6 and KDE Frameworks 6 for the desktop UI
- SGCT source checkout at `src/sgct` or `-DCSTITCH_SGCT_SOURCE_DIR=...`
- Wuffs checkout at `D:/Google/wuffs` or `-DCSTITCH_WUFFS_ROOT=...`
- vcpkg support is enabled by default

## Build

Configure with CMake and build the `C-Stitch` target. A typical local Windows configuration uses a Qt/KF6 prefix through `CMAKE_PREFIX_PATH`, then builds the generated Visual Studio solution in Debug or Release.

The documentation scaffold lives in `docs/`.
