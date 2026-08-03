/*
 * SPDX-FileCopyrightText:
 * 2026 Erik Sundén
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "imagesequenceutils.h"
#include "stitchwuffs.h"

#include <QDir>
#include <QFileInfo>

#include <algorithm>

namespace {

bool isPreferredDuplicate(const ImageSequenceFrameInfo &candidate,
                          const ImageSequenceFrameInfo &existing,
                          const QString &selectedPath,
                          int selectedDigitCount)
{
    const QFileInfo candidateInfo(candidate.path);
    const QFileInfo existingInfo(existing.path);
    const bool candidateIsSelected = candidateInfo == QFileInfo(selectedPath);
    const bool existingIsSelected = existingInfo == QFileInfo(selectedPath);
    if (candidateIsSelected != existingIsSelected) {
        return candidateIsSelected;
    }

    const bool candidateMatchesDigitCount = candidate.digitCount == selectedDigitCount;
    const bool existingMatchesDigitCount = existing.digitCount == selectedDigitCount;
    if (candidateMatchesDigitCount != existingMatchesDigitCount) {
        return candidateMatchesDigitCount;
    }

    if (candidate.digitCount != existing.digitCount) {
        return candidate.digitCount > existing.digitCount;
    }

    return candidate.path < existing.path;
}

} // namespace

int ImageSequenceUtils::trailingDigitCount(const QString &text)
{
    int count = 0;
    for (int index = text.size() - 1; index >= 0; --index) {
        if (!text.at(index).isDigit()) {
            break;
        }
        ++count;
    }
    return count;
}

bool ImageSequenceUtils::containsOnlyDigits(const QString &text)
{
    if (text.isEmpty()) {
        return false;
    }
    for (const QChar character : text) {
        if (!character.isDigit()) {
            return false;
        }
    }
    return true;
}

ImageSequenceScanResult ImageSequenceUtils::parseImageSequencePattern(const QString &path, bool requireExistingFile)
{
    ImageSequenceScanResult result;
    const QFileInfo selected(path);
    if (requireExistingFile && (!selected.exists() || !selected.isFile())) {
        result.message = QStringLiteral("Input file does not exist: %1").arg(path);
        return result;
    }

    const QString selectedStem = selected.completeBaseName();
    const int digitCount = trailingDigitCount(selectedStem);
    if (digitCount == 0) {
        result.message = QStringLiteral("Selected input is not a numbered image-sequence frame: %1").arg(selected.fileName());
        result.count = 1;
        return result;
    }

    const QString prefix = selectedStem.left(selectedStem.size() - digitCount);
    const QString selectedDigits = selectedStem.right(digitCount);
    bool selectedOk = false;
    result.selectedIndex = selectedDigits.toInt(&selectedOk);
    if (!selectedOk) {
        result.message = QStringLiteral("Failed to parse frame number from selected input: %1").arg(selected.fileName());
        return result;
    }

    result.ok = true;
    result.firstIndex = result.selectedIndex;
    result.lastIndex = result.selectedIndex;
    result.count = 1;
    result.prefix = prefix;
    result.suffix = selected.suffix();
    result.digitCount = digitCount;
    return result;
}

ImageSequenceScanResult ImageSequenceUtils::scanImageSequence(const QString &path, ScanProgressCallback progressCallback)
{
    ImageSequenceScanResult result = parseImageSequencePattern(path);
    if (!result.ok) {
        return result;
    }

    result.ok = false;
    result.count = 0;

    const std::vector<ImageSequenceFrameInfo> frames = collectImageSequenceFrames(path, std::move(progressCallback));
    if (frames.empty()) {
        const QFileInfo selected(path);
        result.message = QStringLiteral("No matching numbered image sequence files found for: %1").arg(selected.fileName());
        return result;
    }

    result.count = static_cast<int>(frames.size());
    result.ok = true;
    result.firstIndex = frames.front().frameIndex;
    result.lastIndex = frames.back().frameIndex;
    result.missingFrames = (1 + result.lastIndex - result.firstIndex) != result.count;

    // Check dimensions for all frames
    std::tie(result.dimensions, result.allMatch) = checkSequenceDimensions(frames);

    return result;
}

std::vector<ImageSequenceFrameInfo> ImageSequenceUtils::collectImageSequenceFrames(const QString &path, ScanProgressCallback progressCallback)
{
    const ImageSequenceScanResult pattern = parseImageSequencePattern(path);
    if (!pattern.ok) {
        return {};
    }

    const QFileInfo selected(path);
    const QString suffix = pattern.suffix;
    const QStringList filters = suffix.isEmpty() ? QStringList{ QStringLiteral("*") } : QStringList{ QStringLiteral("*.%1").arg(suffix) };
    const QFileInfoList entries = QDir(selected.absolutePath()).entryInfoList(filters, QDir::Files, QDir::Name);

    std::vector<ImageSequenceFrameInfo> frames;
    frames.reserve(static_cast<std::size_t>(entries.size()));
    int indexedFiles = 0;
    for (const QFileInfo &entry : entries) {
        if (entry.suffix().compare(suffix, Qt::CaseInsensitive) != 0) {
            continue;
        }
        const QString stem = entry.completeBaseName();
        if (!stem.startsWith(pattern.prefix)) {
            continue;
        }
        const QString digits = stem.mid(pattern.prefix.size());
        if (digits.isEmpty() || !containsOnlyDigits(digits)) {
            continue;
        }

        bool ok = false;
        const int frameIndex = digits.toInt(&ok);
        if (!ok) {
            continue;
        }

        ImageSequenceFrameInfo candidate;
        candidate.frameIndex = frameIndex;
        candidate.digitCount = digits.size();
        candidate.path = entry.absoluteFilePath();

        auto existing = std::ranges::find(frames, frameIndex, &ImageSequenceFrameInfo::frameIndex);
        if (existing == frames.end()) {
            frames.push_back(std::move(candidate));
        }
        else if (isPreferredDuplicate(candidate, *existing, selected.absoluteFilePath(), pattern.digitCount)) {
            *existing = std::move(candidate);
        }

        ++indexedFiles;
        if (progressCallback && indexedFiles % 250 == 0 && !progressCallback(indexedFiles)) {
            return {};
        }
    }

    std::ranges::sort(frames, [](const ImageSequenceFrameInfo &left, const ImageSequenceFrameInfo &right) {
        if (left.frameIndex != right.frameIndex) {
            return left.frameIndex < right.frameIndex;
        }
        if (left.digitCount != right.digitCount) {
            return left.digitCount > right.digitCount;
        }
        return left.path < right.path;
    });

    if (progressCallback) {
        progressCallback(indexedFiles);
    }
    return frames;
}

QString ImageSequenceUtils::buildFramePath(const QString &directory, const QString &prefix, int digitCount, const QString &suffix, int frameIndex)
{
    const QString digits = QStringLiteral("%1").arg(frameIndex, digitCount, 10, QLatin1Char('0'));
    QString filename = prefix + digits;
    if (!suffix.isEmpty()) {
        filename += QLatin1Char('.') + suffix;
    }
    return QDir(directory).filePath(filename);
}

int ImageSequenceUtils::expectedFrameCount(int startIndex, int stopIndex, int steps)
{
    if (startIndex == stopIndex) {
        return 1;
    }
    const int step = std::max(1, steps);
    const int first = std::min(startIndex, stopIndex);
    const int last = std::max(startIndex, stopIndex);
    return ((last - first) / step) + 1;
}

std::tuple<ImageDimensions, bool> ImageSequenceUtils::checkSequenceDimensions(const std::vector<ImageSequenceFrameInfo>& frames)
{
    if (frames.empty()) {
        return {{0, 0}, true};
    }

    // Get dimensions for the first frame
    CStitch::Wuffs::ImageInfo firstInfo;
    std::string errorMessage;
    const bool firstOk = CStitch::Wuffs::readImageInfo(QFile::encodeName(frames[0].path).constData(), firstInfo, &errorMessage);

    if (!firstOk) {
        return {{0, 0}, false};
    }

    ImageDimensions firstDimensions{firstInfo.width, firstInfo.height};

    // Check all frames against the first one
    for (size_t i = 1; i < frames.size(); ++i) {
        CStitch::Wuffs::ImageInfo info;
        if (!CStitch::Wuffs::readImageInfo(QFile::encodeName(frames[i].path).constData(), info, &errorMessage)) {
            // If we can't read a frame, skip it (treat as matching to avoid early failure)
            continue;
        }

        if (info.width != firstDimensions.width || info.height != firstDimensions.height) {
            return {firstDimensions, false};
        }
    }

    return {firstDimensions, true};
}
