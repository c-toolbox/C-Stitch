---
title: 'Start here'
sidebar_label: 'Start here'
sidebar_position: 1
slug: /
---

# C-Stitch : Cubemap Stitcher for Fisheye and Fulldome Output

C-Stitch is an open source Qt/Kirigami rewrite of the SGCT cubemap stitcher. It takes four cubemap faces, or separate left and right eye cubemaps, and renders fisheye or fulldome still-image sequences through the SGCT OpenGL pipeline.

C-Stitch follows the C-Slice application model: the UI is the master process, and pressing **Stitch** launches the same executable again in `--node` mode for high-performance rendering.

![C-Stitch main window running a stitch job](/img/C-Stitch_v1.0.0.png)

## Guides

1. [Install C-Stitch](/install)
2. [Setup C-Stitch](/setup/overview)
3. [Media and data structure](/media/overview)
4. [Stitch workflow](/workflow/overview)
5. [Settings](/settings/overview)
6. [Build from code](/build/overview)
7. [Versions](/versions)

## Main features

- Stitch left, right, top, and bottom cubemap faces into fisheye/fulldome images.
- Run stereo as separate left-eye and right-eye node jobs for independent decode, render, readback, and saving.
- Use off-axis stereo controls for eye separation and dome diameter.
- Choose fisheye field of view values beyond the classic 180 degree case.
- Decode images with Wuffs and render with SGCT/OpenGL.
- Save PNG, JPEG, and TGA output through a local threaded stb writer.
- Index numbered image sequences before long stitch jobs.

## Backend

C-Stitch is based on these open source projects:

- [SGCT](https://sgct.github.io/) for the OpenGL rendering and projection pipeline.
- [Qt](https://www.qt.io/) and [KDE Frameworks](https://develop.kde.org/products/frameworks/) for the master UI and settings.
- [Wuffs](https://github.com/google/wuffs) for image decoding.
- [stb](https://github.com/nothings/stb) for local image writing.
