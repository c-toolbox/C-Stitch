/*
 * SPDX-FileCopyrightText:
 * 2026 Erik Sundén
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef CSTITCH_IMAGESEQUENCEUTILS_H
#define CSTITCH_IMAGESEQUENCEUTILS_H

#include <functional>
#include <vector>

#include <QString>

// Forward declaration to allow ImageSequenceScanResult to reference ImageDimensions
struct ImageDimensions;

struct ImageSequenceFrameInfo {
    int frameIndex = 0;
    int digitCount = 0;
    QString path;
};

struct ImageDimensions {
    int width = 0;
    int height = 0;
};

struct ImageSequenceScanResult {
    bool ok = false;
    bool missingFrames = false;
    int count = 0;
    int firstIndex = 0;
    int selectedIndex = 0;
    int lastIndex = 0;
    QString prefix;
    QString suffix;
    int digitCount = 0;
    QString message;
    ImageDimensions dimensions;  // Dimensions of the first image in sequence
    bool allMatch = true;        // Whether all images match the first image's dimensions
};

class ImageSequenceUtils {
public:
    using ScanProgressCallback = std::function<bool(int indexedFiles)>;

    static ImageSequenceScanResult scanImageSequence(const QString &path, ScanProgressCallback progressCallback = {});
    static std::vector<ImageSequenceFrameInfo> collectImageSequenceFrames(const QString &path, ScanProgressCallback progressCallback = {});
    static ImageSequenceScanResult parseImageSequencePattern(const QString &path, bool requireExistingFile = true);
    static QString buildFramePath(const QString &directory, const QString &prefix, int digitCount, const QString &suffix, int frameIndex);
    static int trailingDigitCount(const QString &text);
    static bool containsOnlyDigits(const QString &text);
    static int expectedFrameCount(int startIndex, int stopIndex, int steps);

    // Checks if all images in a sequence have matching dimensions
    // Returns the first image's dimensions and whether all match
    static std::tuple<ImageDimensions, bool> checkSequenceDimensions(const std::vector<ImageSequenceFrameInfo>& frames);
};

#endif // CSTITCH_IMAGESEQUENCEUTILS_H
