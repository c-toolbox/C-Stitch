---
title: Settings
nav_order: 6
---

# Settings

C-Stitch preferences are generated from `src/settings/stitchsettings.kcfg` and stored with KDE KConfig. The settings dialog can load saved startup values, reset to defaults, copy values from the current UI state, and save new startup values.

Important performance settings:

- **Loading threads / eye** controls Wuffs face-loader workers and is constrained to multiples of four.
- **Encoding threads / eye** controls the local stb save queue.
- Stereo mode runs each eye independently, so these counts apply per eye.