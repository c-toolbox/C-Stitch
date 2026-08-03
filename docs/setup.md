---
title: Setup
nav_order: 3
---

# Setup C-Stitch

The stitcher expects four cubemap faces per eye: left, right, top, and bottom. In stereo mode, C-Stitch can launch both eye jobs, only the left-eye job, or only the right-eye job. Mono mode launches one job with no active eye separation.

Stereo eye jobs use off-axis fisheye rendering: the selected eye applies a signed fisheye projection offset from the configured eye separation and dome diameter.

## Cubemap Input Layout

C-Stitch accepts four mono cubemap faces in left, right, top, and bottom order. Stereo jobs use a second set of four faces for the right eye, and can render both eyes or just the selected left or right eye. Image sequences are detected from numbered filenames such as `Frame_0000.png`, and C-Stitch can adopt the detected start and stop frame range automatically.

Cubemap folders can be arranged with short face directory names:

- `LL`, `LR`, `LT`, `LB` for the left-eye or mono left/right/top/bottom faces
- `RL`, `RR`, `RT`, `RB` for the right-eye left/right/top/bottom faces

When automatic face detection is enabled, selecting one numbered image from this layout fills the matching sibling face paths that contain the same filename and indexes the sequence range. If all right-eye face folders are present, stereo mode is enabled automatically. The detection option is available in the main window and can be stored as a startup default in Preferences.

## Preferences

Use **Preferences** to save startup defaults for input paths, projection values, output format, sequence range, thread counts, and auto-detection behavior. These values are stored with KDE KConfig in the same style as C-Slice.
