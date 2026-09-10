---
title: 'Stitch workflow'
sidebar_label: 'Overview'
sidebar_position: 1
---

# Stitch Workflow

Detailed workflow guides:

- [C-Job architecture](/workflow/cjob-architecture)

1. Choose the four cubemap faces for mono output, or the needed left-eye and/or right-eye face sets for stereo output.
1. Select mono, both stereo eyes, left stereo eye only, or right stereo eye only.
1. Set the projection values, including fisheye FOV in degrees, dome diameter in meters, eye separation in meters, resolution in pixels, and cubemap resolution in pixels.
1. Index the sequence when working with numbered files.
1. Choose output directory, filename stem, format, and threaded loading/encoding counts.
1. Press **Verify** to validate inputs or **Stitch** to render the sequence.

The UI launches `C-Stitch.exe --node` for the actual OpenGL stitch operation.

## Rendering Controls

The node decodes input images, uploads cubemap faces to SGCT/OpenGL textures, renders the fisheye output, reads the framebuffer, and writes PNG, JPEG, or TGA frames. Mono jobs use no active eye separation. Stereo eye jobs use off-axis fisheye rendering by shifting the fisheye projection by the signed eye separation relative to the dome diameter.

Important controls include:

- **Stereo output** eye selection, fisheye field of view, dome diameter, eye separation, output resolution, cubemap texture resolution, MSAA, FXAA, and cubic filtering
- **Per-face rotation** for left, right, top, and bottom faces
- **Input sequence** start/stop frame range, output start frame, step size, and texture loading retry behavior
- **Output options** including PNG compression level, JPEG quality, output format, loading threads, and encoding threads
